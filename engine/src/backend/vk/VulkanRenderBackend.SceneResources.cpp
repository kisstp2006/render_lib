#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Log.h"
#include "engine/render/TextureFallback.h"
#include "engine/testing/VisualRegression.h"

#include <stb_image_write.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine {

namespace {

float HalfToFloat(uint16_t half)
{
    const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16u;
    uint32_t exponent = (half >> 10u) & 0x1fu;
    uint32_t mantissa = half & 0x03ffu;
    uint32_t bits = 0;
    if (exponent == 0)
    {
        if (mantissa == 0)
            bits = sign;
        else
        {
            uint32_t floatExponent = 113u;
            while ((mantissa & 0x0400u) == 0)
            {
                mantissa <<= 1u;
                --floatExponent;
            }
            mantissa &= 0x03ffu;
            bits = sign | (floatExponent << 23u) | (mantissa << 13u);
        }
    }
    else if (exponent == 31)
        bits = sign | 0x7f800000u | (mantissa << 13u);
    else
        bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);

    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

VkSamplerAddressMode ToSamplerAddressMode(TextureAddressMode mode)
{
    switch (mode)
    {
    case TextureAddressMode::Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureAddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureAddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkFormat ToTextureFormat(TexturePixelStorage storage, bool srgb)
{
    switch (storage)
    {
    case TexturePixelStorage::Rgba32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TexturePixelStorage::Bc1Rgb:
        return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TexturePixelStorage::Bc3Rgba:
        return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    case TexturePixelStorage::Bc5Rg:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case TexturePixelStorage::Bc7Rgba:
        return srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
    case TexturePixelStorage::Astc4x4Rgba:
        return srgb ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    default:
        return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    }
}

template <typename Channel>
std::vector<Channel> DownsampleRgba(const std::vector<Channel>& source,
                                    uint32_t width, uint32_t height)
{
    const uint32_t nextWidth = std::max(width / 2u, 1u);
    const uint32_t nextHeight = std::max(height / 2u, 1u);
    std::vector<Channel> result(static_cast<size_t>(nextWidth) * nextHeight * 4u);
    for (uint32_t y = 0; y < nextHeight; ++y)
    {
        for (uint32_t x = 0; x < nextWidth; ++x)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                double total = 0.0;
                for (uint32_t offsetY = 0; offsetY < 2; ++offsetY)
                {
                    for (uint32_t offsetX = 0; offsetX < 2; ++offsetX)
                    {
                        const uint32_t sampleX = std::min(x * 2u + offsetX, width - 1u);
                        const uint32_t sampleY = std::min(y * 2u + offsetY, height - 1u);
                        total += static_cast<double>(source[
                            (static_cast<size_t>(sampleY) * width + sampleX) * 4u + channel]);
                    }
                }
                const double average = total * 0.25;
                result[(static_cast<size_t>(y) * nextWidth + x) * 4u + channel] =
                    static_cast<Channel>(average + (std::is_integral_v<Channel> ? 0.5 : 0.0));
            }
        }
    }
    return result;
}

} // namespace

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

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    uint32_t waitCount = 0;
    if (m_pendingTransferWaitValue != 0)
    {
        waitInfo.semaphore = m_transferTimeline;
        waitInfo.value = m_pendingTransferWaitValue;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitCount = 1;
    }
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = waitCount;
    submit.pWaitSemaphoreInfos = waitCount != 0 ? &waitInfo : nullptr;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    if (vkQueueSubmit2(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to submit upload commands");
    m_pendingTransferWaitValue = 0;
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

const VulkanRenderBackend::GpuMesh& VulkanRenderBackend::GetOrCreateMesh(const std::shared_ptr<MeshData>& mesh)
{
    if (!mesh || mesh->Vertices.empty() || mesh->Indices.empty())
        throw std::runtime_error("Vulkan: cannot upload an empty mesh");
    const auto found = m_meshCache.find(mesh.get());
    if (found != m_meshCache.end() && found->second.Revision == mesh->Revision)
        return found->second;

    const VkDeviceSize vertexBytes = mesh->Vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = mesh->Indices.size() * sizeof(uint32_t);

    GpuMesh gpuMesh;
    gpuMesh.Owner = mesh;
    gpuMesh.Revision = mesh->Revision;
    const size_t meshId = m_meshCache.size();
    try
    {
        gpuMesh.VertexBuffer = m_resources.CreateBuffer(
            vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        gpuMesh.IndexBuffer = m_resources.CreateBuffer(
            indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        gpuMesh.IndexCount = static_cast<uint32_t>(mesh->Indices.size());
        m_resources.SetDebugName(gpuMesh.VertexBuffer,
            "Scene Mesh " + std::to_string(meshId) + " Vertex Buffer");
        m_resources.SetDebugName(gpuMesh.IndexBuffer,
            "Scene Mesh " + std::to_string(meshId) + " Index Buffer");

        QueueBufferUploads({
            {mesh->Vertices.data(), {vertexBytes, gpuMesh.VertexBuffer.Handle}},
            {mesh->Indices.data(), {indexBytes, gpuMesh.IndexBuffer.Handle}},
        });
    }
    catch (...)
    {
        m_resources.Destroy(gpuMesh.VertexBuffer);
        m_resources.Destroy(gpuMesh.IndexBuffer);
        throw;
    }

    if (found == m_meshCache.end())
        return m_meshCache.emplace(mesh.get(), std::move(gpuMesh)).first->second;

    GpuMesh previous = std::move(found->second);
    found->second = std::move(gpuMesh);
    m_deferredRelease.Enqueue(m_gpuProfileFrameIndex + kFramesInFlight,
        [this, vertex = previous.VertexBuffer,
         index = previous.IndexBuffer]() mutable {
            m_resources.Destroy(vertex);
            m_resources.Destroy(index);
        });
    return found->second;
}

const VulkanRenderBackend::GpuTexture& VulkanRenderBackend::GetOrCreateTexture(
    const std::shared_ptr<TextureData>& texture, const std::shared_ptr<TextureData>& fallback)
{
    const std::shared_ptr<TextureData>& sourceOwner = texture ? texture : fallback;
    if (!sourceOwner || sourceOwner->Width <= 0 || sourceOwner->Height <= 0)
    {
        throw std::runtime_error("Vulkan: invalid CPU texture data");
    }
    const TextureData* cacheKey = sourceOwner.get();
    if (const auto found = m_textureCache.find(cacheKey); found != m_textureCache.end())
        return found->second;

    TextureData scratch;
    std::string fallbackReason;
    const TextureData* source = ResolveTextureForGpu(*sourceOwner, m_capabilities.Gpu,
                                                      scratch, &fallbackReason);
    if (!source)
    {
        if (!fallback || fallback.get() == cacheKey)
            throw std::runtime_error("Vulkan: " + fallbackReason);
        source = fallback.get();
        fallbackReason += "; using the material default texture";
    }
    if (source != sourceOwner.get())
    {
        log::Warn("Vulkan texture fallback: " + fallbackReason);
        const std::string feature = TextureStorageName(sourceOwner->Storage);
        if (std::none_of(m_capabilities.Gpu.Fallbacks.begin(), m_capabilities.Gpu.Fallbacks.end(),
                         [&feature](const GpuFallbackDecision& decision)
                         { return decision.Feature == feature; }))
            m_capabilities.Gpu.Fallbacks.push_back(
                {"unsupported-texture-format", feature, "native upload",
                 source == fallback.get() ? "material default texture" : "RGBA8 fallback mip chain",
                 fallbackReason});
    }

    const bool floatingPoint = source->Storage == TexturePixelStorage::Rgba32Float;
    const bool blockCompressed = IsBlockCompressed(source->Storage);
    const bool hasCookedMips = !source->MipLevels.empty();
    if (blockCompressed && !hasCookedMips)
        throw std::runtime_error("Vulkan: block-compressed textures require a complete cooked mip chain");
    const uint32_t largestDimension = static_cast<uint32_t>(std::max(source->Width, source->Height));
    const uint32_t mipLevels = hasCookedMips ? static_cast<uint32_t>(source->MipLevels.size()) :
        static_cast<uint32_t>(std::floor(std::log2(largestDimension))) + 1u;

    std::vector<VkBufferImageCopy> copyRegions;
    copyRegions.reserve(mipLevels);
    std::vector<std::byte> uploadBytes;
    VkDeviceSize byteCount = 0;
    if (hasCookedMips)
    {
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const TextureMipData& level = source->MipLevels[mip];
            const size_t expected = TextureMipByteSize(source->Storage, static_cast<uint32_t>(level.Width),
                                                       static_cast<uint32_t>(level.Height));
            const size_t available = floatingPoint ? level.FloatPixels.size() * sizeof(float) : level.Pixels.size();
            if (level.Width <= 0 || level.Height <= 0 ||
                available != expected)
                throw std::runtime_error("Vulkan: invalid cooked texture mip data");
            VkBufferImageCopy copy{};
            copy.bufferOffset = byteCount;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            copy.imageExtent = {static_cast<uint32_t>(level.Width),
                                static_cast<uint32_t>(level.Height), 1};
            copyRegions.push_back(copy);
            byteCount += static_cast<VkDeviceSize>(expected);
        }
        uploadBytes.resize(static_cast<size_t>(byteCount));
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const TextureMipData& level = source->MipLevels[mip];
            const size_t size = TextureMipByteSize(source->Storage, static_cast<uint32_t>(level.Width),
                                                   static_cast<uint32_t>(level.Height));
            const void* pixels = floatingPoint ? static_cast<const void*>(level.FloatPixels.data()) :
                                                 static_cast<const void*>(level.Pixels.data());
            std::memcpy(uploadBytes.data() + copyRegions[mip].bufferOffset, pixels, size);
        }
    }
    else
    {
        const size_t expected = static_cast<size_t>(source->Width) * source->Height * 4u;
        if ((floatingPoint ? source->FloatPixels.size() : source->Pixels.size()) < expected)
            throw std::runtime_error("Vulkan: invalid CPU texture level zero data");
        uint32_t mipWidth = static_cast<uint32_t>(source->Width);
        uint32_t mipHeight = static_cast<uint32_t>(source->Height);
        if (floatingPoint)
        {
            std::vector<float> mipPixels(source->FloatPixels.begin(),
                                         source->FloatPixels.begin() + expected);
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                const size_t size = mipPixels.size() * sizeof(float);
                VkBufferImageCopy copy{};
                copy.bufferOffset = byteCount;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
                copy.imageExtent = {mipWidth, mipHeight, 1};
                copyRegions.push_back(copy);
                uploadBytes.resize(static_cast<size_t>(byteCount) + size);
                std::memcpy(uploadBytes.data() + byteCount, mipPixels.data(), size);
                byteCount += static_cast<VkDeviceSize>(size);
                if (mip + 1u < mipLevels)
                    mipPixels = DownsampleRgba(mipPixels, mipWidth, mipHeight);
                mipWidth = std::max(mipWidth / 2u, 1u);
                mipHeight = std::max(mipHeight / 2u, 1u);
            }
        }
        else
        {
            std::vector<uint8_t> mipPixels(source->Pixels.begin(), source->Pixels.begin() + expected);
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                const size_t size = mipPixels.size();
                VkBufferImageCopy copy{};
                copy.bufferOffset = byteCount;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
                copy.imageExtent = {mipWidth, mipHeight, 1};
                copyRegions.push_back(copy);
                uploadBytes.resize(static_cast<size_t>(byteCount) + size);
                std::memcpy(uploadBytes.data() + byteCount, mipPixels.data(), size);
                byteCount += static_cast<VkDeviceSize>(size);
                if (mip + 1u < mipLevels)
                    mipPixels = DownsampleRgba(mipPixels, mipWidth, mipHeight);
                mipWidth = std::max(mipWidth / 2u, 1u);
                mipHeight = std::max(mipHeight / 2u, 1u);
            }
        }
    }

    GpuTexture gpuTexture;
    const size_t textureId = m_textureCache.size();
    try
    {
        const VkFormat format = ToTextureFormat(source->Storage, source->SRGB);
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &formatProperties);
        if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
            throw std::runtime_error("Vulkan: the GPU does not support the cooked texture block format");
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        gpuTexture.Image = m_resources.CreateImage2D(
            static_cast<uint32_t>(source->Width), static_cast<uint32_t>(source->Height), format,
            usage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
        m_resources.SetDebugName(gpuTexture.Image,
            "Material Texture " + std::to_string(textureId) +
            (source->SRGB ? " (sRGB)" : " (Linear)"));
        QueueTextureUpload(uploadBytes.data(), byteCount, gpuTexture.Image.Handle,
                           mipLevels, copyRegions);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        const bool nearest = source->Filter == TextureFilterMode::Nearest;
        samplerInfo.magFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        samplerInfo.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = (source->Filter == TextureFilterMode::Trilinear ||
                                  source->Filter == TextureFilterMode::Anisotropic) ?
                                 VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = ToSamplerAddressMode(source->AddressU);
        samplerInfo.addressModeV = ToSamplerAddressMode(source->AddressV);
        samplerInfo.addressModeW = ToSamplerAddressMode(source->AddressW);
        samplerInfo.mipLodBias = source->MipBias;
        samplerInfo.anisotropyEnable = m_samplerAnisotropySupported &&
            source->Filter == TextureFilterMode::Anisotropic ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = samplerInfo.anisotropyEnable ?
            std::clamp(source->MaxAnisotropy, 1.0f, m_maxSamplerAnisotropy) : 1.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels - 1u);
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &gpuTexture.Sampler) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create material sampler");
        SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(gpuTexture.Sampler),
                     "Material Texture " + std::to_string(textureId) + " Sampler");
    }
    catch (...)
    {
        if (gpuTexture.Sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device, gpuTexture.Sampler, nullptr);
        m_resources.Destroy(gpuTexture.Image);
        throw;
    }

    return m_textureCache.emplace(cacheKey, std::move(gpuTexture)).first->second;
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

void VulkanRenderBackend::SaveHdrScreenshot(const vulkan::Buffer& readbackBuffer,
                                            const std::string& path) const
{
    const size_t pixelCount = static_cast<size_t>(m_swapchainExtent.width)
                            * m_swapchainExtent.height;
    const size_t byteCount = pixelCount * 4 * sizeof(uint16_t);
    void* mapped = nullptr;
    if (vkMapMemory(m_device, readbackBuffer.Memory, 0, byteCount, 0, &mapped) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to map linear HDR screenshot buffer");

    const auto* source = static_cast<const uint16_t*>(mapped);
    std::vector<float> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        rgb[i * 3 + 0] = HalfToFloat(source[i * 4 + 0]);
        rgb[i * 3 + 1] = HalfToFloat(source[i * 4 + 1]);
        rgb[i * 3 + 2] = HalfToFloat(source[i * 4 + 2]);
    }
    vkUnmapMemory(m_device, readbackBuffer.Memory);

    std::string error;
    if (testing::WriteHdrImage(path, static_cast<int>(m_swapchainExtent.width),
                               static_cast<int>(m_swapchainExtent.height), rgb, &error))
        log::Info("Saved Vulkan linear HDR screenshot: " + path);
    else
        log::Error("Failed to save Vulkan linear HDR screenshot: " + error);
}

} // namespace engine
