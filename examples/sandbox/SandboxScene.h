#pragma once

#include <string>

namespace engine {
class Application;
}

struct SandboxSceneConfig
{
    std::string GltfPath;
    bool SampleGltf = false;
    bool FlashlightOn = false;
    bool ShadowDebug = false;
    bool ShadowStress = false;
    bool ShadowBenchmark = false;
};

void PopulateSandboxScene(engine::Application& app, const SandboxSceneConfig& config);
