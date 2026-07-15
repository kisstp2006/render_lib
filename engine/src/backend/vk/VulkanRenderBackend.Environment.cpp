#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Log.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/render/SceneRenderer.h"
#include "engine/scene/Scene.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace engine {

namespace {

constexpr uint32_t kEnvironmentSize = 256;
constexpr uint32_t kEnvironmentMipLevels = 9;
constexpr uint32_t kIrradianceSize = 32;
constexpr uint32_t kPrefilterSize = 128;
constexpr uint32_t kPrefilterMipLevels = 8;
constexpr uint32_t kBrdfLutSize = 512;

VkImageView CreateSubresourceView(VkDevice device, const vulkan::Image& image,
                                  VkImageViewType type, uint32_t baseMip, uint32_t mipCount,
                                  uint32_t baseLayer, uint32_t layerCount)
{
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image.Handle;
    info.viewType = type;
    info.format = image.Format;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, baseLayer, layerCount};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create environment subresource view");
    return view;
}

void TransitionImage(VkCommandBuffer commandBuffer, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     uint32_t baseMip, uint32_t mipCount, uint32_t layerCount)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, layerCount};

    switch (oldLayout)
    {
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;
    default:
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = 0;
        break;
    }

    switch (newLayout)
    {
    case VK_IMAGE_LAYOUT_GENERAL:
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;
    default:
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        break;
    }

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void HashCombine(uint64_t& seed, uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

void HashFloat(uint64_t& seed, float value)
{
    HashCombine(seed, std::bit_cast<uint32_t>(value));
}

void HashVector(uint64_t& seed, const glm::vec3& value)
{
    HashFloat(seed, value.x);
    HashFloat(seed, value.y);
    HashFloat(seed, value.z);
}

uint64_t EnvironmentStaticSignature(const Scene& scene)
{
    uint64_t result = 1469598103934665603ull;
    HashCombine(result, static_cast<uint64_t>(scene.Environment.Source));
    HashCombine(result, reinterpret_cast<uintptr_t>(scene.Environment.Hdri.get()));
    HashFloat(result, scene.Environment.ExposureEV);
    HashFloat(result, scene.Environment.RotationDegrees);
    HashVector(result, scene.Sun.Color);
    HashFloat(result, scene.Sun.Intensity);
    const SkySettings& sky = scene.Sky;
    HashVector(result, sky.ZenithColor);
    HashVector(result, sky.HorizonColor);
    HashVector(result, sky.GroundColor);
    HashVector(result, sky.NightZenithColor);
    HashVector(result, sky.NightHorizonColor);
    HashFloat(result, sky.NightSkyIntensity);
    HashFloat(result, sky.NightHorizonGlow);
    HashFloat(result, sky.SunAngularRadiusDeg);
    HashFloat(result, sky.SunIntensity);
    HashFloat(result, sky.SkyIntensity);
    HashCombine(result, sky.EnableDayNightCycle ? 1u : 0u);
    return result;
}

uint64_t EnvironmentDirectionSignature(const Scene& scene)
{
    if (scene.Environment.Source != EnvironmentSource::ProceduralSky)
        return 0;
    uint64_t result = 1469598103934665603ull;
    HashVector(result, scene.Sun.Direction);
    return result;
}

VkPipeline CreateComputePipeline(vulkan::PipelineCacheStore& cache,
                                 VkPipelineLayout layout, VkShaderModule shader)
{
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = stage;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (cache.CreateCompute(1, &info, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create environment compute pipeline");
    return pipeline;
}

} // namespace

void VulkanRenderBackend::CreateEnvironmentInfrastructure()
{
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorInfo.bindingCount = static_cast<uint32_t>(std::size(bindings));
    descriptorInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &descriptorInfo, nullptr,
                                    &m_environmentBakeDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create environment bake descriptor layout");

    VkPushConstantRange constants{};
    constants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    constants.size = sizeof(vulkan::EnvironmentBakeConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_environmentBakeDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &constants;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                               &m_environmentBakePipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create environment bake pipeline layout");

    const std::vector<VkShaderModule> shaders = LoadShadersParallel({
        "environment/environment_source.comp", "environment/irradiance.comp",
        "environment/prefilter.comp", "environment/brdf_lut.comp"});
    m_environmentSourceShader = shaders[0];
    m_irradianceShader = shaders[1];
    m_prefilterShader = shaders[2];
    m_brdfShader = shaders[3];
    m_environmentSourcePipeline = CreateComputePipeline(
        m_pipelineCache, m_environmentBakePipelineLayout, m_environmentSourceShader);
    m_irradiancePipeline = CreateComputePipeline(
        m_pipelineCache, m_environmentBakePipelineLayout, m_irradianceShader);
    m_prefilterPipeline = CreateComputePipeline(
        m_pipelineCache, m_environmentBakePipelineLayout, m_prefilterShader);
    m_brdfPipeline = CreateComputePipeline(
        m_pipelineCache, m_environmentBakePipelineLayout, m_brdfShader);
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_environmentSourcePipeline),
                 "IBL Environment Source Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_irradiancePipeline),
                 "IBL Irradiance Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_prefilterPipeline),
                 "IBL GGX Prefilter Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_brdfPipeline),
                 "IBL BRDF LUT Pipeline");
}

void VulkanRenderBackend::DestroyEnvironmentInfrastructure()
{
    if (m_brdfPipeline) vkDestroyPipeline(m_device, m_brdfPipeline, nullptr);
    if (m_prefilterPipeline) vkDestroyPipeline(m_device, m_prefilterPipeline, nullptr);
    if (m_irradiancePipeline) vkDestroyPipeline(m_device, m_irradiancePipeline, nullptr);
    if (m_environmentSourcePipeline) vkDestroyPipeline(m_device, m_environmentSourcePipeline, nullptr);
    if (m_brdfShader) vkDestroyShaderModule(m_device, m_brdfShader, nullptr);
    if (m_prefilterShader) vkDestroyShaderModule(m_device, m_prefilterShader, nullptr);
    if (m_irradianceShader) vkDestroyShaderModule(m_device, m_irradianceShader, nullptr);
    if (m_environmentSourceShader) vkDestroyShaderModule(m_device, m_environmentSourceShader, nullptr);
    if (m_environmentBakePipelineLayout)
        vkDestroyPipelineLayout(m_device, m_environmentBakePipelineLayout, nullptr);
    if (m_environmentBakeDescriptorLayout)
        vkDestroyDescriptorSetLayout(m_device, m_environmentBakeDescriptorLayout, nullptr);
    m_brdfPipeline = m_prefilterPipeline = m_irradiancePipeline = m_environmentSourcePipeline = VK_NULL_HANDLE;
    m_brdfShader = m_prefilterShader = m_irradianceShader = m_environmentSourceShader = VK_NULL_HANDLE;
    m_environmentBakePipelineLayout = VK_NULL_HANDLE;
    m_environmentBakeDescriptorLayout = VK_NULL_HANDLE;
}

void VulkanRenderBackend::CreateEnvironmentResources()
{
    m_environmentCube = m_resources.CreateImage2D(
        kEnvironmentSize, kEnvironmentSize, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, kEnvironmentMipLevels, 6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
    m_irradianceCube = m_resources.CreateImage2D(
        kIrradianceSize, kIrradianceSize, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
    m_prefilterCube = m_resources.CreateImage2D(
        kPrefilterSize, kPrefilterSize, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, kPrefilterMipLevels, 6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
    m_brdfLut = m_resources.CreateImage2D(
        kBrdfLutSize, kBrdfLutSize, VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    m_resources.SetDebugName(m_environmentCube, "IBL Environment Cubemap");
    m_resources.SetDebugName(m_irradianceCube, "IBL Irradiance Cubemap");
    m_resources.SetDebugName(m_prefilterCube, "IBL GGX Prefilter Cubemap");
    m_resources.SetDebugName(m_brdfLut, "IBL BRDF LUT");

    m_environmentStorageView = CreateSubresourceView(
        m_device, m_environmentCube, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 6);
    m_irradianceStorageView = CreateSubresourceView(
        m_device, m_irradianceCube, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 6);
    for (uint32_t mip = 0; mip < kPrefilterMipLevels; ++mip)
        m_prefilterStorageViews[mip] = CreateSubresourceView(
            m_device, m_prefilterCube, VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1, 0, 6);

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.minLod = 0.0f;
    sampler.maxLod = static_cast<float>(kEnvironmentMipLevels - 1u);
    if (vkCreateSampler(m_device, &sampler, nullptr, &m_environmentSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create environment sampler");
    sampler.maxLod = 0.0f;
    if (vkCreateSampler(m_device, &sampler, nullptr, &m_brdfSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create BRDF LUT sampler");

    std::array<VkDescriptorSetLayout, 11> layouts{};
    layouts.fill(m_environmentBakeDescriptorLayout);
    std::array<VkDescriptorSet, 11> sets{};
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = m_descriptorPool;
    allocate.descriptorSetCount = static_cast<uint32_t>(sets.size());
    allocate.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate, sets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate environment bake descriptor sets");
    m_environmentSourceDescriptor = sets[0];
    m_irradianceDescriptor = sets[1];
    std::copy_n(sets.begin() + 2, kPrefilterMipLevels, m_prefilterDescriptors.begin());
    m_brdfDescriptor = sets[10];

    VkDescriptorImageInfo brdfOutput{VK_NULL_HANDLE, m_brdfLut.View, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet brdfWrite{};
    brdfWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    brdfWrite.dstSet = m_brdfDescriptor;
    brdfWrite.dstBinding = 0;
    brdfWrite.descriptorCount = 1;
    brdfWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    brdfWrite.pImageInfo = &brdfOutput;
    vkUpdateDescriptorSets(m_device, 1, &brdfWrite, 0, nullptr);

    const auto start = std::chrono::steady_clock::now();
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    BeginDebugLabel(commandBuffer, "IBL / BRDF LUT Bake", {0.15f, 0.60f, 0.90f, 1.0f});
    TransitionImage(commandBuffer, m_brdfLut.Handle, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL, 0, 1, 1);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_environmentBakePipelineLayout, 0, 1, &m_brdfDescriptor, 0, nullptr);
    vkCmdDispatch(commandBuffer, kBrdfLutSize / 8, kBrdfLutSize / 8, 1);
    TransitionImage(commandBuffer, m_brdfLut.Handle, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1, 1);
    EndDebugLabel(commandBuffer);
    EndImmediateCommands(commandBuffer);
    const float milliseconds = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    log::Info("Vulkan IBL: BRDF LUT baked in " + std::to_string(milliseconds) + " ms");
}

const VulkanRenderBackend::GpuPanorama& VulkanRenderBackend::GetOrCreatePanorama(
    const std::shared_ptr<HdrImageData>& image)
{
    if (!image || image->Width <= 0 || image->Height <= 0
        || image->Pixels.size() < static_cast<size_t>(image->Width * image->Height * 3))
        throw EnvironmentLoadError("Vulkan: invalid HDR panorama data");
    if (const auto found = m_panoramaCache.find(image.get()); found != m_panoramaCache.end())
        return found->second;

    std::vector<glm::vec4> rgba(static_cast<size_t>(image->Width) * image->Height);
    for (size_t pixel = 0; pixel < rgba.size(); ++pixel)
        rgba[pixel] = glm::vec4(image->Pixels[pixel * 3], image->Pixels[pixel * 3 + 1],
                                image->Pixels[pixel * 3 + 2], 1.0f);
    const VkDeviceSize byteCount = rgba.size() * sizeof(glm::vec4);
    vulkan::Buffer staging = m_resources.CreateBuffer(
        byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    if (vkMapMemory(m_device, staging.Memory, 0, byteCount, 0, &mapped) != VK_SUCCESS)
        throw EnvironmentLoadError("Vulkan: failed to map HDR panorama upload buffer");
    std::memcpy(mapped, rgba.data(), static_cast<size_t>(byteCount));
    vkUnmapMemory(m_device, staging.Memory);

    GpuPanorama panorama;
    panorama.Image = m_resources.CreateImage2D(
        static_cast<uint32_t>(image->Width), static_cast<uint32_t>(image->Height),
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    TransitionImage(commandBuffer, panorama.Image.Handle, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 1);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(image->Width), static_cast<uint32_t>(image->Height), 1};
    vkCmdCopyBufferToImage(commandBuffer, staging.Handle, panorama.Image.Handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    TransitionImage(commandBuffer, panorama.Image.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1, 1);
    EndImmediateCommands(commandBuffer);
    m_resources.Destroy(staging);

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &sampler, nullptr, &panorama.Sampler) != VK_SUCCESS)
    {
        m_resources.Destroy(panorama.Image);
        throw EnvironmentLoadError("Vulkan: failed to create HDR panorama sampler");
    }
    return m_panoramaCache.emplace(image.get(), std::move(panorama)).first->second;
}

void VulkanRenderBackend::EnsureEnvironmentBaked(const RenderFrameData& frame)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment.Check", "Renderer/Vulkan/IBL");
    const Scene& scene = *frame.SceneData;
    if (scene.Environment.Source == EnvironmentSource::EquirectangularHdr && !scene.Environment.Hdri)
        throw EnvironmentLoadError("Equirectangular HDR environment selected, but no HDR image was assigned");

    const uint64_t staticSignature = EnvironmentStaticSignature(scene);
    const uint64_t directionSignature = EnvironmentDirectionSignature(scene);
    const bool staticChanged = staticSignature != m_environmentStaticSignature;
    const bool directionChanged = directionSignature != m_environmentDirectionSignature;
    const bool fastUpdate = !staticChanged && directionChanged && m_environmentImagesInitialized;
    if (!staticChanged && !directionChanged && m_environmentImagesInitialized)
        return;
    const double now = glfwGetTime();
    if (!staticChanged && directionChanged && m_environmentImagesInitialized
        && now - m_lastEnvironmentBakeTime < 0.35)
        return;

    const GpuTexture& fallback = GetOrCreateTexture(m_defaultWhiteData, m_defaultWhiteData);
    VkDescriptorImageInfo sourceInput{
        fallback.Sampler, fallback.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (scene.Environment.Source == EnvironmentSource::EquirectangularHdr)
    {
        const GpuPanorama& panorama = GetOrCreatePanorama(scene.Environment.Hdri);
        sourceInput = {panorama.Sampler, panorama.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }

    const auto updateBakeDescriptor = [&](VkDescriptorSet set, VkImageView output,
                                          VkSampler inputSampler, VkImageView inputView) {
        const VkDescriptorImageInfo outputInfo{VK_NULL_HANDLE, output, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo inputInfo{
            inputSampler, inputView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &outputInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &inputInfo;
        vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
    };
    updateBakeDescriptor(m_environmentSourceDescriptor, m_environmentStorageView,
                         sourceInput.sampler, sourceInput.imageView);
    updateBakeDescriptor(m_irradianceDescriptor, m_irradianceStorageView,
                         m_environmentSampler, m_environmentCube.View);
    for (uint32_t mip = 0; mip < kPrefilterMipLevels; ++mip)
        updateBakeDescriptor(m_prefilterDescriptors[mip], m_prefilterStorageViews[mip],
                             m_environmentSampler, m_environmentCube.View);

    vulkan::EnvironmentBakeConstants constants;
    constants.SunDirectionIntensity = glm::vec4(frame.SunDirection, scene.Sky.SunIntensity);
    constants.SunColorAngularRadius = glm::vec4(
        scene.Sun.Color * frame.DayNight.SunTint, glm::radians(scene.Sky.SunAngularRadiusDeg));
    constants.ZenithIntensity = glm::vec4(scene.Sky.ZenithColor, scene.Sky.SkyIntensity);
    constants.HorizonCycle = glm::vec4(
        scene.Sky.HorizonColor, scene.Sky.EnableDayNightCycle ? 1.0f : 0.0f);
    constants.GroundExposure = glm::vec4(
        scene.Sky.GroundColor, std::exp2(scene.Environment.ExposureEV));
    constants.NightZenithIntensity = glm::vec4(
        scene.Sky.NightZenithColor, scene.Sky.NightSkyIntensity);
    constants.NightHorizonGlow = glm::vec4(
        scene.Sky.NightHorizonColor, scene.Sky.NightHorizonGlow);
    constants.BakeParameters = {
        0.0f, static_cast<float>(kEnvironmentSize), glm::radians(scene.Environment.RotationDegrees),
        scene.Environment.Source == EnvironmentSource::EquirectangularHdr ? 1.0f : 0.0f};

    const auto start = std::chrono::steady_clock::now();
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment.Bake", "Renderer/Vulkan/IBL");
    const VkImageLayout oldLayout = m_environmentImagesInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    BeginDebugLabel(commandBuffer, "Environment / IBL Bake", {0.15f, 0.60f, 0.90f, 1.0f});
    BeginDebugLabel(commandBuffer, "IBL / Source to Environment Cubemap",
                    {0.15f, 0.50f, 0.85f, 1.0f});
    TransitionImage(commandBuffer, m_environmentCube.Handle, oldLayout,
                    VK_IMAGE_LAYOUT_GENERAL, 0, 1, 6);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_environmentSourcePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_environmentBakePipelineLayout, 0, 1,
                            &m_environmentSourceDescriptor, 0, nullptr);
    vkCmdPushConstants(commandBuffer, m_environmentBakePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, kEnvironmentSize / 8, kEnvironmentSize / 8, 6);
    TransitionImage(commandBuffer, m_environmentCube.Handle, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1, 6);

    uint32_t previousSize = kEnvironmentSize;
    for (uint32_t mip = 1; mip < kEnvironmentMipLevels; ++mip)
    {
        const uint32_t size = std::max(previousSize / 2u, 1u);
        TransitionImage(commandBuffer, m_environmentCube.Handle, oldLayout,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip, 1, 6);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1u, 0, 6};
        blit.srcOffsets[1] = {static_cast<int32_t>(previousSize), static_cast<int32_t>(previousSize), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 6};
        blit.dstOffsets[1] = {static_cast<int32_t>(size), static_cast<int32_t>(size), 1};
        vkCmdBlitImage(commandBuffer,
                       m_environmentCube.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_environmentCube.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);
        TransitionImage(commandBuffer, m_environmentCube.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mip, 1, 6);
        previousSize = size;
    }
    TransitionImage(commandBuffer, m_environmentCube.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, kEnvironmentMipLevels, 6);
    EndDebugLabel(commandBuffer);

    BeginDebugLabel(commandBuffer, "IBL / Irradiance Convolution",
                    {0.20f, 0.65f, 0.90f, 1.0f});
    TransitionImage(commandBuffer, m_irradianceCube.Handle, oldLayout,
                    VK_IMAGE_LAYOUT_GENERAL, 0, 1, 6);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_irradiancePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_environmentBakePipelineLayout, 0, 1, &m_irradianceDescriptor, 0, nullptr);
    constants.BakeParameters.z = fastUpdate ? 0.10f : 0.05f;
    vkCmdPushConstants(commandBuffer, m_environmentBakePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, kIrradianceSize / 8, kIrradianceSize / 8, 6);
    TransitionImage(commandBuffer, m_irradianceCube.Handle, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1, 6);
    EndDebugLabel(commandBuffer);

    BeginDebugLabel(commandBuffer, "IBL / GGX Prefilter Mip Chain",
                    {0.25f, 0.75f, 0.95f, 1.0f});
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilterPipeline);
    constants.BakeParameters.z = fastUpdate ? 128.0f : 1024.0f;
    for (uint32_t mip = 0; mip < kPrefilterMipLevels; ++mip)
    {
        const uint32_t size = std::max(kPrefilterSize >> mip, 1u);
        TransitionImage(commandBuffer, m_prefilterCube.Handle, oldLayout,
                        VK_IMAGE_LAYOUT_GENERAL, mip, 1, 6);
        constants.BakeParameters.x = static_cast<float>(mip) / static_cast<float>(kPrefilterMipLevels - 1u);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_environmentBakePipelineLayout, 0, 1,
                                &m_prefilterDescriptors[mip], 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_environmentBakePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, (size + 7u) / 8u, (size + 7u) / 8u, 6);
        TransitionImage(commandBuffer, m_prefilterCube.Handle, VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip, 1, 6);
    }
    EndDebugLabel(commandBuffer);
    EndDebugLabel(commandBuffer);
    EndImmediateCommands(commandBuffer);

    m_environmentImagesInitialized = true;
    m_environmentStaticSignature = staticSignature;
    m_environmentDirectionSignature = directionSignature;
    m_lastEnvironmentBakeTime = now;
    UpdateEnvironmentDescriptors();
    const float milliseconds = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    const std::string source = scene.Environment.Source == EnvironmentSource::EquirectangularHdr
        ? "HDRI" : "procedural sky";
    log::Info("Vulkan IBL: " + source + " re-baked in " + std::to_string(milliseconds) + " ms");
}

void VulkanRenderBackend::UpdateEnvironmentDescriptors()
{
    for (int frame = 0; frame < kFramesInFlight; ++frame)
    {
        const std::array<VkDescriptorImageInfo, 4> images{{
            {m_environmentSampler, m_irradianceCube.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_environmentSampler, m_prefilterCube.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_brdfSampler, m_brdfLut.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_environmentSampler, m_environmentCube.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        const uint32_t bindings[] = {
            vulkan::binding::IrradianceMap, vulkan::binding::PrefilteredMap,
            vulkan::binding::BrdfLut, vulkan::binding::EnvironmentMap};
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (size_t i = 0; i < writes.size(); ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_frameDescriptorSets[frame];
            writes[i].dstBinding = bindings[i];
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void VulkanRenderBackend::DestroyEnvironmentResources()
{
    for (auto& [key, panorama] : m_panoramaCache)
    {
        (void)key;
        if (panorama.Sampler) vkDestroySampler(m_device, panorama.Sampler, nullptr);
        m_resources.Destroy(panorama.Image);
    }
    m_panoramaCache.clear();
    if (m_brdfSampler) vkDestroySampler(m_device, m_brdfSampler, nullptr);
    if (m_environmentSampler) vkDestroySampler(m_device, m_environmentSampler, nullptr);
    for (VkImageView& view : m_prefilterStorageViews)
    {
        if (view) vkDestroyImageView(m_device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (m_irradianceStorageView) vkDestroyImageView(m_device, m_irradianceStorageView, nullptr);
    if (m_environmentStorageView) vkDestroyImageView(m_device, m_environmentStorageView, nullptr);
    m_resources.Destroy(m_brdfLut);
    m_resources.Destroy(m_prefilterCube);
    m_resources.Destroy(m_irradianceCube);
    m_resources.Destroy(m_environmentCube);
    m_brdfSampler = m_environmentSampler = VK_NULL_HANDLE;
    m_irradianceStorageView = m_environmentStorageView = VK_NULL_HANDLE;
    m_environmentImagesInitialized = false;
}

} // namespace engine
