#include "SandboxScene.h"

#include "engine/core/Application.h"
#include "engine/scene/GltfLoader.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Texture.h"

#include <memory>

#include <glm/gtc/matrix_transform.hpp>

using namespace engine;

namespace {

void AddProceduralMaterialGrid(Scene& scene, const std::shared_ptr<MeshData>& sphereMesh)
{
    constexpr int kRows = 5;
    constexpr int kCols = 7;
    constexpr float kSpacing = 2.4f;
    for (int row = 0; row < kRows; ++row)
    {
        for (int col = 0; col < kCols; ++col)
        {
            Material material;
            material.Albedo = {0.92f, 0.2f, 0.15f};
            material.Metallic = static_cast<float>(row) / static_cast<float>(kRows - 1);
            material.Roughness = glm::mix(0.05f, 1.0f, static_cast<float>(col) / static_cast<float>(kCols - 1));
            const float x = (col - (kCols - 1) * 0.5f) * kSpacing;
            const float y = (kRows - 1 - row) * kSpacing + 0.4f;
            scene.AddInstance(sphereMesh, material, glm::translate(glm::mat4(1.0f), {x, y, 0.0f}));
        }
    }

    Material glow;
    glow.Albedo = {0.1f, 0.1f, 0.1f};
    glow.Roughness = 0.6f;
    glow.Emissive = {4.0f, 1.6f, 0.4f};
    scene.AddInstance(sphereMesh, glow, glm::translate(glm::mat4(1.0f), {-9.5f, 0.2f, 2.5f}));
}

void AddShadowStressScene(Scene& scene)
{
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    for (int z = -7; z <= 7; ++z)
    {
        for (int x = -7; x <= 7; ++x)
        {
            Material material;
            material.Albedo = glm::mix(glm::vec3(0.18f, 0.24f, 0.32f), glm::vec3(0.75f, 0.42f, 0.16f),
                                       static_cast<float>((x + z + 14) % 5) / 4.0f);
            material.Roughness = 0.35f + static_cast<float>((x - z + 14) % 4) * 0.15f;
            const float height = 1.5f + static_cast<float>((x * x + z * z) % 5);
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), {x * 8.0f, height - 1.0f, z * 8.0f});
            transform = glm::scale(transform, {1.0f, height, 1.0f});
            scene.AddInstance(cube, material, transform);
        }
    }
}

} // namespace

void PopulateSandboxScene(Application& app, const SandboxSceneConfig& config)
{
    Scene& scene = app.GetScene();
    scene.Sun.Direction = glm::normalize(glm::vec3(-0.35f, -0.65f, -0.25f));
    scene.Sun.Color = {1.0f, 0.95f, 0.85f};
    scene.Sun.Intensity = 7.0f;
    scene.Sky.SkyIntensity = 0.45f;
    scene.PostProcess.Exposure = 1.7f;
    scene.PostProcess.AutoExposure = true;
    scene.Shadows.DebugCascades = config.ShadowDebug;
    scene.Shadows.LogPerformance = config.ShadowBenchmark;

    auto sphereMesh = std::make_shared<MeshData>(primitives::MakeSphere(0.9f, 48, 48));
    auto planeMesh = std::make_shared<MeshData>(primitives::MakePlane(config.ShadowStress ? 180.0f : 40.0f, 1));
    Material ground;
    ground.Albedo = {1.0f, 1.0f, 1.0f};
    ground.Roughness = 0.8f;
    ground.AlbedoMap = textures::MakeChecker(256, 2, {0.30f, 0.30f, 0.32f}, {0.38f, 0.38f, 0.40f});
    scene.AddInstance(planeMesh, ground, glm::translate(glm::mat4(1.0f), {0.0f, -1.0f, 0.0f}));

    if (config.ShadowStress)
    {
        AddShadowStressScene(scene);
    }
    else if (config.GltfPath.empty())
    {
        AddProceduralMaterialGrid(scene, sphereMesh);
    }
    else
    {
        glm::mat4 importTransform = glm::translate(glm::mat4(1.0f), {0.0f, 0.15f, 0.0f});
        if (config.SampleGltf)
            importTransform = glm::scale(importTransform, glm::vec3(6.0f));
        LoadGltfScene(config.GltfPath, scene, importTransform);
    }

    scene.AddPointLight({{-6.0f, 5.0f, 6.0f}, {0.4f, 0.55f, 1.0f}, 40.0f, 20.0f});
    scene.AddPointLight({{8.0f, 3.0f, -4.0f}, {1.0f, 0.6f, 0.3f}, 30.0f, 18.0f});

    scene.Fog.Enabled = true;
    scene.Fog.Color = {0.22f, 0.25f, 0.32f};
    scene.Fog.Opacity = 0.8f;
    scene.Fog.Start = 30.0f;
    scene.Fog.End = 140.0f;
    scene.Fog.HeightFadeTop = 30.0f;
    scene.Fog.HeightFadeBottom = -1.0f;

    SpotLight flashlight;
    flashlight.Color = {1.0f, 0.97f, 0.9f};
    flashlight.Intensity = 350.0f;
    flashlight.Range = 45.0f;
    flashlight.InnerConeDeg = 13.0f;
    flashlight.OuterConeDeg = 22.0f;
    flashlight.CastsShadows = true;
    flashlight.Enabled = config.FlashlightOn;
    scene.AddSpotLight(flashlight);

    Camera& camera = app.GetCamera();
    if (config.ShadowStress)
        camera.Position = {0.0f, 12.0f, 35.0f};
    else if (config.GltfPath.empty())
        camera.Position = {0.0f, 5.0f, 16.0f};
    else
        camera.Position = config.SampleGltf ? glm::vec3(0.0f, 0.5f, 2.2f) : glm::vec3(0.0f, 0.5f, 4.0f);
    camera.Yaw = -90.0f;
    camera.Pitch = config.ShadowStress ? -15.0f : (config.GltfPath.empty() ? -8.0f : -5.0f);
}
