#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "engine/asset/ColorGrading.h"

namespace engine {

enum class AntiAliasingMode
{
    None,
    Fxaa,
    Taa
};

struct SkySettings
{
    glm::vec3 ZenithColor{0.18f, 0.32f, 0.66f};
    glm::vec3 HorizonColor{0.72f, 0.80f, 0.94f};
    glm::vec3 GroundColor{0.23f, 0.21f, 0.19f};
    glm::vec3 NightZenithColor{0.003f, 0.008f, 0.028f};
    glm::vec3 NightHorizonColor{0.018f, 0.028f, 0.070f};
    glm::vec3 MilkyWayColor{0.22f, 0.30f, 0.65f};
    glm::vec3 StarWarmColor{1.0f, 0.68f, 0.46f};
    glm::vec3 StarCoolColor{0.62f, 0.78f, 1.0f};
    float SunAngularRadiusDeg = 1.2f;
    float SunIntensity = 80.0f;
    float SkyIntensity = 1.0f;
    float NightSkyIntensity = 1.0f;
    float NightHorizonGlow = 1.0f;
    float StarIntensity = 4.0f;
    float StarDensity = 0.006f;
    float StarSize = 1.0f;
    float StarTwinkle = 0.18f;
    float StarTwinkleSpeed = 1.0f;
    float MilkyWayIntensity = 0.32f;
    float NightSkyRotationDegrees = 0.0f;
    float NightSkyRotationSpeed = 0.35f;
    bool StarsEnabled = true;
    bool MilkyWayEnabled = true;
    bool AnimateNightSky = true;
    bool EnableDayNightCycle = true;
};

struct PostProcessSettings
{
    bool Enabled = true;
    float Exposure = 2.2f;
    float BloomStrength = 0.12f;
    float BloomThreshold = 1.0f;
    float ShoulderStrength = 0.15f;
    float LinearStrength = 0.50f;
    float LinearAngle = 0.10f;
    float ToeStrength = 0.20f;
    float ToeNumerator = 0.02f;
    float ToeDenominator = 0.30f;
    float WhitePoint = 8.0f;

    bool AutoExposure = false;
    float AutoExposureKey = 0.18f;
    float AutoExposureMin = 0.4f;
    float AutoExposureMax = 3.0f;
    float AutoExposureSpeed = 1.8f;

    float Saturation = 1.0f;
    float Contrast = 1.0f;
    glm::vec3 ColorTint{1.0f, 1.0f, 1.0f};

    std::shared_ptr<ColorGradingLutData> ColorLut;
    float ColorLutWeight = 0.0f;

    AntiAliasingMode AntiAliasing = AntiAliasingMode::Taa;
    float FxaaSubpixel = 0.75f;
    float FxaaEdgeThreshold = 0.125f;
    float FxaaEdgeThresholdMin = 0.0312f;
    float TaaHistoryWeight = 0.92f;
    float TaaSharpen = 0.12f;
    float TaaJitterScale = 1.0f;
    float TaaDepthThreshold = 0.0025f;
    bool LogPerformance = false;
};

struct FogSettings
{
    bool Enabled = false;
    glm::vec3 Color{0.35f, 0.40f, 0.50f};
    float Opacity = 0.85f;
    float Start = 20.0f;
    float End = 120.0f;
    float DistanceExponent = 1.6f;
    float HeightFadeTop = 25.0f;
    float HeightFadeBottom = 0.0f;
    float HeightExponent = 1.0f;
};

struct ShadowSettings
{
    float MaxDistance = 150.0f;
    float CascadeSplitLambda = 0.65f;
    float CascadeBlendFraction = 0.10f;
    bool DebugCascades = false;
    bool LogPerformance = false;
};

struct VisibilitySettings
{
    bool Enabled = true;
    bool FrustumCulling = true;
    bool DistanceCulling = true;
    // Zero uses the active camera far plane. Individual instances may choose
    // a shorter distance through MeshInstance::MaxDrawDistance.
    float MaxDistance = 0.0f;
    bool DebugBounds = false;
    bool DebugCulledBounds = true;
};

} // namespace engine
