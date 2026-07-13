#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/debug/DebugOverlay.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/render/SceneRenderer.h"

#include <algorithm>
#include <cstring>

namespace engine {

void VulkanRenderBackend::PrepareDebugOverlay(const RenderFrameData& frame)
{
    if (!frame.DebugOverlay)
        return;
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PrepareDebugOverlay", "Renderer/Vulkan/Debug");
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(debug::DebugOverlayImage::Width)
                                 * debug::DebugOverlayImage::Height * 4;
    void* mapped = nullptr;
    if (vkMapMemory(m_device, m_debugOverlayStaging[m_currentFrame].Memory,
                    0, byteCount, 0, &mapped) != VK_SUCCESS)
        return;
    std::memcpy(mapped, frame.DebugOverlay->Pixels.data(), static_cast<size_t>(byteCount));
    vkUnmapMemory(m_device, m_debugOverlayStaging[m_currentFrame].Memory);
}

void VulkanRenderBackend::RecordDebugOverlay(VkCommandBuffer commandBuffer,
                                             const RenderFrameData& frame,
                                             uint32_t imageIndex)
{
    if (!frame.DebugOverlay || m_debugOverlayPipeline == VK_NULL_HANDLE)
        return;
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RecordDebugOverlay", "Renderer/Vulkan/Debug");

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

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = image.Extent;
    vkCmdCopyBufferToImage(commandBuffer, m_debugOverlayStaging[m_currentFrame].Handle,
                           image.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

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

    constexpr int32_t margin = 10;
    if (m_swapchainExtent.width <= static_cast<uint32_t>(margin)
        || m_swapchainExtent.height <= static_cast<uint32_t>(margin))
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

    VkViewport viewport{};
    viewport.x = static_cast<float>(margin);
    viewport.y = static_cast<float>(margin);
    viewport.width = static_cast<float>(debug::DebugOverlayImage::Width);
    viewport.height = static_cast<float>(debug::DebugOverlayImage::Height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = {margin, margin};
    scissor.extent.width = std::min(debug::DebugOverlayImage::Width,
        m_swapchainExtent.width - static_cast<uint32_t>(margin));
    scissor.extent.height = std::min(debug::DebugOverlayImage::Height,
        m_swapchainExtent.height - static_cast<uint32_t>(margin));
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_debugOverlayPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_debugOverlayPipelineLayout, 0, 1,
                            &m_debugOverlayDescriptorSets[m_currentFrame], 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(commandBuffer);
}

} // namespace engine
