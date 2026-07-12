#include "SandboxScene.h"

#include "engine/core/Application.h"
#include "engine/scene/ColorGrading.h"
#include "engine/scene/GltfLoader.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"
#include "engine/scene/Texture.h"

#include <cmath>
#include <memory>
#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

using namespace engine;

namespace {

void ApplyPostConfig(Scene& scene, const SandboxSceneConfig& config)
{
    if (config.AntiAliasing == "none")
        scene.PostProcess.AntiAliasing = AntiAliasingMode::None;
    else if (config.AntiAliasing == "fxaa")
        scene.PostProcess.AntiAliasing = AntiAliasingMode::Fxaa;
    else if (config.AntiAliasing == "taa")
        scene.PostProcess.AntiAliasing = AntiAliasingMode::Taa;
    else
        throw std::runtime_error("Unknown anti-aliasing mode '" + config.AntiAliasing
                                 + "' (expected none, fxaa or taa)");

    if (!config.ColorLutPath.empty())
        scene.PostProcess.ColorLut = color_grading::LoadCube(config.ColorLutPath);
    else if (config.CinematicLut || config.PostShowcase)
        scene.PostProcess.ColorLut = color_grading::MakeCinematic();
    scene.PostProcess.ColorLutWeight = scene.PostProcess.ColorLut
        ? glm::clamp(config.ColorLutWeight, 0.0f, 1.0f) : 0.0f;
    scene.PostProcess.LogPerformance = config.PostBenchmark;
    scene.PostProcess.FxaaSubpixel = glm::clamp(config.FxaaSubpixel, 0.0f, 1.0f);
    scene.PostProcess.FxaaEdgeThreshold = glm::clamp(config.FxaaEdgeThreshold, 0.0312f, 0.333f);
    scene.PostProcess.FxaaEdgeThresholdMin = glm::clamp(config.FxaaEdgeThresholdMin, 0.0f, 0.0833f);
    scene.PostProcess.TaaHistoryWeight = glm::clamp(config.TaaHistoryWeight, 0.0f, 0.98f);
    scene.PostProcess.TaaSharpen = glm::clamp(config.TaaSharpen, 0.0f, 1.0f);
    scene.PostProcess.TaaJitterScale = glm::clamp(config.TaaJitterScale, 0.0f, 2.0f);
    scene.PostProcess.TaaDepthThreshold = glm::clamp(config.TaaDepthThreshold, 0.00001f, 0.1f);
}

void ApplyNightPreset(SkySettings& sky, const std::string& preset)
{
    if (preset == "cool")
    {
        sky.StarWarmColor = {0.72f, 0.82f, 1.0f};
        sky.StarCoolColor = {0.38f, 0.62f, 1.0f};
        sky.MilkyWayColor = {0.16f, 0.30f, 0.95f};
    }
    else if (preset == "warm")
    {
        sky.StarWarmColor = {1.0f, 0.48f, 0.20f};
        sky.StarCoolColor = {1.0f, 0.86f, 0.62f};
        sky.MilkyWayColor = {0.72f, 0.24f, 0.18f};
    }
    else if (preset == "fantasy")
    {
        sky.StarWarmColor = {1.0f, 0.42f, 0.86f};
        sky.StarCoolColor = {0.38f, 0.88f, 1.0f};
        sky.MilkyWayColor = {0.64f, 0.18f, 1.0f};
    }
}

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

void AddShowcaseObjects(Scene& scene, const std::shared_ptr<MeshData>& cube, float centerZ)
{
    Material neutral;
    neutral.Albedo = {0.42f, 0.45f, 0.50f};
    neutral.Roughness = 0.55f;
    for (float centerX : {-7.0f, 7.0f})
    {
        for (int i = -1; i <= 1; ++i)
        {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), {centerX + i * 2.3f, 0.5f + (i + 1) * 0.45f, centerZ});
            transform = glm::scale(transform, {0.7f, 1.5f + (i + 1) * 0.45f, 0.7f});
            scene.AddInstance(cube, neutral, transform);
        }
    }
}

void PopulateLocalLightShowcase(Application& app, const SandboxSceneConfig& config)
{
    Scene& scene = app.GetScene();
    scene.Sun.Intensity = 0.0f;
    scene.Sun.CastsShadows = false;
    scene.Sky.SkyIntensity = 0.10f;
    scene.Fog.Enabled = false;
    scene.PostProcess.Exposure = 1.45f;
    scene.PostProcess.AutoExposure = false;
    scene.Shadows.LogPerformance = config.ShadowBenchmark;

    const auto plane = std::make_shared<MeshData>(primitives::MakePlane(55.0f, 1));
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    const auto sphere = std::make_shared<MeshData>(primitives::MakeSphere(0.28f, 20, 20));
    const auto cookie = textures::MakeLightCookie();

    Material floor;
    floor.Albedo = {0.32f, 0.34f, 0.37f};
    floor.Roughness = 0.72f;
    floor.AlbedoMap = textures::MakeChecker(256, 16, {0.26f, 0.27f, 0.30f}, {0.39f, 0.40f, 0.43f});
    scene.AddInstance(plane, floor, glm::mat4(1.0f));

    AddShowcaseObjects(scene, cube, 9.0f);
    AddShowcaseObjects(scene, cube, 0.0f);
    AddShowcaseObjects(scene, cube, -9.0f);

    const auto addEmitter = [&](glm::vec3 position, glm::vec3 color, glm::vec3 scale) {
        Material emitter;
        emitter.Albedo = color;
        emitter.Emissive = color * 8.0f;
        emitter.Roughness = 0.35f;
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = glm::scale(transform, scale);
        scene.AddInstance(scale.x == scale.y && scale.y == scale.z ? sphere : cube, emitter, transform);
        scene.Instances().back().CastsShadows = false;
    };

    for (int side : {-1, 1})
    {
        PointLight point;
        point.Position = {side * 7.0f, 4.2f, 9.0f};
        point.Color = {1.0f, 0.28f, 0.12f};
        point.Intensity = 650.0f;
        point.Radius = 11.0f;
        point.CastsShadows = side < 0;
        point.Cookie = cookie;
        scene.AddPointLight(point);
        addEmitter(point.Position, point.Color, {1, 1, 1});

        SpotLight spot;
        spot.Position = {side * 7.0f, 6.0f, 2.0f};
        spot.Direction = glm::normalize(glm::vec3(0, -1, -0.30f));
        spot.Color = {0.15f, 0.42f, 1.0f};
        spot.Intensity = 1100.0f;
        spot.Range = 16.0f;
        spot.InnerConeDeg = 18.0f;
        spot.OuterConeDeg = 32.0f;
        spot.CastsShadows = side < 0;
        spot.Cookie = cookie;
        scene.AddSpotLight(spot);
        addEmitter(spot.Position, spot.Color, {0.7f, 0.35f, 0.7f});

        AreaLight area;
        area.Position = {side * 7.0f, 6.0f, -9.0f};
        area.Direction = {0, -1, 0};
        area.Up = {0, 0, -1};
        area.Color = {0.22f, 1.0f, 0.38f};
        area.Intensity = 950.0f;
        area.Range = 15.0f;
        area.Size = {4.0f, 1.5f};
        area.Softness = {0.20f, 0.28f};
        area.BarnAngleDeg = 42.0f;
        area.CastsShadows = side < 0;
        area.Cookie = cookie;
        scene.AddAreaLight(area);
        addEmitter(area.Position, area.Color, {2.0f, 0.15f, 0.75f});
    }

    Camera& camera = app.GetCamera();
    camera.Position = {0.0f, 11.0f, 28.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = -18.0f;
}

void PopulatePostShowcase(Application& app, const SandboxSceneConfig&)
{
    Scene& scene = app.GetScene();
    scene.Sun.Direction = glm::normalize(glm::vec3(-0.45f, -0.85f, -0.30f));
    scene.Sun.Intensity = 6.0f;
    scene.Sky.SkyIntensity = 0.38f;
    scene.Fog.Enabled = false;
    scene.PostProcess.Exposure = 1.55f;
    scene.PostProcess.AutoExposure = false;
    scene.PostProcess.BloomStrength = 0.08f;

    const auto plane = std::make_shared<MeshData>(primitives::MakePlane(35.0f, 1));
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    const auto sphere = std::make_shared<MeshData>(primitives::MakeSphere(0.8f, 48, 48));

    Material floor;
    floor.Albedo = {0.30f, 0.32f, 0.36f};
    floor.Roughness = 0.72f;
    floor.AlbedoMap = textures::MakeChecker(512, 32, {0.08f, 0.09f, 0.11f}, {0.72f, 0.74f, 0.78f});
    scene.AddInstance(plane, floor, glm::mat4(1.0f));

    Material dark;
    dark.Albedo = {0.025f, 0.035f, 0.055f};
    dark.Metallic = 0.65f;
    dark.Roughness = 0.24f;
    for (int i = -6; i <= 6; ++i)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), {i * 0.72f, 1.65f, -1.5f});
        transform = glm::rotate(transform, glm::radians(i * 4.0f), glm::vec3(0, 1, 0));
        transform = glm::scale(transform, {0.075f, 3.3f, 0.55f});
        scene.AddInstance(cube, dark, transform);
    }

    Material chrome;
    chrome.Albedo = {0.82f, 0.20f, 0.06f};
    chrome.Metallic = 0.82f;
    chrome.Roughness = 0.16f;
    scene.AddInstance(sphere, chrome, glm::translate(glm::mat4(1.0f), {-3.4f, 1.0f, 1.2f}));

    // Kept as the last instance so SandboxControls can animate it. Its stable
    // TemporalId verifies object-motion vectors independently of camera motion.
    Material moving;
    moving.Albedo = {0.08f, 0.35f, 0.95f};
    moving.Metallic = 0.35f;
    moving.Roughness = 0.22f;
    moving.Emissive = {0.02f, 0.12f, 0.65f};
    glm::mat4 movingTransform = glm::translate(glm::mat4(1.0f), {2.2f, 1.1f, 1.1f});
    movingTransform = glm::scale(movingTransform, {0.7f, 1.1f, 0.7f});
    scene.AddInstance(cube, moving, movingTransform);

    PointLight warm;
    warm.Position = {-4.5f, 5.5f, 4.0f};
    warm.Color = {1.0f, 0.35f, 0.12f};
    warm.Intensity = 460.0f;
    warm.Radius = 14.0f;
    scene.AddPointLight(warm);

    PointLight cool;
    cool.Position = {4.5f, 3.5f, 2.0f};
    cool.Color = {0.12f, 0.35f, 1.0f};
    cool.Intensity = 330.0f;
    cool.Radius = 12.0f;
    scene.AddPointLight(cool);

    Camera& camera = app.GetCamera();
    camera.Position = {0.0f, 3.2f, 10.5f};
    camera.Yaw = -90.0f;
    camera.Pitch = -10.0f;
}

void PopulateHdriStudio(Application& app, const SandboxSceneConfig& config)
{
    Scene& scene = app.GetScene();
    scene.Environment.Hdri = environments::LoadHdrFromFile(config.HdriPath);
    scene.Environment.Source = EnvironmentSource::EquirectangularHdr;
    scene.Environment.ExposureEV = -0.5f;
    scene.Environment.BackgroundExposureEV = -0.75f;
    scene.Environment.RotationDegrees = 15.0f;
    scene.Sun.Intensity = 0.0f;
    scene.Sun.CastsShadows = false;
    scene.Fog.Enabled = false;
    scene.PostProcess.AutoExposure = false;
    scene.PostProcess.Exposure = 1.25f;
    scene.PostProcess.BloomStrength = 0.08f;

    const auto plane = std::make_shared<MeshData>(primitives::MakePlane(30.0f, 1));
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    const auto sphere = std::make_shared<MeshData>(primitives::MakeSphere(1.0f, 64, 64));

    Material floor;
    floor.Albedo = {0.18f, 0.19f, 0.21f};
    floor.Roughness = 0.42f;
    scene.AddInstance(plane, floor, glm::translate(glm::mat4(1.0f), {0.0f, -1.0f, 0.0f}));

    Material pedestal;
    pedestal.Albedo = {0.24f, 0.25f, 0.27f};
    pedestal.Metallic = 0.05f;
    pedestal.Roughness = 0.28f;
    glm::mat4 pedestalTransform = glm::translate(glm::mat4(1.0f), {0.0f, -0.65f, 0.0f});
    pedestalTransform = glm::scale(pedestalTransform, {2.2f, 0.35f, 2.2f});
    scene.AddInstance(cube, pedestal, pedestalTransform);

    const glm::vec3 colors[] = {{0.92f, 0.20f, 0.10f}, {0.92f, 0.72f, 0.25f}, {0.72f, 0.76f, 0.82f}};
    const glm::vec3 positions[] = {{-4.4f, 0.15f, -2.0f}, {-2.5f, 0.15f, -2.3f}, {3.5f, 0.15f, -2.0f}};
    const float metallic[] = {0.0f, 1.0f, 1.0f};
    const float roughness[] = {0.18f, 0.08f, 0.48f};
    for (int i = 0; i < 3; ++i)
    {
        Material material;
        material.Albedo = colors[i];
        material.Metallic = metallic[i];
        material.Roughness = roughness[i];
        const glm::vec3 position = positions[i];
        scene.AddInstance(sphere, material, glm::translate(glm::mat4(1.0f), position));
    }

    glm::mat4 bottleTransform = glm::translate(glm::mat4(1.0f), {0.0f, 0.48f, 0.2f});
    bottleTransform = glm::scale(bottleTransform, glm::vec3(13.0f));
    LoadGltfScene(std::string(ENGINE_ASSET_DIR) + "/WaterBottle.glb", scene, bottleTransform);

    Camera& camera = app.GetCamera();
    camera.Position = {0.0f, 1.7f, 8.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = -10.0f;
}

} // namespace

void PopulateSandboxScene(Application& app, const SandboxSceneConfig& config)
{
    ApplyPostConfig(app.GetScene(), config);
    if (config.PostShowcase)
    {
        PopulatePostShowcase(app, config);
        return;
    }
    if (config.HdriStudio)
    {
        PopulateHdriStudio(app, config);
        return;
    }
    if (config.LocalLightShowcase)
    {
        PopulateLocalLightShowcase(app, config);
        return;
    }
    Scene& scene = app.GetScene();
    const float sunAzimuth = glm::radians(config.SunAzimuthDegrees);
    const float sunElevation = glm::radians(config.SunElevationDegrees);
    const glm::vec3 toSun{
        std::cos(sunElevation) * std::cos(sunAzimuth),
        std::sin(sunElevation),
        std::cos(sunElevation) * std::sin(sunAzimuth)};
    scene.Sun.Direction = -toSun;
    scene.Sun.Color = {1.0f, 0.95f, 0.85f};
    scene.Sun.Intensity = 7.0f;
    scene.Sky.SkyIntensity = 0.45f;
    scene.Sky.StarDensity = glm::clamp(config.StarDensity, 0.0f, 0.05f);
    scene.Sky.StarIntensity = glm::max(config.StarIntensity, 0.0f);
    scene.Sky.StarSize = glm::clamp(config.StarSize, 0.25f, 4.0f);
    scene.Sky.StarTwinkle = glm::clamp(config.StarTwinkle, 0.0f, 1.0f);
    scene.Sky.MilkyWayIntensity = glm::max(config.MilkyWayIntensity, 0.0f);
    scene.Sky.NightSkyIntensity = glm::max(config.NightSkyIntensity, 0.0f);
    scene.Sky.NightHorizonGlow = glm::max(config.NightHorizonGlow, 0.0f);
    scene.Sky.StarTwinkleSpeed = glm::max(config.StarTwinkleSpeed, 0.0f);
    scene.Sky.NightSkyRotationSpeed = config.NightSkyRotationSpeed;
    scene.Sky.StarsEnabled = config.StarsEnabled;
    scene.Sky.MilkyWayEnabled = config.MilkyWayEnabled;
    scene.Sky.AnimateNightSky = config.AnimateNightSky;
    ApplyNightPreset(scene.Sky, config.NightPreset);
    scene.PostProcess.Exposure = 1.7f;
    scene.PostProcess.AutoExposure = true;
    scene.Shadows.DebugCascades = config.ShadowDebug;
    scene.Shadows.LogPerformance = config.ShadowBenchmark;
    if (!config.HdriPath.empty())
    {
        scene.Environment.Hdri = environments::LoadHdrFromFile(config.HdriPath);
        scene.Environment.Source = EnvironmentSource::EquirectangularHdr;
    }

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
