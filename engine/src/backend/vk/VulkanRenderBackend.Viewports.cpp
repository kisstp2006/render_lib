#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/core/Window.h"
#include "engine/render/SceneRenderer.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace engine
{

struct VulkanRenderBackend::ViewTargetState
{
    VkExtent2D Extent{};
    std::vector<VkImage> OutputImages;
    std::vector<VkImageView> OutputViews;
    std::vector<vulkan::Image> DepthImages;
    std::array<vulkan::Image, kFramesInFlight> MsaaDepth{};

    VkDescriptorPool PostDescriptorPool = VK_NULL_HANDLE;
    std::vector<vulkan::Image> HdrImages;
    std::vector<vulkan::Image> LdrImages;
    std::vector<vulkan::Image> VelocityImages;
    std::array<vulkan::Image, kFramesInFlight> MsaaHdr{};
    std::array<vulkan::Image, kFramesInFlight> MsaaVelocity{};
    std::vector<BloomChain> BloomChains;
    std::array<vulkan::Image, 2> TaaColor{};
    std::array<vulkan::Image, 2> TaaDepth{};
    std::vector<vulkan::Buffer> PostUniformBuffers;
    std::vector<VkDescriptorSet> PostDescriptorSets;
    std::vector<VkDescriptorSet> FxaaDescriptorSets;
    std::vector<VkDescriptorSet> TaaDescriptorSets;
    std::vector<VkDescriptorSet> ExposureDescriptorSets;
    std::array<vulkan::Buffer, kFramesInFlight> ExposureBuffers{};
    std::array<bool, kFramesInFlight> ExposureReadbackValid{};
    std::array<vulkan::Image, kFramesInFlight> DebugOverlayImages{};
    std::array<vulkan::Buffer, kFramesInFlight> DebugOverlayStaging{};
    std::array<VkDescriptorSet, kFramesInFlight> DebugOverlayDescriptorSets{};
    std::array<bool, kFramesInFlight> DebugOverlayInitialized{};
    std::array<uint64_t, kFramesInFlight> DebugOverlayRevisions{};
    float AutoExposure = 1.0f;
    double LastExposureTime = 0.0;
    bool TaaActive = false;
    bool TaaHistoryValid = false;
    int TaaHistoryIndex = 0;
    uint64_t TaaFrameIndex = 0;
    glm::mat4 CurrentViewProjection{1.0f};
    glm::mat4 PreviousViewProjection{1.0f};
    glm::vec3 PreviousCameraPosition{0.0f};
    glm::vec3 PreviousCameraForward{0.0f, 0.0f, -1.0f};
    float PreviousCameraFov = 60.0f;

    VkDescriptorPool OcclusionDescriptorPool = VK_NULL_HANDLE;
    vulkan::Image HiZImage;
    std::vector<VkImageView> HiZMipViews;
    std::vector<VkDescriptorSet> HiZCopySets;
    std::vector<VkDescriptorSet> HiZReduceSets;
    std::array<VkDescriptorSet, kFramesInFlight> OcclusionSets{};
    std::array<OcclusionFrameSlot, kFramesInFlight> OcclusionSlots{};
    TemporalOcclusionState OcclusionState;
    std::unordered_set<uint32_t> OcclusionCulled;
    glm::mat4 HiZViewProjection{1.0f};
    uint32_t HiZMipLevels = 1;
    bool HiZInitialized = false;
    bool HiZValid = false;
    const Scene* PreviousScene = nullptr;
    AntiAliasingMode PreviousAa = AntiAliasingMode::None;
    std::unordered_map<uint64_t, glm::mat4> PreviousTransforms;

    uint32_t LastFrameDebugFrameSlot = 0;
    uint32_t LastFrameDebugImageIndex = 0;
    bool LastFrameDebugTaaActive = false;
    bool LastFrameDebugFxaaActive = false;
    bool LastFrameDebugPostEnabled = false;
    bool HasFrameDebugFrame = false;
};

struct VulkanRenderBackend::OffscreenViewport
{
    RenderViewportDesc Desc;
    vulkan::Image Output;
    ViewTargetState Targets;
    uint64_t Generation = 1;
    bool Initialized = false;
};

struct VulkanRenderBackend::ViewportStorage
{
    std::unordered_map<uint64_t, std::unique_ptr<OffscreenViewport>> Items;
};

VulkanRenderBackend::VulkanRenderBackend() = default;
VulkanRenderBackend::~VulkanRenderBackend() = default;

void VulkanRenderBackend::SwapViewTargetState(ViewTargetState& state)
{
    using std::swap;
    swap(m_swapchainExtent, state.Extent);
    swap(m_swapchainImages, state.OutputImages);
    swap(m_swapchainImageViews, state.OutputViews);
    swap(m_depthImages, state.DepthImages);
    swap(m_msaaDepthImages, state.MsaaDepth);
    swap(m_postDescriptorPool, state.PostDescriptorPool);
    swap(m_hdrImages, state.HdrImages);
    swap(m_ldrImages, state.LdrImages);
    swap(m_velocityImages, state.VelocityImages);
    swap(m_msaaHdrImages, state.MsaaHdr);
    swap(m_msaaVelocityImages, state.MsaaVelocity);
    swap(m_bloomChains, state.BloomChains);
    swap(m_taaHistoryColor, state.TaaColor);
    swap(m_taaHistoryDepth, state.TaaDepth);
    swap(m_postUniformBuffers, state.PostUniformBuffers);
    swap(m_postDescriptorSets, state.PostDescriptorSets);
    swap(m_fxaaDescriptorSets, state.FxaaDescriptorSets);
    swap(m_taaDescriptorSets, state.TaaDescriptorSets);
    swap(m_exposureDescriptorSets, state.ExposureDescriptorSets);
    swap(m_exposureBuffers, state.ExposureBuffers);
    swap(m_exposureReadbackValid, state.ExposureReadbackValid);
    swap(m_debugOverlayImages, state.DebugOverlayImages);
    swap(m_debugOverlayStaging, state.DebugOverlayStaging);
    swap(m_debugOverlayDescriptorSets, state.DebugOverlayDescriptorSets);
    swap(m_debugOverlayImageInitialized, state.DebugOverlayInitialized);
    swap(m_debugOverlayRevisions, state.DebugOverlayRevisions);
    swap(m_autoExposure, state.AutoExposure);
    swap(m_lastExposureTime, state.LastExposureTime);
    swap(m_taaActive, state.TaaActive);
    swap(m_taaHistoryValid, state.TaaHistoryValid);
    swap(m_taaHistoryIndex, state.TaaHistoryIndex);
    swap(m_taaFrameIndex, state.TaaFrameIndex);
    swap(m_currentViewProjection, state.CurrentViewProjection);
    swap(m_previousViewProjection, state.PreviousViewProjection);
    swap(m_previousCameraPosition, state.PreviousCameraPosition);
    swap(m_previousCameraForward, state.PreviousCameraForward);
    swap(m_previousCameraFov, state.PreviousCameraFov);
    swap(m_occlusionDescriptorPool, state.OcclusionDescriptorPool);
    swap(m_hizImage, state.HiZImage);
    swap(m_hizMipViews, state.HiZMipViews);
    swap(m_hizCopyDescriptorSets, state.HiZCopySets);
    swap(m_hizReduceDescriptorSets, state.HiZReduceSets);
    swap(m_occlusionDescriptorSets, state.OcclusionSets);
    swap(m_occlusionSlots, state.OcclusionSlots);
    swap(m_occlusionState, state.OcclusionState);
    swap(m_occlusionCulledInstances, state.OcclusionCulled);
    swap(m_hizViewProjection, state.HiZViewProjection);
    swap(m_hizMipLevels, state.HiZMipLevels);
    swap(m_hizInitialized, state.HiZInitialized);
    swap(m_hizValid, state.HiZValid);
    swap(m_previousScene, state.PreviousScene);
    swap(m_previousAaMode, state.PreviousAa);
    swap(m_previousTransforms, state.PreviousTransforms);
    swap(m_lastFrameDebugFrameSlot, state.LastFrameDebugFrameSlot);
    swap(m_lastFrameDebugImageIndex, state.LastFrameDebugImageIndex);
    swap(m_lastFrameDebugTaaActive, state.LastFrameDebugTaaActive);
    swap(m_lastFrameDebugFxaaActive, state.LastFrameDebugFxaaActive);
    swap(m_lastFrameDebugPostEnabled, state.LastFrameDebugPostEnabled);
    swap(m_hasFrameDebugFrame, state.HasFrameDebugFrame);
}

void VulkanRenderBackend::CreateViewportTargets(OffscreenViewport& viewport)
{
    const uint32_t width = std::max(viewport.Desc.Width, 1u);
    const uint32_t height = std::max(viewport.Desc.Height, 1u);
    viewport.Output = m_resources.CreateImage2D(
        width, height, m_swapchainFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    m_resources.SetDebugName(viewport.Output, viewport.Desc.Name + " Output");

    SwapViewTargetState(viewport.Targets);
    m_swapchainExtent = {width, height};
    m_swapchainImages = {viewport.Output.Handle};
    m_swapchainImageViews = {viewport.Output.View};
    try
    {
        CreateDepthResources();
        CreatePostTargets();
        CreateOcclusionTargets();
    }
    catch (...)
    {
        DestroyOcclusionTargets();
        DestroyPostTargets();
        for (vulkan::Image& image : m_depthImages) m_resources.Destroy(image);
        m_depthImages.clear();
        for (vulkan::Image& image : m_msaaDepthImages) m_resources.Destroy(image);
        SwapViewTargetState(viewport.Targets);
        m_resources.Destroy(viewport.Output);
        throw;
    }
    SwapViewTargetState(viewport.Targets);
    viewport.Initialized = false;
}

void VulkanRenderBackend::DestroyViewportTargets(OffscreenViewport& viewport)
{
    SwapViewTargetState(viewport.Targets);
    DestroyOcclusionTargets();
    DestroyPostTargets();
    for (vulkan::Image& image : m_depthImages) m_resources.Destroy(image);
    m_depthImages.clear();
    for (vulkan::Image& image : m_msaaDepthImages) m_resources.Destroy(image);
    m_swapchainImages.clear();
    m_swapchainImageViews.clear();
    SwapViewTargetState(viewport.Targets);
    m_resources.Destroy(viewport.Output);
}

RenderViewportHandle VulkanRenderBackend::CreateViewport(const RenderViewportDesc& requested)
{
    if (m_device == VK_NULL_HANDLE)
        return {};
    if (!m_viewports)
        m_viewports = std::make_unique<ViewportStorage>();
    auto viewport = std::make_unique<OffscreenViewport>();
    viewport->Desc = requested;
    viewport->Desc.Width = std::max(requested.Width, 1u);
    viewport->Desc.Height = std::max(requested.Height, 1u);
    vkDeviceWaitIdle(m_device);
    CreateViewportTargets(*viewport);
    const RenderViewportHandle handle{m_nextViewportId++};
    m_viewports->Items.emplace(handle.Value, std::move(viewport));
    return handle;
}

bool VulkanRenderBackend::ResizeViewport(RenderViewportHandle handle,
                                         uint32_t width, uint32_t height)
{
    if (!m_viewports)
        return false;
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end() || width == 0 || height == 0)
        return false;
    OffscreenViewport& viewport = *found->second;
    if (viewport.Desc.Width == width && viewport.Desc.Height == height)
        return true;
    // Build the new complete target set first. If allocation fails, callers
    // can keep showing the previous generation instead of receiving a dead
    // image handle from a partially resized viewport.
    OffscreenViewport replacement;
    replacement.Desc = viewport.Desc;
    replacement.Desc.Width = width;
    replacement.Desc.Height = height;
    replacement.Generation = viewport.Generation + 1;
    CreateViewportTargets(replacement);
    vkDeviceWaitIdle(m_device);
    DestroyViewportTargets(viewport);
    viewport = std::move(replacement);
    return true;
}

void VulkanRenderBackend::DestroyViewport(RenderViewportHandle handle)
{
    if (!m_viewports)
        return;
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end())
        return;
    vkDeviceWaitIdle(m_device);
    DestroyViewportTargets(*found->second);
    m_viewports->Items.erase(found);
}

bool VulkanRenderBackend::RenderViewport(RenderViewportHandle handle,
                                         const RenderFrameData& frame)
{
    if (!m_viewports)
        return false;
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end() || !frame.SceneData || !frame.CameraData)
        return false;
    OffscreenViewport& viewport = *found->second;
    if (frame.Width != static_cast<int>(viewport.Desc.Width) ||
        frame.Height != static_cast<int>(viewport.Desc.Height))
        return false;
    SwapViewTargetState(viewport.Targets);
    m_activeOffscreenViewport = &viewport;
    m_activeOffscreenWasInitialized = viewport.Initialized;
    try
    {
        RenderFrame(frame);
    }
    catch (...)
    {
        m_activeOffscreenViewport = nullptr;
        m_activeOffscreenWasInitialized = false;
        SwapViewTargetState(viewport.Targets);
        throw;
    }
    m_activeOffscreenViewport = nullptr;
    m_activeOffscreenWasInitialized = false;
    SwapViewTargetState(viewport.Targets);
    viewport.Initialized = true;
    return true;
}

RenderTextureHandle VulkanRenderBackend::GetViewportTexture(
    RenderViewportHandle handle) const
{
    if (!m_viewports)
        return {};
    const auto found = m_viewports->Items.find(handle.Value);
    if (found == m_viewports->Items.end())
        return {};
    const OffscreenViewport& viewport = *found->second;
    return {RenderBackendApi::Vulkan, handle, 0,
            reinterpret_cast<uint64_t>(viewport.Output.View),
            reinterpret_cast<uint64_t>(m_postSampler), viewport.Desc.Width,
            viewport.Desc.Height, viewport.Generation};
}

NativeGraphicsContext VulkanRenderBackend::GetNativeGraphicsContext() const
{
    NativeGraphicsContext result;
    result.Api = RenderBackendApi::Vulkan;
    result.Window = m_window ? m_window->Handle() : nullptr;
    result.Instance = reinterpret_cast<uint64_t>(m_instance);
    result.PhysicalDevice = reinterpret_cast<uint64_t>(m_physicalDevice);
    result.Device = reinterpret_cast<uint64_t>(m_device);
    result.Queue = reinterpret_cast<uint64_t>(m_graphicsQueue);
    result.ColorFormat = static_cast<uint32_t>(m_swapchainFormat);
    result.ImageCount = static_cast<uint32_t>(m_swapchainImages.size());
    result.MinImageCount = std::min(result.ImageCount, 2u);
    const QueueFamilyIndices queues = FindQueueFamilies(m_physicalDevice);
    result.QueueFamily = queues.Graphics.value_or(0);
    return result;
}

void VulkanRenderBackend::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);
}

void VulkanRenderBackend::DestroyAllViewports()
{
    if (!m_viewports)
        return;
    while (!m_viewports->Items.empty())
        DestroyViewport({m_viewports->Items.begin()->first});
    m_viewports.reset();
}

} // namespace engine
