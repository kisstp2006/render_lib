#include "engine/render/SceneRenderer.h"

#include "engine/core/Camera.h"
#include "engine/scene/Scene.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

namespace {

float TonemapScalar(float value, const PostProcessSettings& post)
{
    const float numerator = value * (post.ShoulderStrength * value
                          + post.LinearStrength * post.LinearAngle)
                          + post.ToeNumerator * post.ToeStrength;
    const float denominator = value * (post.ShoulderStrength * value + post.LinearStrength)
                            + post.ToeDenominator * post.ToeStrength;
    return numerator / denominator - post.ToeNumerator / post.ToeDenominator;
}

glm::vec3 SafeUp(const glm::vec3& direction, const glm::vec3& requested)
{
    const glm::vec3 normalizedDirection = glm::normalize(direction);
    glm::vec3 up = requested;
    if (glm::length(up) < 0.001f
        || std::abs(glm::dot(glm::normalize(up), normalizedDirection)) > 0.98f)
    {
        up = std::abs(normalizedDirection.y) > 0.98f
            ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return glm::normalize(up - normalizedDirection * glm::dot(up, normalizedDirection));
}

glm::mat4 ProjectedLightMatrix(const glm::vec3& position, const glm::vec3& direction,
                               float outerAngle, float range, float aspect = 1.0f)
{
    const glm::vec3 normalizedDirection = glm::normalize(direction);
    const glm::vec3 up = SafeUp(normalizedDirection, {0.0f, 1.0f, 0.0f});
    return glm::perspective(glm::radians(glm::clamp(outerAngle * 2.0f, 1.0f, 175.0f)),
                            aspect, 0.1f, range)
         * glm::lookAt(position, position + normalizedDirection, up);
}

} // namespace

const RenderFrameData& SceneRenderer::PrepareFrame(const Scene& scene, const Camera& camera,
                                                    int width, int height,
                                                    const debug::DebugOverlayImage* debugOverlay,
                                                    float timeSeconds,
                                                    float deltaSeconds)
{
    const uint64_t nextFrameIndex = m_frame.FrameIndex + 1;
    m_frame = {};
    m_frame.SceneData = &scene;
    m_frame.CameraData = &camera;
    m_frame.Width = std::max(width, 1);
    m_frame.Height = std::max(height, 1);
    m_frame.AspectRatio = static_cast<float>(m_frame.Width) / static_cast<float>(m_frame.Height);
    m_frame.FrameIndex = nextFrameIndex;
    m_frame.TimeSeconds = timeSeconds;
    m_frame.DeltaSeconds = std::max(deltaSeconds, 0.0f);
    m_frame.DebugOverlay = debugOverlay;
    m_frame.View = camera.GetView();
    m_frame.BaseProjection = camera.GetProjection(m_frame.AspectRatio);

    m_frame.SunDirection = glm::normalize(scene.Sun.Direction);
    const float elevation = glm::degrees(std::asin(glm::clamp(-m_frame.SunDirection.y, -1.0f, 1.0f)));
    m_frame.ProceduralDayNight = scene.Environment.Source == EnvironmentSource::ProceduralSky
                              && scene.Sky.EnableDayNightCycle;
    m_frame.DayNight = m_frame.ProceduralDayNight ? EvaluateDayNight(elevation) : DayNightState{};
    m_frame.EffectiveSunColor = scene.Sun.Color * m_frame.DayNight.SunTint
                              * (scene.Sun.Intensity * m_frame.DayNight.DirectSunAmount);
    m_frame.SunShadowsActive = scene.Sun.CastsShadows && scene.Sun.Intensity > 0.0f
                            && m_frame.DayNight.DirectSunAmount > 0.001f;

    CascadeShadowConfig cascadeConfig;
    cascadeConfig.MaxDistance = scene.Shadows.MaxDistance;
    cascadeConfig.SplitLambda = scene.Shadows.CascadeSplitLambda;
    cascadeConfig.BlendFraction = scene.Shadows.CascadeBlendFraction;
    m_frame.Cascades = BuildCascadeShadows(
        camera, m_frame.AspectRatio, m_frame.SunDirection, cascadeConfig);

    for (const PointLight& light : scene.PointLights())
    {
        if (!light.Enabled || m_frame.LocalLights.PointCount >= PreparedLocalLights::MaxPointLights)
            continue;
        m_frame.LocalLights.Points[m_frame.LocalLights.PointCount++].Source = &light;
    }
    for (const SpotLight& light : scene.SpotLights())
    {
        if (!light.Enabled || m_frame.LocalLights.SpotCount >= PreparedLocalLights::MaxSpotLights)
            continue;
        PreparedSpotLight& prepared = m_frame.LocalLights.Spots[m_frame.LocalLights.SpotCount++];
        prepared.Source = &light;
        prepared.Direction = glm::normalize(light.Direction);
        prepared.Projection = ProjectedLightMatrix(
            light.Position, prepared.Direction, light.OuterConeDeg, light.Range);
    }
    for (const AreaLight& light : scene.AreaLights())
    {
        if (!light.Enabled || m_frame.LocalLights.AreaCount >= PreparedLocalLights::MaxAreaLights)
            continue;
        PreparedAreaLight& prepared = m_frame.LocalLights.Areas[m_frame.LocalLights.AreaCount++];
        prepared.Source = &light;
        prepared.Direction = glm::normalize(light.Direction);
        prepared.Up = SafeUp(prepared.Direction, light.Up);
        prepared.Right = glm::normalize(glm::cross(prepared.Direction, prepared.Up));
        const float aspect = std::max(light.Size.x / std::max(light.Size.y, 0.01f), 0.01f);
        prepared.Projection = ProjectedLightMatrix(
            light.Position, prepared.Direction, light.BarnAngleDeg, light.Range, aspect);
    }

    const float whitePointValue = TonemapScalar(scene.PostProcess.WhitePoint, scene.PostProcess);
    m_frame.TonemapWhitePointScale = 1.0f / std::max(whitePointValue, 1.0e-6f);
    return m_frame;
}

} // namespace engine
