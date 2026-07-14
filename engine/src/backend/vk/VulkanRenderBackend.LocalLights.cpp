#include "engine/backend/vk/VulkanRenderBackend.h"

#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/render/SceneRenderer.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

namespace {

constexpr uint32_t kPointShadowSize = 512;
constexpr uint32_t kMaxPointShadows = 4;
constexpr uint32_t kLocalShadowAtlasSize = 4096;
constexpr uint32_t kLocalShadowTileSize = 1024;
constexpr uint32_t kCookieAtlasSize = 1024;
constexpr uint32_t kCookieTileSize = 256;

VkImageView CreateView(VkDevice device, VkImage image, VkFormat format, VkImageViewType type,
                       VkImageAspectFlags aspect, uint32_t baseLayer, uint32_t layerCount)
{
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = type;
    info.format = format;
    info.subresourceRange = {aspect, 0, 1, baseLayer, layerCount};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create local shadow image view");
    return view;
}

void Transition(VkCommandBuffer commandBuffer, VkImage image, VkImageAspectFlags aspect,
                VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layers)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, layers};
    if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        || oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    else
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                              | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
             || newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

glm::vec4 AtlasRect(int slot)
{
    constexpr int columns = kLocalShadowAtlasSize / kLocalShadowTileSize;
    constexpr float scale = static_cast<float>(kLocalShadowTileSize) / kLocalShadowAtlasSize;
    return {static_cast<float>(slot % columns) * scale,
            static_cast<float>(slot / columns) * scale, scale, scale};
}

VkPipeline CreatePointShadowPipeline(vulkan::PipelineCacheStore& cache,
                                     VkPipelineLayout layout, VkFormat depthFormat,
                                     VkShaderModule vertexShader, VkShaderModule fragmentShader)
{
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr},
    };
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Position))},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, UV))},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.depthAttachmentFormat = depthFormat;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(std::size(stages));
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterization;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (cache.CreateGraphics(1, &info, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create point shadow pipeline");
    return pipeline;
}

} // namespace

void VulkanRenderBackend::CreateLocalLightResources()
{
    const std::vector<VkShaderModule> shaders = LoadShadersParallel({
        "lighting/point_shadow.vert", "lighting/point_shadow.frag"});
    m_pointShadowVertexShader = shaders[0];
    m_pointShadowFragmentShader = shaders[1];
    m_pointShadowPipeline = CreatePointShadowPipeline(
        m_pipelineCache, m_shadowPipelineLayout, m_depthFormat,
        m_pointShadowVertexShader, m_pointShadowFragmentShader);
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(m_pointShadowPipeline),
                 "Point Light Shadow Pipeline");

    m_pointShadowArray = m_resources.CreateImage2D(
        kPointShadowSize, kPointShadowSize, m_depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT, 1, kMaxPointShadows * 6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
    m_pointShadowCubeArrayView = CreateView(
        m_device, m_pointShadowArray.Handle, m_depthFormat, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        VK_IMAGE_ASPECT_DEPTH_BIT, 0, kMaxPointShadows * 6);
    for (uint32_t layer = 0; layer < m_pointShadowFaceViews.size(); ++layer)
        m_pointShadowFaceViews[layer] = CreateView(
            m_device, m_pointShadowArray.Handle, m_depthFormat, VK_IMAGE_VIEW_TYPE_2D,
            VK_IMAGE_ASPECT_DEPTH_BIT, layer, 1);

    m_localShadowAtlas = m_resources.CreateImage2D(
        kLocalShadowAtlasSize, kLocalShadowAtlasSize, m_depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    m_cookieAtlas = m_resources.CreateImage2D(
        kCookieAtlasSize, kCookieAtlasSize, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    m_resources.SetDebugName(m_pointShadowArray, "Point Light Shadow Cube Array");
    m_resources.SetDebugName(m_localShadowAtlas, "Spot + Area Shadow Atlas");
    m_resources.SetDebugName(m_cookieAtlas, "Local Light Cookie Atlas");

    VkSamplerCreateInfo shadowSampler{};
    shadowSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadowSampler.magFilter = VK_FILTER_LINEAR;
    shadowSampler.minFilter = VK_FILTER_LINEAR;
    shadowSampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadowSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &shadowSampler, nullptr, &m_pointShadowSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create point shadow sampler");
    shadowSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowSampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(m_device, &shadowSampler, nullptr, &m_localShadowSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create local shadow sampler");
    shadowSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &shadowSampler, nullptr, &m_cookieSampler) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to create cookie sampler");
    SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_pointShadowSampler),
                 "Point Shadow Sampler");
    SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_localShadowSampler),
                 "Spot + Area Shadow Sampler");
    SetDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_cookieSampler),
                 "Light Cookie Sampler");

    std::array<VkDescriptorSetLayout, 32> layouts{};
    layouts.fill(m_shadowDescriptorLayout);
    std::array<VkDescriptorSet, 32> sets{};
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = m_descriptorPool;
    allocate.descriptorSetCount = static_cast<uint32_t>(sets.size());
    allocate.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(m_device, &allocate, sets.data()) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: failed to allocate local shadow descriptor sets");

    for (uint32_t i = 0; i < m_pointShadowUniformBuffers.size(); ++i)
    {
        m_pointShadowUniformBuffers[i] = m_resources.CreateBuffer(
            sizeof(vulkan::ShadowUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_resources.SetDebugName(m_pointShadowUniformBuffers[i],
            "Point Shadow Face Uniforms " + std::to_string(i));
        m_pointShadowDescriptorSets[i] = sets[i];
        const VkDescriptorBufferInfo buffer{
            m_pointShadowUniformBuffers[i].Handle, 0, sizeof(vulkan::ShadowUniforms)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
    for (uint32_t i = 0; i < m_projectedShadowUniformBuffers.size(); ++i)
    {
        m_projectedShadowUniformBuffers[i] = m_resources.CreateBuffer(
            sizeof(vulkan::ShadowUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_resources.SetDebugName(m_projectedShadowUniformBuffers[i],
            "Projected Local Shadow Uniforms " + std::to_string(i));
        m_projectedShadowDescriptorSets[i] = sets[m_pointShadowUniformBuffers.size() + i];
        const VkDescriptorBufferInfo buffer{
            m_projectedShadowUniformBuffers[i].Handle, 0, sizeof(vulkan::ShadowUniforms)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_projectedShadowDescriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    Transition(commandBuffer, m_pointShadowArray.Handle, VK_IMAGE_ASPECT_DEPTH_BIT,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
               kMaxPointShadows * 6);
    Transition(commandBuffer, m_localShadowAtlas.Handle, VK_IMAGE_ASPECT_DEPTH_BIT,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1);
    Transition(commandBuffer, m_cookieAtlas.Handle, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
    const VkClearColorValue white{{1.0f, 1.0f, 1.0f, 1.0f}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(commandBuffer, m_cookieAtlas.Handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);
    Transition(commandBuffer, m_cookieAtlas.Handle, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    EndImmediateCommands(commandBuffer);
    m_localShadowImagesInitialized = true;

    for (int frame = 0; frame < kFramesInFlight; ++frame)
    {
        const std::array<VkDescriptorImageInfo, 3> images{{
            {m_pointShadowSampler, m_pointShadowCubeArrayView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {m_localShadowSampler, m_localShadowAtlas.View, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {m_cookieSampler, m_cookieAtlas.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        const uint32_t bindings[] = {
            vulkan::binding::PointShadowMaps, vulkan::binding::LocalShadowAtlas,
            vulkan::binding::LightCookieAtlas};
        std::array<VkWriteDescriptorSet, 3> writes{};
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

void VulkanRenderBackend::PrepareLocalLights(const RenderFrameData& frame)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PrepareLocalLights", "Renderer/Vulkan");
    m_pointShadowSlots.fill(-1);
    m_pointCookieSlots.fill(0);
    m_spotShadowRects.fill(glm::vec4(0.0f));
    m_spotCookieSlots.fill(0);
    m_areaShadowRects.fill(glm::vec4(0.0f));
    m_areaCookieSlots.fill(0);
    m_pointShadowCount = 0;
    m_projectedShadowCount = 0;

    std::unordered_map<const TextureData*, int> desiredCookies;
    int nextCookie = 1;
    auto addCookie = [&](const std::shared_ptr<TextureData>& cookie) {
        if (cookie && !desiredCookies.contains(cookie.get()) && nextCookie < 16)
            desiredCookies.emplace(cookie.get(), nextCookie++);
    };
    for (uint32_t i = 0; i < frame.LocalLights.PointCount; ++i)
        addCookie(frame.LocalLights.Points[i].Source->Cookie);
    for (uint32_t i = 0; i < frame.LocalLights.SpotCount; ++i)
        addCookie(frame.LocalLights.Spots[i].Source->Cookie);
    for (uint32_t i = 0; i < frame.LocalLights.AreaCount; ++i)
        addCookie(frame.LocalLights.Areas[i].Source->Cookie);

    if (desiredCookies != m_cookieSlots)
    {
        m_cookieSlots = desiredCookies;
        std::vector<uint8_t> atlas(static_cast<size_t>(kCookieAtlasSize) * kCookieAtlasSize, 255);
        for (const auto& [texture, slot] : m_cookieSlots)
        {
            const int tileX = (slot % 4) * static_cast<int>(kCookieTileSize);
            const int tileY = (slot / 4) * static_cast<int>(kCookieTileSize);
            for (uint32_t y = 0; y < kCookieTileSize; ++y)
            for (uint32_t x = 0; x < kCookieTileSize; ++x)
            {
                const int sourceX = std::min(static_cast<int>(x) * texture->Width
                                             / static_cast<int>(kCookieTileSize), texture->Width - 1);
                const int sourceY = std::min(static_cast<int>(y) * texture->Height
                                             / static_cast<int>(kCookieTileSize), texture->Height - 1);
                atlas[static_cast<size_t>(tileY + y) * kCookieAtlasSize + tileX + x]
                    = texture->Pixels[(static_cast<size_t>(sourceY) * texture->Width + sourceX)
                                      * texture->Channels];
            }
        }
        vulkan::Buffer staging = m_resources.CreateBuffer(
            atlas.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped = nullptr;
        vkMapMemory(m_device, staging.Memory, 0, staging.Size, 0, &mapped);
        std::memcpy(mapped, atlas.data(), atlas.size());
        vkUnmapMemory(m_device, staging.Memory);
        VkCommandBuffer commandBuffer = BeginImmediateCommands();
        Transition(commandBuffer, m_cookieAtlas.Handle, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {kCookieAtlasSize, kCookieAtlasSize, 1};
        vkCmdCopyBufferToImage(commandBuffer, staging.Handle, m_cookieAtlas.Handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Transition(commandBuffer, m_cookieAtlas.Handle, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        EndImmediateCommands(commandBuffer);
        m_resources.Destroy(staging);
    }

    const auto cookieSlot = [&](const std::shared_ptr<TextureData>& cookie) {
        if (const auto found = m_cookieSlots.find(cookie.get()); found != m_cookieSlots.end())
            return found->second;
        return 0;
    };
    glm::mat4 correction(1.0f);
    correction[1][1] = -1.0f;
    correction[2][2] = 0.5f;
    correction[3][2] = 0.5f;
    const std::array<glm::vec3, 6> directions{{
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    const std::array<glm::vec3, 6> ups{{
        {0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};

    for (uint32_t lightIndex = 0; lightIndex < frame.LocalLights.PointCount; ++lightIndex)
    {
        const PointLight& light = *frame.LocalLights.Points[lightIndex].Source;
        m_pointCookieSlots[lightIndex] = cookieSlot(light.Cookie);
        if (!light.CastsShadows || m_pointShadowCount >= static_cast<int>(kMaxPointShadows))
            continue;
        const int slot = m_pointShadowCount++;
        m_pointShadowSlots[lightIndex] = slot;
        const glm::mat4 projection = correction * glm::perspective(
            glm::radians(90.0f), 1.0f, 0.1f, light.Radius);
        for (int face = 0; face < 6; ++face)
        {
            const vulkan::ShadowUniforms uniforms{
                projection * glm::lookAt(light.Position, light.Position + directions[face], ups[face]),
                glm::vec4(light.Position, light.Radius)};
            vulkan::Buffer& buffer = m_pointShadowUniformBuffers[slot * 6 + face];
            void* mapped = nullptr;
            vkMapMemory(m_device, buffer.Memory, 0, sizeof(uniforms), 0, &mapped);
            std::memcpy(mapped, &uniforms, sizeof(uniforms));
            vkUnmapMemory(m_device, buffer.Memory);
        }
    }

    for (uint32_t i = 0; i < frame.LocalLights.SpotCount; ++i)
    {
        const PreparedSpotLight& prepared = frame.LocalLights.Spots[i];
        m_spotCookieSlots[i] = cookieSlot(prepared.Source->Cookie);
        m_spotShadowMatrices[i] = correction * prepared.Projection;
        if (prepared.Source->CastsShadows && m_projectedShadowCount < 8)
        {
            const int slot = m_projectedShadowCount++;
            m_spotShadowRects[i] = AtlasRect(slot);
            const vulkan::ShadowUniforms uniforms{m_spotShadowMatrices[i], glm::vec4(0.0f)};
            void* mapped = nullptr;
            vkMapMemory(m_device, m_projectedShadowUniformBuffers[slot].Memory,
                        0, sizeof(uniforms), 0, &mapped);
            std::memcpy(mapped, &uniforms, sizeof(uniforms));
            vkUnmapMemory(m_device, m_projectedShadowUniformBuffers[slot].Memory);
        }
    }
    for (uint32_t i = 0; i < frame.LocalLights.AreaCount; ++i)
    {
        const PreparedAreaLight& prepared = frame.LocalLights.Areas[i];
        m_areaCookieSlots[i] = cookieSlot(prepared.Source->Cookie);
        m_areaShadowMatrices[i] = correction * prepared.Projection;
        if (prepared.Source->CastsShadows && m_projectedShadowCount < 8)
        {
            const int slot = m_projectedShadowCount++;
            m_areaShadowRects[i] = AtlasRect(slot);
            const vulkan::ShadowUniforms uniforms{m_areaShadowMatrices[i], glm::vec4(0.0f)};
            void* mapped = nullptr;
            vkMapMemory(m_device, m_projectedShadowUniformBuffers[slot].Memory,
                        0, sizeof(uniforms), 0, &mapped);
            std::memcpy(mapped, &uniforms, sizeof(uniforms));
            vkUnmapMemory(m_device, m_projectedShadowUniformBuffers[slot].Memory);
        }
    }
}

void VulkanRenderBackend::RecordLocalLightShadows(VkCommandBuffer commandBuffer, const Scene& scene)
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RecordLocalLightShadows", "Renderer/Vulkan");
    const auto drawScene = [&](VkPipelineLayout layout) {
        for (const MeshInstance& instance : scene.Instances())
        {
            if (!instance.CastsShadows || !instance.Mesh)
                continue;
            const auto meshFound = m_meshCache.find(instance.Mesh.get());
            const auto materialFound = m_materialCache.find(&instance.Mat);
            if (meshFound == m_meshCache.end() || materialFound == m_materialCache.end())
                throw std::runtime_error("Vulkan: local shadow draw resources were not prepared before recording");
            const GpuMesh& mesh = meshFound->second;
            GpuMaterial& material = materialFound->second;
            const vulkan::ObjectConstants object{instance.Transform};
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh.VertexBuffer.Handle, &offset);
            vkCmdBindIndexBuffer(commandBuffer, mesh.IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                    1, 1, &material.DescriptorSets[m_currentFrame], 0, nullptr);
            vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(object), &object);
            vkCmdDrawIndexed(commandBuffer, mesh.IndexCount, 1, 0, 0, 0);
            ++m_gpuDrawCallsThisFrame;
        }
    };

    if (m_pointShadowCount > 0)
    {
        Transition(commandBuffer, m_pointShadowArray.Handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kMaxPointShadows * 6);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pointShadowPipeline);
        for (int slot = 0; slot < m_pointShadowCount; ++slot)
        for (int face = 0; face < 6; ++face)
        {
            VkRenderingAttachmentInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = m_pointShadowFaceViews[slot * 6 + face];
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = {kPointShadowSize, kPointShadowSize};
            rendering.layerCount = 1;
            rendering.pDepthAttachment = &depth;
            vkCmdBeginRendering(commandBuffer, &rendering);
            VkViewport viewport{0.0f, 0.0f, static_cast<float>(kPointShadowSize),
                                static_cast<float>(kPointShadowSize), 0.0f, 1.0f};
            VkRect2D scissor{{0,0},{kPointShadowSize,kPointShadowSize}};
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            VkDescriptorSet set = m_pointShadowDescriptorSets[slot * 6 + face];
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_shadowPipelineLayout, 0, 1, &set, 0, nullptr);
            drawScene(m_shadowPipelineLayout);
            vkCmdEndRendering(commandBuffer);
        }
        Transition(commandBuffer, m_pointShadowArray.Handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, kMaxPointShadows * 6);
    }

    if (m_projectedShadowCount > 0)
    {
        Transition(commandBuffer, m_localShadowAtlas.Handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
        VkRenderingAttachmentInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = m_localShadowAtlas.View;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = {kLocalShadowAtlasSize, kLocalShadowAtlasSize};
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(commandBuffer, &rendering);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
        for (int slot = 0; slot < m_projectedShadowCount; ++slot)
        {
            const glm::vec4 rect = AtlasRect(slot);
            VkViewport viewport{
                rect.x * kLocalShadowAtlasSize, rect.y * kLocalShadowAtlasSize,
                static_cast<float>(kLocalShadowTileSize), static_cast<float>(kLocalShadowTileSize), 0.0f, 1.0f};
            VkRect2D scissor{
                {static_cast<int32_t>(rect.x * kLocalShadowAtlasSize),
                 static_cast<int32_t>(rect.y * kLocalShadowAtlasSize)},
                {kLocalShadowTileSize, kLocalShadowTileSize}};
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            VkDescriptorSet set = m_projectedShadowDescriptorSets[slot];
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_shadowPipelineLayout, 0, 1, &set, 0, nullptr);
            drawScene(m_shadowPipelineLayout);
        }
        vkCmdEndRendering(commandBuffer);
        Transition(commandBuffer, m_localShadowAtlas.Handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1);
    }
}

void VulkanRenderBackend::DestroyLocalLightResources()
{
    for (vulkan::Buffer& buffer : m_projectedShadowUniformBuffers) m_resources.Destroy(buffer);
    for (vulkan::Buffer& buffer : m_pointShadowUniformBuffers) m_resources.Destroy(buffer);
    if (m_cookieSampler) vkDestroySampler(m_device, m_cookieSampler, nullptr);
    if (m_localShadowSampler) vkDestroySampler(m_device, m_localShadowSampler, nullptr);
    if (m_pointShadowSampler) vkDestroySampler(m_device, m_pointShadowSampler, nullptr);
    for (VkImageView& view : m_pointShadowFaceViews)
    {
        if (view) vkDestroyImageView(m_device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (m_pointShadowCubeArrayView)
        vkDestroyImageView(m_device, m_pointShadowCubeArrayView, nullptr);
    m_resources.Destroy(m_cookieAtlas);
    m_resources.Destroy(m_localShadowAtlas);
    m_resources.Destroy(m_pointShadowArray);
    if (m_pointShadowPipeline) vkDestroyPipeline(m_device, m_pointShadowPipeline, nullptr);
    if (m_pointShadowFragmentShader) vkDestroyShaderModule(m_device, m_pointShadowFragmentShader, nullptr);
    if (m_pointShadowVertexShader) vkDestroyShaderModule(m_device, m_pointShadowVertexShader, nullptr);
    m_pointShadowCubeArrayView = VK_NULL_HANDLE;
    m_pointShadowPipeline = VK_NULL_HANDLE;
    m_pointShadowFragmentShader = m_pointShadowVertexShader = VK_NULL_HANDLE;
    m_cookieSampler = m_localShadowSampler = m_pointShadowSampler = VK_NULL_HANDLE;
    m_cookieSlots.clear();
}

} // namespace engine
