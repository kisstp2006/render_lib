#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/backend/gl/GLDebug.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/render/SceneRenderer.h"
#include "engine/render/Exposure.h"
#include "engine/render/TemporalAA.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/scene/Scene.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace engine {

namespace {

constexpr int kUnitAlbedo = 1;
constexpr int kUnitNormal = 2;
constexpr int kUnitMrao = 3;
constexpr int kUnitIrradiance = 4;
constexpr int kUnitPrefilter = 5;
constexpr int kUnitBrdfLut = 6;
constexpr int kUnitEmissive = 8;
constexpr int kUnitOcclusion = 9;
constexpr std::array<int, kShadowCascadeCount> kUnitCascades{0, 10, 11, 12};

} // namespace

void GLRenderBackend::RenderFrame(const RenderFrameData& frame)
{
    gl_debug::ScopedGroup frameMarker("Frame");
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("OpenGL.RenderFrame", "Renderer/OpenGL");
    const Scene& scene = *frame.SceneData;
    const Camera& camera = *frame.CameraData;
    {
        gl_debug::ScopedGroup marker("Environment / IBL Update");
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Environment", "Renderer/OpenGL");
        m_environment->EnsureBaked(scene.Sun, scene.Sky, scene.Environment);
    }
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

    // --- Four-cascade sun shadow pass ---
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
                for (const auto& instance : scene.Instances())
                {
                    if (!instance.CastsShadows)
                        continue;
                    m_shadowShader->SetMat4("uModel", instance.Transform);
                    const Material& mat = instance.Mat;
                    m_shadowShader->SetBool("uAlphaMasked", mat.Alpha == Material::AlphaMode::Mask);
                    m_shadowShader->SetBool("uHasAlbedoMap", mat.AlbedoMap != nullptr);
                    m_shadowShader->SetFloat("uBaseColorAlpha", mat.BaseColorAlpha);
                    m_shadowShader->SetFloat("uAlphaCutoff", mat.AlphaCutoff);
                    m_shadowShader->SetInt("uAlbedoMap", kUnitAlbedo);
                    BindMaterialTexture(mat.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
                    GetOrCreateMesh(instance.Mesh).Draw();
                    ++m_gpuDrawCallsThisFrame;
                }
            }
            glCullFace(GL_BACK);
        }
    }
    EndGpuProfilerPass();

    BeginGpuProfilerPass(LocalShadowPass);
    {
        gl_debug::ScopedGroup marker("Shadows / Local Lights");
        RenderLocalLightShadows(frame);
    }
    EndGpuProfilerPass();

    // --- Main HDR pass (MSAA) ---
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
    m_pbrShader->SetInt("uMetallicRoughnessMap", kUnitMrao);
    m_pbrShader->SetInt("uEmissiveMap", kUnitEmissive);
    m_pbrShader->SetInt("uOcclusionMap", kUnitOcclusion);

    for (const auto& instance : scene.Instances())
    {
        m_pbrShader->SetMat4("uModel", instance.Transform);
        const auto previousTransform = instance.TemporalId != 0
            ? m_previousTransforms.find(instance.TemporalId) : m_previousTransforms.end();
        m_pbrShader->SetMat4("uPreviousModel", taaActive && m_taaHistoryValid
            && previousTransform != m_previousTransforms.end() ? previousTransform->second : instance.Transform);
        m_pbrShader->SetMat4("uNormalMatrix", glm::transpose(glm::inverse(instance.Transform)));

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
        BindMaterialTexture(mat.AlbedoMap, kUnitAlbedo, *m_defaultWhite);
        BindMaterialTexture(mat.NormalMap, kUnitNormal, *m_defaultNormal);
        BindMaterialTexture(mat.MetallicRoughnessMap ? mat.MetallicRoughnessMap : mat.MraoMap, kUnitMrao, *m_defaultWhite);
        BindMaterialTexture(mat.EmissiveMap, kUnitEmissive, *m_defaultWhite);
        BindMaterialTexture(mat.OcclusionMap, kUnitOcclusion, *m_defaultWhite);

        GetOrCreateMesh(instance.Mesh).Draw();
        ++m_gpuDrawCallsThisFrame;
    }

    // --- Skybox (only fills pixels the geometry left at depth 1.0) ---
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
    glBindFramebuffer(GL_FRAMEBUFFER, fxaaActive ? m_postFbo : 0);
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
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    BeginGpuProfilerPass(DebugUiPass);
    {
        gl_debug::ScopedGroup marker("Debug UI");
        RenderDebugOverlay(frame);
    }
    EndGpuProfilerPass();
    EndGpuProfilerFrame(scene);
    glEnable(GL_DEPTH_TEST);

    if (!m_screenshotPath.empty())
        SaveScreenshot();
    m_hasFrameDebugFrame = true;
}

} // namespace engine
