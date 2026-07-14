#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/render/SceneRenderer.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace engine
{

namespace
{

struct HiZBuildConstants
{
    glm::ivec4 Parameters{0};
};

struct OcclusionConstants
{
    glm::mat4 ViewProjection{1.0f};
    glm::vec4 Parameters{0.0f};
};

static_assert(sizeof(HiZBuildConstants) == 16);
static_assert(sizeof(OcclusionConstants) == 80);

VkImageView CreateMipView(VkDevice device, VkImage image, VkFormat format,
                          VkImageAspectFlags aspect, uint32_t mip)
{
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {aspect, mip, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z mip view");
    return view;
}

void ImageBarrier(VkCommandBuffer commandBuffer, VkImage image,
                  uint32_t mip, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
                  VkPipelineStageFlags2 destinationStage,
                  VkAccessFlags2 destinationAccess)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

} // namespace

void VulkanRenderBackend::CreateOcclusionInfrastructure()
{
    m_hizBuildShader = LoadShader("visibility/hiz_build.comp");
    m_occlusionTestShader = LoadShader("visibility/occlusion_test.comp");

    const VkDescriptorSetLayoutBinding buildBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
         VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(buildBindings));
    layoutInfo.pBindings = buildBindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                    &m_hizBuildDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z build descriptor layout");

    const VkDescriptorSetLayoutBinding cullBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(cullBindings));
    layoutInfo.pBindings = cullBindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                    &m_occlusionDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z culling descriptor layout");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = sizeof(HiZBuildConstants);
    VkPipelineLayoutCreateInfo pipelineLayout{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &m_hizBuildDescriptorLayout;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr,
                               &m_hizBuildPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z build pipeline layout");

    push.size = sizeof(OcclusionConstants);
    pipelineLayout.pSetLayouts = &m_occlusionDescriptorLayout;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr,
                               &m_occlusionPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z culling pipeline layout");

    const auto createPipeline = [&](VkShaderModule shader,
                                    VkPipelineLayout pipelineLayoutHandle,
                                    VkPipeline& pipeline) {
        VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shader;
        stage.pName = "main";
        VkComputePipelineCreateInfo info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = stage;
        info.layout = pipelineLayoutHandle;
        if (m_pipelineCache.CreateCompute(1, &info, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create Hi-Z compute pipeline");
    };
    createPipeline(m_hizBuildShader, m_hizBuildPipelineLayout,
                   m_hizBuildPipeline);
    createPipeline(m_occlusionTestShader, m_occlusionPipelineLayout,
                   m_occlusionPipeline);

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.minLod = 0.0f;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(m_device, &sampler, nullptr, &m_hizSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z sampler");

    SetDebugName(VK_OBJECT_TYPE_PIPELINE,
                 reinterpret_cast<uint64_t>(m_hizBuildPipeline),
                 "Visibility Hi-Z Pyramid Build Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE,
                 reinterpret_cast<uint64_t>(m_occlusionPipeline),
                 "Visibility Hi-Z Occlusion Cull Pipeline");
}

void VulkanRenderBackend::DestroyOcclusionInfrastructure()
{
    if (m_hizSampler) vkDestroySampler(m_device, m_hizSampler, nullptr);
    if (m_occlusionPipeline) vkDestroyPipeline(m_device, m_occlusionPipeline, nullptr);
    if (m_hizBuildPipeline) vkDestroyPipeline(m_device, m_hizBuildPipeline, nullptr);
    if (m_occlusionPipelineLayout) vkDestroyPipelineLayout(m_device, m_occlusionPipelineLayout, nullptr);
    if (m_hizBuildPipelineLayout) vkDestroyPipelineLayout(m_device, m_hizBuildPipelineLayout, nullptr);
    if (m_occlusionDescriptorLayout) vkDestroyDescriptorSetLayout(m_device, m_occlusionDescriptorLayout, nullptr);
    if (m_hizBuildDescriptorLayout) vkDestroyDescriptorSetLayout(m_device, m_hizBuildDescriptorLayout, nullptr);
    if (m_occlusionTestShader) vkDestroyShaderModule(m_device, m_occlusionTestShader, nullptr);
    if (m_hizBuildShader) vkDestroyShaderModule(m_device, m_hizBuildShader, nullptr);
    m_hizSampler = VK_NULL_HANDLE;
    m_occlusionPipeline = VK_NULL_HANDLE;
    m_hizBuildPipeline = VK_NULL_HANDLE;
    m_occlusionPipelineLayout = VK_NULL_HANDLE;
    m_hizBuildPipelineLayout = VK_NULL_HANDLE;
    m_occlusionDescriptorLayout = VK_NULL_HANDLE;
    m_hizBuildDescriptorLayout = VK_NULL_HANDLE;
    m_occlusionTestShader = VK_NULL_HANDLE;
    m_hizBuildShader = VK_NULL_HANDLE;
}

void VulkanRenderBackend::CreateOcclusionTargets()
{
    m_hizMipLevels = 1u + static_cast<uint32_t>(std::floor(std::log2(
        static_cast<double>(std::max(m_swapchainExtent.width,
                                     m_swapchainExtent.height)))));
    m_hizImage = m_resources.CreateImage2D(
        m_swapchainExtent.width, m_swapchainExtent.height,
        VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, m_hizMipLevels);
    m_resources.SetDebugName(m_hizImage, "Visibility Hi-Z Maximum Depth Pyramid");

    m_hizMipViews.resize(m_hizMipLevels);
    for (uint32_t mip = 0; mip < m_hizMipLevels; ++mip)
    {
        m_hizMipViews[mip] = CreateMipView(m_device, m_hizImage.Handle,
                                           VK_FORMAT_R32_SFLOAT,
                                           VK_IMAGE_ASPECT_COLOR_BIT, mip);
        SetDebugName(VK_OBJECT_TYPE_IMAGE_VIEW,
                     reinterpret_cast<uint64_t>(m_hizMipViews[mip]),
                     "Visibility Hi-Z Mip " + std::to_string(mip));
    }

    const uint32_t buildSetCount = static_cast<uint32_t>(m_depthImages.size()) +
        std::max(m_hizMipLevels - 1u, 0u);
    const uint32_t totalSets = buildSetCount + kFramesInFlight;
    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         buildSetCount + kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, buildSetCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight * 2u},
    };
    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = totalSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr,
                               &m_occlusionDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create Hi-Z descriptor pool");

    const auto allocateSets = [&](VkDescriptorSetLayout layout, uint32_t count,
                                  VkDescriptorSet* output) {
        if (count == 0)
            return;
        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = m_occlusionDescriptorPool;
        allocation.descriptorSetCount = count;
        allocation.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(m_device, &allocation, output) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to allocate Hi-Z descriptor sets");
    };

    m_hizCopyDescriptorSets.resize(m_depthImages.size());
    allocateSets(m_hizBuildDescriptorLayout,
                 static_cast<uint32_t>(m_hizCopyDescriptorSets.size()),
                 m_hizCopyDescriptorSets.data());
    m_hizReduceDescriptorSets.resize(m_hizMipLevels - 1u);
    allocateSets(m_hizBuildDescriptorLayout,
                 static_cast<uint32_t>(m_hizReduceDescriptorSets.size()),
                 m_hizReduceDescriptorSets.data());
    allocateSets(m_occlusionDescriptorLayout, kFramesInFlight,
                 m_occlusionDescriptorSets.data());

    const auto updateBuildSet = [&](VkDescriptorSet set, VkImageView source,
                                    VkImageLayout sourceLayout,
                                    VkImageView target) {
        VkDescriptorImageInfo sourceInfo{m_hizSampler, source, sourceLayout};
        VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, target,
                                         VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &targetInfo, nullptr, nullptr},
        };
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(std::size(writes)),
                               writes, 0, nullptr);
    };
    for (size_t image = 0; image < m_depthImages.size(); ++image)
    {
        updateBuildSet(m_hizCopyDescriptorSets[image], m_depthImages[image].View,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                       m_hizMipViews[0]);
    }
    for (uint32_t mip = 1; mip < m_hizMipLevels; ++mip)
    {
        updateBuildSet(m_hizReduceDescriptorSets[mip - 1u],
                       m_hizMipViews[mip - 1u],
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       m_hizMipViews[mip]);
    }
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
        EnsureOcclusionCapacity(frame, 64);

    m_hizInitialized = false;
    m_hizValid = false;
}

void VulkanRenderBackend::DestroyOcclusionTargets()
{
    for (OcclusionFrameSlot& slot : m_occlusionSlots)
    {
        m_resources.Destroy(slot.CandidateBuffer);
        m_resources.Destroy(slot.ResultBuffer);
        slot = {};
    }
    if (m_occlusionDescriptorPool)
        vkDestroyDescriptorPool(m_device, m_occlusionDescriptorPool, nullptr);
    m_occlusionDescriptorPool = VK_NULL_HANDLE;
    m_occlusionDescriptorSets = {};
    m_hizCopyDescriptorSets.clear();
    m_hizReduceDescriptorSets.clear();
    for (VkImageView view : m_hizMipViews)
        vkDestroyImageView(m_device, view, nullptr);
    m_hizMipViews.clear();
    m_resources.Destroy(m_hizImage);
    m_occlusionState.Reset();
    m_occlusionCulledInstances.clear();
    m_hizInitialized = false;
    m_hizValid = false;
}

void VulkanRenderBackend::EnsureOcclusionCapacity(uint32_t frameIndex,
                                                   size_t count)
{
    OcclusionFrameSlot& slot = m_occlusionSlots[frameIndex];
    if (slot.Capacity >= count)
        return;
    slot.Capacity = std::max<size_t>(64, std::bit_ceil(count));
    m_resources.Destroy(slot.CandidateBuffer);
    m_resources.Destroy(slot.ResultBuffer);
    slot.CandidateBuffer = m_resources.CreateBuffer(
        slot.Capacity * sizeof(GpuOcclusionBounds),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    slot.ResultBuffer = m_resources.CreateBuffer(
        slot.Capacity * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_resources.SetDebugName(slot.CandidateBuffer,
        "Hi-Z Candidate Bounds Frame " + std::to_string(frameIndex));
    m_resources.SetDebugName(slot.ResultBuffer,
        "Hi-Z Visibility Results Frame " + std::to_string(frameIndex));

    VkDescriptorImageInfo hizInfo{m_hizSampler, m_hizImage.View,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo candidateInfo{
        slot.CandidateBuffer.Handle, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo resultInfo{
        slot.ResultBuffer.Handle, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         m_occlusionDescriptorSets[frameIndex], 0, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hizInfo, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         m_occlusionDescriptorSets[frameIndex], 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &candidateInfo, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         m_occlusionDescriptorSets[frameIndex], 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &resultInfo, nullptr},
    };
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(std::size(writes)),
                           writes, 0, nullptr);
}

void VulkanRenderBackend::PrepareOcclusionFrame(
    const RenderFrameData& frame,
    std::vector<const PreparedRenderCommand*>& visibleCommands)
{
    const VisibilitySettings& settings = frame.SceneData->Visibility;
    const bool historyReset = m_occlusionState.BeginFrame(
        BuildOcclusionFrameSignature(frame), settings);
    if (historyReset)
        m_hizValid = false;

    m_frameStats.GpuOcclusionActive = m_occlusionState.Active();
    m_frameStats.GpuOcclusionHistoryReset = historyReset;
    m_frameStats.GpuOcclusionCandidates = m_occlusionState.Active()
        ? static_cast<uint32_t>(std::count_if(
            frame.RenderCommands.begin(), frame.RenderCommands.end(),
            [](const PreparedRenderCommand& command)
            { return !command.Source->AlwaysVisible; }))
        : 0u;
    m_frameStats.GpuOcclusionCulled = 0;
    m_frameStats.GpuOcclusionResultsConsumed = 0;
    m_frameStats.HiZMipLevels = m_occlusionState.Active() ? m_hizMipLevels : 0u;
    m_frameStats.OcclusionReadbackLatencyFrames = 0;
    m_occlusionCulledInstances.clear();

    OcclusionFrameSlot& slot = m_occlusionSlots[m_currentFrame];
    if (slot.Issued)
    {
        std::vector<uint32_t> visibleBits(slot.Records.size(), 1u);
        if (!visibleBits.empty())
        {
            void* mapped = nullptr;
            if (vkMapMemory(m_device, slot.ResultBuffer.Memory, 0,
                            visibleBits.size() * sizeof(uint32_t), 0,
                            &mapped) != VK_SUCCESS)
                throw std::runtime_error("Vulkan: failed to map Hi-Z result buffer");
            std::memcpy(visibleBits.data(), mapped,
                        visibleBits.size() * sizeof(uint32_t));
            vkUnmapMemory(m_device, slot.ResultBuffer.Memory);
            if (slot.Generation == m_occlusionState.Generation())
            {
                m_occlusionState.ApplyResults(slot.Generation, slot.Records,
                                               visibleBits);
                m_frameStats.GpuOcclusionResultsConsumed =
                    static_cast<uint32_t>(visibleBits.size());
                m_frameStats.OcclusionReadbackLatencyFrames =
                    static_cast<uint32_t>(m_occlusionState.FrameIndex() -
                                          slot.SubmittedFrame);
            }
        }
        slot.Records.clear();
        slot.Issued = false;
    }

    visibleCommands.reserve(frame.RenderCommands.size());
    for (const PreparedRenderCommand& command : frame.RenderCommands)
    {
        const OcclusionQueryRecord record = MakeOcclusionQueryRecord(command);
        if (command.Source->AlwaysVisible || m_occlusionState.ShouldDraw(record))
            visibleCommands.push_back(&command);
        else
        {
            m_occlusionCulledInstances.insert(command.InstanceIndex);
            ++m_frameStats.GpuOcclusionCulled;
        }
    }
}

void VulkanRenderBackend::DispatchOcclusionQueries(
    VkCommandBuffer commandBuffer, const RenderFrameData& frame)
{
    if (!m_occlusionState.Active() || !m_hizValid ||
        frame.RenderCommands.empty())
        return;

    const size_t count = std::count_if(
        frame.RenderCommands.begin(), frame.RenderCommands.end(),
        [](const PreparedRenderCommand& command)
        { return !command.Source->AlwaysVisible; });
    if (count == 0)
        return;
    EnsureOcclusionCapacity(m_currentFrame, count);
    OcclusionFrameSlot& slot = m_occlusionSlots[m_currentFrame];
    std::vector<GpuOcclusionBounds> candidates;
    candidates.reserve(count);
    slot.Records.clear();
    slot.Records.reserve(count);
    const float inflation = frame.SceneData->Visibility.OcclusionBoundsInflation;
    for (const PreparedRenderCommand& command : frame.RenderCommands)
    {
        if (command.Source->AlwaysVisible)
            continue;
        candidates.push_back(MakeGpuOcclusionBounds(command, inflation));
        slot.Records.push_back(MakeOcclusionQueryRecord(command));
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_device, slot.CandidateBuffer.Memory, 0,
                    candidates.size() * sizeof(GpuOcclusionBounds), 0,
                    &mapped) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to map Hi-Z candidate buffer");
    std::memcpy(mapped, candidates.data(),
                candidates.size() * sizeof(GpuOcclusionBounds));
    vkUnmapMemory(m_device, slot.CandidateBuffer.Memory);

    BeginDebugLabel(commandBuffer, "Visibility / Hi-Z Occlusion Cull",
                    {0.55f, 0.20f, 0.90f, 1.0f});
    VkBufferMemoryBarrier2 bufferBarriers[2]{};
    for (VkBufferMemoryBarrier2& barrier : bufferBarriers)
    {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }
    bufferBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    bufferBarriers[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
    bufferBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    bufferBarriers[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    bufferBarriers[0].buffer = slot.CandidateBuffer.Handle;
    bufferBarriers[1] = bufferBarriers[0];
    bufferBarriers[1].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    bufferBarriers[1].buffer = slot.ResultBuffer.Handle;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 2;
    dependency.pBufferMemoryBarriers = bufferBarriers;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      m_occlusionPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_occlusionPipelineLayout, 0, 1,
                            &m_occlusionDescriptorSets[m_currentFrame], 0,
                            nullptr);
    OcclusionConstants constants;
    constants.ViewProjection = m_hizViewProjection;
    constants.Parameters = {
        std::max(frame.SceneData->Visibility.OcclusionDepthBias, 0.0f),
        static_cast<float>(m_hizMipLevels - 1u),
        static_cast<float>(count), 0.0f};
    vkCmdPushConstants(commandBuffer, m_occlusionPipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDispatch(commandBuffer, static_cast<uint32_t>((count + 63u) / 64u),
                  1, 1);
    ++m_gpuDispatchesThisFrame;

    VkBufferMemoryBarrier2 readback{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    readback.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    readback.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    readback.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    readback.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    readback.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readback.buffer = slot.ResultBuffer.Handle;
    readback.offset = 0;
    readback.size = VK_WHOLE_SIZE;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &readback;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    EndDebugLabel(commandBuffer);

    slot.Generation = m_occlusionState.Generation();
    slot.SubmittedFrame = m_occlusionState.FrameIndex();
    slot.Issued = true;
}

void VulkanRenderBackend::BuildHiZPyramid(VkCommandBuffer commandBuffer,
                                           uint32_t imageIndex)
{
    if (!m_occlusionState.Active() || m_hizImage.Handle == VK_NULL_HANDLE)
        return;

    BeginDebugLabel(commandBuffer, "Visibility / Build Hi-Z Pyramid",
                    {0.30f, 0.25f, 0.95f, 1.0f});
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      m_hizBuildPipeline);
    for (uint32_t mip = 0; mip < m_hizMipLevels; ++mip)
    {
        ImageBarrier(commandBuffer, m_hizImage.Handle, mip,
                     m_hizInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                      : VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_GENERAL,
                     m_hizInitialized ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                                      : VK_PIPELINE_STAGE_2_NONE,
                     m_hizInitialized ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const VkDescriptorSet set = mip == 0
            ? m_hizCopyDescriptorSets[imageIndex]
            : m_hizReduceDescriptorSets[mip - 1u];
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_hizBuildPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        HiZBuildConstants constants;
        // Every reduction descriptor exposes exactly one source mip. Texture
        // LODs are relative to that image view, therefore its sole valid LOD
        // is always zero even when the view starts at a higher image mip.
        constants.Parameters.x = 0;
        constants.Parameters.y = mip == 0 ? 1 : 0;
        vkCmdPushConstants(commandBuffer, m_hizBuildPipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                           &constants);
        const uint32_t width = std::max(m_swapchainExtent.width >> mip, 1u);
        const uint32_t height = std::max(m_swapchainExtent.height >> mip, 1u);
        vkCmdDispatch(commandBuffer, (width + 7u) / 8u,
                      (height + 7u) / 8u, 1);
        ++m_gpuDispatchesThisFrame;
        ImageBarrier(commandBuffer, m_hizImage.Handle, mip,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    EndDebugLabel(commandBuffer);
    m_hizViewProjection = m_currentViewProjection;
    m_hizInitialized = true;
    m_hizValid = true;
}

} // namespace engine
