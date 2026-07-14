#include "engine/backend/vk/VulkanRenderBackend.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine
{

void VulkanRenderBackend::CreateAsyncResourceInfrastructure()
{
    const QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *indices.Transfer;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_transferCommandPool) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create transfer command pool");
    SetDebugName(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(m_transferCommandPool),
                 "Asynchronous Transfer Command Pool");

    VkSemaphoreTypeCreateInfo timelineType{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineType.initialValue = 0;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphoreInfo.pNext = &timelineType;
    if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_transferTimeline) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create transfer timeline semaphore");
    SetDebugName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(m_transferTimeline),
                 "Asynchronous Transfer Timeline");

    m_stagingRingBuffer = m_resources.CreateBuffer(
        kStagingRingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_resources.SetDebugName(m_stagingRingBuffer, "Persistent Asset Upload Staging Ring");
    if (vkMapMemory(m_device, m_stagingRingBuffer.Memory, 0, kStagingRingBytes, 0,
                    &m_stagingRingMapped) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to map the asset staging ring");
    m_stagingRing = std::make_unique<render::StagingRingAllocator>(kStagingRingBytes);

    m_frameArenaBuffer = m_resources.CreateBuffer(
        kFrameArenaBytes * kFramesInFlight,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_resources.SetDebugName(m_frameArenaBuffer, "Per-frame GPU Linear Arena");
    if (vkMapMemory(m_device, m_frameArenaBuffer.Memory, 0, VK_WHOLE_SIZE, 0,
                    &m_frameArenaMapped) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to map the per-frame GPU arena");
    m_frameGpuArena = std::make_unique<render::FrameGpuArena>(kFrameArenaBytes, kFramesInFlight);
}

void VulkanRenderBackend::ReclaimTransferUploads()
{
    if (m_transferTimeline == VK_NULL_HANDLE || !m_stagingRing)
        return;
    uint64_t completed = 0;
    if (vkGetSemaphoreCounterValue(m_device, m_transferTimeline, &completed) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to query transfer timeline");
    m_stagingRing->Reclaim(completed);
    for (auto iterator = m_pendingTransferCommands.begin(); iterator != m_pendingTransferCommands.end();)
    {
        if (iterator->TimelineValue <= completed)
        {
            vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &iterator->Command);
            iterator = m_pendingTransferCommands.erase(iterator);
        }
        else
            ++iterator;
    }
}

uint64_t VulkanRenderBackend::QueueBufferUploads(
    const std::vector<std::pair<const void*, std::pair<VkDeviceSize, VkBuffer>>>& uploads)
{
    if (uploads.empty())
        return m_transferTimelineValue;
    ReclaimTransferUploads();
    uint64_t completed = 0;
    vkGetSemaphoreCounterValue(m_device, m_transferTimeline, &completed);

    std::vector<render::StagingAllocation> allocations;
    allocations.reserve(uploads.size());
    const auto discardAllocations = [&] {
        for (const render::StagingAllocation& allocation : allocations)
            m_stagingRing->Discard(allocation.Serial);
        allocations.clear();
    };
    const auto allocateBatch = [&]() {
        for (const auto& upload : uploads)
        {
            const render::StagingAllocation allocation = m_stagingRing->Allocate(
                upload.second.first, 16, completed);
            if (!allocation)
            {
                discardAllocations();
                return false;
            }
            std::memcpy(static_cast<std::byte*>(m_stagingRingMapped) + allocation.Offset,
                        upload.first, static_cast<size_t>(allocation.Size));
            allocations.push_back(allocation);
        }
        return true;
    };
    if (!allocateBatch())
    {
        if (m_transferTimelineValue > completed)
        {
            VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &m_transferTimeline;
            waitInfo.pValues = &m_transferTimelineValue;
            if (vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX) != VK_SUCCESS)
                throw std::runtime_error("Vulkan: failed waiting for staging-ring space");
            completed = m_transferTimelineValue;
            ReclaimTransferUploads();
        }
        if (!allocateBatch())
            throw std::runtime_error("Vulkan: upload is larger than the persistent staging-ring capacity");
    }

    VkCommandBufferAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocationInfo.commandPool = m_transferCommandPool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocationInfo, &commandBuffer) != VK_SUCCESS)
    {
        discardAllocations();
        throw std::runtime_error("Vulkan: failed to allocate transfer command buffer");
    }
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
        discardAllocations();
        throw std::runtime_error("Vulkan: failed to begin transfer command buffer");
    }

    std::vector<VkBufferMemoryBarrier2> barriers;
    barriers.reserve(uploads.size());
    for (size_t index = 0; index < uploads.size(); ++index)
    {
        const VkBufferCopy copy{allocations[index].Offset, 0, uploads[index].second.first};
        vkCmdCopyBuffer(commandBuffer, m_stagingRingBuffer.Handle, uploads[index].second.second, 1, &copy);
        VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = uploads[index].second.second;
        barrier.offset = 0;
        barrier.size = uploads[index].second.first;
        barriers.push_back(barrier);
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pBufferMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
        discardAllocations();
        throw std::runtime_error("Vulkan: failed to finish transfer commands");
    }

    const uint64_t signalValue = m_transferTimelineValue + 1;
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = m_transferTimeline;
    signalInfo.value = signalValue;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    if (vkQueueSubmit2(m_transferQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
        discardAllocations();
        throw std::runtime_error("Vulkan: failed to submit asynchronous transfer");
    }

    m_transferTimelineValue = signalValue;
    for (const render::StagingAllocation& allocation : allocations)
        m_stagingRing->Retire(allocation.Serial, signalValue);
    m_pendingTransferCommands.push_back({commandBuffer, signalValue});
    m_pendingTransferWaitValue = std::max(m_pendingTransferWaitValue, signalValue);
    return signalValue;
}

uint64_t VulkanRenderBackend::QueueTextureUpload(
    const void* pixels, VkDeviceSize byteCount, VkImage image, uint32_t mipLevels,
    const std::vector<VkBufferImageCopy>& copyRegions)
{
    if (!pixels || byteCount == 0 || image == VK_NULL_HANDLE ||
        mipLevels == 0 || copyRegions.empty())
        throw std::invalid_argument("Vulkan: invalid asynchronous texture upload request");

    ReclaimTransferUploads();
    uint64_t completed = 0;
    if (vkGetSemaphoreCounterValue(m_device, m_transferTimeline, &completed) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to query transfer timeline for texture upload");

    render::StagingAllocation allocation = m_stagingRing->Allocate(byteCount, 16, completed);
    if (!allocation && m_transferTimelineValue > completed)
    {
        VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_transferTimeline;
        waitInfo.pValues = &m_transferTimelineValue;
        if (vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed waiting for texture staging-ring space");
        completed = m_transferTimelineValue;
        ReclaimTransferUploads();
        allocation = m_stagingRing->Allocate(byteCount, 16, completed);
    }
    if (!allocation)
        throw std::runtime_error("Vulkan: texture upload is larger than the persistent staging-ring capacity");
    std::memcpy(static_cast<std::byte*>(m_stagingRingMapped) + allocation.Offset,
                pixels, static_cast<size_t>(byteCount));

    VkCommandBufferAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocationInfo.commandPool = m_transferCommandPool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocationInfo, &commandBuffer) != VK_SUCCESS)
    {
        m_stagingRing->Discard(allocation.Serial);
        throw std::runtime_error("Vulkan: failed to allocate texture transfer command buffer");
    }
    const auto discard = [&] {
        vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
        m_stagingRing->Discard(allocation.Serial);
    };

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        discard();
        throw std::runtime_error("Vulkan: failed to begin texture transfer command buffer");
    }

    VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    std::vector<VkBufferImageCopy> adjustedRegions = copyRegions;
    for (VkBufferImageCopy& region : adjustedRegions)
        region.bufferOffset += allocation.Offset;
    vkCmdCopyBufferToImage(commandBuffer, m_stagingRingBuffer.Handle, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(adjustedRegions.size()), adjustedRegions.data());

    VkImageMemoryBarrier2 toShader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toShader.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toShader.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.image = image;
    toShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toShader;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        discard();
        throw std::runtime_error("Vulkan: failed to finish texture transfer commands");
    }

    const uint64_t signalValue = m_transferTimelineValue + 1;
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = m_transferTimeline;
    signalInfo.value = signalValue;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    if (vkQueueSubmit2(m_transferQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        discard();
        throw std::runtime_error("Vulkan: failed to submit asynchronous texture transfer");
    }

    m_transferTimelineValue = signalValue;
    m_stagingRing->Retire(allocation.Serial, signalValue);
    m_pendingTransferCommands.push_back({commandBuffer, signalValue});
    m_pendingTransferWaitValue = std::max(m_pendingTransferWaitValue, signalValue);
    return signalValue;
}

void VulkanRenderBackend::DestroyAsyncResourceInfrastructure()
{
    m_deferredRelease.Flush();
    m_pendingTransferCommands.clear();
    if (m_frameArenaMapped)
        vkUnmapMemory(m_device, m_frameArenaBuffer.Memory);
    m_frameArenaMapped = nullptr;
    if (m_stagingRingMapped)
        vkUnmapMemory(m_device, m_stagingRingBuffer.Memory);
    m_stagingRingMapped = nullptr;
    m_resources.Destroy(m_frameArenaBuffer);
    m_resources.Destroy(m_stagingRingBuffer);
    m_frameGpuArena.reset();
    m_stagingRing.reset();
    if (m_transferTimeline != VK_NULL_HANDLE)
        vkDestroySemaphore(m_device, m_transferTimeline, nullptr);
    if (m_transferCommandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_device, m_transferCommandPool, nullptr);
    m_transferTimeline = VK_NULL_HANDLE;
    m_transferCommandPool = VK_NULL_HANDLE;
}

} // namespace engine
