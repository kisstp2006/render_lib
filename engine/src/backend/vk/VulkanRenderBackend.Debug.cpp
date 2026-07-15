#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/debug/DebugOverlay.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/render/SceneRenderer.h"

#include <algorithm>
#include <cstring>
#include <glm/vec4.hpp>

namespace engine {

void VulkanRenderBackend::LoadDebugUtils()
{
    if (!m_debugUtilsEnabled)
        return;
    m_setDebugObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT"));
    m_beginDebugLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdBeginDebugUtilsLabelEXT"));
    m_endDebugLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device, "vkCmdEndDebugUtilsLabelEXT"));
    SetDebugName(VK_OBJECT_TYPE_DEVICE, reinterpret_cast<uint64_t>(m_device),
                 "Renderer Vulkan Device");
    SetDebugName(VK_OBJECT_TYPE_QUEUE, reinterpret_cast<uint64_t>(m_graphicsQueue),
                 "Graphics Queue");
    SetDebugName(VK_OBJECT_TYPE_QUEUE, reinterpret_cast<uint64_t>(m_presentQueue),
                 "Present Queue");
}

void VulkanRenderBackend::SetDebugName(VkObjectType type, uint64_t handle,
                                       std::string_view name) const
{
    if (m_setDebugObjectName == nullptr || handle == 0 || name.empty())
        return;
    const std::string ownedName(name);
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = ownedName.c_str();
    m_setDebugObjectName(m_device, &info);
}

void VulkanRenderBackend::BeginDebugLabel(VkCommandBuffer commandBuffer,
                                          std::string_view name,
                                          const std::array<float, 4>& color) const
{
    if (m_beginDebugLabel == nullptr)
        return;
    const std::string ownedName(name);
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = ownedName.c_str();
    std::copy(color.begin(), color.end(), label.color);
    m_beginDebugLabel(commandBuffer, &label);
}

void VulkanRenderBackend::EndDebugLabel(VkCommandBuffer commandBuffer) const
{
    if (m_endDebugLabel != nullptr)
        m_endDebugLabel(commandBuffer);
}

void VulkanRenderBackend::PrepareDebugOverlay(const RenderFrameData& frame)
{
    if (!frame.DebugOverlay || frame.DebugOverlay->LayerCount == 0)
        return;
    if (m_debugOverlayRevisions[m_currentFrame] == frame.DebugOverlay->Revision)
        return;
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PrepareDebugOverlay", "Renderer/Vulkan/Debug");
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(debug::DebugOverlayImage::Width)
                                 * debug::DebugOverlayImage::Height * 4;
    void* mapped = nullptr;
    if (vkMapMemory(m_device, m_debugOverlayStaging[m_currentFrame].Memory,
                    0, byteCount, 0, &mapped) != VK_SUCCESS)
        return;
    auto* destination = static_cast<uint8_t*>(mapped);
    for (uint32_t index = 0; index < frame.DebugOverlay->LayerCount; ++index)
    {
        const debug::DebugOverlayLayer& layer = frame.DebugOverlay->Layers[index];
        for (uint32_t row = 0; row < layer.Height; ++row)
        {
            const size_t offset =
                (static_cast<size_t>(layer.SourceY + row) *
                     debug::DebugOverlayImage::TextureWidth +
                 layer.SourceX) * 4;
            std::memcpy(destination + offset,
                        frame.DebugOverlay->Pixels.data() + offset,
                        static_cast<size_t>(layer.Width) * 4);
        }
    }
    vkUnmapMemory(m_device, m_debugOverlayStaging[m_currentFrame].Memory);
}

void VulkanRenderBackend::RecordDebugOverlay(VkCommandBuffer commandBuffer,
                                             const RenderFrameData& frame,
                                             uint32_t imageIndex)
{
    if (!frame.DebugOverlay || frame.DebugOverlay->LayerCount == 0 ||
        m_debugOverlayPipeline == VK_NULL_HANDLE)
        return;
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RecordDebugOverlay", "Renderer/Vulkan/Debug");

    if (m_debugOverlayRevisions[m_currentFrame] != frame.DebugOverlay->Revision)
    {
        vulkan::Image& image = m_debugOverlayImages[m_currentFrame];
        VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toTransfer.srcStageMask = m_debugOverlayImageInitialized[m_currentFrame]
            ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE;
        toTransfer.srcAccessMask = m_debugOverlayImageInitialized[m_currentFrame]
            ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = m_debugOverlayImageInitialized[m_currentFrame]
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image.Handle;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);

        std::array<VkBufferImageCopy, debug::DebugOverlayImage::MaximumLayers> copies{};
        uint32_t copyCount = 0;
        for (uint32_t index = 0; index < frame.DebugOverlay->LayerCount; ++index)
        {
            const debug::DebugOverlayLayer& layer = frame.DebugOverlay->Layers[index];
            if (layer.Width == 0 || layer.Height == 0)
                continue;
            VkBufferImageCopy& copy = copies[copyCount++];
            copy.bufferOffset =
                (static_cast<VkDeviceSize>(layer.SourceY) *
                     debug::DebugOverlayImage::TextureWidth +
                 layer.SourceX) * 4;
            copy.bufferRowLength = debug::DebugOverlayImage::TextureWidth;
            copy.bufferImageHeight = debug::DebugOverlayImage::TextureHeight;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageOffset = {static_cast<int32_t>(layer.SourceX),
                                static_cast<int32_t>(layer.SourceY), 0};
            copy.imageExtent = {layer.Width, layer.Height, 1};
        }
        vkCmdCopyBufferToImage(commandBuffer, m_debugOverlayStaging[m_currentFrame].Handle,
                               image.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               copyCount, copies.data());

        VkImageMemoryBarrier2 toSample = toTransfer;
        toSample.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toSample.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toSample.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dependency.pImageMemoryBarriers = &toSample;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
        m_debugOverlayImageInitialized[m_currentFrame] = true;
        m_debugOverlayRevisions[m_currentFrame] = frame.DebugOverlay->Revision;
    }

    if (m_swapchainExtent.width == 0 || m_swapchainExtent.height == 0)
        return;

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = m_swapchainImageViews[imageIndex];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = m_swapchainExtent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(commandBuffer, &rendering);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_debugOverlayPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_debugOverlayPipelineLayout, 0, 1,
                            &m_debugOverlayDescriptorSets[m_currentFrame], 0, nullptr);
    for (uint32_t index = 0; index < frame.DebugOverlay->LayerCount; ++index)
    {
        const debug::DebugOverlayDrawRect rect = debug::ResolveDebugOverlayLayer(
            frame.DebugOverlay->Layers[index], m_swapchainExtent.width,
            m_swapchainExtent.height);
        if (rect.Width == 0 || rect.Height == 0)
            continue;
        VkViewport viewport{};
        viewport.x = static_cast<float>(rect.X);
        viewport.y = static_cast<float>(rect.Y);
        viewport.width = static_cast<float>(rect.Width);
        viewport.height = static_cast<float>(rect.Height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.offset = {rect.X, rect.Y};
        scissor.extent = {rect.Width, rect.Height};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        const glm::vec4 uvRect{
            static_cast<float>(rect.SourceX) / debug::DebugOverlayImage::TextureWidth,
            static_cast<float>(rect.SourceY) / debug::DebugOverlayImage::TextureHeight,
            static_cast<float>(rect.Width) / debug::DebugOverlayImage::TextureWidth,
            static_cast<float>(rect.Height) / debug::DebugOverlayImage::TextureHeight};
        vkCmdPushConstants(commandBuffer, m_debugOverlayPipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uvRect), &uvRect);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++m_gpuDrawCallsThisFrame;
    }
    vkCmdEndRendering(commandBuffer);
}

} // namespace engine
