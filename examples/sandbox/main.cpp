// Source-like PBR renderer sandbox and glTF test application.

#include "SandboxControls.h"
#include "SandboxScene.h"

#include "engine/core/Application.h"
#include "engine/scene/GltfLoader.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace engine;

int main(int argc, char** argv)
{
    WindowDesc window;
    window.title = "Source-Like PBR Sandbox";
    window.width = 1600;
    window.height = 900;

    SandboxSceneConfig sceneConfig;
    std::string screenshotPath;
    int screenshotFrame = 10;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--vulkan") window.api = GraphicsApi::Vulkan;
        else if (argument == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
        else if (argument == "--frames" && i + 1 < argc) screenshotFrame = std::atoi(argv[++i]);
        else if (argument == "--flashlight") sceneConfig.FlashlightOn = true;
        else if (argument == "--shadow-debug") sceneConfig.ShadowDebug = true;
        else if (argument == "--shadow-stress") sceneConfig.ShadowStress = true;
        else if (argument == "--shadow-benchmark") sceneConfig.ShadowBenchmark = true;
        else if (argument == "--light-showcase") sceneConfig.LocalLightShowcase = true;
        else if (argument == "--post-showcase") { sceneConfig.PostShowcase = true; sceneConfig.CinematicLut = true; }
        else if (argument == "--post-benchmark") sceneConfig.PostBenchmark = true;
        else if (argument == "--aa" && i + 1 < argc) sceneConfig.AntiAliasing = argv[++i];
        else if (argument == "--color-lut" && i + 1 < argc) sceneConfig.ColorLutPath = argv[++i];
        else if (argument == "--cinematic-lut") sceneConfig.CinematicLut = true;
        else if (argument == "--lut-weight" && i + 1 < argc) sceneConfig.ColorLutWeight = std::strtof(argv[++i], nullptr);
        else if (argument == "--fxaa-subpixel" && i + 1 < argc) sceneConfig.FxaaSubpixel = std::strtof(argv[++i], nullptr);
        else if (argument == "--fxaa-edge-threshold" && i + 1 < argc) sceneConfig.FxaaEdgeThreshold = std::strtof(argv[++i], nullptr);
        else if (argument == "--fxaa-edge-threshold-min" && i + 1 < argc) sceneConfig.FxaaEdgeThresholdMin = std::strtof(argv[++i], nullptr);
        else if (argument == "--taa-history" && i + 1 < argc) sceneConfig.TaaHistoryWeight = std::strtof(argv[++i], nullptr);
        else if (argument == "--taa-sharpen" && i + 1 < argc) sceneConfig.TaaSharpen = std::strtof(argv[++i], nullptr);
        else if (argument == "--taa-jitter" && i + 1 < argc) sceneConfig.TaaJitterScale = std::strtof(argv[++i], nullptr);
        else if (argument == "--taa-depth-threshold" && i + 1 < argc) sceneConfig.TaaDepthThreshold = std::strtof(argv[++i], nullptr);
        else if (argument == "--hdri" && i + 1 < argc) sceneConfig.HdriPath = argv[++i];
        else if (argument == "--sun-azimuth" && i + 1 < argc) sceneConfig.SunAzimuthDegrees = std::strtof(argv[++i], nullptr);
        else if (argument == "--sun-elevation" && i + 1 < argc) sceneConfig.SunElevationDegrees = std::strtof(argv[++i], nullptr);
        else if (argument == "--day-night-seconds" && i + 1 < argc) sceneConfig.DayNightCycleSeconds = std::strtof(argv[++i], nullptr);
        else if (argument == "--star-density" && i + 1 < argc) sceneConfig.StarDensity = std::strtof(argv[++i], nullptr);
        else if (argument == "--star-intensity" && i + 1 < argc) sceneConfig.StarIntensity = std::strtof(argv[++i], nullptr);
        else if (argument == "--star-size" && i + 1 < argc) sceneConfig.StarSize = std::strtof(argv[++i], nullptr);
        else if (argument == "--star-twinkle" && i + 1 < argc) sceneConfig.StarTwinkle = std::strtof(argv[++i], nullptr);
        else if (argument == "--milky-way" && i + 1 < argc) sceneConfig.MilkyWayIntensity = std::strtof(argv[++i], nullptr);
        else if (argument == "--night-brightness" && i + 1 < argc) sceneConfig.NightSkyIntensity = std::strtof(argv[++i], nullptr);
        else if (argument == "--night-horizon-glow" && i + 1 < argc) sceneConfig.NightHorizonGlow = std::strtof(argv[++i], nullptr);
        else if (argument == "--star-twinkle-speed" && i + 1 < argc) sceneConfig.StarTwinkleSpeed = std::strtof(argv[++i], nullptr);
        else if (argument == "--night-sky-speed" && i + 1 < argc) sceneConfig.NightSkyRotationSpeed = std::strtof(argv[++i], nullptr);
        else if (argument == "--night-preset" && i + 1 < argc) sceneConfig.NightPreset = argv[++i];
        else if (argument == "--no-stars") sceneConfig.StarsEnabled = false;
        else if (argument == "--no-milky-way") sceneConfig.MilkyWayEnabled = false;
        else if (argument == "--static-night-sky") sceneConfig.AnimateNightSky = false;
        else if (argument == "--day-night-showcase")
        {
            sceneConfig.DayNightShowcase = true;
            sceneConfig.SampleGltf = true;
            sceneConfig.GltfPath = std::string(ENGINE_ASSET_DIR) + "/WaterBottle.glb";
        }
        else if (argument == "--hdri-studio")
        {
            sceneConfig.HdriStudio = true;
            sceneConfig.HdriPath = std::string(ENGINE_ASSET_DIR) + "/studio_small_09_1k.hdr";
        }
        else if (argument == "--sample-gltf")
        {
            sceneConfig.SampleGltf = true;
            sceneConfig.GltfPath = std::string(ENGINE_ASSET_DIR) + "/WaterBottle.glb";
        }
        else if (argument == "--gltf" && i + 1 < argc) sceneConfig.GltfPath = argv[++i];
    }

    Application app(window);
    try
    {
        PopulateSandboxScene(app, sceneConfig);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }

    SandboxControls controls(app, sceneConfig.FlashlightOn,
                             !sceneConfig.LocalLightShowcase && !sceneConfig.HdriStudio,
                             sceneConfig.LocalLightShowcase, sceneConfig.DayNightShowcase, sceneConfig.PostShowcase,
                             sceneConfig.DayNightCycleSeconds, sceneConfig.SunAzimuthDegrees,
                             sceneConfig.SunElevationDegrees, screenshotPath, screenshotFrame);
    app.SetUpdateCallback([&controls](float deltaTime) { controls.Update(deltaTime); });
    app.Run();
    return EXIT_SUCCESS;
}
