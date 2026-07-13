#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/backend/vk/VulkanShaderInterop.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace engine {

void VulkanRenderBackend::CreateShadowResources()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create cascade shadow sampler");
    SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_shadowSampler),
                 "Directional Shadow PCF Sampler");

    try
    {
        std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight * 4, m_shadowDescriptorLayout);
        std::vector<VkDescriptorSet> sets(layouts.size());
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_descriptorPool;
        allocateInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocateInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(m_device, &allocateInfo, sets.data()) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to allocate cascade shadow descriptor sets");

        size_t setIndex = 0;
        for (int frame = 0; frame < kFramesInFlight; ++frame)
        {
            for (int cascade = 0; cascade < 4; ++cascade)
            {
                m_shadowMaps[frame][cascade] = m_resources.CreateImage2D(
                    m_shadowSizes[cascade], m_shadowSizes[cascade], m_depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
                m_shadowUniformBuffers[frame][cascade] = m_resources.CreateBuffer(
                    sizeof(vulkan::ShadowUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                m_resources.SetDebugName(m_shadowMaps[frame][cascade],
                    "Directional Shadow Frame " + std::to_string(frame) + " Cascade " +
                    std::to_string(cascade));
                m_resources.SetDebugName(m_shadowUniformBuffers[frame][cascade],
                    "Directional Shadow Uniforms Frame " + std::to_string(frame) +
                    " Cascade " + std::to_string(cascade));
                m_shadowDescriptorSets[frame][cascade] = sets[setIndex++];

                VkDescriptorBufferInfo bufferInfo{
                    m_shadowUniformBuffers[frame][cascade].Handle, 0, sizeof(vulkan::ShadowUniforms)};
                VkWriteDescriptorSet shadowWrite{};
                shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                shadowWrite.dstSet = m_shadowDescriptorSets[frame][cascade];
                shadowWrite.dstBinding = 0;
                shadowWrite.descriptorCount = 1;
                shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                shadowWrite.pBufferInfo = &bufferInfo;
                vkUpdateDescriptorSets(m_device, 1, &shadowWrite, 0, nullptr);
            }
        }

        for (int frame = 0; frame < kFramesInFlight; ++frame)
        {
            std::array<VkDescriptorImageInfo, 4> imageInfos{};
            std::array<VkWriteDescriptorSet, 4> writes{};
            for (int cascade = 0; cascade < 4; ++cascade)
            {
                imageInfos[cascade] = {
                    m_shadowSampler, m_shadowMaps[frame][cascade].View,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
                writes[cascade].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[cascade].dstSet = m_frameDescriptorSets[frame];
                writes[cascade].dstBinding = vulkan::binding::SunShadowMap + cascade;
                writes[cascade].descriptorCount = 1;
                writes[cascade].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[cascade].pImageInfo = &imageInfos[cascade];
            }
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        VkCommandBuffer commandBuffer = BeginImmediateCommands();
        std::array<VkImageMemoryBarrier2, kFramesInFlight * 4> barriers{};
        size_t barrierIndex = 0;
        for (int frame = 0; frame < kFramesInFlight; ++frame)
        {
            for (int cascade = 0; cascade < 4; ++cascade)
            {
                VkImageMemoryBarrier2& barrier = barriers[barrierIndex++];
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = m_shadowMaps[frame][cascade].Handle;
                barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            }
        }
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependency.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
        EndImmediateCommands(commandBuffer);
    }
    catch (...)
    {
        DestroyShadowResources();
        throw;
    }
}

void VulkanRenderBackend::DestroyShadowResources()
{
    for (int frame = 0; frame < kFramesInFlight; ++frame)
    {
        for (int cascade = 0; cascade < 4; ++cascade)
        {
            m_resources.Destroy(m_shadowUniformBuffers[frame][cascade]);
            m_resources.Destroy(m_shadowMaps[frame][cascade]);
            m_shadowDescriptorSets[frame][cascade] = VK_NULL_HANDLE;
        }
    }
    if (m_shadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(m_device, m_shadowSampler, nullptr);
    m_shadowSampler = VK_NULL_HANDLE;
}

} // namespace engine
