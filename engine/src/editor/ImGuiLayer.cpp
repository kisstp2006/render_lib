#include "engine/editor/ImGuiLayer.h"

#include "engine/core/Log.h"
#include "engine/core/Window.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#if ENGINE_HAS_VULKAN
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace engine::editor
{
namespace
{

#if ENGINE_HAS_VULKAN
void CheckVulkanResult(VkResult result)
{
    if (result < 0)
        log::Error("ImGui", "Vulkan backend error: " + std::to_string(result));
}
#endif

} // namespace

ImGuiLayer::~ImGuiLayer()
{
    Shutdown();
}

bool ImGuiLayer::Initialize(Window& window, IRenderBackend& backend,
                            const ImGuiLayerConfig& config)
{
    if (m_initialized)
        return true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    m_iniFilename = config.IniFilename;
    if (m_iniFilename.empty())
        io.IniFilename = nullptr;
    else
    {
        const std::filesystem::path iniPath(m_iniFilename);
        std::error_code directoryError;
        if (iniPath.has_parent_path())
            std::filesystem::create_directories(iniPath.parent_path(), directoryError);
        if (directoryError)
        {
            log::Warn("ImGui", "Cannot create ini directory; layout persistence disabled: " +
                      directoryError.message());
            m_iniFilename.clear();
            io.IniFilename = nullptr;
        }
        else
            io.IniFilename = m_iniFilename.c_str();
    }
    if (config.Docking)
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (config.KeyboardNavigation)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (config.PlatformViewports)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    m_platformViewports = config.PlatformViewports;
    ImGui::StyleColorsDark();
    if (m_platformViewports)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    m_backend = &backend;
    const NativeGraphicsContext native = backend.GetNativeGraphicsContext();
    m_api = native.Api;
    bool glfwInitialized = false;
    bool rendererInitialized = false;
    bool initialized = false;
    if (native.Api == RenderBackendApi::OpenGL)
    {
        glfwInitialized = ImGui_ImplGlfw_InitForOpenGL(window.Handle(), true);
        rendererInitialized = glfwInitialized &&
            ImGui_ImplOpenGL3_Init("#version 460 core");
        initialized = rendererInitialized;
    }
    else
    {
#if ENGINE_HAS_VULKAN
        glfwInitialized = ImGui_ImplGlfw_InitForVulkan(window.Handle(), true);
        initialized = glfwInitialized;
        if (glfwInitialized)
        {
            ImGui_ImplVulkan_InitInfo info{};
            info.ApiVersion = VK_API_VERSION_1_3;
            info.Instance = reinterpret_cast<VkInstance>(native.Instance);
            info.PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(native.PhysicalDevice);
            info.Device = reinterpret_cast<VkDevice>(native.Device);
            info.QueueFamily = native.QueueFamily;
            info.Queue = reinterpret_cast<VkQueue>(native.Queue);
            info.DescriptorPoolSize = 2048;
            info.MinImageCount = std::max(native.MinImageCount, 2u);
            info.ImageCount = std::max(native.ImageCount, info.MinImageCount);
            info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            info.UseDynamicRendering = true;
            const VkFormat colorFormat = static_cast<VkFormat>(native.ColorFormat);
            info.PipelineRenderingCreateInfo = {
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
            info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
            info.CheckVkResultFn = CheckVulkanResult;
            rendererInitialized = ImGui_ImplVulkan_Init(&info);
            initialized = rendererInitialized;
        }
#else
        (void)window;
#endif
    }
    if (!initialized)
    {
        if (native.Api == RenderBackendApi::OpenGL && rendererInitialized)
            ImGui_ImplOpenGL3_Shutdown();
        if (glfwInitialized)
            ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_backend = nullptr;
        return false;
    }
    backend.SetUiRenderCallback(
        [this](const NativeUiRenderContext& context) { Render(context); });
    m_initialized = true;
    log::Info("ImGui", "Docking UI initialized for " +
        std::string(native.Api == RenderBackendApi::OpenGL ? "OpenGL" : "Vulkan"));
    return true;
}

void ImGuiLayer::Shutdown()
{
    if (!m_initialized)
        return;
    if (m_backend)
        m_backend->WaitIdle();
    if (m_backend)
        m_backend->SetUiRenderCallback({});
#if ENGINE_HAS_VULKAN
    if (m_api == RenderBackendApi::Vulkan)
    {
        for (const auto& [viewport, binding] : m_textureBindings)
        {
            (void)viewport;
            if (binding.Id != 0)
                ImGui_ImplVulkan_RemoveTexture(
                    reinterpret_cast<VkDescriptorSet>(binding.Id));
        }
        ImGui_ImplVulkan_Shutdown();
    }
    else
#endif
    {
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_textureBindings.clear();
    m_iniFilename.clear();
    m_backend = nullptr;
    m_initialized = false;
    m_frameBegun = false;
    m_drawDataReady = false;
}

void ImGuiLayer::BeginFrame()
{
    if (!m_initialized || m_frameBegun)
        return;
    if (m_api == RenderBackendApi::OpenGL)
        ImGui_ImplOpenGL3_NewFrame();
#if ENGINE_HAS_VULKAN
    else
        ImGui_ImplVulkan_NewFrame();
#endif
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_frameBegun = true;
    m_drawDataReady = false;
}

void ImGuiLayer::EndFrame()
{
    if (!m_initialized || !m_frameBegun)
        return;
    if (m_buildCallback)
        m_buildCallback();
    ImGui::Render();
    m_drawDataReady = true;
    m_frameBegun = false;
}

bool ImGuiLayer::WantsKeyboard() const
{
    return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiLayer::WantsMouse() const
{
    return m_initialized && ImGui::GetIO().WantCaptureMouse;
}

ImTextureID ImGuiLayer::TextureId(const RenderTextureHandle& texture)
{
    if (!m_initialized || !texture || texture.Api != m_api)
        return 0;
    if (texture.Api == RenderBackendApi::OpenGL)
        return static_cast<ImTextureID>(texture.NativeTexture);
#if ENGINE_HAS_VULKAN
    auto found = m_textureBindings.find(texture.Viewport.Value);
    if (found != m_textureBindings.end() &&
        found->second.Generation == texture.Generation)
        return found->second.Id;
    if (found != m_textureBindings.end() && found->second.Id != 0)
        ImGui_ImplVulkan_RemoveTexture(
            reinterpret_cast<VkDescriptorSet>(found->second.Id));
    const VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
        reinterpret_cast<VkSampler>(texture.NativeSampler),
        reinterpret_cast<VkImageView>(texture.NativeImageView),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const ImTextureID id = reinterpret_cast<uint64_t>(descriptor);
    m_textureBindings[texture.Viewport.Value] = {texture.Generation, id};
    return id;
#else
    return 0;
#endif
}

void ImGuiLayer::ForgetTexture(RenderViewportHandle viewport)
{
    const auto found = m_textureBindings.find(viewport.Value);
    if (found == m_textureBindings.end())
        return;
#if ENGINE_HAS_VULKAN
    if (m_api == RenderBackendApi::Vulkan && found->second.Id != 0)
        ImGui_ImplVulkan_RemoveTexture(
            reinterpret_cast<VkDescriptorSet>(found->second.Id));
#endif
    m_textureBindings.erase(found);
}

void ImGuiLayer::DrawViewportImage(const RenderTextureHandle& texture,
                                   const ImVec2& size, const ImVec4& tint)
{
    const ImTextureID id = TextureId(texture);
    if (id == 0)
        return;
    // OpenGL render targets have a bottom-left origin, Vulkan has top-left.
    const ImVec2 uv0 = texture.Api == RenderBackendApi::OpenGL
        ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
    const ImVec2 uv1 = texture.Api == RenderBackendApi::OpenGL
        ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
    ImGui::ImageWithBg(id, size, uv0, uv1, ImVec4(0, 0, 0, 0), tint);
}

void ImGuiLayer::Render(const NativeUiRenderContext& context)
{
    if (!m_initialized || !m_drawDataReady || context.Api != m_api)
        return;
    if (m_api == RenderBackendApi::OpenGL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if ENGINE_HAS_VULKAN
    else
        ImGui_ImplVulkan_RenderDrawData(
            ImGui::GetDrawData(),
            reinterpret_cast<VkCommandBuffer>(context.CommandBuffer));
#endif
    if (m_platformViewports)
    {
        // The docking branch creates and renders secondary native windows
        // through the installed GLFW/API renderer backends. OpenGL changes
        // the current context while doing so, therefore restore the engine's
        // main context before control returns to the frame renderer.
        GLFWwindow* previousContext = m_api == RenderBackendApi::OpenGL
            ? glfwGetCurrentContext() : nullptr;
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        if (previousContext)
            glfwMakeContextCurrent(previousContext);
    }
    m_drawDataReady = false;
}

} // namespace engine::editor
