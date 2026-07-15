#include "engine/backend/vk/VulkanRenderBackend.h"
#include "engine/backend/vk/VulkanShaderCompiler.h"

#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/core/Camera.h"
#include "engine/render/SceneRenderer.h"
#include "engine/render/TemporalAA.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <stdexcept>

#include <GLFW/glfw3.h>

namespace engine {

VkShaderModule VulkanRenderBackend::LoadShader(
    const std::filesystem::path& relativePath,
    const std::vector<ShaderDefine>& defines,
    PipelineCacheStatistics* statistics)
{
    // Older validation layers have mutable shader-module bookkeeping which is
    // not reliably concurrent even though native Vulkan creation is. Keep
    // production compilation parallel, but serialize this diagnostic fallback.
    std::unique_lock validationLock(m_shaderLoadMutex, std::defer_lock);
    if (m_validationEnabled)
        validationLock.lock();
    const std::filesystem::path shaderRoot =
        std::filesystem::path(ENGINE_SHADER_DIR) / "vk";
    const std::filesystem::path cachePath =
        m_shaderCacheDirectory / (relativePath.string() + ".spv");
    VkShaderModule module = vulkan::CompileAndLoadShaderModule(
        m_device, shaderRoot / relativePath, cachePath,
        { shaderRoot, std::filesystem::path(ENGINE_SHADER_DIR) }, defines,
        m_config.EnablePipelineCache, statistics ? statistics : &m_pipelineCacheStats);
    SetDebugName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(module),
                 "Shader Module: " + relativePath.generic_string());
    return module;
}

std::vector<VkShaderModule> VulkanRenderBackend::LoadShadersParallel(
    const std::vector<std::filesystem::path>& relativePaths)
{
    if (m_validationEnabled)
    {
        std::vector<VkShaderModule> modules;
        modules.reserve(relativePaths.size());
        try
        {
            for (const std::filesystem::path& path : relativePaths)
                modules.push_back(LoadShader(path));
        }
        catch (...)
        {
            for (VkShaderModule module : modules)
                vkDestroyShaderModule(m_device, module, nullptr);
            throw;
        }
        return modules;
    }

    std::vector<PipelineCacheStatistics> statistics(relativePaths.size());
    std::vector<concurrency::AsyncResult<VkShaderModule>> jobs;
    jobs.reserve(relativePaths.size());
    for (size_t index = 0; index < relativePaths.size(); ++index)
    {
        jobs.push_back(m_pipelineTasks->SubmitFuture(
            [this, path = relativePaths[index], &statistics, index] {
                return LoadShader(path, {}, &statistics[index]);
            }, concurrency::TaskPriority::High));
    }

    std::vector<VkShaderModule> modules(relativePaths.size(), VK_NULL_HANDLE);
    std::exception_ptr failure;
    for (size_t index = 0; index < jobs.size(); ++index)
    {
        try
        {
            modules[index] = jobs[index].Get();
        }
        catch (...)
        {
            if (!failure)
                failure = std::current_exception();
        }
    }
    if (failure)
    {
        for (VkShaderModule module : modules)
            if (module != VK_NULL_HANDLE)
                vkDestroyShaderModule(m_device, module, nullptr);
        std::rethrow_exception(failure);
    }
    for (const PipelineCacheStatistics& item : statistics)
    {
        m_pipelineCacheStats.PersistentCacheLoaded |= item.PersistentCacheLoaded;
        m_pipelineCacheStats.ShaderPermutationHits += item.ShaderPermutationHits;
        m_pipelineCacheStats.ShaderPermutationMisses += item.ShaderPermutationMisses;
        m_pipelineCacheStats.CacheBytesLoaded += item.CacheBytesLoaded;
        m_pipelineCacheStats.CacheBytesSaved += item.CacheBytesSaved;
        m_pipelineCacheStats.ShaderCompileMilliseconds += item.ShaderCompileMilliseconds;
        m_pipelineCacheStats.ShaderCacheLoadMilliseconds += item.ShaderCacheLoadMilliseconds;
    }
    return modules;
}

void VulkanRenderBackend::CreateShaderInfrastructure()
{
    try
    {
        const std::vector<VkShaderModule> shaders = LoadShadersParallel({
            "lighting/pbr.vert", "lighting/pbr.frag", "lighting/shadow.vert",
            "lighting/shadow.frag", "environment/sky.vert", "environment/sky.frag",
            "debug/bounds.vert", "debug/bounds.frag"});
        m_pbrVertexShader = shaders[0];
        m_pbrFragmentShader = shaders[1];
        m_shadowVertexShader = shaders[2];
        m_shadowFragmentShader = shaders[3];
        m_skyVertexShader = shaders[4];
        m_skyFragmentShader = shaders[5];
        m_boundsDebugVertexShader = shaders[6];
        m_boundsDebugFragmentShader = shaders[7];

        const VkShaderStageFlags frameStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        const VkDescriptorSetLayoutBinding frameBindings[] = {
            {vulkan::binding::FrameUniforms, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, frameStages, nullptr},
            {vulkan::binding::IrradianceMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::PrefilteredMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::BrdfLut, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::SunShadowMap + 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::SunShadowMap + 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::SunShadowMap + 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::SunShadowMap + 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::EnvironmentMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::PointShadowMaps, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::LocalShadowAtlas, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::LightCookieAtlas, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo frameLayoutInfo{};
        frameLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        frameLayoutInfo.bindingCount = static_cast<uint32_t>(std::size(frameBindings));
        frameLayoutInfo.pBindings = frameBindings;
        if (vkCreateDescriptorSetLayout(m_device, &frameLayoutInfo, nullptr, &m_frameDescriptorLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create frame descriptor layout");
        SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                     reinterpret_cast<uint64_t>(m_frameDescriptorLayout),
                     "Frame Descriptor Layout");

        const VkDescriptorSetLayoutBinding materialBindings[] = {
            {vulkan::binding::MaterialUniforms, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::BaseColorMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::NormalMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::MetallicRoughnessMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::OcclusionMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {vulkan::binding::EmissiveMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
        materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        materialLayoutInfo.bindingCount = static_cast<uint32_t>(std::size(materialBindings));
        materialLayoutInfo.pBindings = materialBindings;
        if (vkCreateDescriptorSetLayout(m_device, &materialLayoutInfo, nullptr, &m_materialDescriptorLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create material descriptor layout");
        SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                     reinterpret_cast<uint64_t>(m_materialDescriptorLayout),
                     "Material Descriptor Layout");

        VkDescriptorSetLayoutBinding instanceBinding{};
        instanceBinding.binding = vulkan::binding::InstanceTransforms;
        instanceBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        instanceBinding.descriptorCount = 1;
        instanceBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo instanceLayoutInfo{};
        instanceLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        instanceLayoutInfo.bindingCount = 1;
        instanceLayoutInfo.pBindings = &instanceBinding;
        if (vkCreateDescriptorSetLayout(m_device, &instanceLayoutInfo, nullptr,
                                        &m_instanceDescriptorLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create instance descriptor layout");
        SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                     reinterpret_cast<uint64_t>(m_instanceDescriptorLayout),
                     "GPU Instance Transform Descriptor Layout");

        VkDescriptorSetLayoutBinding shadowBinding{};
        shadowBinding.binding = 0;
        shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowBinding.descriptorCount = 1;
        shadowBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo shadowLayoutInfo{};
        shadowLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        shadowLayoutInfo.bindingCount = 1;
        shadowLayoutInfo.pBindings = &shadowBinding;
        if (vkCreateDescriptorSetLayout(m_device, &shadowLayoutInfo, nullptr, &m_shadowDescriptorLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create shadow descriptor layout");
        SetDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                     reinterpret_cast<uint64_t>(m_shadowDescriptorLayout),
                     "Shadow Descriptor Layout");

        const VkDescriptorSetLayout setLayouts[] = {
            m_frameDescriptorLayout, m_materialDescriptorLayout,
            m_instanceDescriptorLayout};

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(std::size(setLayouts));
        pipelineLayoutInfo.pSetLayouts = setLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pbrPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create PBR pipeline layout");
        SetDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                     reinterpret_cast<uint64_t>(m_pbrPipelineLayout), "PBR Pipeline Layout");

        VkPushConstantRange boundsRange{};
        boundsRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        boundsRange.offset = 0;
        boundsRange.size = sizeof(vulkan::BoundsDebugConstants);
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_frameDescriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &boundsRange;
        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr,
                                   &m_boundsDebugPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create visibility bounds pipeline layout");
        SetDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                     reinterpret_cast<uint64_t>(m_boundsDebugPipelineLayout),
                     "Visibility Bounds Debug Pipeline Layout");

        const VkDescriptorSetLayout shadowSetLayouts[] = {
            m_shadowDescriptorLayout, m_materialDescriptorLayout,
            m_instanceDescriptorLayout};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(std::size(shadowSetLayouts));
        pipelineLayoutInfo.pSetLayouts = shadowSetLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create shadow pipeline layout");
        SetDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                     reinterpret_cast<uint64_t>(m_shadowPipelineLayout), "Shadow Pipeline Layout");

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 * 5},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 4096;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to create descriptor pool");

        std::vector<VkDescriptorSetLayout> frameLayouts(kFramesInFlight, m_frameDescriptorLayout);
        m_frameDescriptorSets.resize(kFramesInFlight);
        VkDescriptorSetAllocateInfo descriptorAllocate{};
        descriptorAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorAllocate.descriptorPool = m_descriptorPool;
        descriptorAllocate.descriptorSetCount = kFramesInFlight;
        descriptorAllocate.pSetLayouts = frameLayouts.data();
        if (vkAllocateDescriptorSets(m_device, &descriptorAllocate, m_frameDescriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to allocate frame descriptor sets");

        for (int i = 0; i < kFramesInFlight; ++i)
        {
            VkDescriptorBufferInfo bufferInfo{m_frameArenaBuffer.Handle,
                static_cast<VkDeviceSize>(i) * kFrameArenaBytes, sizeof(vulkan::FrameUniforms)};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_frameDescriptorSets[i];
            write.dstBinding = vulkan::binding::FrameUniforms;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        }

        std::array<VkDescriptorSetLayout, kFramesInFlight> instanceLayouts{};
        instanceLayouts.fill(m_instanceDescriptorLayout);
        descriptorAllocate.descriptorSetCount = kFramesInFlight;
        descriptorAllocate.pSetLayouts = instanceLayouts.data();
        if (vkAllocateDescriptorSets(m_device, &descriptorAllocate,
                                     m_instanceDescriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to allocate instance descriptor sets");
    }
    catch (...)
    {
        DestroyShaderInfrastructure();
        throw;
    }
}

void VulkanRenderBackend::DestroyShaderInfrastructure()
{
    m_frameDescriptorSets.clear();
    m_frameUniformBuffers.clear();
    m_instanceDescriptorSets.fill(VK_NULL_HANDLE);

    if (m_descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    if (m_pbrPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_pbrPipelineLayout, nullptr);
    if (m_shadowPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr);
    if (m_boundsDebugPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_boundsDebugPipelineLayout, nullptr);
    if (m_shadowDescriptorLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_shadowDescriptorLayout, nullptr);
    if (m_materialDescriptorLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_materialDescriptorLayout, nullptr);
    if (m_instanceDescriptorLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_instanceDescriptorLayout, nullptr);
    if (m_frameDescriptorLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_frameDescriptorLayout, nullptr);
    if (m_shadowFragmentShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_shadowFragmentShader, nullptr);
    if (m_shadowVertexShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_shadowVertexShader, nullptr);
    if (m_pbrFragmentShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_pbrFragmentShader, nullptr);
    if (m_pbrVertexShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_pbrVertexShader, nullptr);
    if (m_skyFragmentShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_skyFragmentShader, nullptr);
    if (m_skyVertexShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_skyVertexShader, nullptr);
    if (m_boundsDebugFragmentShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_boundsDebugFragmentShader, nullptr);
    if (m_boundsDebugVertexShader != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_boundsDebugVertexShader, nullptr);

    m_pbrPipelineLayout = VK_NULL_HANDLE;
    m_shadowPipelineLayout = VK_NULL_HANDLE;
    m_boundsDebugPipelineLayout = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_materialDescriptorLayout = VK_NULL_HANDLE;
    m_instanceDescriptorLayout = VK_NULL_HANDLE;
    m_frameDescriptorLayout = VK_NULL_HANDLE;
    m_shadowDescriptorLayout = VK_NULL_HANDLE;
    m_shadowFragmentShader = VK_NULL_HANDLE;
    m_shadowVertexShader = VK_NULL_HANDLE;
    m_pbrFragmentShader = VK_NULL_HANDLE;
    m_pbrVertexShader = VK_NULL_HANDLE;
    m_skyFragmentShader = VK_NULL_HANDLE;
    m_skyVertexShader = VK_NULL_HANDLE;
    m_boundsDebugFragmentShader = VK_NULL_HANDLE;
    m_boundsDebugVertexShader = VK_NULL_HANDLE;
}

void VulkanRenderBackend::UpdateFrameUniforms(const RenderFrameData& frame)
{
    const Scene& scene = *frame.SceneData;
    const Camera& camera = *frame.CameraData;
    vulkan::FrameUniforms uniforms;
    uniforms.View = frame.View;

    const PostProcessSettings& post = scene.PostProcess;
    m_taaActive = post.Enabled && post.AntiAliasing == AntiAliasingMode::Taa;
    const bool modeChanged = post.AntiAliasing != m_previousAaMode;
    const bool cameraCut = m_taaHistoryValid && IsTemporalCameraCut(
        m_previousCameraPosition, camera.Position, m_previousCameraForward,
        camera.Forward(), m_previousCameraFov, camera.FovDegrees);
    if (modeChanged || m_previousScene != &scene || cameraCut)
    {
        m_taaHistoryValid = false;
        m_previousTransforms.clear();
        m_taaFrameIndex = 0;
    }
    m_previousAaMode = post.AntiAliasing;

    glm::mat4 clipCorrection(1.0f);
    clipCorrection[1][1] = -1.0f; // Vulkan framebuffer Y points down.
    clipCorrection[2][2] = 0.5f;  // Convert OpenGL [-1, 1] depth to Vulkan [0, 1].
    clipCorrection[3][2] = 0.5f;
    const glm::vec2 jitter = m_taaActive
        ? TemporalJitterPixels(m_taaFrameIndex) : glm::vec2(0.0f);
    const glm::mat4 projection = m_taaActive
        ? ApplyProjectionJitter(frame.BaseProjection, jitter, frame.Width, frame.Height,
                                post.TaaJitterScale)
        : frame.BaseProjection;
    uniforms.Projection = clipCorrection * projection;
    m_currentViewProjection = uniforms.Projection * frame.View;
    uniforms.PreviousViewProjection = m_taaActive && m_taaHistoryValid
        ? m_previousViewProjection : m_currentViewProjection;
    uniforms.CameraPosition = glm::vec4(camera.Position, 1.0f);
    const glm::vec3& lightDirection = frame.SunDirection;
    const DayNightState& dayNight = frame.DayNight;
    uniforms.SunDirectionIntensity = glm::vec4(lightDirection,
                                                scene.Sun.Intensity * dayNight.DirectSunAmount);
    uniforms.SunColor = glm::vec4(scene.Sun.Color * dayNight.SunTint, 1.0f);

    uniforms.SkyZenithIntensity = glm::vec4(scene.Sky.ZenithColor, scene.Sky.SkyIntensity);
    uniforms.SkyHorizonPostEnabled = glm::vec4(scene.Sky.HorizonColor, post.Enabled ? 1.0f : 0.0f);
    uniforms.SkyGroundExposure = glm::vec4(scene.Sky.GroundColor, post.Exposure);
    uniforms.SkySunParameters = {
        scene.Sky.SunIntensity, glm::radians(scene.Sky.SunAngularRadiusDeg),
        std::exp2(scene.Environment.BackgroundExposureEV),
        scene.Environment.Source == EnvironmentSource::ProceduralSky ? 1.0f : 0.0f};
    uniforms.PostCurve0 = {post.ShoulderStrength, post.LinearStrength, post.LinearAngle, post.ToeStrength};
    uniforms.PostCurve1 = {
        post.ToeNumerator, post.ToeDenominator, frame.TonemapWhitePointScale, post.Saturation};
    uniforms.PostGrade = {post.Contrast, post.ColorTint.r, post.ColorTint.g, post.ColorTint.b};
    uniforms.FogColorOpacity = glm::vec4(scene.Fog.Color, scene.Fog.Opacity);
    uniforms.FogStartEndExponents = {
        scene.Fog.Start, scene.Fog.End, scene.Fog.DistanceExponent, scene.Fog.HeightExponent};
    uniforms.FogHeightEnabled = {
        scene.Fog.HeightFadeTop, scene.Fog.HeightFadeBottom, scene.Fog.Enabled ? 1.0f : 0.0f, 0.0f};
    uniforms.NightZenithIntensity = glm::vec4(scene.Sky.NightZenithColor, scene.Sky.NightSkyIntensity);
    uniforms.NightHorizonGlow = glm::vec4(scene.Sky.NightHorizonColor, scene.Sky.NightHorizonGlow);
    uniforms.MilkyWayColorIntensity = glm::vec4(scene.Sky.MilkyWayColor, scene.Sky.MilkyWayIntensity);
    uniforms.StarWarmDensity = glm::vec4(scene.Sky.StarWarmColor, scene.Sky.StarDensity);
    uniforms.StarCoolSize = glm::vec4(scene.Sky.StarCoolColor, scene.Sky.StarSize);
    uniforms.StarAnimation = {
        scene.Sky.StarIntensity, scene.Sky.StarTwinkle, scene.Sky.StarTwinkleSpeed,
        frame.TimeSeconds};
    uniforms.NightRotationFlags = {
        scene.Sky.NightSkyRotationDegrees, scene.Sky.NightSkyRotationSpeed, 0.0f, 0.0f};
    uniforms.SkyFeatureFlags = {
        frame.ProceduralDayNight ? 1.0f : 0.0f,
        scene.Sky.StarsEnabled ? 1.0f : 0.0f,
        scene.Sky.MilkyWayEnabled ? 1.0f : 0.0f,
        scene.Sky.AnimateNightSky ? 1.0f : 0.0f};

    const CascadeShadowData& cascades = frame.Cascades;
    glm::mat4 shadowClipCorrection(1.0f);
    shadowClipCorrection[1][1] = -1.0f;
    shadowClipCorrection[2][2] = 0.5f;
    shadowClipCorrection[3][2] = 0.5f;
    for (int cascade = 0; cascade < 4; ++cascade)
    {
        uniforms.CascadeMatrices[cascade] = shadowClipCorrection * cascades.LightMatrices[cascade];
        uniforms.CascadeSplits[cascade] = cascades.SplitDepths[cascade];

        const vulkan::ShadowUniforms shadowUniforms{uniforms.CascadeMatrices[cascade], glm::vec4(0.0f)};
        void* shadowMapped = nullptr;
        vulkan::Buffer& shadowBuffer = m_shadowUniformBuffers[m_currentFrame][cascade];
        if (vkMapMemory(m_device, shadowBuffer.Memory, 0, sizeof(shadowUniforms), 0, &shadowMapped) != VK_SUCCESS)
            throw std::runtime_error("Vulkan: failed to map shadow uniform buffer");
        std::memcpy(shadowMapped, &shadowUniforms, sizeof(shadowUniforms));
        vkUnmapMemory(m_device, shadowBuffer.Memory);
    }
    uniforms.ShadowParameters = {
        scene.Shadows.CascadeBlendFraction, frame.SunShadowsActive ? 1.0f : 0.0f,
        scene.Shadows.DebugCascades ? 1.0f : 0.0f, 0.0f};

    for (uint32_t i = 0; i < frame.LocalLights.PointCount; ++i)
    {
        const PointLight& light = *frame.LocalLights.Points[i].Source;
        uniforms.PointPositionRadius[i] = glm::vec4(light.Position, light.Radius);
        uniforms.PointColor[i] = glm::vec4(light.Color * light.Intensity, 0.0f);
        uniforms.PointShadowCookie[i] = {
            static_cast<float>(m_pointShadowSlots[i]), static_cast<float>(m_pointCookieSlots[i]), 0.0f, 0.0f};
    }
    for (uint32_t i = 0; i < frame.LocalLights.SpotCount; ++i)
    {
        const PreparedSpotLight& prepared = frame.LocalLights.Spots[i];
        const SpotLight& light = *prepared.Source;
        uniforms.SpotPositionRange[i] = glm::vec4(light.Position, light.Range);
        uniforms.SpotDirectionCosOuter[i] = glm::vec4(
            prepared.Direction, std::cos(glm::radians(light.OuterConeDeg)));
        uniforms.SpotColorCosInner[i] = glm::vec4(
            light.Color * light.Intensity, std::cos(glm::radians(light.InnerConeDeg)));
        uniforms.SpotMatrices[i] = m_spotShadowMatrices[i];
        uniforms.SpotShadowRects[i] = m_spotShadowRects[i];
        uniforms.SpotCookieData[i].x = static_cast<float>(m_spotCookieSlots[i]);
    }
    for (uint32_t i = 0; i < frame.LocalLights.AreaCount; ++i)
    {
        const PreparedAreaLight& prepared = frame.LocalLights.Areas[i];
        const AreaLight& light = *prepared.Source;
        uniforms.AreaPositionRange[i] = glm::vec4(light.Position, light.Range);
        uniforms.AreaDirectionMinRoughness[i] = glm::vec4(prepared.Direction, light.MinRoughness);
        uniforms.AreaRightHalfWidth[i] = glm::vec4(prepared.Right, light.Size.x * 0.5f);
        uniforms.AreaUpHalfHeight[i] = glm::vec4(prepared.Up, light.Size.y * 0.5f);
        uniforms.AreaColor[i] = glm::vec4(light.Color * light.Intensity, 0.0f);
        uniforms.AreaSoftness[i] = glm::vec4(light.Softness, 0.0f, 0.0f);
        uniforms.AreaMatrices[i] = m_areaShadowMatrices[i];
        uniforms.AreaShadowRects[i] = m_areaShadowRects[i];
        uniforms.AreaCookieData[i].x = static_cast<float>(m_areaCookieSlots[i]);
    }
    uniforms.LightCounts = {
        frame.LocalLights.PointCount, frame.LocalLights.SpotCount, frame.LocalLights.AreaCount, 0u};

    const render::ArenaAllocation allocation = m_frameGpuArena->Allocate(
        m_currentFrame, sizeof(uniforms), 256);
    if (!allocation)
        throw std::runtime_error("Vulkan: per-frame GPU arena exhausted by frame uniforms");
    std::memcpy(static_cast<std::byte*>(m_frameArenaMapped) + allocation.Offset,
                &uniforms, sizeof(uniforms));
}

} // namespace engine
