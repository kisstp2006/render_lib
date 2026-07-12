#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/core/Camera.h"
#include "engine/core/Log.h"
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

// Same curve as post.frag's TonemapColor, used to precompute the white
// point scale on the CPU exactly like VRF does (g_flWhitePointScale).
float TonemapScalar(float x, const PostProcessSettings& pp)
{
    const float num = x * (pp.ShoulderStrength * x + pp.LinearStrength * pp.LinearAngle) + pp.ToeNumerator * pp.ToeStrength;
    const float den = x * (pp.ShoulderStrength * x + pp.LinearStrength) + pp.ToeDenominator * pp.ToeStrength;
    return num / den - pp.ToeNumerator / pp.ToeDenominator;
}

} // namespace

void GLRenderBackend::RenderFrame(const Scene& scene, const Camera& camera)
{
    m_environment->EnsureBaked(scene.Sun, scene.Sky, scene.Environment);

    const float aspect = m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
    const glm::vec3 lightDir = glm::normalize(scene.Sun.Direction);
    const float sunElevation = glm::degrees(std::asin(glm::clamp(-lightDir.y, -1.0f, 1.0f)));
    const bool proceduralDayNight = scene.Environment.Source == EnvironmentSource::ProceduralSky
                                 && scene.Sky.EnableDayNightCycle;
    const DayNightState dayNight = proceduralDayNight ? EvaluateDayNight(sunElevation) : DayNightState{};
    const glm::vec3 effectiveSunColor = scene.Sun.Color * dayNight.SunTint
                                      * (scene.Sun.Intensity * dayNight.DirectSunAmount);
    const bool sunShadowsActive = scene.Sun.CastsShadows && dayNight.DirectSunAmount > 0.001f
                               && scene.Sun.Intensity > 0.0f;
    CascadeShadowConfig cascadeConfig;
    cascadeConfig.MaxDistance = scene.Shadows.MaxDistance;
    cascadeConfig.SplitLambda = scene.Shadows.CascadeSplitLambda;
    cascadeConfig.BlendFraction = scene.Shadows.CascadeBlendFraction;
    cascadeConfig.Resolutions = m_shadowSizes;
    const CascadeShadowData cascades = BuildCascadeShadows(camera, aspect, lightDir, cascadeConfig);

    // --- Four-cascade sun shadow pass ---
    if (sunShadowsActive)
    {
        const int readQuery = (m_shadowQueryIndex + 1) % 2;
        if (m_shadowQueryFrames > 0)
        {
            int available = 0;
            glGetQueryObjectiv(m_shadowTimeQueries[readQuery], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available)
            {
                unsigned long long nanoseconds = 0;
                glGetQueryObjectui64v(m_shadowTimeQueries[readQuery], GL_QUERY_RESULT, &nanoseconds);
                m_lastShadowGpuMs = static_cast<float>(nanoseconds) / 1'000'000.0f;
                if (m_shadowQueryFrames > 10)
                {
                    m_shadowGpuTotalMs += m_lastShadowGpuMs;
                    m_shadowGpuMinMs = std::min(m_shadowGpuMinMs, m_lastShadowGpuMs);
                    m_shadowGpuMaxMs = std::max(m_shadowGpuMaxMs, m_lastShadowGpuMs);
                    ++m_shadowGpuSamples;
                }
            }
        }
        glBeginQuery(GL_TIME_ELAPSED, m_shadowTimeQueries[m_shadowQueryIndex]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
        m_shadowShader->Use();
        glCullFace(GL_FRONT);
        for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
        {
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
            }
        }
        glCullFace(GL_BACK);
        glEndQuery(GL_TIME_ELAPSED);
        m_shadowQueryIndex = readQuery;
        ++m_shadowQueryFrames;
        if (scene.Shadows.LogPerformance && m_shadowQueryFrames % 120 == 0 && m_shadowGpuSamples > 0)
        {
            const float average = m_shadowGpuTotalMs / static_cast<float>(m_shadowGpuSamples);
            log::Info("CSM GPU: avg " + std::to_string(average) + " ms, min " + std::to_string(m_shadowGpuMinMs)
                      + " ms, max " + std::to_string(m_shadowGpuMaxMs) + " ms (" + std::to_string(m_shadowGpuSamples) + " samples)");
            m_shadowGpuTotalMs = 0.0f;
            m_shadowGpuMinMs = 1.0e9f;
            m_shadowGpuMaxMs = 0.0f;
            m_shadowGpuSamples = 0;
        }
    }

    RenderLocalLightShadows(scene);

    // --- Main HDR pass (MSAA) ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 view = camera.GetView();
    const glm::mat4 proj = camera.GetProjection(aspect);

    m_pbrShader->Use();
    m_pbrShader->SetMat4("uView", view);
    m_pbrShader->SetMat4("uProj", proj);
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
    }

    // --- Skybox (only fills pixels the geometry left at depth 1.0) ---
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    m_skyShader->Use();
    m_skyShader->SetMat4("uInvProj", glm::inverse(proj));
    m_skyShader->SetMat4("uInvView", glm::inverse(view));
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
    m_skyShader->SetBool("uEnableDayNightCycle", proceduralDayNight);
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
    m_skyShader->SetFloat("uTime", static_cast<float>(glfwGetTime()));
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // --- Resolve MSAA -> HDR texture ---
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // --- Auto-exposure (Source 2 tonemap controller style) ---
    // Average scene luminance from the top mip of the HDR resolve texture,
    // adapted over time toward Key/avgLum within [Min, Max] multipliers.
    // This is still a simple average rather than a percentile histogram, but
    // the double-buffered PBO readback keeps it off the current frame's CPU
    // critical path.
    const PostProcessSettings& pp = scene.PostProcess;
    const double now = glfwGetTime();
    const float deltaTime = m_lastFrameTime > 0.0 ? static_cast<float>(now - m_lastFrameTime) : 0.016f;
    m_lastFrameTime = now;

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
                const float avgLum = std::max(0.2126f * avg[0] + 0.7152f * avg[1] + 0.0722f * avg[2], 1e-4f);
                const float target = std::clamp(pp.AutoExposureKey / avgLum, pp.AutoExposureMin, pp.AutoExposureMax);
                const float blend = 1.0f - std::exp(-deltaTime * pp.AutoExposureSpeed);
                m_autoExposure += (target - m_autoExposure) * blend;
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

    const float effectiveExposure = pp.Exposure * m_autoExposure;

    // --- Bloom ---
    if (pp.Enabled && !m_bloomChain.empty())
        RenderBloom(pp.BloomThreshold, effectiveExposure);

    // --- Post pass to backbuffer ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);

    m_postShader->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    m_postShader->SetInt("uSceneColor", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (pp.Enabled && !m_bloomChain.empty()) ? m_bloomChain[0].Texture : m_defaultWhite->Id());
    m_postShader->SetInt("uBloom", 1);

    m_postShader->SetBool("uPostEnabled", pp.Enabled);
    m_postShader->SetFloat("uExposure", effectiveExposure);
    m_postShader->SetFloat("uSaturation", pp.Saturation);
    m_postShader->SetFloat("uContrast", pp.Contrast);
    m_postShader->SetVec3("uColorTint", pp.ColorTint);
    m_postShader->SetFloat("uBloomStrength", (pp.Enabled && !m_bloomChain.empty()) ? pp.BloomStrength : 0.0f);
    m_postShader->SetFloat("uShoulderStrength", pp.ShoulderStrength);
    m_postShader->SetFloat("uLinearStrength", pp.LinearStrength);
    m_postShader->SetFloat("uLinearAngle", pp.LinearAngle);
    m_postShader->SetFloat("uToeStrength", pp.ToeStrength);
    m_postShader->SetFloat("uToeNumerator", pp.ToeNumerator);
    m_postShader->SetFloat("uToeDenominator", pp.ToeDenominator);
    m_postShader->SetFloat("uWhitePointScale", 1.0f / TonemapScalar(pp.WhitePoint, pp));

    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    if (!m_screenshotPath.empty())
        SaveScreenshot();
}

} // namespace engine
