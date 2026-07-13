#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Log.h"

#include <stb_image_write.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine {

VkCommandBuffer VulkanRenderBackend::BeginImmediateCommands()
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = m_commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocateInfo, &commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate upload command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        throw std::runtime_error("Vulkan: failed to begin upload command buffer");
    }
    return commandBuffer;
}

void VulkanRenderBackend::EndImmediateCommands(VkCommandBuffer commandBuffer)
{
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to end upload command buffer");

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    if (vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to submit upload commands");
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

const VulkanRenderBackend::GpuMesh& VulkanRenderBackend::GetOrCreateMesh(const std::shared_ptr<MeshData>& mesh)
{
    if (!mesh || mesh->Vertices.empty() || mesh->Indices.empty())
        throw std::runtime_error("Vulkan: cannot upload an empty mesh");
    if (const auto found = m_meshCache.find(mesh.get()); found != m_meshCache.end())
        return found->second;

    const VkDeviceSize vertexBytes = mesh->Vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = mesh->Indices.size() * sizeof(uint32_t);
    vulkan::Buffer vertexStaging = m_resources.CreateBuffer(
        vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vulkan::Buffer indexStaging = m_resources.CreateBuffer(
        indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    GpuMesh gpuMesh;
    try
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, vertexStaging.Memory, 0, vertexBytes, 0, &mapped);
        std::memcpy(mapped, mesh->Vertices.data(), static_cast<size_t>(vertexBytes));
        vkUnmapMemory(m_device, vertexStaging.Memory);
        vkMapMemory(m_device, indexStaging.Memory, 0, indexBytes, 0, &mapped);
        std::memcpy(mapped, mesh->Indices.data(), static_cast<size_t>(indexBytes));
        vkUnmapMemory(m_device, indexStaging.Memory);

        gpuMesh.VertexBuffer = m_resources.CreateBuffer(
            vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        gpuMesh.IndexBuffer = m_resources.CreateBuffer(
            indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        gpuMesh.IndexCount = static_cast<uint32_t>(mesh->Indices.size());

        VkCommandBuffer commandBuffer = BeginImmediateCommands();
        const VkBufferCopy vertexCopy{0, 0, vertexBytes};
        const VkBufferCopy indexCopy{0, 0, indexBytes};
        vkCmdCopyBuffer(commandBuffer, vertexStaging.Handle, gpuMesh.VertexBuffer.Handle, 1, &vertexCopy);
        vkCmdCopyBuffer(commandBuffer, indexStaging.Handle, gpuMesh.IndexBuffer.Handle, 1, &indexCopy);
        EndImmediateCommands(commandBuffer);
    }
    catch (...)
    {
        m_resources.Destroy(gpuMesh.VertexBuffer);
        m_resources.Destroy(gpuMesh.IndexBuffer);
        m_resources.Destroy(vertexStaging);
        m_resources.Destroy(indexStaging);
        throw;
    }

    m_resources.Destroy(vertexStaging);
    m_resources.Destroy(indexStaging);
    return m_meshCache.emplace(mesh.get(), std::move(gpuMesh)).first->second;
}

const VulkanRenderBackend::GpuTexture& VulkanRenderBackend::GetOrCreateTexture(
    const std::shared_ptr<TextureData>& texture, const std::shared_ptr<TextureData>& fallback)
{
    const std::shared_ptr<TextureData>& source = texture ? texture : fallback;
    if (!source || source->Width <= 0 || source->Height <= 0
        || source->Pixels.size() < static_cast<size_t>(source->Width * source->Height * 4))
    {
        throw std::runtime_error("Vulkan: invalid CPU texture data");
    }
    if (const auto found = m_textureCache.find(source.get()); found != m_textureCache.end())
        return found->second;

    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(source->Width)
                                 * static_cast<VkDeviceSize>(source->Height) * 4;
    vulkan::Buffer staging = m_resources.CreateBuffer(
        byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    GpuTexture gpuTexture;
    try
    {
        void* mapped = nullptr;
        if (vkMapMemory(m_device, staging.Memory, 0, byteCount, 0, &mapped) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to map texture staging memory");
        std::memcpy(mapped, source->Pixels.data(), static_cast<size_t>(byteCount));
        vkUnmapMemory(m_device, staging.Memory);

        const VkFormat format = source->SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        const uint32_t largestDimension = static_cast<uint32_t>(std::max(source->Width, source->Height));
        const uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(largestDimension))) + 1u;
        gpuTexture.Image = m_resources.CreateImage2D(
            static_cast<uint32_t>(source->Width), static_cast<uint32_t>(source->Height), format,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

        VkCommandBuffer commandBuffer = BeginImmediateCommands();
        VkImageMemoryBarrier2 toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = gpuTexture.Image.Handle;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<uint32_t>(source->Width), static_cast<uint32_t>(source->Height), 1};
        vkCmdCopyBufferToImage(commandBuffer, staging.Handle, gpuTexture.Image.Handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        int32_t mipWidth = source->Width;
        int32_t mipHeight = source->Height;
        for (uint32_t mip = 1; mip < mipLevels; ++mip)
        {
            VkImageMemoryBarrier2 toSource{};
            toSource.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            toSource.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toSource.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toSource.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toSource.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            toSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSource.image = gpuTexture.Image.Handle;
            toSource.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1u, 1, 0, 1};
            dependency.pImageMemoryBarriers = &toSource;
            vkCmdPipelineBarrier2(commandBuffer, &dependency);

            const int32_t nextWidth = std::max(mipWidth / 2, 1);
            const int32_t nextHeight = std::max(mipHeight / 2, 1);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1u, 0, 1};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
            vkCmdBlitImage(commandBuffer,
                           gpuTexture.Image.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           gpuTexture.Image.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);
            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        std::array<VkImageMemoryBarrier2, 2> toShader{};
        uint32_t barrierCount = 0;
        if (mipLevels > 1)
        {
            VkImageMemoryBarrier2& generated = toShader[barrierCount++];
            generated.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            generated.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            generated.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            generated.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            generated.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            generated.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            generated.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            generated.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            generated.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            generated.image = gpuTexture.Image.Handle;
            generated.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels - 1u, 0, 1};
        }
        VkImageMemoryBarrier2& last = toShader[barrierCount++];
        last.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        last.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        last.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        last.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        last.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        last.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        last.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        last.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        last.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        last.image = gpuTexture.Image.Handle;
        last.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1u, 1, 0, 1};
        dependency.imageMemoryBarrierCount = barrierCount;
        dependency.pImageMemoryBarriers = toShader.data();
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
        EndImmediateCommands(commandBuffer);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = m_samplerAnisotropySupported ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = m_maxSamplerAnisotropy;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels - 1u);
        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &gpuTexture.Sampler) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create material sampler");
    }
    catch (...)
    {
        if (gpuTexture.Sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device, gpuTexture.Sampler, nullptr);
        m_resources.Destroy(gpuTexture.Image);
        m_resources.Destroy(staging);
        throw;
    }

    m_resources.Destroy(staging);
    return m_textureCache.emplace(source.get(), std::move(gpuTexture)).first->second;
}

VulkanRenderBackend::GpuMaterial& VulkanRenderBackend::GetOrCreateMaterial(const Material& material)
{
    auto found = m_materialCache.find(&material);
    if (found == m_materialCache.end())
    {
        GpuMaterial gpuMaterial;
        gpuMaterial.UniformBuffers.reserve(kFramesInFlight);
        for (int i = 0; i < kFramesInFlight; ++i)
        {
            gpuMaterial.UniformBuffers.push_back(m_resources.CreateBuffer(
                sizeof(vulkan::MaterialUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        }

        std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, m_materialDescriptorLayout);
        gpuMaterial.DescriptorSets.resize(kFramesInFlight);
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_descriptorPool;
        allocateInfo.descriptorSetCount = kFramesInFlight;
        allocateInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(m_device, &allocateInfo, gpuMaterial.DescriptorSets.data()) != VK_SUCCESS)
        {
            for (vulkan::Buffer& buffer : gpuMaterial.UniformBuffers)
                m_resources.Destroy(buffer);
            throw std::runtime_error("Vulkan: failed to allocate material descriptor set");
        }
        found = m_materialCache.emplace(&material, std::move(gpuMaterial)).first;
    }
    UpdateMaterial(material, found->second, m_currentFrame);
    return found->second;
}

void VulkanRenderBackend::UpdateMaterial(const Material& material, GpuMaterial& gpuMaterial, uint32_t frameIndex)
{
    const vulkan::MaterialUniforms uniforms = vulkan::PackMaterialUniforms(material);
    void* mapped = nullptr;
    if (vkMapMemory(m_device, gpuMaterial.UniformBuffers[frameIndex].Memory,
                    0, sizeof(uniforms), 0, &mapped) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to map material uniform buffer");
    std::memcpy(mapped, &uniforms, sizeof(uniforms));
    vkUnmapMemory(m_device, gpuMaterial.UniformBuffers[frameIndex].Memory);

    const GpuTexture& baseColor = GetOrCreateTexture(material.AlbedoMap, m_defaultWhiteData);
    const GpuTexture& normal = GetOrCreateTexture(material.NormalMap, m_defaultNormalData);
    const GpuTexture& metallicRoughness = GetOrCreateTexture(
        material.MetallicRoughnessMap ? material.MetallicRoughnessMap : material.MraoMap, m_defaultWhiteData);
    const GpuTexture& occlusion = GetOrCreateTexture(material.OcclusionMap, m_defaultWhiteData);
    const GpuTexture& emissive = GetOrCreateTexture(material.EmissiveMap, m_defaultWhiteData);

    VkDescriptorBufferInfo uniformInfo{gpuMaterial.UniformBuffers[frameIndex].Handle, 0, sizeof(uniforms)};
    const std::array<VkDescriptorImageInfo, 5> images = {{
        {baseColor.Sampler, baseColor.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {normal.Sampler, normal.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {metallicRoughness.Sampler, metallicRoughness.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {occlusion.Sampler, occlusion.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {emissive.Sampler, emissive.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    std::array<VkWriteDescriptorSet, 6> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = gpuMaterial.DescriptorSets[frameIndex];
    writes[0].dstBinding = vulkan::binding::MaterialUniforms;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &uniformInfo;
    for (uint32_t i = 0; i < images.size(); ++i)
    {
        writes[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i + 1].dstSet = gpuMaterial.DescriptorSets[frameIndex];
        writes[i + 1].dstBinding = vulkan::binding::BaseColorMap + i;
        writes[i + 1].descriptorCount = 1;
        writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i + 1].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanRenderBackend::CreateDefaultResources()
{
    m_defaultWhiteData = textures::MakeSolidColor({1.0f, 1.0f, 1.0f, 1.0f});
    m_defaultNormalData = textures::MakeFlatNormal();
    GetOrCreateTexture(m_defaultWhiteData, m_defaultWhiteData);
    GetOrCreateTexture(m_defaultNormalData, m_defaultNormalData);
}

void VulkanRenderBackend::DestroySceneResources()
{
    for (auto& [key, material] : m_materialCache)
        for (vulkan::Buffer& buffer : material.UniformBuffers)
            m_resources.Destroy(buffer);
    m_materialCache.clear();
    for (auto& [key, texture] : m_textureCache)
    {
        if (texture.Sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device, texture.Sampler, nullptr);
        m_resources.Destroy(texture.Image);
    }
    m_textureCache.clear();
    for (auto& [key, mesh] : m_meshCache)
    {
        m_resources.Destroy(mesh.VertexBuffer);
        m_resources.Destroy(mesh.IndexBuffer);
    }
    m_meshCache.clear();
    m_defaultWhiteData.reset();
    m_defaultNormalData.reset();
}

void VulkanRenderBackend::SaveScreenshot(const vulkan::Buffer& readbackBuffer, const std::string& path) const
{
    const size_t pixelCount = static_cast<size_t>(m_swapchainExtent.width) * m_swapchainExtent.height;
    const size_t byteCount = pixelCount * 4;
    void* mapped = nullptr;
    if (vkMapMemory(m_device, readbackBuffer.Memory, 0, byteCount, 0, &mapped) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to map screenshot buffer");

    const auto* source = static_cast<const uint8_t*>(mapped);
    std::vector<uint8_t> rgb(pixelCount * 3);
    const bool bgra = m_swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB
                   || m_swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        rgb[i * 3 + 0] = source[i * 4 + (bgra ? 2 : 0)];
        rgb[i * 3 + 1] = source[i * 4 + 1];
        rgb[i * 3 + 2] = source[i * 4 + (bgra ? 0 : 2)];
    }
    vkUnmapMemory(m_device, readbackBuffer.Memory);

    if (stbi_write_png(path.c_str(), static_cast<int>(m_swapchainExtent.width),
                       static_cast<int>(m_swapchainExtent.height), 3, rgb.data(),
                       static_cast<int>(m_swapchainExtent.width * 3)))
        log::Info("Saved Vulkan screenshot: " + path);
    else
        log::Error("Failed to save Vulkan screenshot: " + path);
}

} // namespace engine
