#include "engine/debug/RenderDocCapture.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define RENDERDOC_CC __cdecl
#else
#include <dlfcn.h>
#define RENDERDOC_CC
#endif

namespace engine::debug
{

namespace
{

// Minimal ABI declaration of RenderDoc's MIT-licensed in-application API 1.6.
// The field order intentionally matches renderdoc_app.h. Only the functions
// used by this controller receive concrete types; the remaining pointer slots
// preserve the stable API layout.
using RenderDocGetApi = int(RENDERDOC_CC*)(int version, void** api);
using RenderDocGetApiVersion = void(RENDERDOC_CC*)(int*, int*, int*);
using RenderDocSetCaptureOptionU32 = int(RENDERDOC_CC*)(int, uint32_t);
using RenderDocSetCapturePath = void(RENDERDOC_CC*)(const char*);
using RenderDocGetCapturePath = const char*(RENDERDOC_CC*)();
using RenderDocGetNumCaptures = uint32_t(RENDERDOC_CC*)();
using RenderDocGetCapture = uint32_t(RENDERDOC_CC*)(uint32_t, char*, uint32_t*, uint64_t*);
using RenderDocStartCapture = void(RENDERDOC_CC*)(void*, void*);
using RenderDocIsCapturing = uint32_t(RENDERDOC_CC*)();
using RenderDocEndCapture = uint32_t(RENDERDOC_CC*)(void*, void*);
using RenderDocSetCaptureTitle = void(RENDERDOC_CC*)(const char*);

struct RenderDocApi16
{
    RenderDocGetApiVersion GetAPIVersion;
    RenderDocSetCaptureOptionU32 SetCaptureOptionU32;
    void* SetCaptureOptionF32;
    void* GetCaptureOptionU32;
    void* GetCaptureOptionF32;
    void* SetFocusToggleKeys;
    void* SetCaptureKeys;
    void* GetOverlayBits;
    void* MaskOverlayBits;
    void* RemoveHooks;
    void* UnloadCrashHandler;
    RenderDocSetCapturePath SetCaptureFilePathTemplate;
    RenderDocGetCapturePath GetCaptureFilePathTemplate;
    RenderDocGetNumCaptures GetNumCaptures;
    RenderDocGetCapture GetCapture;
    void* TriggerCapture;
    void* IsTargetControlConnected;
    void* LaunchReplayUI;
    void* SetActiveWindow;
    RenderDocStartCapture StartFrameCapture;
    RenderDocIsCapturing IsFrameCapturing;
    RenderDocEndCapture EndFrameCapture;
    void* TriggerMultiFrameCapture;
    void* SetCaptureFileComments;
    void* DiscardFrameCapture;
    void* ShowReplayUI;
    RenderDocSetCaptureTitle SetCaptureTitle;
};

constexpr int kRenderDocApiVersion16 = 10600;
constexpr int kRenderDocOptionApiValidation = 2;

void* FindLoadedRenderDocModule()
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetModuleHandleW(L"renderdoc.dll"));
#else
    return dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
#endif
}

void* LoadRenderDocModule(const std::string& path)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryW(std::filesystem::path(path).c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* FindRenderDocSymbol(void* module)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(module), "RENDERDOC_GetAPI"));
#else
    return dlsym(module, "RENDERDOC_GetAPI");
#endif
}

void UnloadRenderDocModule(void* module)
{
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}

std::string AbsoluteCaptureTemplate(const std::string& requested)
{
    const std::filesystem::path input = requested.empty()
        ? std::filesystem::path("captures/renderer")
        : std::filesystem::path(requested);
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(input, error);
    return (error ? input : absolute).lexically_normal().generic_string();
}

void PreparePortableVulkanLayer(const std::string& libraryPath)
{
    const std::filesystem::path directory =
        std::filesystem::absolute(std::filesystem::path(libraryPath)).parent_path();
    if (!std::filesystem::exists(directory / "renderdoc.json"))
        return;

#if defined(_WIN32)
    const auto appendVariable = [&](const wchar_t* name, const std::wstring& value)
    {
        const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
        std::wstring combined;
        if (length > 1)
        {
            combined.resize(length);
            GetEnvironmentVariableW(name, combined.data(), length);
            combined.resize(length - 1);
        }
        if (combined.find(value) == std::wstring::npos)
        {
            if (!combined.empty())
                combined += L';';
            combined += value;
            SetEnvironmentVariableW(name, combined.c_str());
        }
    };
    appendVariable(L"VK_ADD_LAYER_PATH", directory.wstring());
    appendVariable(L"VK_INSTANCE_LAYERS", L"VK_LAYER_RENDERDOC_Capture");
#else
    const auto appendVariable = [](const char* name, const std::string& value)
    {
        std::string combined;
        if (const char* current = std::getenv(name))
            combined = current;
        if (combined.find(value) == std::string::npos)
        {
            if (!combined.empty())
                combined += ':';
            combined += value;
            setenv(name, combined.c_str(), 1);
        }
    };
    appendVariable("VK_ADD_LAYER_PATH", directory.string());
    appendVariable("VK_INSTANCE_LAYERS", "VK_LAYER_RENDERDOC_Capture");
#endif
}

} // namespace

RenderDocCapture::~RenderDocCapture()
{
    Shutdown();
}

bool RenderDocCapture::Initialize(const RenderDocCaptureConfig& config)
{
    Shutdown();
    m_config = config;
    m_config.FixedDeltaSeconds = std::max(m_config.FixedDeltaSeconds, 0.0001f);
    m_config.CapturePathTemplate = AbsoluteCaptureTemplate(m_config.CapturePathTemplate);
    m_captureCompleted = false;
    m_lastCapturePath.clear();
    m_lastError.clear();

    if (!m_config.Enabled)
        return false;

    if (m_config.PrepareVulkanLayer && !m_config.LibraryPath.empty())
        PreparePortableVulkanLayer(m_config.LibraryPath);

    m_module = FindLoadedRenderDocModule();
    if (m_module == nullptr && !m_config.LibraryPath.empty())
    {
        m_module = LoadRenderDocModule(m_config.LibraryPath);
        m_moduleOwned = m_module != nullptr;
    }

    if (m_module == nullptr)
    {
        m_lastError = "RenderDoc capture was requested, but renderdoc.dll/librenderdoc.so is not "
                      "loaded. Start the sample from RenderDoc or pass --renderdoc-library <path>.";
        if (m_config.RequireAvailable)
            throw std::runtime_error(m_lastError);
        log::Warn(m_lastError);
        return false;
    }

    const auto getApi = reinterpret_cast<RenderDocGetApi>(FindRenderDocSymbol(m_module));
    RenderDocApi16* api = nullptr;
    if (getApi == nullptr || getApi(kRenderDocApiVersion16, reinterpret_cast<void**>(&api)) != 1 ||
        api == nullptr)
    {
        m_lastError = "The loaded RenderDoc module does not expose the required in-app API 1.6.";
        if (m_config.RequireAvailable)
            throw std::runtime_error(m_lastError);
        log::Warn(m_lastError);
        return false;
    }

    m_api = api;
    api->SetCaptureFilePathTemplate(m_config.CapturePathTemplate.c_str());
    api->SetCaptureOptionU32(kRenderDocOptionApiValidation, m_config.ApiValidation ? 1u : 0u);
    int major = 0, minor = 0, patch = 0;
    api->GetAPIVersion(&major, &minor, &patch);
    log::Info("RenderDoc in-app capture ready (API " + std::to_string(major) + "." +
              std::to_string(minor) + "." + std::to_string(patch) + ", target frame " +
              std::to_string(m_config.FrameIndex) + ")");
    return true;
}

void RenderDocCapture::Shutdown()
{
    if (m_captureActive && m_api != nullptr)
        reinterpret_cast<RenderDocApi16*>(m_api)->EndFrameCapture(nullptr, nullptr);
    m_captureActive = false;
    m_api = nullptr;
    if (m_moduleOwned && m_module != nullptr)
        UnloadRenderDocModule(m_module);
    m_module = nullptr;
    m_moduleOwned = false;
}

bool RenderDocCapture::BeginFrame(uint64_t frameIndex)
{
    if (m_api == nullptr || m_captureCompleted || m_captureActive ||
        frameIndex != m_config.FrameIndex)
        return false;

    auto* api = reinterpret_cast<RenderDocApi16*>(m_api);
    m_captureCountBefore = api->GetNumCaptures();
    api->StartFrameCapture(nullptr, nullptr);
    m_captureActive = api->IsFrameCapturing() != 0;
    if (!m_captureActive)
    {
        m_lastError = "RenderDoc did not start the requested frame capture.";
        log::Warn(m_lastError);
        return false;
    }
    if (!m_config.CaptureTitle.empty())
        api->SetCaptureTitle(m_config.CaptureTitle.c_str());
    log::Info("RenderDoc capture started for deterministic frame " + std::to_string(frameIndex));
    return true;
}

bool RenderDocCapture::EndFrame()
{
    if (!m_captureActive || m_api == nullptr)
        return false;

    auto* api = reinterpret_cast<RenderDocApi16*>(m_api);
    const bool success = api->EndFrameCapture(nullptr, nullptr) != 0;
    m_captureActive = false;
    if (!success)
    {
        m_lastError = "RenderDoc failed to finish the requested frame capture.";
        log::Warn(m_lastError);
        return false;
    }

    m_captureCompleted = true;
    UpdateLastCapturePath(m_captureCountBefore);
    log::Info(m_lastCapturePath.empty() ? "RenderDoc capture completed"
                                       : "Saved RenderDoc capture: " + m_lastCapturePath);
    return true;
}

void RenderDocCapture::UpdateLastCapturePath(uint32_t previousCaptureCount)
{
    auto* api = reinterpret_cast<RenderDocApi16*>(m_api);
    const uint32_t count = api->GetNumCaptures();
    if (count <= previousCaptureCount)
        return;

    uint32_t length = 0;
    if (api->GetCapture(count - 1, nullptr, &length, nullptr) == 0 || length == 0)
        return;
    std::vector<char> path(length + 1, '\0');
    if (api->GetCapture(count - 1, path.data(), &length, nullptr) != 0)
        m_lastCapturePath.assign(path.data());
}

} // namespace engine::debug
