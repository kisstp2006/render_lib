#include "engine/renderer/Renderer.h"

#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLRenderBackend.h"
#if ENGINE_HAS_VULKAN
#include "engine/backend/vk/VulkanRenderBackend.h"
#endif
#include "engine/core/Camera.h"
#include "engine/core/Window.h"
#include "engine/render/SceneRenderer.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace rendering {
namespace {

glm::vec3 Vec3(const float value[3])
{
    return {value[0], value[1], value[2]};
}

engine::Material ConvertMaterial(const MaterialDesc& source)
{
    engine::Material material;
    material.Albedo = Vec3(source.Albedo);
    material.BaseColorAlpha = source.Alpha;
    material.Metallic = source.Metallic;
    material.Roughness = std::clamp(source.Roughness, 0.02f, 1.0f);
    material.Emissive = Vec3(source.Emissive);
    material.AmbientOcclusion = source.AmbientOcclusion;
    material.SpecularF0 = source.SpecularF0;
    return material;
}

engine::PointLight ConvertPointLight(const PointLightDesc& source)
{
    engine::PointLight light;
    light.Position = Vec3(source.Position);
    light.Color = Vec3(source.Color);
    light.Intensity = source.Intensity;
    light.Radius = std::max(source.Radius, 0.01f);
    light.CastsShadows = source.CastsShadows;
    return light;
}

glm::mat4 Matrix(const std::array<float, 16>& values)
{
    return glm::make_mat4(values.data());
}

std::unique_ptr<engine::IRenderBackend> CreateBackend(Backend backend)
{
    if (backend == Backend::OpenGL)
        return std::make_unique<engine::GLRenderBackend>();
#if ENGINE_HAS_VULKAN
    if (backend == Backend::Vulkan)
        return std::make_unique<engine::VulkanRenderBackend>();
#endif
    throw std::runtime_error(
        "Vulkan was requested, but this Rendering Engine SDK was built without Vulkan support");
}

std::filesystem::path ModuleDirectory()
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module))
        return {};
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#elif defined(__linux__) || defined(__APPLE__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&ModuleDirectory), &info) == 0 ||
        !info.dli_fname)
        return {};
    return std::filesystem::path(info.dli_fname).parent_path();
#else
    return {};
#endif
}

std::filesystem::path DiscoverShaderDirectory()
{
#if defined(_WIN32)
    char* configured = nullptr;
    size_t configuredLength = 0;
    if (_dupenv_s(&configured, &configuredLength,
                  "RENDERING_ENGINE_SHADER_DIR") == 0 && configured)
    {
        const std::filesystem::path result(configured);
        std::free(configured);
        if (!result.empty()) return result;
    }
#else
    if (const char* configured = std::getenv("RENDERING_ENGINE_SHADER_DIR");
        configured && configured[0] != '\0')
        return configured;
#endif

    const std::filesystem::path module = ModuleDirectory();
    const std::array candidates{
        module / "shaders",
        module / ".." / "share" / "RenderingEngine" / "shaders",
        std::filesystem::current_path() / "shaders"};
    for (const std::filesystem::path& candidate : candidates)
        if (std::filesystem::exists(candidate / "hlsl"))
            return std::filesystem::weakly_canonical(candidate);
    return {};
}

} // namespace

struct Renderer::Impl
{
    struct ObjectRecord
    {
        ObjectHandle Handle = 0;
        MaterialHandle Material = 0;
    };
    struct PointLightRecord
    {
        PointLightHandle Handle = 0;
    };

    explicit Impl(const RendererDesc& sourceDesc)
        : Desc(sourceDesc), ActiveApi(sourceDesc.GraphicsBackend)
    {
        if (Desc.ShaderDirectory.empty())
            Desc.ShaderDirectory = DiscoverShaderDirectory();
        if (Desc.Width == 0 || Desc.Height == 0)
            throw std::invalid_argument("Renderer window dimensions must be non-zero");
        if (!Desc.ShaderDirectory.empty() &&
            !std::filesystem::exists(Desc.ShaderDirectory / "hlsl"))
            throw std::runtime_error("Renderer shader directory has no hlsl subtree: " +
                                     Desc.ShaderDirectory.string());

        engine::WindowDesc windowDesc;
        windowDesc.title = Desc.WindowTitle;
        windowDesc.width = static_cast<int>(Desc.Width);
        windowDesc.height = static_cast<int>(Desc.Height);
        windowDesc.api = ActiveApi == Backend::OpenGL
            ? engine::GraphicsApi::OpenGL : engine::GraphicsApi::Vulkan;
        windowDesc.mode = Desc.Resizable ? engine::WindowMode::WindowedResizable
                                         : engine::WindowMode::WindowedFixed;
        windowDesc.visible = Desc.Visible;
        Window = std::make_unique<engine::Window>(windowDesc);

        BackendImpl = CreateBackend(ActiveApi);
        engine::RenderBackendConfig backendConfig;
        backendConfig.Presentation = Desc.VSync ? engine::PresentMode::VSync
                                                : engine::PresentMode::Immediate;
        backendConfig.EnableValidation = Desc.Validation;
        backendConfig.MsaaSamples = std::max(Desc.MsaaSamples, 1u);
        backendConfig.ShaderDirectory = Desc.ShaderDirectory.string();
        backendConfig.PipelineCacheDirectory = Desc.PipelineCacheDirectory.string();
        BackendImpl->Init(*Window, backendConfig);

        Window->SetResizeCallback([this](int width, int height) {
            if (width > 0 && height > 0 && BackendImpl)
            {
                BackendImpl->Resize(width, height);
                Frontend.ResetFrameHistory();
            }
        });

        Scene.PostProcess.AutoExposure = false;
        Scene.PostProcess.AntiAliasing = engine::AntiAliasingMode::Taa;
    }

    ~Impl()
    {
        if (BackendImpl)
        {
            BackendImpl->WaitIdle();
            BackendImpl->Shutdown();
            BackendImpl.reset();
        }
    }

    RendererDesc Desc;
    Backend ActiveApi = Backend::OpenGL;
    std::unique_ptr<engine::Window> Window;
    std::unique_ptr<engine::IRenderBackend> BackendImpl;
    engine::SceneRenderer Frontend;
    engine::Scene Scene;
    engine::Camera Camera;
    std::unordered_map<MeshHandle, std::shared_ptr<engine::MeshData>> Meshes;
    std::unordered_map<MaterialHandle, engine::Material> Materials;
    std::vector<ObjectRecord> Objects;
    std::vector<PointLightRecord> PointLights;
    uint64_t NextHandle = 1;
    float TimeSeconds = 0.0f;
};

RendererPtr Renderer::Create(const RendererDesc& desc)
{
    auto impl = std::make_unique<Impl>(desc);
    RendererPtr renderer(new Renderer(impl.get()));
    impl.release();
    return renderer;
}

void RendererDeleter::operator()(Renderer* renderer) const noexcept
{
    delete renderer;
}

Renderer::Renderer(Impl* impl) : m_impl(impl) {}
Renderer::~Renderer() { delete m_impl; }
Renderer::Renderer(Renderer&& other) noexcept
    : m_impl(std::exchange(other.m_impl, nullptr))
{
}

Renderer& Renderer::operator=(Renderer&& other) noexcept
{
    if (this != &other)
    {
        delete m_impl;
        m_impl = std::exchange(other.m_impl, nullptr);
    }
    return *this;
}

bool Renderer::PumpEvents()
{
    m_impl->Window->PollEvents();
    return !m_impl->Window->ShouldClose();
}

void Renderer::RequestClose() { m_impl->Window->RequestClose(); }
bool Renderer::ShouldClose() const { return m_impl->Window->ShouldClose(); }

void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        throw std::invalid_argument("Renderer resize dimensions must be non-zero");
    m_impl->Window->SetSize(static_cast<int>(width), static_cast<int>(height));
}

void Renderer::RenderFrame(float deltaSeconds)
{
    const int width = m_impl->Window->Width();
    const int height = m_impl->Window->Height();
    if (width <= 0 || height <= 0 || m_impl->Window->IsMinimized())
        return;
    const float dt = std::max(deltaSeconds, 0.0f);
    m_impl->TimeSeconds += dt;
    const engine::RenderFrameData& frame = m_impl->Frontend.PrepareFrame(
        m_impl->Scene, m_impl->Camera, width, height, nullptr,
        m_impl->TimeSeconds, dt);
    m_impl->BackendImpl->RenderFrame(frame);
    if (m_impl->ActiveApi == Backend::OpenGL)
        m_impl->Window->SwapBuffers();
}

bool Renderer::Tick(float deltaSeconds)
{
    if (!PumpEvents())
        return false;
    RenderFrame(deltaSeconds);
    return !ShouldClose();
}

MeshHandle Renderer::CreateMesh(std::span<const Vertex> vertices,
                                std::span<const uint32_t> indices)
{
    if (vertices.empty() || indices.empty() || (indices.size() % 3u) != 0u)
        throw std::invalid_argument("A renderer mesh needs vertices and triangle-list indices");
    auto mesh = std::make_shared<engine::MeshData>();
    mesh->Vertices.reserve(vertices.size());
    for (const Vertex& source : vertices)
    {
        engine::Vertex target;
        target.Position = Vec3(source.Position);
        target.Normal = Vec3(source.Normal);
        target.Tangent = {source.Tangent[0], source.Tangent[1],
                          source.Tangent[2], source.Tangent[3]};
        target.UV = {source.Uv[0], source.Uv[1]};
        mesh->Vertices.push_back(target);
    }
    mesh->Indices.assign(indices.begin(), indices.end());
    for (uint32_t index : mesh->Indices)
        if (index >= mesh->Vertices.size())
            throw std::invalid_argument("Renderer mesh index is outside the vertex array");
    const MeshHandle handle = m_impl->NextHandle++;
    m_impl->Meshes.emplace(handle, std::move(mesh));
    return handle;
}

MeshHandle Renderer::CreateCube(float halfExtent)
{
    auto mesh = std::make_shared<engine::MeshData>(
        engine::primitives::MakeCube(std::max(halfExtent, 0.001f)));
    const MeshHandle handle = m_impl->NextHandle++;
    m_impl->Meshes.emplace(handle, std::move(mesh));
    return handle;
}

MeshHandle Renderer::CreateSphere(float radius, uint32_t stacks, uint32_t slices)
{
    auto mesh = std::make_shared<engine::MeshData>(engine::primitives::MakeSphere(
        std::max(radius, 0.001f), static_cast<int>(std::max(stacks, 3u)),
        static_cast<int>(std::max(slices, 3u))));
    const MeshHandle handle = m_impl->NextHandle++;
    m_impl->Meshes.emplace(handle, std::move(mesh));
    return handle;
}

void Renderer::DestroyMesh(MeshHandle mesh) { m_impl->Meshes.erase(mesh); }

MaterialHandle Renderer::CreateMaterial(const MaterialDesc& desc)
{
    const MaterialHandle handle = m_impl->NextHandle++;
    m_impl->Materials.emplace(handle, ConvertMaterial(desc));
    return handle;
}

bool Renderer::UpdateMaterial(MaterialHandle material, const MaterialDesc& desc)
{
    const auto found = m_impl->Materials.find(material);
    if (found == m_impl->Materials.end())
        return false;
    found->second = ConvertMaterial(desc);
    for (size_t index = 0; index < m_impl->Objects.size(); ++index)
        if (m_impl->Objects[index].Material == material)
            m_impl->Scene.Instances()[index].Mat = found->second;
    return true;
}

void Renderer::DestroyMaterial(MaterialHandle material)
{
    m_impl->Materials.erase(material);
}

ObjectHandle Renderer::AddObject(MeshHandle mesh, MaterialHandle material,
                                 const std::array<float, 16>& transform)
{
    const auto meshFound = m_impl->Meshes.find(mesh);
    const auto materialFound = m_impl->Materials.find(material);
    if (meshFound == m_impl->Meshes.end() || materialFound == m_impl->Materials.end())
        throw std::invalid_argument("Renderer object references an invalid mesh or material handle");
    const ObjectHandle handle = m_impl->NextHandle++;
    m_impl->Scene.AddInstance(meshFound->second, materialFound->second, Matrix(transform));
    m_impl->Objects.push_back({handle, material});
    return handle;
}

bool Renderer::SetObjectTransform(ObjectHandle object,
                                  const std::array<float, 16>& transform)
{
    const auto found = std::find_if(m_impl->Objects.begin(), m_impl->Objects.end(),
        [object](const Impl::ObjectRecord& item) { return item.Handle == object; });
    if (found == m_impl->Objects.end())
        return false;
    const size_t index = static_cast<size_t>(found - m_impl->Objects.begin());
    m_impl->Scene.Instances()[index].Transform = Matrix(transform);
    m_impl->Scene.Instances()[index].Mobility = engine::MeshMobility::Movable;
    return true;
}

bool Renderer::RemoveObject(ObjectHandle object)
{
    const auto found = std::find_if(m_impl->Objects.begin(), m_impl->Objects.end(),
        [object](const Impl::ObjectRecord& item) { return item.Handle == object; });
    if (found == m_impl->Objects.end())
        return false;
    const size_t index = static_cast<size_t>(found - m_impl->Objects.begin());
    m_impl->Objects.erase(found);
    m_impl->Scene.Instances().erase(m_impl->Scene.Instances().begin() +
                                    static_cast<std::ptrdiff_t>(index));
    return true;
}

void Renderer::ClearObjects()
{
    m_impl->Objects.clear();
    m_impl->Scene.Instances().clear();
}

PointLightHandle Renderer::AddPointLight(const PointLightDesc& desc)
{
    const PointLightHandle handle = m_impl->NextHandle++;
    m_impl->Scene.AddPointLight(ConvertPointLight(desc));
    m_impl->PointLights.push_back({handle});
    return handle;
}

bool Renderer::SetPointLight(PointLightHandle light, const PointLightDesc& desc)
{
    const auto found = std::find_if(m_impl->PointLights.begin(), m_impl->PointLights.end(),
        [light](const Impl::PointLightRecord& item) { return item.Handle == light; });
    if (found == m_impl->PointLights.end())
        return false;
    m_impl->Scene.PointLights()[static_cast<size_t>(found - m_impl->PointLights.begin())] =
        ConvertPointLight(desc);
    return true;
}

bool Renderer::RemovePointLight(PointLightHandle light)
{
    const auto found = std::find_if(m_impl->PointLights.begin(), m_impl->PointLights.end(),
        [light](const Impl::PointLightRecord& item) { return item.Handle == light; });
    if (found == m_impl->PointLights.end())
        return false;
    const size_t index = static_cast<size_t>(found - m_impl->PointLights.begin());
    m_impl->PointLights.erase(found);
    m_impl->Scene.PointLights().erase(m_impl->Scene.PointLights().begin() +
                                      static_cast<std::ptrdiff_t>(index));
    return true;
}

void Renderer::ClearPointLights()
{
    m_impl->PointLights.clear();
    m_impl->Scene.PointLights().clear();
}

void Renderer::SetCamera(const CameraDesc& desc)
{
    m_impl->Camera.Position = Vec3(desc.Position);
    m_impl->Camera.Yaw = desc.YawDegrees;
    m_impl->Camera.Pitch = std::clamp(desc.PitchDegrees, -89.9f, 89.9f);
    m_impl->Camera.FovDegrees = std::clamp(desc.VerticalFovDegrees, 1.0f, 179.0f);
    m_impl->Camera.NearPlane = std::max(desc.NearPlane, 0.0001f);
    m_impl->Camera.FarPlane = std::max(desc.FarPlane, m_impl->Camera.NearPlane + 0.01f);
}

void Renderer::SetSun(const DirectionalLightDesc& desc)
{
    m_impl->Scene.Sun.Direction = glm::normalize(Vec3(desc.Direction));
    m_impl->Scene.Sun.Color = Vec3(desc.Color);
    m_impl->Scene.Sun.Intensity = desc.Intensity;
    m_impl->Scene.Sun.CastsShadows = desc.CastsShadows;
}

void Renderer::SetExposure(float exposure)
{
    m_impl->Scene.PostProcess.Exposure = std::max(exposure, 0.0f);
}

void Renderer::SetBackgroundColor(const std::array<float, 3>& zenith,
                                  const std::array<float, 3>& horizon)
{
    m_impl->Scene.Sky.ZenithColor = {zenith[0], zenith[1], zenith[2]};
    m_impl->Scene.Sky.HorizonColor = {horizon[0], horizon[1], horizon[2]};
}

void Renderer::RequestScreenshot(const std::filesystem::path& path)
{
    if (path.empty())
        throw std::invalid_argument("Screenshot path is empty");
    m_impl->BackendImpl->RequestScreenshot(path.string());
}

FrameStats Renderer::GetFrameStats() const
{
    const engine::BackendFrameStats backend = m_impl->BackendImpl->GetFrameStats();
    return {backend.GpuFrameMilliseconds, backend.GpuInstanceBatchCount,
            backend.GpuInstanceCount, backend.GpuDrawCallsSaved};
}

std::string_view Renderer::BackendName() const { return m_impl->BackendImpl->Name(); }
Backend Renderer::ActiveBackend() const { return m_impl->ActiveApi; }

std::array<float, 16> IdentityTransform()
{
    std::array<float, 16> result{};
    result[0] = result[5] = result[10] = result[15] = 1.0f;
    return result;
}

std::array<float, 16> ComposeTransform(const std::array<float, 3>& translation,
                                       const std::array<float, 3>& rotationDegrees,
                                       const std::array<float, 3>& scale)
{
    glm::mat4 matrix(1.0f);
    matrix = glm::translate(matrix, {translation[0], translation[1], translation[2]});
    matrix = glm::rotate(matrix, glm::radians(rotationDegrees[1]), {0.0f, 1.0f, 0.0f});
    matrix = glm::rotate(matrix, glm::radians(rotationDegrees[0]), {1.0f, 0.0f, 0.0f});
    matrix = glm::rotate(matrix, glm::radians(rotationDegrees[2]), {0.0f, 0.0f, 1.0f});
    matrix = glm::scale(matrix, {scale[0], scale[1], scale[2]});
    std::array<float, 16> result;
    std::copy(glm::value_ptr(matrix), glm::value_ptr(matrix) + 16, result.begin());
    return result;
}

} // namespace rendering
