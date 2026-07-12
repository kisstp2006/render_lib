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
    catch (const GltfLoadError& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }

    SandboxControls controls(app, sceneConfig.FlashlightOn, screenshotPath, screenshotFrame);
    app.SetUpdateCallback([&controls](float deltaTime) { controls.Update(deltaTime); });
    app.Run();
    return EXIT_SUCCESS;
}
