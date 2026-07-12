#pragma once

#include <string>

namespace engine {
class Application;
}

struct SandboxSceneConfig
{
    std::string GltfPath;
    std::string HdriPath;
    std::string ColorLutPath;
    std::string AntiAliasing = "taa";
    std::string NightPreset = "natural";
    bool SampleGltf = false;
    bool FlashlightOn = false;
    bool ShadowDebug = false;
    bool ShadowStress = false;
    bool ShadowBenchmark = false;
    bool LocalLightShowcase = false;
    bool HdriStudio = false;
    bool DayNightShowcase = false;
    bool PostShowcase = false;
    bool CinematicLut = false;
    bool PostBenchmark = false;
    float ColorLutWeight = 1.0f;
    float FxaaSubpixel = 0.75f;
    float FxaaEdgeThreshold = 0.125f;
    float FxaaEdgeThresholdMin = 0.0312f;
    float TaaHistoryWeight = 0.92f;
    float TaaSharpen = 0.12f;
    float TaaJitterScale = 1.0f;
    float TaaDepthThreshold = 0.0025f;
    float SunAzimuthDegrees = 255.0f;
    float SunElevationDegrees = 15.0f;
    float DayNightCycleSeconds = 24.0f;
    float StarDensity = 0.006f;
    float StarIntensity = 4.0f;
    float StarSize = 1.0f;
    float StarTwinkle = 0.18f;
    float MilkyWayIntensity = 0.32f;
    float NightSkyIntensity = 1.0f;
    float NightHorizonGlow = 1.0f;
    float StarTwinkleSpeed = 1.0f;
    float NightSkyRotationSpeed = 0.35f;
    bool StarsEnabled = true;
    bool MilkyWayEnabled = true;
    bool AnimateNightSky = true;
};

void PopulateSandboxScene(engine::Application& app, const SandboxSceneConfig& config);
