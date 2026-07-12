#pragma once

#include <glm/glm.hpp>

namespace engine {

struct SkySettings
{
    glm::vec3 ZenithColor{0.18f, 0.32f, 0.66f};
    glm::vec3 HorizonColor{0.72f, 0.80f, 0.94f};
    glm::vec3 GroundColor{0.23f, 0.21f, 0.19f};
    float SunAngularRadiusDeg = 1.2f;
    float SunIntensity = 80.0f;
    float SkyIntensity = 1.0f;
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

} // namespace engine
