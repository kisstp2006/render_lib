#include "engine/backend/vk/VulkanRenderBackend.h"
#include "engine/render/Exposure.h"

#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/debug/DebugOverlay.h"
#include "engine/render/SceneRenderer.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <GLFW/glfw3.h>

namespace engine {

namespace {

constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kLdrFormat = VK_FORMAT_R8G8B8A8_UNORM;

VkImageMemoryBarrier2 ImageBarrier(VkImage image, VkImageLayout oldLayout,
                                   VkImageLayout newLayout,
                                   VkPipelineStageFlags2 sourceStage,
                                   VkAccessFlags2 sourceAccess,
                                   VkPipelineStageFlags2 destinationStage,
                                   VkAccessFlags2 destinationAccess,
                                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    return barrier;
}

void EmitBarrier(VkCommandBuffer commandBuffer, const VkImageMemoryBarrier2& barrier)
{
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

} // namespace

void VulkanRenderBackend::CreatePostInfrastructure()
{
    m_fullscreenVertexShader = LoadShader("post/fullscreen.vert");
    m_postFragmentShader = LoadShader("post/post.frag");
    m_fxaaFragmentShader = LoadShader("post/fxaa.frag");
    m_bloomDownsampleShader = LoadShader("post/bloom_downsample.comp");
    m_bloomUpsampleShader = LoadShader("post/bloom_upsample.comp");
    m_taaFragmentShader = LoadShader("post/taa_resolve.frag");
    m_exposureShader = LoadShader("post/auto_exposure.comp");
    m_debugOverlayFragmentShader = LoadShader("debug/overlay.frag");

    const VkDescriptorSetLayoutBinding postBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(postBindings));
    layoutInfo.pBindings = postBindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_postDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create post descriptor layout");

    VkDescriptorSetLayoutBinding fxaaBinding{
        0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &fxaaBinding;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_fxaaDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create FXAA descriptor layout");

    const VkDescriptorSetLayoutBinding bloomBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(bloomBindings));
    layoutInfo.pBindings = bloomBindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bloomDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create bloom descriptor layout");

    std::array<VkDescriptorSetLayoutBinding, 5> taaBindings{};
    for (uint32_t binding = 0; binding < taaBindings.size(); ++binding)
        taaBindings[binding] = {binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    layoutInfo.bindingCount = static_cast<uint32_t>(taaBindings.size());
    layoutInfo.pBindings = taaBindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_taaDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create TAA descriptor layout");

    const VkDescriptorSetLayoutBinding exposureBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(exposureBindings));
    layoutInfo.pBindings = exposureBindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                    &m_exposureDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create auto-exposure descriptor layout");

    VkDescriptorSetLayoutBinding overlayBinding{
        0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
        VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &overlayBinding;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                    &m_debugOverlayDescriptorLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create debug overlay descriptor layout");

    VkPushConstantRange postPush{};
    postPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    postPush.size = sizeof(int32_t);
    VkPipelineLayoutCreateInfo pipelineLayout{};
    pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &m_postDescriptorLayout;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &postPush;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_postPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create post pipeline layout");

    VkPushConstantRange fxaaPush{};
    fxaaPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    fxaaPush.size = sizeof(glm::vec4);
    pipelineLayout.pSetLayouts = &m_fxaaDescriptorLayout;
    pipelineLayout.pPushConstantRanges = &fxaaPush;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_fxaaPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create FXAA pipeline layout");

    VkPushConstantRange bloomPush{};
    bloomPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bloomPush.size = sizeof(vulkan::BloomConstants);
    pipelineLayout.pSetLayouts = &m_bloomDescriptorLayout;
    pipelineLayout.pPushConstantRanges = &bloomPush;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_bloomPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create bloom pipeline layout");

    VkPushConstantRange taaPush{};
    taaPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    taaPush.size = sizeof(vulkan::TaaConstants);
    pipelineLayout.pSetLayouts = &m_taaDescriptorLayout;
    pipelineLayout.pPushConstantRanges = &taaPush;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_taaPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create TAA pipeline layout");

    pipelineLayout.pSetLayouts = &m_exposureDescriptorLayout;
    pipelineLayout.pushConstantRangeCount = 0;
    pipelineLayout.pPushConstantRanges = nullptr;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr,
                               &m_exposurePipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create auto-exposure pipeline layout");

    VkPushConstantRange overlayPush{};
    overlayPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    overlayPush.size = sizeof(glm::vec4);
    pipelineLayout.pSetLayouts = &m_debugOverlayDescriptorLayout;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &overlayPush;
    if (vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr,
                               &m_debugOverlayPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create debug overlay pipeline layout");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_postSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create post sampler");
}

void VulkanRenderBackend::DestroyPostInfrastructure()
{
    for (auto& [data, image] : m_colorLutCache)
    {
        (void)data;
        m_resources.Destroy(image);
    }
    m_colorLutCache.clear();
    m_defaultColorLut.reset();
    m_activeColorLut.reset();

    if (m_postSampler != VK_NULL_HANDLE) vkDestroySampler(m_device, m_postSampler, nullptr);
    if (m_debugOverlayPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_debugOverlayPipelineLayout, nullptr);
    if (m_exposurePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_exposurePipelineLayout, nullptr);
    if (m_taaPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_taaPipelineLayout, nullptr);
    if (m_bloomPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_bloomPipelineLayout, nullptr);
    if (m_fxaaPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_fxaaPipelineLayout, nullptr);
    if (m_postPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_postPipelineLayout, nullptr);
    if (m_bloomDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_bloomDescriptorLayout, nullptr);
    if (m_fxaaDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_fxaaDescriptorLayout, nullptr);
    if (m_postDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_postDescriptorLayout, nullptr);
    if (m_debugOverlayDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_debugOverlayDescriptorLayout, nullptr);
    if (m_exposureDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_exposureDescriptorLayout, nullptr);
    if (m_exposureShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_exposureShader, nullptr);
    if (m_debugOverlayFragmentShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_debugOverlayFragmentShader, nullptr);
    if (m_taaDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_taaDescriptorLayout, nullptr);
    if (m_taaFragmentShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_taaFragmentShader, nullptr);
    if (m_bloomUpsampleShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_bloomUpsampleShader, nullptr);
    if (m_bloomDownsampleShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_bloomDownsampleShader, nullptr);
    if (m_fxaaFragmentShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_fxaaFragmentShader, nullptr);
    if (m_postFragmentShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_postFragmentShader, nullptr);
    if (m_fullscreenVertexShader != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, m_fullscreenVertexShader, nullptr);

    m_postSampler = VK_NULL_HANDLE;
    m_debugOverlayPipelineLayout = VK_NULL_HANDLE;
    m_exposurePipelineLayout = VK_NULL_HANDLE;
    m_taaPipelineLayout = VK_NULL_HANDLE;
    m_bloomPipelineLayout = VK_NULL_HANDLE;
    m_fxaaPipelineLayout = VK_NULL_HANDLE;
    m_postPipelineLayout = VK_NULL_HANDLE;
    m_bloomDescriptorLayout = VK_NULL_HANDLE;
    m_fxaaDescriptorLayout = VK_NULL_HANDLE;
    m_postDescriptorLayout = VK_NULL_HANDLE;
    m_debugOverlayDescriptorLayout = VK_NULL_HANDLE;
    m_exposureDescriptorLayout = VK_NULL_HANDLE;
    m_exposureShader = VK_NULL_HANDLE;
    m_debugOverlayFragmentShader = VK_NULL_HANDLE;
    m_taaDescriptorLayout = VK_NULL_HANDLE;
    m_taaFragmentShader = VK_NULL_HANDLE;
    m_bloomUpsampleShader = VK_NULL_HANDLE;
    m_bloomDownsampleShader = VK_NULL_HANDLE;
    m_fxaaFragmentShader = VK_NULL_HANDLE;
    m_postFragmentShader = VK_NULL_HANDLE;
    m_fullscreenVertexShader = VK_NULL_HANDLE;
}

const vulkan::Image& VulkanRenderBackend::GetOrCreateColorLut(
    const std::shared_ptr<ColorGradingLutData>& data)
{
    if (const auto found = m_colorLutCache.find(data); found != m_colorLutCache.end())
        return found->second;
    if (!data || data->Size < 2
        || data->Values.size() != static_cast<size_t>(data->Size) * data->Size * data->Size)
        throw std::runtime_error("Vulkan: cannot upload invalid 3D color grading LUT");

    std::vector<glm::vec4> pixels;
    pixels.reserve(data->Values.size());
    for (const glm::vec3& value : data->Values)
        pixels.emplace_back(value, 1.0f);

    const VkDeviceSize byteCount = pixels.size() * sizeof(glm::vec4);
    vulkan::Buffer staging = m_resources.CreateBuffer(
        byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    vkMapMemory(m_device, staging.Memory, 0, byteCount, 0, &mapped);
    std::memcpy(mapped, pixels.data(), static_cast<size_t>(byteCount));
    vkUnmapMemory(m_device, staging.Memory);

    vulkan::Image image = m_resources.CreateImage3D(
        static_cast<uint32_t>(data->Size), static_cast<uint32_t>(data->Size),
        static_cast<uint32_t>(data->Size), VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    EmitBarrier(commandBuffer, ImageBarrier(
        image.Handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT));
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = image.Extent;
    vkCmdCopyBufferToImage(commandBuffer, staging.Handle, image.Handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    EmitBarrier(commandBuffer, ImageBarrier(
        image.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
    EndImmediateCommands(commandBuffer);
    m_resources.Destroy(staging);

    log::Info("Vulkan: uploaded " + std::to_string(data->Size) + "^3 color LUT: "
              + data->SourcePath);
    return m_colorLutCache.emplace(data, std::move(image)).first->second;
}

void VulkanRenderBackend::CreatePostTargets()
{
    DestroyPostTargets();
    const size_t imageCount = m_swapchainImages.size();
    m_hdrImages.reserve(imageCount);
    m_ldrImages.reserve(imageCount);
    m_velocityImages.reserve(imageCount);
    m_bloomChains.resize(imageCount);
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        m_hdrImages.push_back(m_resources.CreateImage2D(
            m_swapchainExtent.width, m_swapchainExtent.height, kHdrFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT));
        m_ldrImages.push_back(m_resources.CreateImage2D(
            m_swapchainExtent.width, m_swapchainExtent.height, kLdrFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT));
        m_velocityImages.push_back(m_resources.CreateImage2D(
            m_swapchainExtent.width, m_swapchainExtent.height,
            VK_FORMAT_R16G16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT));
        m_resources.SetDebugName(m_hdrImages.back(),
            "Main HDR Color " + std::to_string(imageIndex));
        m_resources.SetDebugName(m_ldrImages.back(),
            "Post Tonemap LDR " + std::to_string(imageIndex));
        m_resources.SetDebugName(m_velocityImages.back(),
            "Main HDR Velocity " + std::to_string(imageIndex));
        uint32_t width = std::max(1u, m_swapchainExtent.width / 2u);
        uint32_t height = std::max(1u, m_swapchainExtent.height / 2u);
        size_t bloomLevel = 0;
        for (vulkan::Image& level : m_bloomChains[imageIndex].Levels)
        {
            level = m_resources.CreateImage2D(
                width, height, kHdrFormat,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            m_resources.SetDebugName(level, "Bloom Image " + std::to_string(imageIndex) +
                " Level " + std::to_string(bloomLevel++));
            width = std::max(1u, width / 2u);
            height = std::max(1u, height / 2u);
        }
    }
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        for (int frame = 0; frame < kFramesInFlight; ++frame)
        {
            m_msaaHdrImages[frame] = m_resources.CreateImage2D(
                m_swapchainExtent.width, m_swapchainExtent.height, kHdrFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                1, 1, 0, m_msaaSamples);
            m_msaaVelocityImages[frame] = m_resources.CreateImage2D(
                m_swapchainExtent.width, m_swapchainExtent.height,
                VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, m_msaaSamples);
            m_resources.SetDebugName(m_msaaHdrImages[frame],
                "Main HDR MSAA Color " + std::to_string(frame));
            m_resources.SetDebugName(m_msaaVelocityImages[frame],
                "Main HDR MSAA Velocity " + std::to_string(frame));
        }
    }

    for (int history = 0; history < 2; ++history)
    {
        m_taaHistoryColor[history] = m_resources.CreateImage2D(
            m_swapchainExtent.width, m_swapchainExtent.height, kHdrFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_taaHistoryDepth[history] = m_resources.CreateImage2D(
            m_swapchainExtent.width, m_swapchainExtent.height, VK_FORMAT_R32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_resources.SetDebugName(m_taaHistoryColor[history],
            "TAA History Color " + std::to_string(history));
        m_resources.SetDebugName(m_taaHistoryDepth[history],
            "TAA History Depth " + std::to_string(history));
    }
    {
        VkCommandBuffer commandBuffer = BeginImmediateCommands();
        std::array<VkImageMemoryBarrier2, 4> barriers{};
        for (int history = 0; history < 2; ++history)
        {
            barriers[history * 2] = ImageBarrier(
                m_taaHistoryColor[history].Handle, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            barriers[history * 2 + 1] = ImageBarrier(
                m_taaHistoryDepth[history].Handle, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependency.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
        EndImmediateCommands(commandBuffer);
    }

    m_postUniformBuffers.reserve(kFramesInFlight);
    for (int frame = 0; frame < kFramesInFlight; ++frame)
    {
        m_postUniformBuffers.push_back(m_resources.CreateBuffer(
            sizeof(vulkan::PostUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        m_exposureBuffers[frame] = m_resources.CreateBuffer(
            sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_exposureReadbackValid[frame] = false;
        m_debugOverlayImages[frame] = m_resources.CreateImage2D(
            debug::DebugOverlayImage::Width, debug::DebugOverlayImage::Height,
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_debugOverlayStaging[frame] = m_resources.CreateBuffer(
            static_cast<VkDeviceSize>(debug::DebugOverlayImage::Width)
                * debug::DebugOverlayImage::Height * 4,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_debugOverlayImageInitialized[frame] = false;
        m_resources.SetDebugName(m_postUniformBuffers.back(),
            "Post Uniforms " + std::to_string(frame));
        m_resources.SetDebugName(m_exposureBuffers[frame],
            "Auto Exposure Buffer " + std::to_string(frame));
        m_resources.SetDebugName(m_debugOverlayImages[frame],
            "Debug UI Atlas " + std::to_string(frame));
        m_resources.SetDebugName(m_debugOverlayStaging[frame],
            "Debug UI Upload " + std::to_string(frame));
    }

    const uint32_t postSetCount = static_cast<uint32_t>(imageCount) * kFramesInFlight;
    const uint32_t bloomSetCount = static_cast<uint32_t>(imageCount) * 11u;
    const uint32_t taaSetCount = static_cast<uint32_t>(imageCount) * 2u;
    const uint32_t exposureSetCount = static_cast<uint32_t>(imageCount) * kFramesInFlight;
    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, postSetCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, postSetCount * 3u
            + static_cast<uint32_t>(imageCount) + bloomSetCount + taaSetCount * 5u
            + exposureSetCount + kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, bloomSetCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, exposureSetCount},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = postSetCount + static_cast<uint32_t>(imageCount)
                     + bloomSetCount + taaSetCount;
    poolInfo.maxSets += exposureSetCount + kFramesInFlight;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_postDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create post descriptor pool");

    if (!m_defaultColorLut)
        m_defaultColorLut = color_grading::MakeIdentity(2);
    const vulkan::Image& identityLut = GetOrCreateColorLut(m_defaultColorLut);
    m_activeColorLut = m_defaultColorLut;

    std::vector<VkDescriptorSetLayout> postLayouts(postSetCount, m_postDescriptorLayout);
    m_postDescriptorSets.resize(postSetCount);
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = m_postDescriptorPool;
    allocate.descriptorSetCount = postSetCount;
    allocate.pSetLayouts = postLayouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate, m_postDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate post descriptor sets");

    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
        {
            VkDescriptorBufferInfo uniformInfo{
                m_postUniformBuffers[frame].Handle, 0, sizeof(vulkan::PostUniforms)};
            const VkDescriptorImageInfo imageInfos[] = {
                {m_postSampler, m_hdrImages[imageIndex].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_postSampler, m_bloomChains[imageIndex].Levels[0].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_postSampler, identityLut.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            };
            const uint32_t setIndex = frame * static_cast<uint32_t>(imageCount) + imageIndex;
            std::array<VkWriteDescriptorSet, 4> writes{};
            for (VkWriteDescriptorSet& write : writes) write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_postDescriptorSets[setIndex],
                         0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr};
            for (uint32_t binding = 1; binding <= 3; ++binding)
            {
                writes[binding].dstSet = m_postDescriptorSets[setIndex];
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[binding].pImageInfo = &imageInfos[binding - 1];
            }
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    std::vector<VkDescriptorSetLayout> fxaaLayouts(imageCount, m_fxaaDescriptorLayout);
    m_fxaaDescriptorSets.resize(imageCount);
    allocate.descriptorSetCount = static_cast<uint32_t>(imageCount);
    allocate.pSetLayouts = fxaaLayouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate, m_fxaaDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate FXAA descriptor sets");
    for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        VkDescriptorImageInfo imageInfo{
            m_postSampler, m_ldrImages[imageIndex].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_fxaaDescriptorSets[imageIndex];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    std::vector<VkDescriptorSetLayout> bloomLayouts(bloomSetCount, m_bloomDescriptorLayout);
    std::vector<VkDescriptorSet> bloomSets(bloomSetCount);
    allocate.descriptorSetCount = bloomSetCount;
    allocate.pSetLayouts = bloomLayouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate, bloomSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate bloom descriptor sets");
    uint32_t nextSet = 0;
    for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        BloomChain& chain = m_bloomChains[imageIndex];
        for (uint32_t level = 0; level < chain.Levels.size(); ++level)
        {
            chain.DownsampleSets[level] = bloomSets[nextSet++];
            const vulkan::Image& source = level == 0 ? m_hdrImages[imageIndex] : chain.Levels[level - 1];
            VkDescriptorImageInfo sourceInfo{m_postSampler, source.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, chain.Levels[level].View, VK_IMAGE_LAYOUT_GENERAL};
            const VkWriteDescriptorSet writes[] = {
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, chain.DownsampleSets[level], 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, chain.DownsampleSets[level], 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &targetInfo, nullptr, nullptr},
            };
            vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
        }
        for (uint32_t targetLevel = 0; targetLevel < chain.UpsampleSets.size(); ++targetLevel)
        {
            chain.UpsampleSets[targetLevel] = bloomSets[nextSet++];
            VkDescriptorImageInfo sourceInfo{
                m_postSampler, chain.Levels[targetLevel + 1].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo targetInfo{
                VK_NULL_HANDLE, chain.Levels[targetLevel].View, VK_IMAGE_LAYOUT_GENERAL};
            const VkWriteDescriptorSet writes[] = {
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, chain.UpsampleSets[targetLevel], 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, chain.UpsampleSets[targetLevel], 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &targetInfo, nullptr, nullptr},
            };
            vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
        }
    }

    std::vector<VkDescriptorSetLayout> taaLayouts(taaSetCount, m_taaDescriptorLayout);
    m_taaDescriptorSets.resize(taaSetCount);
    allocate.descriptorSetCount = taaSetCount;
    allocate.pSetLayouts = taaLayouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate, m_taaDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate TAA descriptor sets");
    for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        for (uint32_t readHistory = 0; readHistory < 2; ++readHistory)
        {
            const VkDescriptorImageInfo infos[] = {
                {m_postSampler, m_hdrImages[imageIndex].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_postSampler, m_velocityImages[imageIndex].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_postSampler, m_depthImages[imageIndex].View, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
                {m_postSampler, m_taaHistoryColor[readHistory].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {m_postSampler, m_taaHistoryDepth[readHistory].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            };
            const VkDescriptorSet set = m_taaDescriptorSets[imageIndex * 2 + readHistory];
            std::array<VkWriteDescriptorSet, 5> writes{};
            for (uint32_t binding = 0; binding < writes.size(); ++binding)
            {
                writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet = set;
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[binding].pImageInfo = &infos[binding];
            }
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    std::vector<VkDescriptorSetLayout> exposureLayouts(
        exposureSetCount, m_exposureDescriptorLayout);
    m_exposureDescriptorSets.resize(exposureSetCount);
    allocate.descriptorSetCount = exposureSetCount;
    allocate.pSetLayouts = exposureLayouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate,
                                 m_exposureDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate auto-exposure descriptor sets");
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
        {
            VkDescriptorImageInfo imageInfo{
                m_postSampler, m_hdrImages[imageIndex].View,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorBufferInfo bufferInfo{
                m_exposureBuffers[frame].Handle, 0, sizeof(float)};
            const uint32_t setIndex = frame * static_cast<uint32_t>(imageCount) + imageIndex;
            const VkWriteDescriptorSet writes[] = {
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                 m_exposureDescriptorSets[setIndex], 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, nullptr, nullptr},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                 m_exposureDescriptorSets[setIndex], 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr},
            };
            vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
        }
    }

    std::array<VkDescriptorSetLayout, kFramesInFlight> overlayLayouts{};
    overlayLayouts.fill(m_debugOverlayDescriptorLayout);
    allocate.descriptorSetCount = kFramesInFlight;
    allocate.pSetLayouts = overlayLayouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate,
                                 m_debugOverlayDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate debug overlay descriptor sets");
    for (uint32_t frame = 0; frame < kFramesInFlight; ++frame)
    {
        VkDescriptorImageInfo imageInfo{
            m_postSampler, m_debugOverlayImages[frame].View,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_debugOverlayDescriptorSets[frame];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

void VulkanRenderBackend::DestroyPostTargets()
{
    m_postDescriptorSets.clear();
    m_fxaaDescriptorSets.clear();
    m_taaDescriptorSets.clear();
    m_exposureDescriptorSets.clear();
    m_debugOverlayDescriptorSets.fill(VK_NULL_HANDLE);
    if (m_postDescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_postDescriptorPool, nullptr);
    m_postDescriptorPool = VK_NULL_HANDLE;
    for (vulkan::Buffer& buffer : m_postUniformBuffers) m_resources.Destroy(buffer);
    m_postUniformBuffers.clear();
    for (vulkan::Buffer& buffer : m_exposureBuffers) m_resources.Destroy(buffer);
    for (vulkan::Buffer& buffer : m_debugOverlayStaging) m_resources.Destroy(buffer);
    for (vulkan::Image& image : m_debugOverlayImages) m_resources.Destroy(image);
    m_debugOverlayImageInitialized.fill(false);
    m_exposureReadbackValid.fill(false);
    for (BloomChain& chain : m_bloomChains)
        for (vulkan::Image& image : chain.Levels) m_resources.Destroy(image);
    m_bloomChains.clear();
    for (vulkan::Image& image : m_msaaVelocityImages) m_resources.Destroy(image);
    for (vulkan::Image& image : m_msaaHdrImages) m_resources.Destroy(image);
    for (vulkan::Image& image : m_taaHistoryDepth) m_resources.Destroy(image);
    for (vulkan::Image& image : m_taaHistoryColor) m_resources.Destroy(image);
    for (vulkan::Image& image : m_velocityImages) m_resources.Destroy(image);
    m_velocityImages.clear();
    for (vulkan::Image& image : m_ldrImages) m_resources.Destroy(image);
    m_ldrImages.clear();
    for (vulkan::Image& image : m_hdrImages) m_resources.Destroy(image);
    m_hdrImages.clear();
    m_taaHistoryValid = false;
    m_taaHistoryIndex = 0;
}

void VulkanRenderBackend::CreatePostPipelines()
{
    auto createFullscreenPipeline = [&](VkShaderModule fragmentShader, VkPipelineLayout layout,
                                        VkFormat format, bool alphaBlend, VkPipeline& output) {
        const VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, m_fullscreenVertexShader, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr},
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                  | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = alphaBlend ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(states));
        dynamic.pDynamicStates = states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = static_cast<uint32_t>(std::size(stages));
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        if (m_pipelineCache.CreateGraphics(1, &info, &output) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create fullscreen post pipeline");
    };

    createFullscreenPipeline(m_postFragmentShader, m_postPipelineLayout,
                             m_swapchainFormat, false, m_postSwapchainPipeline);
    createFullscreenPipeline(m_postFragmentShader, m_postPipelineLayout,
                             kLdrFormat, false, m_postLdrPipeline);
    createFullscreenPipeline(m_fxaaFragmentShader, m_fxaaPipelineLayout,
                             m_swapchainFormat, false, m_fxaaPipeline);
    createFullscreenPipeline(m_debugOverlayFragmentShader, m_debugOverlayPipelineLayout,
                             m_swapchainFormat, true, m_debugOverlayPipeline);
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_postSwapchainPipeline),
                 "Post Tonemap to Swapchain Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_postLdrPipeline),
                 "Post Tonemap to LDR Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_fxaaPipeline),
                 "Post FXAA Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_debugOverlayPipeline),
                 "Debug UI Pipeline");

    {
        const VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, m_fullscreenVertexShader, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, m_taaFragmentShader, "main", nullptr},
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        std::array<VkPipelineColorBlendAttachmentState, 2> attachments{};
        for (auto& attachment : attachments)
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = static_cast<uint32_t>(attachments.size());
        blend.pAttachments = attachments.data();
        const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(states));
        dynamic.pDynamicStates = states;
        const VkFormat formats[] = {kHdrFormat, VK_FORMAT_R32_SFLOAT};
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = static_cast<uint32_t>(std::size(formats));
        rendering.pColorAttachmentFormats = formats;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = static_cast<uint32_t>(std::size(stages));
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = m_taaPipelineLayout;
        if (m_pipelineCache.CreateGraphics(1, &info, &m_taaPipeline) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create TAA pipeline");
    }
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_taaPipeline),
                 "Post TAA Resolve Pipeline");

    auto createComputePipeline = [&](VkShaderModule shader, VkPipeline& pipeline) {
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shader;
        stage.pName = "main";
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = stage;
        info.layout = m_bloomPipelineLayout;
        if (m_pipelineCache.CreateCompute(1, &info, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create bloom compute pipeline");
    };
    createComputePipeline(m_bloomDownsampleShader, m_bloomDownsamplePipeline);
    createComputePipeline(m_bloomUpsampleShader, m_bloomUpsamplePipeline);
    SetDebugName(VK_OBJECT_TYPE_PIPELINE,
                 reinterpret_cast<uint64_t>(m_bloomDownsamplePipeline),
                 "Post Bloom Downsample Pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE,
                 reinterpret_cast<uint64_t>(m_bloomUpsamplePipeline),
                 "Post Bloom Upsample Pipeline");

    VkPipelineShaderStageCreateInfo exposureStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    exposureStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    exposureStage.module = m_exposureShader;
    exposureStage.pName = "main";
    VkComputePipelineCreateInfo exposureInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    exposureInfo.stage = exposureStage;
    exposureInfo.layout = m_exposurePipelineLayout;
    if (m_pipelineCache.CreateCompute(1, &exposureInfo, &m_exposurePipeline) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create auto-exposure pipeline");
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_exposurePipeline),
                 "Post Auto Exposure Pipeline");
}

void VulkanRenderBackend::DestroyPostPipelines()
{
    if (m_taaPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_taaPipeline, nullptr);
    if (m_exposurePipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_exposurePipeline, nullptr);
    if (m_bloomUpsamplePipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_bloomUpsamplePipeline, nullptr);
    if (m_bloomDownsamplePipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_bloomDownsamplePipeline, nullptr);
    if (m_fxaaPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_fxaaPipeline, nullptr);
    if (m_postLdrPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_postLdrPipeline, nullptr);
    if (m_postSwapchainPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_postSwapchainPipeline, nullptr);
    if (m_debugOverlayPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_debugOverlayPipeline, nullptr);
    m_bloomUpsamplePipeline = VK_NULL_HANDLE;
    m_bloomDownsamplePipeline = VK_NULL_HANDLE;
    m_fxaaPipeline = VK_NULL_HANDLE;
    m_postLdrPipeline = VK_NULL_HANDLE;
    m_postSwapchainPipeline = VK_NULL_HANDLE;
    m_debugOverlayPipeline = VK_NULL_HANDLE;
    m_taaPipeline = VK_NULL_HANDLE;
    m_exposurePipeline = VK_NULL_HANDLE;
}

void VulkanRenderBackend::UpdateAutoExposure(const RenderFrameData& frame)
{
    const PostProcessSettings& settings = frame.SceneData->PostProcess;
    const float deltaTime = frame.DeltaSeconds;
    m_lastExposureTime = frame.TimeSeconds;

    if (settings.Enabled && settings.AutoExposure)
    {
        if (m_exposureReadbackValid[m_currentFrame])
        {
            void* mapped = nullptr;
            if (vkMapMemory(m_device, m_exposureBuffers[m_currentFrame].Memory,
                            0, sizeof(float), 0, &mapped) == VK_SUCCESS)
            {
                const float averageLuminance = *static_cast<float*>(mapped);
                vkUnmapMemory(m_device, m_exposureBuffers[m_currentFrame].Memory);
                m_autoExposure = AdaptExposure(
                    m_autoExposure, averageLuminance, settings.AutoExposureKey,
                    settings.AutoExposureMin, settings.AutoExposureMax,
                    settings.AutoExposureSpeed, deltaTime);
            }
        }
    }
    else
    {
        m_autoExposure = 1.0f;
        m_exposureReadbackValid.fill(false);
    }
}

void VulkanRenderBackend::RecordAutoExposure(VkCommandBuffer commandBuffer,
                                              const RenderFrameData& frame,
                                              uint32_t imageIndex)
{
    const PostProcessSettings& settings = frame.SceneData->PostProcess;
    if (!settings.Enabled || !settings.AutoExposure)
        return;
    BeginDebugLabel(commandBuffer, "Post / Auto Exposure", {0.95f, 0.70f, 0.20f, 1.0f});
    const uint32_t setIndex = m_currentFrame
        * static_cast<uint32_t>(m_swapchainImages.size()) + imageIndex;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_exposurePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_exposurePipelineLayout, 0, 1,
                            &m_exposureDescriptorSets[setIndex], 0, nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    ++m_gpuDispatchesThisFrame;

    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_exposureBuffers[m_currentFrame].Handle;
    barrier.size = sizeof(float);
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    m_exposureReadbackValid[m_currentFrame] = true;
    EndDebugLabel(commandBuffer);
}

void VulkanRenderBackend::PreparePost(const RenderFrameData& frame, uint32_t imageIndex)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PreparePost", "Renderer/Vulkan/Post");
    const PostProcessSettings& settings = frame.SceneData->PostProcess;
    const bool lutEnabled = settings.Enabled && settings.ColorLut
                         && settings.ColorLutWeight > 0.0f;
    const std::shared_ptr<ColorGradingLutData> desiredLut = lutEnabled
        ? settings.ColorLut : m_defaultColorLut;
    if (desiredLut != m_activeColorLut)
    {
        // Descriptor sets span swapchain images and frames. LUT switches are
        // rare authoring operations; idling here guarantees no in-flight set
        // is rewritten and keeps the normal frame path synchronization-free.
        vkDeviceWaitIdle(m_device);
        const vulkan::Image& lut = GetOrCreateColorLut(desiredLut);
        VkDescriptorImageInfo imageInfo{
            m_postSampler, lut.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        for (VkDescriptorSet set : m_postDescriptorSets)
        {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set;
            write.dstBinding = 3;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        }
        m_activeColorLut = desiredLut;
    }

    vulkan::PostUniforms uniforms;
    const float exposure = settings.Exposure * m_autoExposure;
    uniforms.ExposureBloomPostLut = {
        exposure, settings.Enabled ? settings.BloomStrength : 0.0f,
        settings.Enabled ? 1.0f : 0.0f,
        lutEnabled ? settings.ColorLutWeight : 0.0f};
    uniforms.Curve0 = {settings.ShoulderStrength, settings.LinearStrength,
                       settings.LinearAngle, settings.ToeStrength};
    uniforms.Curve1 = {settings.ToeNumerator, settings.ToeDenominator,
                       frame.TonemapWhitePointScale, settings.Saturation};
    uniforms.Grade = glm::vec4(settings.Contrast, settings.ColorTint);
    const ColorGradingLutData& lut = *desiredLut;
    uniforms.LutDomainMinSize = glm::vec4(lut.DomainMin, static_cast<float>(lut.Size));
    uniforms.LutDomainMaxDither = glm::vec4(
        lut.DomainMax, settings.AntiAliasing == AntiAliasingMode::Fxaa ? 0.0f : 1.0f);
    uniforms.Fxaa = {settings.FxaaSubpixel, settings.FxaaEdgeThreshold,
                     settings.FxaaEdgeThresholdMin, 0.0f};

    const vulkan::Image& postSource = m_taaActive
        ? m_taaHistoryColor[(m_taaHistoryIndex + 1) % 2]
        : m_hdrImages[imageIndex];
    VkDescriptorImageInfo sourceInfo{
        m_postSampler, postSource.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const uint32_t postSetIndex = m_currentFrame
        * static_cast<uint32_t>(m_swapchainImages.size()) + imageIndex;
    VkWriteDescriptorSet sourceWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    sourceWrite.dstSet = m_postDescriptorSets[postSetIndex];
    sourceWrite.dstBinding = 1;
    sourceWrite.descriptorCount = 1;
    sourceWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sourceWrite.pImageInfo = &sourceInfo;
    vkUpdateDescriptorSets(m_device, 1, &sourceWrite, 0, nullptr);

    void* mapped = nullptr;
    vkMapMemory(m_device, m_postUniformBuffers[m_currentFrame].Memory,
                0, sizeof(uniforms), 0, &mapped);
    std::memcpy(mapped, &uniforms, sizeof(uniforms));
    vkUnmapMemory(m_device, m_postUniformBuffers[m_currentFrame].Memory);
}

const vulkan::Image& VulkanRenderBackend::RecordTemporalAA(
    VkCommandBuffer commandBuffer, const RenderFrameData& frame, uint32_t imageIndex)
{
    BeginDebugLabel(commandBuffer, "Post / TAA Resolve", {0.90f, 0.65f, 0.20f, 1.0f});
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RecordTemporalAA", "Renderer/Vulkan/Post");
    const int readHistory = m_taaHistoryIndex;
    const int writeHistory = (readHistory + 1) % 2;
    const PostProcessSettings& settings = frame.SceneData->PostProcess;
    const Camera& camera = *frame.CameraData;

    const VkImageMemoryBarrier2 inputBarriers[] = {
        ImageBarrier(m_velocityImages[imageIndex].Handle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),
        ImageBarrier(m_depthImages[imageIndex].Handle,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT),
        ImageBarrier(m_taaHistoryColor[writeHistory].Handle,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
        ImageBarrier(m_taaHistoryDepth[writeHistory].Handle,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
    };
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(std::size(inputBarriers));
    dependency.pImageMemoryBarriers = inputBarriers;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    VkRenderingAttachmentInfo attachments[2]{};
    for (VkRenderingAttachmentInfo& attachment : attachments)
    {
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    attachments[0].imageView = m_taaHistoryColor[writeHistory].View;
    attachments[1].imageView = m_taaHistoryDepth[writeHistory].View;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = m_swapchainExtent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<uint32_t>(std::size(attachments));
    rendering.pColorAttachments = attachments;
    vkCmdBeginRendering(commandBuffer, &rendering);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, m_swapchainExtent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_taaPipeline);
    const VkDescriptorSet descriptor = m_taaDescriptorSets[imageIndex * 2 + readHistory];
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_taaPipelineLayout, 0, 1, &descriptor, 0, nullptr);
    vulkan::TaaConstants constants;
    constants.Parameters = {m_taaHistoryValid ? 1.0f : 0.0f,
                            settings.TaaHistoryWeight, settings.TaaDepthThreshold,
                            settings.TaaSharpen};
    constants.Depth = {camera.NearPlane, camera.FarPlane, 0.0f, 0.0f};
    vkCmdPushConstants(commandBuffer, m_taaPipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    ++m_gpuDrawCallsThisFrame;
    vkCmdEndRendering(commandBuffer);

    const VkImageMemoryBarrier2 outputBarriers[] = {
        ImageBarrier(m_taaHistoryColor[writeHistory].Handle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),
        ImageBarrier(m_taaHistoryDepth[writeHistory].Handle,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),
    };
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(std::size(outputBarriers));
    dependency.pImageMemoryBarriers = outputBarriers;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    m_taaHistoryIndex = writeHistory;
    m_taaHistoryValid = true;
    ++m_taaFrameIndex;
    EndDebugLabel(commandBuffer);
    return m_taaHistoryColor[writeHistory];
}

void VulkanRenderBackend::RecordPost(VkCommandBuffer commandBuffer,
                                     const RenderFrameData& frame,
                                     uint32_t imageIndex)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RecordPost", "Renderer/Vulkan/Post");
    const PostProcessSettings& settings = frame.SceneData->PostProcess;
    BloomChain& bloom = m_bloomChains[imageIndex];
    BeginDebugLabel(commandBuffer, "Post / Bloom Pyramid", {0.95f, 0.45f, 0.15f, 1.0f});
    if (settings.Enabled)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_bloomDownsamplePipeline);
        for (uint32_t level = 0; level < bloom.Levels.size(); ++level)
        {
            vulkan::Image& target = bloom.Levels[level];
            EmitBarrier(commandBuffer, ImageBarrier(
                target.Handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_bloomPipelineLayout, 0, 1,
                                    &bloom.DownsampleSets[level], 0, nullptr);
            vulkan::BloomConstants constants;
            constants.Parameters = {level == 0 ? 1.0f : 0.0f,
                                    settings.BloomThreshold,
                                    settings.Exposure * m_autoExposure, 0.0f};
            vkCmdPushConstants(commandBuffer, m_bloomPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(constants), &constants);
            vkCmdDispatch(commandBuffer, (target.Extent.width + 7) / 8,
                          (target.Extent.height + 7) / 8, 1);
            ++m_gpuDispatchesThisFrame;
            EmitBarrier(commandBuffer, ImageBarrier(
                target.Handle, VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_bloomUpsamplePipeline);
        for (int targetLevel = static_cast<int>(bloom.UpsampleSets.size()) - 1;
             targetLevel >= 0; --targetLevel)
        {
            vulkan::Image& target = bloom.Levels[static_cast<size_t>(targetLevel)];
            EmitBarrier(commandBuffer, ImageBarrier(
                target.Handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
            VkDescriptorSet set = bloom.UpsampleSets[static_cast<size_t>(targetLevel)];
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_bloomPipelineLayout, 0, 1, &set, 0, nullptr);
            vkCmdDispatch(commandBuffer, (target.Extent.width + 7) / 8,
                          (target.Extent.height + 7) / 8, 1);
            ++m_gpuDispatchesThisFrame;
            EmitBarrier(commandBuffer, ImageBarrier(
                target.Handle, VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                targetLevel == 0 ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                 : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
        }
    }
    EndDebugLabel(commandBuffer);

    const bool fxaa = settings.Enabled && settings.AntiAliasing == AntiAliasingMode::Fxaa;
    BeginDebugLabel(commandBuffer, fxaa ? "Post / Tonemap + Color Grade + FXAA"
                                       : "Post / Tonemap + Color Grade",
                    {0.90f, 0.55f, 0.15f, 1.0f});
    EmitBarrier(commandBuffer, ImageBarrier(
        m_swapchainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, m_swapchainExtent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const uint32_t postSetIndex = m_currentFrame
        * static_cast<uint32_t>(m_swapchainImages.size()) + imageIndex;
    auto drawPost = [&](VkImageView targetView, VkPipeline pipeline, int32_t srgbAttachment) {
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = targetView;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = m_swapchainExtent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(commandBuffer, &rendering);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postPipelineLayout, 0, 1,
                                &m_postDescriptorSets[postSetIndex], 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_postPipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(srgbAttachment), &srgbAttachment);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++m_gpuDrawCallsThisFrame;
        vkCmdEndRendering(commandBuffer);
    };

    if (fxaa)
    {
        EmitBarrier(commandBuffer, ImageBarrier(
            m_ldrImages[imageIndex].Handle, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));
        drawPost(m_ldrImages[imageIndex].View, m_postLdrPipeline, 0);
        EmitBarrier(commandBuffer, ImageBarrier(
            m_ldrImages[imageIndex].Handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));

        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = m_swapchainImageViews[imageIndex];
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = m_swapchainExtent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(commandBuffer, &rendering);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_fxaaPipelineLayout, 0, 1,
                                &m_fxaaDescriptorSets[imageIndex], 0, nullptr);
        const glm::vec4 constants{settings.FxaaSubpixel, settings.FxaaEdgeThreshold,
                                  settings.FxaaEdgeThresholdMin, 0.0f};
        vkCmdPushConstants(commandBuffer, m_fxaaPipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++m_gpuDrawCallsThisFrame;
        vkCmdEndRendering(commandBuffer);
    }
    else
        drawPost(m_swapchainImageViews[imageIndex], m_postSwapchainPipeline, 1);
    EndDebugLabel(commandBuffer);
}

} // namespace engine
