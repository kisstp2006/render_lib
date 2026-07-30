#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/backend/gl/GLDebug.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/render/SceneRenderer.h"
#include "engine/render/Exposure.h"
#include "engine/render/RendererFrameGraph.h"
#include "engine/render/TemporalAA.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/scene/Scene.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace engine {

namespace {

constexpr int kUnitAlbedo = 0;
constexpr int kUnitNormal = 1;
constexpr int kUnitMrao = 2;
constexpr int kUnitMetallicRoughness = 15;
constexpr int kUnitEmissive = 3;
constexpr int kUnitOcclusion = 4;
constexpr int kUnitIrradiance = 5;
constexpr int kUnitPrefilter = 6;
constexpr int kUnitBrdfLut = 7;
constexpr std::array<int, kShadowCascadeCount> kUnitCascades{8, 9, 10, 11};

void ApplyGraphBarriers(const rendergraph::CompiledPass& pass)
{
    GLbitfield bits = 0;
    for (const rendergraph::Barrier& barrier : pass.Barriers)
    {
        using rendergraph::ResourceState;
        if (barrier.Before == ResourceState::ShaderWrite ||
            barrier.After == ResourceState::ShaderWrite)
            bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        if (barrier.After == ResourceState::ShaderRead ||
            barrier.After == ResourceState::DepthRead)
            bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
        if (barrier.Before == ResourceState::ColorAttachment ||
            barrier.Before == ResourceState::DepthWrite ||
            barrier.After == ResourceState::ColorAttachment ||
            barrier.After == ResourceState::DepthWrite)
            bits |= GL_FRAMEBUFFER_BARRIER_BIT;
        if (barrier.Before == ResourceState::TransferDestination ||
            barrier.After == ResourceState::TransferSource ||
            barrier.After == ResourceState::TransferDestination)
            bits |= GL_TEXTURE_UPDATE_BARRIER_BIT;
    }
    if (bits != 0)
        glMemoryBarrier(bits);
}

} // namespace

void GLRenderBackend::RenderFrame(const RenderFrameData& frame)
{
    gl_debug::ScopedGroup frameMarker("Frame");
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("OpenGL.RenderFrame", "Renderer/OpenGL");
    const Scene& scene = *frame.SceneData;
    const Camera& camera = *frame.CameraData;
    BeginGpuProfilerFrame();

    const PostProcessSettings& pp = scene.PostProcess;
    const bool taaActive = pp.Enabled && pp.AntiAliasing == AntiAliasingMode::Taa;
    const bool temporalModeChanged = pp.AntiAliasing != m_previousAaMode;
    const bool cameraCut = m_taaHistoryValid && IsTemporalCameraCut(
        m_previousCameraPosition, camera.Position, m_previousCameraForward, camera.Forward(),
        m_previousCameraFov, camera.FovDegrees);
    if (temporalModeChanged || m_previousScene != &scene || cameraCut)
    {
        m_taaHistoryValid = false;
        m_previousTransforms.clear();
        m_taaFrameIndex = 0;
    }
    m_previousAaMode = pp.AntiAliasing;

    const glm::mat4 view = frame.View;
    const glm::mat4 baseProjection = frame.BaseProjection;
    const glm::vec2 jitter = taaActive ? TemporalJitterPixels(m_taaFrameIndex) : glm::vec2(0.0f);
    const glm::mat4 proj = taaActive
        ? ApplyProjectionJitter(baseProjection, jitter, m_width, m_height, pp.TaaJitterScale)
        : baseProjection;
    const glm::mat4 currentViewProjection = proj * view;
    const glm::mat4 previousViewProjection = taaActive && m_taaHistoryValid
        ? m_previousViewProjection : currentViewProjection;
    const glm::vec3& lightDir = frame.SunDirection;
    const DayNightState& dayNight = frame.DayNight;
    const glm::vec3& effectiveSunColor = frame.EffectiveSunColor;
    const bool sunShadowsActive = frame.SunShadowsActive;
    const CascadeShadowData& cascades = frame.Cascades;
    std::vector<const PreparedRenderCommand*> visibleCommands;
    PrepareOcclusionFrame(frame, visibleCommands);

    std::vector<const PreparedRenderCommand*> shadowCommandPointers;
    shadowCommandPointers.reserve(frame.ShadowCommands.size());
    for (const PreparedRenderCommand& command : frame.ShadowCommands)
        shadowCommandPointers.push_back(&command);
    InstanceBatchBuildResult shadowBatches = BuildInstanceBatches(
        shadowCommandPointers, scene.Instancing, 0);
    const uint32_t shadowInstanceCount = shadowBatches.Statistics.SourceInstances;
    InstanceBatchBuildResult mainBatches = BuildInstanceBatches(
        visibleCommands, scene.Instancing, shadowInstanceCount);

    std::vector<GpuInstanceData> instanceData;
    instanceData.reserve(static_cast<size_t>(shadowInstanceCount) +
                         mainBatches.Statistics.SourceInstances);
    const auto appendInstances = [&](const InstanceBatchBuildResult& submission,
                                     bool usePreviousTransform) {
        for (const InstanceDrawBatch& batch : submission.Batches)
        for (const PreparedRenderCommand* command : batch.Commands)
        {
            const MeshInstance& instance = *command->Source;
            GpuInstanceData gpu;
            gpu.Model = instance.Transform;
            const auto previous = instance.TemporalId != 0
                ? m_previousTransforms.find(instance.TemporalId)
                : m_previousTransforms.end();
            gpu.PreviousModel = usePreviousTransform && taaActive &&
                m_taaHistoryValid && previous != m_previousTransforms.end()
                ? previous->second : instance.Transform;
            instanceData.push_back(gpu);
        }
    };
    appendInstances(shadowBatches, false);
    appendInstances(mainBatches, true);
    if (instanceData.empty())
        instanceData.push_back({});
    m_instanceTransformBytes = instanceData.size() * sizeof(GpuInstanceData);
    glNamedBufferData(m_instanceTransformBuffer,
        static_cast<GLsizeiptr>(m_instanceTransformBytes), instanceData.data(),
        GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_instanceTransformBuffer);
    m_frameStats.GpuInstancingActive = scene.Instancing.Enabled;
    m_frameStats.GpuInstanceCount = mainBatches.Statistics.SourceInstances;
    m_frameStats.GpuInstanceBatchCount = mainBatches.Statistics.DrawBatches;
    m_frameStats.GpuInstancedBatchCount = mainBatches.Statistics.InstancedBatches;
    m_frameStats.GpuDrawCallsSaved = mainBatches.Statistics.DrawCallsSaved;

    const auto environmentPass = [&]() {
        gl_debug::ScopedGroup marker("Environment / IBL Update");
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment", "Renderer/OpenGL");
        m_environment->EnsureBaked(scene.Sun, scene.Sky, scene.Environment);
    };

    // --- Four-cascade sun shadow pass ---
    const auto directionalShadowPass = [&]() {
    BeginGpuProfilerPass(DirectionalShadowPass);
    {
        gl_debug::ScopedGroup marker("Shadows / Directional Cascades");
        if (sunShadowsActive)
        {
            ENGINE_CPU_PROFILE_SCOPE_CATEGORY("DirectionalShadows", "Renderer/OpenGL");
            glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
            m_shadowShader->Use();
            glCullFace(GL_FRONT);
            for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
            {
                gl_debug::ScopedGroup cascadeMarker(
                    "Directional Cascade " + std::to_string(cascade));
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowMaps[cascade], 0);
                glViewport(0, 0, m_shadowSizes[cascade], m_shadowSizes[cascade]);
                glClear(GL_DEPTH_BUFFER_BIT);
                m_shadowShader->SetMat4("uLightSpaceMatrix", cascades.LightMatrices[cascade]);
                for (const InstanceDrawBatch& batch : shadowBatches.Batches)
                {
                    const MeshInstance& instance = *batch.Representative->Source;
                    if (!instance.CastsShadows)
                        continue;
                    const Material& mat = instance.Mat;
                    m_shadowShader->SetBool("uAlphaMasked", mat.Alpha == Material::AlphaMode::Mask);
                    m_shadowShader->SetBool("uHasAlbedoMap", mat.AlbedoMap != nullptr);
                    m_shadowShader->SetFloat("uBaseColorAlpha", mat.BaseColorAlpha);
                    m_shadowShader->SetFloat("uAlphaCutoff", mat.AlphaCutoff);
                    m_shadowShader->SetInt("uAlbedoMap", kUnitAlbedo);
                    m_shadowShader->SetInt("uBaseInstance",
                                           static_cast<int>(batch.FirstInstance));
                    BindMaterialTexture(mat.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
                    GetOrCreateMesh(GetCommandMesh(*batch.Representative)).DrawInstanced(
                        static_cast<uint32_t>(batch.Commands.size()), batch.FirstInstance);
                    ++m_gpuDrawCallsThisFrame;
                }
            }
            glCullFace(GL_BACK);
        }
    }
    EndGpuProfilerPass();
    };

    const auto localShadowPass = [&]() {
    BeginGpuProfilerPass(LocalShadowPass);
    {
        gl_debug::ScopedGroup marker("Shadows / Local Lights");
        RenderLocalLightShadows(frame, shadowBatches);
    }
    EndGpuProfilerPass();
    };

    // --- Main HDR pass (MSAA) ---
    const auto mainHdrPass = [&]() {
    {
    gl_debug::ScopedGroup marker("Main HDR / Geometry + Sky + Resolve");
    profiling::CpuProfileScope mainHdrScope("MainHDR", "Renderer/OpenGL");
    BeginGpuProfilerPass(MainHdrPass);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_pbrShader->Use();
    m_pbrShader->SetMat4("uView", view);
    m_pbrShader->SetMat4("uCurrentViewProjection", currentViewProjection);
    m_pbrShader->SetMat4("uPreviousViewProjection", previousViewProjection);
    m_pbrShader->SetVec3("uCameraPos", camera.Position);

    m_pbrShader->SetVec3("uSunDirection", lightDir);
    m_pbrShader->SetVec3("uSunColor", effectiveSunColor);
    m_pbrShader->SetBool("uSunCastsShadows", sunShadowsActive);
    m_pbrShader->SetFloat("uCascadeBlendFraction", scene.Shadows.CascadeBlendFraction);
    m_pbrShader->SetBool("uDebugCascades", scene.Shadows.DebugCascades);
    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
    {
        const std::string index = "[" + std::to_string(cascade) + "]";
        m_pbrShader->SetMat4("uCascadeMatrices" + index, cascades.LightMatrices[cascade]);
        m_pbrShader->SetFloat("uCascadeSplits" + index, cascades.SplitDepths[cascade]);
        glActiveTexture(GL_TEXTURE0 + kUnitCascades[cascade]);
        glBindTexture(GL_TEXTURE_2D, m_shadowMaps[cascade]);
        m_pbrShader->SetInt("uShadowMaps" + index, kUnitCascades[cascade]);
    }

    BindLocalLights();

    const FogSettings& fog = scene.Fog;
    m_pbrShader->SetBool("uFogEnabled", fog.Enabled);
    m_pbrShader->SetVec3("uFogColor", fog.Color);
    m_pbrShader->SetFloat("uFogOpacity", fog.Opacity);
    m_pbrShader->SetVec2("uFogStartEnd", {fog.Start, fog.End});
    m_pbrShader->SetFloat("uFogDistanceExponent", fog.DistanceExponent);
    m_pbrShader->SetVec2("uFogHeightTopBottom", {fog.HeightFadeTop, fog.HeightFadeBottom});
    m_pbrShader->SetFloat("uFogHeightExponent", fog.HeightExponent);

    m_environment->BindIrradianceMap(kUnitIrradiance);
    m_environment->BindPrefilterMap(kUnitPrefilter);
    m_environment->BindBrdfLut(kUnitBrdfLut);
    m_pbrShader->SetInt("uIrradianceMap", kUnitIrradiance);
    m_pbrShader->SetInt("uPrefilterMap", kUnitPrefilter);
    m_pbrShader->SetInt("uBrdfLut", kUnitBrdfLut);
    m_pbrShader->SetFloat("uPrefilterMips", static_cast<float>(GLEnvironment::kPrefilterMips));

    m_pbrShader->SetInt("uAlbedoMap", kUnitAlbedo);
    m_pbrShader->SetInt("uNormalMap", kUnitNormal);
    m_pbrShader->SetInt("uMraoMap", kUnitMrao);
    m_pbrShader->SetInt("uMetallicRoughnessMap", kUnitMetallicRoughness);
    m_pbrShader->SetInt("uEmissiveMap", kUnitEmissive);
    m_pbrShader->SetInt("uOcclusionMap", kUnitOcclusion);

    for (const InstanceDrawBatch& batch : mainBatches.Batches)
    {
        const MeshInstance& instance = *batch.Representative->Source;

        const Material& mat = instance.Mat;
        m_pbrShader->SetVec3("uAlbedo", mat.Albedo);
        m_pbrShader->SetFloat("uBaseColorAlpha", mat.BaseColorAlpha);
        m_pbrShader->SetFloat("uMetallic", mat.Metallic);
        m_pbrShader->SetFloat("uRoughness", mat.Roughness);
        m_pbrShader->SetVec3("uEmissive", mat.Emissive);
        m_pbrShader->SetFloat("uAO", mat.AmbientOcclusion);
        m_pbrShader->SetFloat("uSpecularF0", mat.SpecularF0);

        m_pbrShader->SetBool("uHasAlbedoMap", mat.AlbedoMap != nullptr);
        m_pbrShader->SetBool("uHasNormalMap", mat.NormalMap != nullptr);
        m_pbrShader->SetBool("uHasMraoMap", mat.MraoMap != nullptr);
        m_pbrShader->SetBool("uHasMetallicRoughnessMap", mat.MetallicRoughnessMap != nullptr);
        m_pbrShader->SetBool("uHasOcclusionMap", mat.OcclusionMap != nullptr);
        m_pbrShader->SetBool("uHasEmissiveMap", mat.EmissiveMap != nullptr);
        m_pbrShader->SetBool("uAlphaMasked", mat.Alpha == Material::AlphaMode::Mask);
        m_pbrShader->SetFloat("uAlphaCutoff", mat.AlphaCutoff);
        m_pbrShader->SetInt("uBaseInstance",
                            static_cast<int>(batch.FirstInstance));
        BindMaterialTexture(mat.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
        BindMaterialTexture(mat.NormalMap, kUnitNormal, *m_defaultNormal);
        BindMaterialTexture(mat.MraoMap, kUnitMrao, *m_defaultWhite);
        BindMaterialTexture(mat.MetallicRoughnessMap, kUnitMetallicRoughness,
                            *m_defaultWhite);
        BindMaterialTexture(mat.EmissiveMap, kUnitEmissive, *m_defaultWhite);
        BindMaterialTexture(mat.OcclusionMap, kUnitOcclusion, *m_defaultWhite);

        GetOrCreateMesh(GetCommandMesh(*batch.Representative)).DrawInstanced(
            static_cast<uint32_t>(batch.Commands.size()), batch.FirstInstance);
        ++m_gpuDrawCallsThisFrame;
    }

    // --- Skybox (only fills pixels the geometry left at depth 1.0) ---
    if (scene.Sky.VisibleBackground)
    {
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    m_skyShader->Use();
    m_skyShader->SetMat4("uInvProj", glm::inverse(proj));
    m_skyShader->SetMat4("uInvView", glm::inverse(view));
    m_skyShader->SetMat4("uPreviousViewProjection", previousViewProjection);
    m_environment->BindEnvironmentMap(0);
    m_skyShader->SetInt("uEnvMap", 0);
    m_skyShader->SetFloat("uBackgroundMultiplier", std::exp2(scene.Environment.BackgroundExposureEV));
    m_skyShader->SetBool("uUseProceduralSky", scene.Environment.Source == EnvironmentSource::ProceduralSky);
    m_skyShader->SetVec3("uZenithColor", scene.Sky.ZenithColor);
    m_skyShader->SetVec3("uHorizonColor", scene.Sky.HorizonColor);
    m_skyShader->SetVec3("uGroundColor", scene.Sky.GroundColor);
    m_skyShader->SetVec3("uNightZenithColor", scene.Sky.NightZenithColor);
    m_skyShader->SetVec3("uNightHorizonColor", scene.Sky.NightHorizonColor);
    m_skyShader->SetVec3("uMilkyWayColor", scene.Sky.MilkyWayColor);
    m_skyShader->SetVec3("uStarWarmColor", scene.Sky.StarWarmColor);
    m_skyShader->SetVec3("uStarCoolColor", scene.Sky.StarCoolColor);
    m_skyShader->SetFloat("uSkyIntensity", scene.Sky.SkyIntensity);
    m_skyShader->SetFloat("uNightSkyIntensity", scene.Sky.NightSkyIntensity);
    m_skyShader->SetFloat("uNightHorizonGlow", scene.Sky.NightHorizonGlow);
    m_skyShader->SetBool("uDrawProceduralSun", scene.Environment.Source == EnvironmentSource::ProceduralSky
                                              && scene.Sky.SunIntensity > 0.0f);
    m_skyShader->SetVec3("uSunDirection", scene.Sun.Direction);
    m_skyShader->SetVec3("uSunColor", scene.Sun.Color * dayNight.SunTint);
    m_skyShader->SetFloat("uSunIntensity", scene.Sky.SunIntensity);
    m_skyShader->SetFloat("uSunAngularRadius", glm::radians(scene.Sky.SunAngularRadiusDeg));
    m_skyShader->SetBool("uEnableDayNightCycle", frame.ProceduralDayNight);
    m_skyShader->SetFloat("uStarIntensity", scene.Sky.StarIntensity);
    m_skyShader->SetFloat("uStarDensity", scene.Sky.StarDensity);
    m_skyShader->SetFloat("uStarSize", scene.Sky.StarSize);
    m_skyShader->SetFloat("uStarTwinkle", scene.Sky.StarTwinkle);
    m_skyShader->SetFloat("uStarTwinkleSpeed", scene.Sky.StarTwinkleSpeed);
    m_skyShader->SetFloat("uMilkyWayIntensity", scene.Sky.MilkyWayIntensity);
    m_skyShader->SetFloat("uNightSkyRotationDegrees", scene.Sky.NightSkyRotationDegrees);
    m_skyShader->SetFloat("uNightSkyRotationSpeed", scene.Sky.NightSkyRotationSpeed);
    m_skyShader->SetBool("uStarsEnabled", scene.Sky.StarsEnabled);
    m_skyShader->SetBool("uMilkyWayEnabled", scene.Sky.MilkyWayEnabled);
    m_skyShader->SetBool("uAnimateNightSky", scene.Sky.AnimateNightSky);
    m_skyShader->SetFloat("uTime", frame.TimeSeconds);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++m_gpuDrawCallsThisFrame;
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    }

    if (!frame.VisibilityDebug.empty())
    {
        gl_debug::ScopedGroup boundsMarker("Visibility / Bounds Debug");
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        m_boundsDebugShader->Use();
        m_boundsDebugShader->SetMat4("uViewProjection", currentViewProjection);
        m_boundsDebugShader->SetBool("uLineMode", false);
        glBindVertexArray(m_boundsDebugVao);
        for (const VisibilityDebugBounds& debugBounds : frame.VisibilityDebug)
        {
            glm::vec3 color{0.1f, 3.0f, 0.25f};
            if (scene.Visibility.DebugOcclusion &&
                m_occlusionCulledInstances.contains(debugBounds.InstanceIndex))
                color = {2.2f, 0.15f, 3.0f};
            else if (debugBounds.Classification == VisibilityClassification::FrustumCulled)
                color = {3.0f, 0.12f, 0.08f};
            else if (debugBounds.Classification == VisibilityClassification::DistanceCulled)
                color = {3.0f, 1.4f, 0.05f};
            m_boundsDebugShader->SetVec3("uBoundsMinimum", debugBounds.Bounds.Minimum);
            m_boundsDebugShader->SetVec3("uBoundsMaximum", debugBounds.Bounds.Maximum);
            m_boundsDebugShader->SetVec3("uColor", color);
            glDrawArrays(GL_LINES, 0, 24);
            ++m_gpuDrawCallsThisFrame;
        }
        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }

    if (!frame.DebugLines.empty())
    {
        gl_debug::ScopedGroup lineMarker("Debug Draw / Lines");
        glDisable(GL_CULL_FACE);
        m_boundsDebugShader->Use();
        m_boundsDebugShader->SetMat4("uViewProjection", currentViewProjection);
        m_boundsDebugShader->SetBool("uLineMode", true);
        glBindVertexArray(m_boundsDebugVao);
        for (const DebugLine& line : frame.DebugLines)
        {
            if (line.Depth == DebugDepthMode::DepthTested)
                glEnable(GL_DEPTH_TEST);
            else
                glDisable(GL_DEPTH_TEST);
            m_boundsDebugShader->SetVec3("uBoundsMinimum", line.Start);
            m_boundsDebugShader->SetVec3("uBoundsMaximum", line.End);
            m_boundsDebugShader->SetVec3("uColor", line.Color);
            glDrawArrays(GL_LINES, 0, 2);
            ++m_gpuDrawCallsThisFrame;
        }
        m_boundsDebugShader->SetBool("uLineMode", false);
        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }

    // --- Resolve MSAA -> HDR texture ---
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glDrawBuffer(GL_COLOR_ATTACHMENT1);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    EndGpuProfilerPass();
    mainHdrScope.End();
    }
    };

    const auto postProcessPass = [&]() {
    {
    gl_debug::ScopedGroup marker("Post Process");
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PostProcessing", "Renderer/OpenGL");
    BeginGpuProfilerPass(PostProcessPass);

    unsigned int postSourceTexture = m_hdrColorTex;
    if (taaActive)
    {
        postSourceTexture = ResolveTemporalAA(scene, camera, currentViewProjection);
        ++m_taaFrameIndex;
    }
    else
    {
        m_previousViewProjection = currentViewProjection;
        m_previousCameraPosition = camera.Position;
        m_previousCameraForward = camera.Forward();
        m_previousCameraFov = camera.FovDegrees;
        m_previousScene = &scene;
    }

    // --- Auto-exposure (Source 2 tonemap controller style) ---
    // Average scene luminance from the top mip of the HDR resolve texture,
    // adapted over time toward Key/avgLum within [Min, Max] multipliers.
    // This is still a simple average rather than a percentile histogram, but
    // the double-buffered PBO readback keeps it off the current frame's CPU
    // critical path.
    const float deltaTime = frame.DeltaSeconds;
    m_lastFrameTime = frame.TimeSeconds;

    {
    gl_debug::ScopedGroup autoExposureMarker("Post / Auto Exposure");
    if (pp.Enabled && pp.AutoExposure)
    {
        glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
        glGenerateMipmap(GL_TEXTURE_2D);

        const int maxDim = std::max(m_width, m_height);
        const int topMip = static_cast<int>(std::floor(std::log2(static_cast<float>(maxDim))));

        const int writeIndex = m_exposurePboIndex;
        const int readIndex = (writeIndex + 1) % 2;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_exposurePbos[writeIndex]);
        glGetTexImage(GL_TEXTURE_2D, topMip, GL_RGBA, GL_FLOAT, nullptr);

        if (m_exposurePboFrames > 0)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_exposurePbos[readIndex]);
            const auto* avg = static_cast<const float*>(glMapBufferRange(
                GL_PIXEL_PACK_BUFFER, 0, sizeof(float) * 4, GL_MAP_READ_BIT));
            if (avg)
            {
                const float avgLum = 0.2126f * avg[0] + 0.7152f * avg[1] + 0.0722f * avg[2];
                m_autoExposure = AdaptExposure(
                    m_autoExposure, avgLum, pp.AutoExposureKey,
                    pp.AutoExposureMin, pp.AutoExposureMax,
                    pp.AutoExposureSpeed, deltaTime);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_exposurePboIndex = readIndex;
        ++m_exposurePboFrames;
    }
    else
    {
        m_autoExposure = 1.0f;
        m_exposurePboFrames = 0;
    }
    }

    const float effectiveExposure = pp.Exposure * m_autoExposure;

    // --- Bloom ---
    if (pp.Enabled && !m_bloomChain.empty())
        RenderBloom(postSourceTexture, pp.BloomThreshold, effectiveExposure);

    // --- Tonemap/color-grade pass; FXAA consumes an intermediate LDR image. ---
    gl_debug::ScopedGroup outputMarker("Post / Tonemap + Color Grade + AA");
    const bool fxaaActive = pp.Enabled && pp.AntiAliasing == AntiAliasingMode::Fxaa;
    glBindFramebuffer(GL_FRAMEBUFFER, fxaaActive ? m_postFbo : m_activeOutputFramebuffer);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);

    m_postShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, postSourceTexture);
    m_postShader->SetInt("uSceneColor", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (pp.Enabled && !m_bloomChain.empty()) ? m_bloomChain[0].Texture : m_defaultWhite->Id());
    m_postShader->SetInt("uBloom", 1);

    m_postShader->SetBool("uPostEnabled", pp.Enabled);
    m_postShader->SetFloat("uExposure", effectiveExposure);
    m_postShader->SetFloat("uSaturation", pp.Saturation);
    m_postShader->SetFloat("uContrast", pp.Contrast);
    m_postShader->SetVec3("uColorTint", pp.ColorTint);
    const bool colorLutActive = pp.Enabled && pp.ColorLut && pp.ColorLutWeight > 0.0f;
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, colorLutActive ? GetOrCreateColorLut(pp.ColorLut) : 0);
    m_postShader->SetInt("uColorLut", 2);
    m_postShader->SetFloat("uColorLutWeight", colorLutActive ? pp.ColorLutWeight : 0.0f);
    m_postShader->SetVec3("uColorLutDomainMin", colorLutActive ? pp.ColorLut->DomainMin : glm::vec3(0.0f));
    m_postShader->SetVec3("uColorLutDomainMax", colorLutActive ? pp.ColorLut->DomainMax : glm::vec3(1.0f));
    m_postShader->SetFloat("uColorLutSize", colorLutActive ? static_cast<float>(pp.ColorLut->Size) : 2.0f);
    m_postShader->SetBool("uDitherEnabled", !fxaaActive);
    m_postShader->SetFloat("uBloomStrength", (pp.Enabled && !m_bloomChain.empty()) ? pp.BloomStrength : 0.0f);
    m_postShader->SetFloat("uShoulderStrength", pp.ShoulderStrength);
    m_postShader->SetFloat("uLinearStrength", pp.LinearStrength);
    m_postShader->SetFloat("uLinearAngle", pp.LinearAngle);
    m_postShader->SetFloat("uToeStrength", pp.ToeStrength);
    m_postShader->SetFloat("uToeNumerator", pp.ToeNumerator);
    m_postShader->SetFloat("uToeDenominator", pp.ToeDenominator);
    m_postShader->SetFloat("uWhitePointScale", frame.TonemapWhitePointScale);

    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++m_gpuDrawCallsThisFrame;

    if (fxaaActive)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_activeOutputFramebuffer);
        glViewport(0, 0, m_width, m_height);
        m_fxaaShader->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_postColorTex);
        m_fxaaShader->SetInt("uColor", 0);
        m_fxaaShader->SetFloat("uSubpixel", pp.FxaaSubpixel);
        m_fxaaShader->SetFloat("uEdgeThreshold", pp.FxaaEdgeThreshold);
        m_fxaaShader->SetFloat("uEdgeThresholdMin", pp.FxaaEdgeThresholdMin);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ++m_gpuDrawCallsThisFrame;
    }
    if (!m_hdrScreenshotPath.empty())
        SaveHdrScreenshot(postSourceTexture);
    EndGpuProfilerPass();
    }
    };
    const auto occlusionCullPass = [&]() {
        DispatchOcclusionQueries(frame);
    };
    const auto hiZBuildPass = [&]() {
        BuildHiZPyramid(currentViewProjection);
    };
    const auto debugUiPass = [&]() {
    BeginGpuProfilerPass(DebugUiPass);
    {
        gl_debug::ScopedGroup marker("Debug UI");
        RenderDebugOverlay(frame);
        if (!m_renderingOffscreen && m_uiRenderCallback)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_activeOutputFramebuffer);
            glViewport(0, 0, m_width, m_height);
            m_uiRenderCallback({RenderBackendApi::OpenGL, 0, 0,
                                static_cast<uint32_t>(m_width),
                                static_cast<uint32_t>(m_height)});
        }
    }
    EndGpuProfilerPass();
    };

    rendergraph::RendererFrameGraphFeatures graphFeatures;
    graphFeatures.Width = static_cast<uint32_t>(std::max(m_width, 1));
    graphFeatures.Height = static_cast<uint32_t>(std::max(m_height, 1));
    graphFeatures.MsaaSamples = static_cast<uint32_t>(std::max(m_msaaSamples, 1));
    graphFeatures.BloomLevels = static_cast<uint32_t>(m_bloomChain.size());
    graphFeatures.SunShadows = sunShadowsActive;
    graphFeatures.LocalShadows = !scene.PointLights().empty() || !scene.SpotLights().empty() ||
                                 !scene.AreaLights().empty();
    graphFeatures.TemporalAA = taaActive;
    graphFeatures.TaaReadIndex = static_cast<uint32_t>(m_taaHistoryIndex);
    graphFeatures.TaaWriteIndex = static_cast<uint32_t>((m_taaHistoryIndex + 1) % 2);
    graphFeatures.Bloom = pp.Enabled && !m_bloomChain.empty();
    graphFeatures.Fxaa = pp.Enabled && pp.AntiAliasing == AntiAliasingMode::Fxaa;
    graphFeatures.DebugUi = true;
    graphFeatures.OcclusionCulling = m_occlusionState.Active();
    graphFeatures.OcclusionCandidateCount = m_frameStats.GpuOcclusionCandidates;
    graphFeatures.HiZMipLevels = static_cast<uint32_t>(m_hizMipLevels);
    rendergraph::RendererFrameGraphCallbacks graphCallbacks;
    graphCallbacks[rendergraph::RendererPass::Environment] = environmentPass;
    graphCallbacks[rendergraph::RendererPass::DirectionalShadows] = directionalShadowPass;
    graphCallbacks[rendergraph::RendererPass::LocalShadows] = localShadowPass;
    graphCallbacks[rendergraph::RendererPass::OcclusionCull] = occlusionCullPass;
    graphCallbacks[rendergraph::RendererPass::MainHdr] = mainHdrPass;
    graphCallbacks[rendergraph::RendererPass::HiZBuild] = hiZBuildPass;
    graphCallbacks[rendergraph::RendererPass::PostProcess] = postProcessPass;
    graphCallbacks[rendergraph::RendererPass::DebugUi] = debugUiPass;
    std::string graphError;
    if (!rendergraph::BuildRendererFrameGraph(m_renderGraph, m_renderGraphConfig,
                                               graphFeatures, std::move(graphCallbacks),
                                               &graphError))
        throw std::runtime_error("OpenGL render graph: " + graphError);
    m_renderGraph.ExecutePhase(rendergraph::PassPhase::Prepare, ApplyGraphBarriers);
    m_renderGraph.ExecutePhase(rendergraph::PassPhase::Render, ApplyGraphBarriers);

    // Keep every buffered profiler query available even when the optional
    // visibility passes are absent from this frame's graph.
    if (!m_occlusionState.Active())
    {
        BeginGpuProfilerPass(OcclusionCullPass);
        EndGpuProfilerPass();
        BeginGpuProfilerPass(HiZBuildPass);
        EndGpuProfilerPass();
    }

    EndGpuProfilerFrame(scene);
    glEnable(GL_DEPTH_TEST);

    if (!m_screenshotPath.empty())
        SaveScreenshot();
    m_hasFrameDebugFrame = true;
}

} // namespace engine
