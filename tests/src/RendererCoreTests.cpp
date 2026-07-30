#include "engine/core/Camera.h"
#include "engine/core/Log.h"
#include "engine/concurrency/TaskSystem.h"
#include "engine/core/ApplicationConfig.h"
#include "engine/debug/DebugOverlay.h"
#include "engine/debug/RenderDocCapture.h"
#include "engine/render/CascadedShadows.h"
#include "engine/render/AsyncRenderResources.h"
#include "engine/render/Batching.h"
#include "engine/render/Exposure.h"
#include "engine/render/GpuTiming.h"
#include "engine/render/Instancing.h"
#include "engine/render/LevelOfDetail.h"
#include "engine/render/GpuCapabilities.h"
#include "engine/render/PipelineCache.h"
#include "engine/render/Picking.h"
#include "engine/render/RenderGraph.h"
#include "engine/render/RendererFrameGraph.h"
#include "engine/render/OcclusionCulling.h"
#include "engine/render/TextureFallback.h"
#include "engine/render/SceneRenderer.h"
#include "engine/render/ShaderSource.h"
#include "engine/render/TemporalAA.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/profiling/GpuProfiler.h"
#include "engine/profiling/MemoryProfiler.h"
#include "engine/plugin/PluginManager.h"
#include "engine/runtime/World.h"
#include "engine/runtime/RenderComponents.h"
#include "engine/runtime/WorldRenderBridge.h"
#include "engine/testing/VisualRegression.h"
#include "engine/asset/ColorGrading.h"
#include "engine/asset/WorldSceneSerialization.h"
#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/scene/Texture.h"
#include "engine/scene/Environment.h"
#include "engine/scene/MeshCombiner.h"
#include "engine/scene/Scene.h"
#include "engine/shader/HlslCompiler.h"
#include "engine/resource/ResourceManager.h"
#include "engine/resource/ResourceStreaming.h"
#include "TestRuntimePluginShared.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <future>
#include <mutex>

#include <glm/gtc/epsilon.hpp>

using namespace engine;
namespace jobs = engine::concurrency;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

bool MatrixNear(const glm::mat4& a, const glm::mat4& b, float epsilon)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(a[column][row] - b[column][row]) > epsilon)
                return false;
    return true;
}

void TestSplitDistribution()
{
    const auto linear = CalculateCascadeSplits(1.0f, 101.0f, 0.0f);
    Require(std::abs(linear[0] - 26.0f) < 0.001f, "lambda=0 must produce linear splits");
    const auto logarithmic = CalculateCascadeSplits(1.0f, 10000.0f, 1.0f);
    Require(std::abs(logarithmic[0] - 10.0f) < 0.001f, "lambda=1 must produce logarithmic splits");
    Require(std::abs(logarithmic[3] - 10000.0f) < 0.001f, "last split must equal shadow distance");
    for (int i = 1; i < kShadowCascadeCount; ++i)
        Require(logarithmic[i] > logarithmic[i - 1], "splits must be strictly increasing");
}

void TestCoverageAndResolution()
{
    Camera camera;
    camera.Position = {3.0f, 5.0f, 7.0f};
    camera.Yaw = -72.0f;
    camera.Pitch = -12.0f;
    camera.NearPlane = 0.1f;
    camera.FarPlane = 500.0f;
    CascadeShadowConfig config;
    const CascadeShadowData cascades = BuildCascadeShadows(camera, 16.0f / 9.0f, {-0.4f, -0.8f, -0.3f}, config);

    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade)
    {
        Require(cascades.SplitDepths[cascade] > cascades.NearDepths[cascade], "cascade depth interval must be positive");
        Require(cascades.WorldUnitsPerTexel[cascade] > 0.0f, "texel world size must be positive");
        if (cascade > 0)
        {
            Require(cascades.NearDepths[cascade] < cascades.SplitDepths[cascade - 1],
                    "adjacent cascades must overlap for transition blending");
            Require(cascades.WorldUnitsPerTexel[cascade] > cascades.WorldUnitsPerTexel[cascade - 1],
                    "far cascades must have lower effective world resolution");
        }

        const float middle = (cascades.NearDepths[cascade] + cascades.SplitDepths[cascade]) * 0.5f;
        const glm::vec4 clip = cascades.LightMatrices[cascade] * glm::vec4(camera.Position + camera.Forward() * middle, 1.0f);
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        Require(std::abs(ndc.x) <= 1.001f && std::abs(ndc.y) <= 1.001f && std::abs(ndc.z) <= 1.001f,
                "cascade center must be covered by its light projection");

        const glm::vec3 forward = camera.Forward();
        const glm::vec3 right = camera.Right();
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        const float tanHalfFov = std::tan(glm::radians(camera.FovDegrees) * 0.5f);
        for (float depth : {cascades.NearDepths[cascade], cascades.SplitDepths[cascade]})
        {
            const float halfHeight = tanHalfFov * depth;
            const float halfWidth = halfHeight * (16.0f / 9.0f);
            const glm::vec3 center = camera.Position + forward * depth;
            for (float x : {-1.0f, 1.0f})
            {
                for (float y : {-1.0f, 1.0f})
                {
                    const glm::vec3 corner = center + right * halfWidth * x + up * halfHeight * y;
                    const glm::vec4 cornerClip = cascades.LightMatrices[cascade] * glm::vec4(corner, 1.0f);
                    const glm::vec3 cornerNdc = glm::vec3(cornerClip) / cornerClip.w;
                    Require(std::abs(cornerNdc.x) <= 1.001f && std::abs(cornerNdc.y) <= 1.001f
                                && std::abs(cornerNdc.z) <= 1.001f,
                            "every cascade frustum corner must be covered by its light projection");
                }
            }
        }
    }
}

void TestTexelStabilization()
{
    Camera camera;
    camera.Position = {0.0f, 2.0f, 5.0f};
    CascadeShadowConfig config;
    const glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f));
    const CascadeShadowData first = BuildCascadeShadows(camera, 16.0f / 9.0f, lightDirection, config);

    const glm::vec3 referenceUp = std::abs(lightDirection.y) > 0.98f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 lightRight = glm::normalize(glm::cross(lightDirection, referenceUp));
    camera.Position += lightRight * first.WorldUnitsPerTexel[0] * 0.2f;
    const CascadeShadowData second = BuildCascadeShadows(camera, 16.0f / 9.0f, lightDirection, config);
    Require(MatrixNear(first.LightMatrices[0], second.LightMatrices[0], 1e-5f),
            "sub-texel lateral camera motion must not move the nearest shadow projection");
}

void TestProceduralLightCookie()
{
    const auto cookie = textures::MakeLightCookie(64);
    Require(cookie && cookie->Width == 64 && cookie->Height == 64, "procedural cookie must have the requested dimensions");
    const auto sample = [&](int x, int y) { return cookie->Pixels[(static_cast<size_t>(y) * cookie->Width + x) * 4]; };
    Require(sample(32, 32) > 100, "cookie center must transmit light");
    Require(sample(0, 0) == 0, "cookie corners must block light");
}

void TestHdrEnvironmentLoadingAndCache()
{
    const std::string path = std::string(TEST_ASSET_DIR) + "/studio_small_09_1k.hdr";
    const auto first = environments::LoadHdrFromFile(path);
    const auto second = environments::LoadHdrFromFile(path);
    Require(first != nullptr, "HDR environment must load");
    Require(first == second, "repeated HDR loads must reuse the CPU cache");
    Require(first->Width == 1024 && first->Height == 512 && first->Channels == 3,
            "HDR environment dimensions and channels must be preserved");
    Require(first->Pixels.size() == static_cast<size_t>(first->Width) * first->Height * 3,
            "HDR environment must contain tightly packed RGB floats");
    bool hasHdrValue = false;
    for (float value : first->Pixels)
    {
        Require(std::isfinite(value) && value >= 0.0f, "HDR pixels must be finite positive linear radiance");
        hasHdrValue = hasHdrValue || value > 1.0f;
    }
    Require(hasHdrValue, "HDR loader must preserve radiance above the LDR range");

    bool missingFileReported = false;
    try
    {
        environments::LoadHdrFromFile(std::string(TEST_ASSET_DIR) + "/does-not-exist.hdr");
    }
    catch (const EnvironmentLoadError& error)
    {
        missingFileReported = std::string(error.what()).find("does not exist") != std::string::npos;
    }
    Require(missingFileReported, "missing HDR assets must report a useful error");

    bool invalidFileReported = false;
    try
    {
        environments::LoadHdrFromFile(std::string(TEST_ASSET_DIR) + "/WaterBottle.glb");
    }
    catch (const EnvironmentLoadError& error)
    {
        invalidFileReported = std::string(error.what()).find("not a valid Radiance") != std::string::npos;
    }
    Require(invalidFileReported, "non-HDR assets must report their invalid format");
}

void TestDayNightTransitions()
{
    const DayNightState day = EvaluateDayNight(30.0f);
    Require(day.DayAmount > 0.999f && day.NightAmount < 0.001f,
            "high sun elevation must produce daylight");
    Require(day.DirectSunAmount > 0.999f, "daylight must keep direct sun lighting enabled");

    const DayNightState sunset = EvaluateDayNight(0.0f);
    Require(sunset.TwilightAmount > 0.8f, "sunset must strongly activate twilight scattering");
    Require(sunset.DirectSunAmount > 0.0f && sunset.DirectSunAmount < 1.0f,
            "direct sunlight must fade continuously around the horizon");
    Require(sunset.SunTint.g < day.SunTint.g, "low sun must become warmer than midday sun");

    const DayNightState nauticalTwilight = EvaluateDayNight(-12.0f);
    Require(nauticalTwilight.NightAmount > 0.0f && nauticalTwilight.NightAmount < 1.0f,
            "nautical twilight must transition toward night");
    Require(nauticalTwilight.DirectSunAmount < 0.001f,
            "sun below the horizon must not provide direct lighting");

    const DayNightState night = EvaluateDayNight(-20.0f);
    Require(night.NightAmount > 0.999f && night.DayAmount < 0.001f,
            "sun below astronomical twilight must produce full night");
}

void TestColorGradingLut()
{
    const auto identity = color_grading::MakeIdentity(4);
    Require(identity->Values.size() == 64, "identity LUT must contain size^3 samples");
    Require(glm::all(glm::epsilonEqual(identity->Values.front(), glm::vec3(0.0f), 1e-6f)),
            "identity LUT must begin at black");
    Require(glm::all(glm::epsilonEqual(identity->Values.back(), glm::vec3(1.0f), 1e-6f)),
            "identity LUT must end at white");
    Require(identity->Values[1].r > identity->Values[0].r && identity->Values[1].g == 0.0f,
            "LUT storage must use red-fastest .cube/OpenGL ordering");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "renderer_test_lut.cube";
    {
        std::ofstream file(path);
        file << "TITLE \"test\"\nLUT_3D_SIZE 2\nDOMAIN_MIN -1 0 0\nDOMAIN_MAX 1 2 3\n"
             << "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    }
    const auto loaded = color_grading::LoadCube(path.string());
    Require(loaded->Size == 2 && loaded->Values.size() == 8, ".cube loader must preserve dimensions and samples");
    Require(loaded->DomainMin.x == -1.0f && loaded->DomainMax.z == 3.0f,
            ".cube loader must preserve input domain metadata");
    std::filesystem::remove(path);

    const std::filesystem::path malformedPath = std::filesystem::temp_directory_path() / "renderer_bad_lut.cube";
    {
        std::ofstream file(malformedPath);
        file << "LUT_3D_SIZE 2\n0 0 0\n";
    }
    bool malformedReported = false;
    try
    {
        color_grading::LoadCube(malformedPath.string());
    }
    catch (const std::runtime_error& error)
    {
        malformedReported = std::string(error.what()).find("expected 8 RGB samples") != std::string::npos;
    }
    std::filesystem::remove(malformedPath);
    Require(malformedReported, "malformed .cube files must report the expected sample count");

    bool usefulError = false;
    try
    {
        color_grading::LoadCube(path.string());
    }
    catch (const std::runtime_error& error)
    {
        usefulError = std::string(error.what()).find(path.string()) != std::string::npos;
    }
    Require(usefulError, "missing .cube files must report the asset path");
}

void TestTemporalSamplingAndCuts()
{
    const PostProcessSettings defaults;
    Require(std::abs(defaults.TaaJitterScale - 0.5f) < 1e-6f &&
                std::abs(defaults.TaaHistoryWeight - 0.95f) < 1e-6f &&
                std::abs(defaults.TaaSharpen - 0.06f) < 1e-6f,
            "TAA defaults must prioritize a stable static image");

    glm::vec2 average(0.0f);
    for (uint64_t i = 0; i < 8; ++i)
    {
        const glm::vec2 jitter = TemporalJitterPixels(i);
        Require(glm::all(glm::greaterThanEqual(jitter, glm::vec2(-0.5f)))
                    && glm::all(glm::lessThan(jitter, glm::vec2(0.5f))),
                "Halton jitter must remain inside one centered pixel");
        average += jitter;
    }
    average /= 8.0f;
    Require(glm::length(average) < 0.12f, "the temporal jitter cycle must remain approximately centered");

    const glm::mat4 projection(1.0f);
    const glm::mat4 jittered = ApplyProjectionJitter(projection, {0.5f, -0.5f}, 100, 50);
    Require(std::abs(jittered[2][0] - 0.01f) < 1e-6f && std::abs(jittered[2][1] + 0.02f) < 1e-6f,
            "projection jitter must convert pixel offsets to NDC");
    Require(!IsTemporalCameraCut({0, 0, 0}, {0.1f, 0, 0}, {0, 0, -1}, {0.01f, 0, -1}, 60, 60),
            "ordinary camera motion must preserve TAA history");
    Require(IsTemporalCameraCut({0, 0, 0}, {10, 0, 0}, {0, 0, -1}, {0, 0, -1}, 60, 60),
            "camera teleports must invalidate TAA history");
    Require(IsTemporalCameraCut({0, 0, 0}, {0, 0, 0}, {0, 0, -1}, {0, 0, 1}, 60, 60),
            "large view rotations must invalidate TAA history");
}

void TestVulkanMaterialPacking()
{
    Material material;
    material.Albedo = {0.2f, 0.4f, 0.6f};
    material.BaseColorAlpha = 0.75f;
    material.Metallic = 0.8f;
    material.Roughness = 0.3f;
    material.AmbientOcclusion = 0.9f;
    material.SpecularF0 = 0.06f;
    material.Alpha = Material::AlphaMode::Mask;
    material.AlbedoMap = textures::MakeSolidColor({1.0f, 1.0f, 1.0f, 1.0f});
    material.NormalMap = textures::MakeFlatNormal();
    material.EmissiveMap = textures::MakeSolidColor({0.0f, 0.0f, 0.0f, 1.0f}, false);

    const vulkan::MaterialUniforms packed = vulkan::PackMaterialUniforms(material);
    Require(glm::all(glm::epsilonEqual(glm::vec3(packed.BaseColorFactor), material.Albedo, 1e-6f)),
            "Vulkan material packing must preserve base color");
    Require(std::abs(packed.BaseColorFactor.a - 0.75f) < 1e-6f,
            "Vulkan material packing must preserve alpha");
    Require(std::abs(packed.EmissiveMetallic.w - 0.8f) < 1e-6f,
            "Vulkan material packing must preserve metalness");
    Require(std::abs(packed.RoughnessAoAlphaCutoff.w - 0.06f) < 1e-6f,
            "Vulkan material packing must preserve dielectric F0");

    const uint32_t flags = packed.TextureFlags.x;
    Require((flags & vulkan::HasBaseColorMap) != 0 && (flags & vulkan::HasNormalMap) != 0,
            "Vulkan material packing must mark present base-color and normal maps");
    Require((flags & vulkan::HasEmissiveMap) != 0 && (flags & vulkan::UsesAlphaMask) != 0,
            "Vulkan material packing must mark emissive maps and alpha masking");
    Require((flags & vulkan::HasOcclusionMap) == 0,
            "Vulkan material packing must not mark absent textures");
}

void TestSharedSceneRendererFrame()
{
    Scene scene;
    scene.Sun.Direction = glm::normalize(glm::vec3(-0.35f, -0.8f, -0.2f));
    PointLight enabledPoint;
    enabledPoint.Position = {1.0f, 2.0f, 3.0f};
    scene.AddPointLight(enabledPoint);
    PointLight disabledPoint;
    disabledPoint.Enabled = false;
    scene.AddPointLight(disabledPoint);
    SpotLight spot;
    spot.Direction = {0.2f, -1.0f, 0.1f};
    scene.AddSpotLight(spot);
    AreaLight area;
    area.Direction = {0.0f, -1.0f, 0.2f};
    area.Up = {0.0f, 1.0f, 0.0f};
    scene.AddAreaLight(area);
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube());
    for (int index = 0; index < 96; ++index)
        scene.AddInstance(cube, Material{}, glm::mat4(1.0f));

    Camera camera;
    camera.Position = {2.0f, 3.0f, 7.0f};
    SceneRenderer renderer;
    std::atomic<uint32_t> parallelStages{0};
    const uint64_t animation = renderer.AddFrameWork(
        FrameWorkStage::Animation,
        [&parallelStages](const FrameWorkContext&, const jobs::CancellationToken&) {
            parallelStages.fetch_or(1u, std::memory_order_release);
        });
    renderer.AddFrameWork(
        FrameWorkStage::Particles,
        [&parallelStages](const FrameWorkContext&, const jobs::CancellationToken&) {
            parallelStages.fetch_or(2u, std::memory_order_release);
        });
    renderer.AddFrameWork(
        FrameWorkStage::Visibility,
        [&parallelStages](const FrameWorkContext&, const jobs::CancellationToken&) {
            if ((parallelStages.load(std::memory_order_acquire) & 3u) == 3u)
                parallelStages.fetch_or(4u, std::memory_order_release);
        });
    const RenderFrameData first = renderer.PrepareFrame(
        scene, camera, 1600, 900, nullptr, 2.5f, 1.0f / 120.0f);
    Require(first.SceneData == &scene && first.CameraData == &camera,
            "shared frame must reference the exact scene and camera consumed by both backends");
    Require(std::abs(first.AspectRatio - 16.0f / 9.0f) < 1.0e-6f,
            "shared frame must compute the viewport aspect ratio once");
    Require(first.LocalLights.PointCount == 1 && first.LocalLights.SpotCount == 1
                && first.LocalLights.AreaCount == 1,
            "shared frame must select enabled local lights once for both backends");
    Require(first.LocalLights.Points[0].Source == &scene.PointLights()[0],
            "prepared local lights must preserve their common scene identity");
    Require(glm::all(glm::epsilonEqual(first.SunDirection, scene.Sun.Direction, 1.0e-6f)),
            "shared frame must normalize the directional light consistently");
    Require(first.Cascades.SplitDepths[3] == scene.Shadows.MaxDistance,
            "shared frame must use scene shadow settings for both backends");
    Require(std::isfinite(first.TonemapWhitePointScale) && first.TonemapWhitePointScale > 0.0f,
            "shared frame must provide a valid common tonemap scale");
    Require(std::abs(first.TimeSeconds - 2.5f) < 1.0e-6f &&
                std::abs(first.DeltaSeconds - 1.0f / 120.0f) < 1.0e-6f,
            "shared frame must carry deterministic animation time to both backends");
    Require(first.RenderCommands.size() == scene.Instances().size() &&
                first.Preparation.RenderCommandCount == scene.Instances().size(),
            "shared multithreaded preparation must emit one deterministic command per valid mesh instance");
    Require(first.ShadowCommands.size() == scene.Instances().size() &&
                first.Preparation.ShadowCommandCount == scene.Instances().size(),
            "visible shadow casters must retain a deterministic directional-shadow command");
    for (size_t index = 0; index < first.RenderCommands.size(); ++index)
        Require(first.RenderCommands[index].InstanceIndex == index &&
                    first.RenderCommands[index].Source == &scene.Instances()[index],
                "parallel command generation must preserve stable scene order and identity");
    Require(parallelStages.load(std::memory_order_acquire) == 7u &&
                first.Preparation.CustomTaskCount == 3,
            "animation and particle work must finish before parallel visibility preparation");
    Require(renderer.RemoveFrameWork(animation), "registered frame work must be removable by token");

    const RenderFrameData second = renderer.PrepareFrame(scene, camera, 800, 800);
    Require(second.FrameIndex == first.FrameIndex + 1,
            "shared scene renderer frame index must advance exactly once per application frame");
    Require(std::abs(second.AspectRatio - 1.0f) < 1.0e-6f,
            "shared frame must react to viewport resize independently of the backend");
}

void TestVisibilityCulling()
{
    const MeshData cubeData = primitives::MakeCube(1.0f);
    const AxisAlignedBounds local = ComputeMeshBounds(cubeData);
    Require(local.Valid && glm::all(glm::epsilonEqual(local.Minimum, glm::vec3(-1.0f), 1.0e-6f)) &&
                glm::all(glm::epsilonEqual(local.Maximum, glm::vec3(1.0f), 1.0e-6f)),
            "visibility bounds must be derived exactly from mesh positions");
    const glm::mat4 transformed = glm::translate(glm::mat4(1.0f), {3.0f, 2.0f, -4.0f}) *
        glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), {2.0f, 1.0f, 0.5f});
    const AxisAlignedBounds world = TransformBounds(local, transformed);
    Require(world.Valid && glm::all(glm::greaterThan(world.Maximum, world.Minimum)),
            "rotated and non-uniformly scaled mesh bounds must remain conservative and finite");

    Scene scene;
    scene.Visibility.MaxDistance = 100.0f;
    scene.Visibility.DebugBounds = true;
    const auto cube = std::make_shared<MeshData>(cubeData);
    scene.AddInstance(cube, Material{}, glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -5.0f}));
    scene.AddInstance(cube, Material{}, glm::translate(glm::mat4(1.0f), {50.0f, 0.0f, -5.0f}));
    scene.AddInstance(cube, Material{}, glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -150.0f}));
    scene.AddInstance(cube, Material{}, glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 5.0f}));
    scene.Instances().back().AlwaysVisible = true;
    scene.AddInstance({}, Material{}, glm::mat4(1.0f));

    Camera camera;
    camera.Position = {0.0f, 0.0f, 0.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = 0.0f;
    camera.NearPlane = 0.1f;
    camera.FarPlane = 100.0f;
    SceneRenderer renderer;
    const RenderFrameData frame = renderer.PrepareFrame(scene, camera, 1280, 720);
    Require(frame.RenderCommands.size() == 2 &&
                frame.RenderCommands[0].InstanceIndex == 0 &&
                frame.RenderCommands[1].InstanceIndex == 3,
            "CPU culling must keep visible/always-visible objects and preserve stable scene order");
    Require(frame.ShadowCommands.size() == 3 &&
                frame.ShadowCommands[1].InstanceIndex == 1,
            "frustum-culled nearby casters must remain in the shadow list while distant ones are removed");
    Require(frame.Visibility.Tested == 5 && frame.Visibility.Visible == 2 &&
                frame.Visibility.FrustumCulled == 1 && frame.Visibility.DistanceCulled == 1 &&
                frame.Visibility.InvalidBounds == 1 && frame.Visibility.ShadowCasters == 3,
            "visibility statistics must classify every instance exactly once");
    Require(frame.VisibilityDebug.size() == 4,
            "bounds debug data must include visible and culled finite bounds but exclude invalid meshes");
    Require(SquaredDistanceToBounds({0.0f, 0.0f, 0.0f}, frame.VisibilityDebug[0].Bounds) == 16.0f,
            "distance culling must use nearest AABB distance rather than center distance");
}

void TestScreenSpaceMeshLods()
{
    const auto lod0 = std::make_shared<MeshData>(primitives::MakeSphere(1.0f, 32, 32));
    const auto lod1 = std::make_shared<MeshData>(primitives::MakeSphere(1.0f, 12, 12));
    const auto lod2 = std::make_shared<MeshData>(primitives::MakeSphere(1.0f, 5, 5));
    const std::array<MeshLodLevel, 2> levels{{
        {lod1, static_cast<float>(lod1->Indices.size()) / static_cast<float>(lod0->Indices.size()), 0.01f},
        {lod2, static_cast<float>(lod2->Indices.size()) / static_cast<float>(lod0->Indices.size()), 0.04f}}};
    Camera camera;
    camera.Position = {0.0f, 0.0f, 0.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = 0.0f;
    LodSettings settings;
    settings.TargetScreenSpaceErrorPixels = 1.0f;
    settings.HysteresisFraction = 0.15f;
    LodSelector selector;
    LodStatistics statistics;
    auto boundsAt = [](float z) {
        return AxisAlignedBounds{{-1.0f, -1.0f, z - 1.0f}, {1.0f, 1.0f, z + 1.0f}, true};
    };
    Require(selector.Select(77, 1, boundsAt(-5.0f), camera, 720, levels, settings,
                            &statistics).Level == 0,
            "near geometry must retain LOD0 when its projected simplification error is visible");
    Require(selector.Select(77, 2, boundsAt(-120.0f), camera, 720, levels, settings,
                            &statistics).Level == 2,
            "distant geometry must select the coarsest LOD under the screen-space error target");
    Require(selector.Select(77, 3, boundsAt(-80.0f), camera, 720, levels, settings,
                            &statistics).Level == 2,
            "LOD hysteresis must retain a coarse level inside the transition band");
    Require(selector.Select(77, 4, boundsAt(-65.0f), camera, 720, levels, settings,
                            &statistics).Level == 1,
            "LOD hysteresis must restore detail after leaving the transition band");
    selector.Reset();
    Require(selector.Select(0, 5, boundsAt(-120.0f), camera, 720, levels, settings).Level == 2 &&
                selector.Select(0, 6, boundsAt(-80.0f), camera, 720, levels, settings).Level == 1,
            "anonymous renderer instances must not share temporal LOD hysteresis");
    const std::array<MeshLodLevel, 2> incompleteLevels{{
        {lod1, 0.5f, 0.01f}, {nullptr, 0.2f, 0.04f}}};
    settings.ForcedLevel = 2;
    Require(selector.Select(77, 5, boundsAt(-5.0f), camera, 720, levels, settings,
                            &statistics).Level == 2,
            "forced LOD selection must provide deterministic visual QA");
    Require(selector.Select(78, 6, boundsAt(-120.0f), camera, 720, incompleteLevels,
                            settings).Level == 1,
            "forced LOD selection must clamp to the contiguous valid mesh chain");

    Scene scene;
    scene.Visibility.Enabled = false;
    scene.Batching.Enabled = true;
    scene.Lods = settings;
    scene.Lods.ForcedLevel = UINT32_MAX;
    scene.AddInstance(lod0, Material{}, glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -5.0f}));
    scene.Instances().back().LodLevels.assign(levels.begin(), levels.end());
    scene.AddInstance(lod0, Material{}, glm::translate(glm::mat4(1.0f), {3.0f, 0.0f, -120.0f}));
    scene.Instances().back().LodLevels.assign(levels.begin(), levels.end());
    SceneRenderer renderer;
    const RenderFrameData& frame = renderer.PrepareFrame(scene, camera, 1280, 720);
    Require(frame.RenderCommands.size() == 2 && frame.ShadowCommands.size() == 2,
            "LOD instances must remain eligible for both main and shadow submission");
    Require(GetCommandMesh(frame.RenderCommands[0]) == lod0 &&
                GetCommandMesh(frame.RenderCommands[1]) == lod2,
            "shared frame preparation must select near and far LOD geometry exactly once");
    Require(GetCommandMesh(frame.ShadowCommands[1]) == lod2,
            "the shadow path must consume the exact LOD chosen for the main pass");
    std::array<const PreparedRenderCommand*, 2> commands{
        &frame.RenderCommands[0], &frame.RenderCommands[1]};
    const InstanceBatchBuildResult batches = BuildInstanceBatches(commands, scene.Instancing);
    Require(batches.Batches.size() == 2,
            "GPU instancing must never combine commands that selected different LOD geometry");
    Require(frame.Lods.Selected[0] == 1 && frame.Lods.Selected[2] == 1 &&
                frame.Lods.SubmittedTriangles < frame.Lods.Lod0Triangles,
            "LOD frame statistics must expose selected levels and triangle savings");
}

void TestTemporalHiZOcclusionPolicy()
{
    Scene scene;
    scene.Visibility.GpuOcclusionCulling = true;
    scene.Visibility.OcclusionConfirmationFrames = 2;
    scene.Visibility.OcclusionMaxHiddenFrames = 3;
    scene.AddInstance(std::make_shared<MeshData>(primitives::MakeCube(1.0f)),
                      Material{}, glm::translate(glm::mat4(1.0f),
                                                 {0.0f, 0.0f, -5.0f}));
    Camera camera;
    camera.Position = {0.0f, 0.0f, 0.0f};
    camera.Yaw = -90.0f;
    SceneRenderer renderer;
    const RenderFrameData frame = renderer.PrepareFrame(scene, camera, 1280, 720);
    Require(frame.RenderCommands.size() == 1,
            "Hi-Z policy test requires one CPU-visible candidate");
    const PreparedRenderCommand& command = frame.RenderCommands.front();
    const OcclusionQueryRecord record = MakeOcclusionQueryRecord(command);
    const GpuOcclusionBounds bounds = MakeGpuOcclusionBounds(command, 0.1f);
    Require(glm::all(glm::lessThan(glm::vec3(bounds.Minimum),
                                   command.WorldBounds.Minimum)) &&
                glm::all(glm::greaterThan(glm::vec3(bounds.Maximum),
                                          command.WorldBounds.Maximum)),
            "GPU Hi-Z bounds must inflate conservatively on every axis");

    TemporalOcclusionState state;
    const OcclusionFrameSignature signature = BuildOcclusionFrameSignature(frame);
    Require(state.BeginFrame(signature, scene.Visibility),
            "first Hi-Z frame must reset empty temporal history");
    const uint64_t generation = state.Generation();
    const std::array<OcclusionQueryRecord, 1> records{record};
    const std::array<uint32_t, 1> occluded{0u};
    state.ApplyResults(generation, records, occluded);
    Require(state.ShouldDraw(record),
            "one occluded result must not hide an object");
    Require(!state.BeginFrame(signature, scene.Visibility),
            "stationary camera and scene must preserve Hi-Z history");
    state.ApplyResults(generation, records, occluded);
    Require(!state.ShouldDraw(record),
            "two confirmed occluded results must suppress the draw");
    const std::array<uint32_t, 1> visible{1u};
    state.ApplyResults(generation, records, visible);
    Require(state.ShouldDraw(record),
            "a visible GPU result must immediately restore the draw");

    OcclusionQueryRecord transformedRecord = record;
    ++transformedRecord.TransformHash;
    state.ApplyResults(generation, records, occluded);
    state.ApplyResults(generation, records, occluded);
    Require(state.ShouldDraw(transformedRecord),
            "an object transform change must invalidate only that object's hidden history");

    OcclusionFrameSignature moved = signature;
    moved.CameraPosition.x += 0.5f;
    Require(state.BeginFrame(moved, scene.Visibility) &&
                state.Generation() != generation,
            "camera motion beyond the threshold must invalidate Hi-Z history");
    state.ApplyResults(generation, records, occluded);
    Require(state.ShouldDraw(record),
            "stale asynchronous results must be rejected after a history reset");

    // Sub-threshold motion must accumulate against the last accepted camera,
    // otherwise slow camera movement could keep unsafe history forever.
    TemporalOcclusionState accumulated;
    Require(accumulated.BeginFrame(signature, scene.Visibility),
            "accumulated motion test must initialize history");
    OcclusionFrameSignature smallMove = signature;
    smallMove.CameraPosition.x += scene.Visibility.OcclusionCameraPositionThreshold * 0.6f;
    Require(!accumulated.BeginFrame(smallMove, scene.Visibility),
            "a single sub-threshold camera move should retain history");
    smallMove.CameraPosition.x += scene.Visibility.OcclusionCameraPositionThreshold * 0.6f;
    Require(accumulated.BeginFrame(smallMove, scene.Visibility),
            "repeated sub-threshold motion must eventually invalidate history");

    scene.Visibility.Enabled = false;
    Require(accumulated.BeginFrame(signature, scene.Visibility) &&
                !accumulated.Active() && accumulated.ShouldDraw(record),
            "disabling scene visibility must disable GPU Hi-Z and draw conservatively");
}

void TestGpuInstancingAndHism()
{
    const auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    Material sharedMaterial;
    sharedMaterial.Albedo = {0.2f, 0.6f, 0.9f};
    Scene batchScene;
    batchScene.Batching.Enabled = false;
    batchScene.Visibility.Enabled = false;
    for (int index = 0; index < 64; ++index)
        batchScene.AddInstance(cube, sharedMaterial,
            glm::translate(glm::mat4(1.0f), {static_cast<float>(index), 0.0f, -10.0f}));
    Material distinctMaterial = sharedMaterial;
    distinctMaterial.Roughness = 0.9f;
    batchScene.AddInstance(cube, distinctMaterial,
                           glm::translate(glm::mat4(1.0f), {0.0f, 2.0f, -10.0f}));
    batchScene.AddInstance(cube, sharedMaterial,
                           glm::translate(glm::mat4(1.0f), {0.0f, 4.0f, -10.0f}));
    batchScene.Instances().back().AllowInstancing = false;
    Camera camera;
    camera.Position = {0.0f, 0.0f, 0.0f};
    camera.Yaw = -90.0f;
    SceneRenderer renderer;
    const RenderFrameData& batchFrame = renderer.PrepareFrame(
        batchScene, camera, 1280, 720);
    std::vector<const PreparedRenderCommand*> commands;
    for (const PreparedRenderCommand& command : batchFrame.RenderCommands)
        commands.push_back(&command);
    const InstanceBatchBuildResult batches = BuildInstanceBatches(
        commands, batchScene.Instancing, 7);
    Require(batches.Statistics.SourceInstances == 66 &&
                batches.Statistics.DrawBatches == 3 &&
                batches.Statistics.InstancedBatches == 1 &&
                batches.Statistics.InstancedInstances == 64 &&
                batches.Statistics.DrawCallsSaved == 63,
            "GPU instancing must merge exact mesh/material matches and preserve opt-outs");
    Require(batches.Batches.front().FirstInstance == 7 &&
                batches.Batches.front().Commands.size() == 64,
            "instance batches must preserve the caller's base-instance range");
    InstancingSettings disabledInstancing = batchScene.Instancing;
    disabledInstancing.Enabled = false;
    const InstanceBatchBuildResult singletonBatches = BuildInstanceBatches(
        commands, disabledInstancing);
    Require(singletonBatches.Statistics.DrawBatches == commands.size() &&
                singletonBatches.Statistics.DrawCallsSaved == 0,
            "disabled GPU instancing must retain the deterministic singleton fallback");

    const ViewFrustum frustum = ExtractViewFrustum(
        camera.GetProjection(16.0f / 9.0f) * camera.GetView());
    std::vector<HismCullItem> items;
    items.reserve(1024);
    for (uint32_t index = 0; index < 512; ++index)
    {
        const glm::vec3 center{
            static_cast<float>(index % 32) * 0.2f - 3.0f,
            static_cast<float>((index / 32) % 16) * 0.2f - 1.5f,
            -12.0f - static_cast<float>(index / 512)};
        items.push_back({index, {center - glm::vec3(0.05f),
                                 center + glm::vec3(0.05f), true}});
    }
    for (uint32_t index = 0; index < 512; ++index)
    {
        const glm::vec3 center{400.0f + static_cast<float>(index % 32),
                               0.0f, -12.0f};
        items.push_back({512u + index, {center - glm::vec3(0.05f),
                                        center + glm::vec3(0.05f), true}});
    }
    HismCullStatistics hism;
    const std::vector<VisibilityClassification> classifications =
        CullHierarchicalInstances(items, frustum, camera.Position, camera.FarPlane,
                                  true, false, 8, &hism);
    for (size_t index = 0; index < items.size(); ++index)
    {
        const VisibilityClassification expected = IntersectsFrustum(
            frustum, items[index].Bounds)
            ? VisibilityClassification::Visible
            : VisibilityClassification::FrustumCulled;
        Require(classifications[index] == expected,
                "HISM hierarchy must exactly match conservative leaf frustum results");
    }
    Require(hism.NodesCulled > 0 && hism.InstancesCulled >= 512 &&
                hism.LeafTests < items.size(),
            "HISM must reject coherent clusters before per-instance leaf testing");

    Scene hismScene;
    hismScene.Batching.Enabled = false;
    hismScene.Visibility.DistanceCulling = false;
    hismScene.Instancing.HismMinimumGroupSize = 16;
    hismScene.Instancing.HismLeafSize = 4;
    for (const HismCullItem& item : items)
        hismScene.AddInstance(cube, sharedMaterial,
            glm::translate(glm::mat4(1.0f), item.Bounds.Center()));
    const RenderFrameData& hismFrame = renderer.PrepareFrame(
        hismScene, camera, 1280, 720);
    (void)hismFrame;
    const VisibilityStatistics& visibility = renderer.GetVisibilityStatistics();
    Require(visibility.HismGroups == 1 && visibility.HismNodesCulled > 0 &&
                visibility.HismLeafTests < hismScene.Instances().size(),
            "SceneRenderer must route large compatible groups through HISM");
}

void TestGeometryCombiningAndBatching()
{
    const MeshData cube = primitives::MakeCube(0.5f);
    const std::array<MeshCombineSource, 2> sources{{
        {&cube, glm::translate(glm::mat4(1.0f), {-2.0f, 0.0f, 0.0f})},
        {&cube, glm::scale(glm::translate(glm::mat4(1.0f),
                                         {2.0f, 0.0f, 0.0f}),
                           {-1.0f, 1.0f, 1.0f})}}};
    MeshData combined;
    MeshCombineReport report;
    std::string error;
    Require(CombineMeshes(sources, combined, &report, &error), error.c_str());
    Require(combined.Vertices.size() == cube.Vertices.size() * 2u &&
                combined.Indices.size() == cube.Indices.size() * 2u &&
                report.SourceMeshes == 2 && report.MirroredMeshes == 1,
            "mesh combining must bake transforms and retain mirrored triangle geometry");
    for (const Vertex& vertex : combined.Vertices)
    {
        Require(std::abs(glm::length(vertex.Normal) - 1.0f) < 0.001f &&
                    std::abs(glm::dot(vertex.Normal, glm::vec3(vertex.Tangent))) < 0.001f,
                "combined normals and tangents must remain normalized and orthogonal");
    }

    auto sharedCube = std::make_shared<MeshData>(cube);
    Material material;
    material.Albedo = {0.2f, 0.6f, 0.9f};
    Scene scene;
    scene.Visibility.Enabled = false;
    scene.Batching.MinimumStaticBatchSize = 2;
    scene.Batching.PreferInstancingForRepeatedMeshes = false;
    scene.Batching.MaximumCachedBatches = 1;
    scene.Batching.SpatialCellSize = 100.0f;
    for (int index = 0; index < 4; ++index)
    {
        scene.AddInstance(sharedCube, material,
            glm::translate(glm::mat4(1.0f), {static_cast<float>(index), 0.0f, -5.0f}));
        scene.Instances().back().TemporalId = 100u + static_cast<uint64_t>(index);
    }
    SceneBatcher batcher;
    const SceneBatchBuildResult& first = batcher.Build(scene, 1);
    Require(first.Instances.size() == 1 && first.Statistics.StaticBatches == 1 &&
                first.Statistics.StaticBatchedInstances == 4 &&
                first.Statistics.DrawCallsSaved == 3 &&
                first.Instances.front()->Mesh->Vertices.size() == cube.Vertices.size() * 4u,
            "static batching must replace compatible cell geometry with one cached mesh");
    const uint64_t staticRevision = first.Instances.front()->Mesh->Revision;
    const SceneBatchBuildResult& cached = batcher.Build(scene, 2);
    Require(cached.Statistics.CacheHits == 1 &&
                cached.Instances.front()->Mesh->Revision == staticRevision,
            "unchanged static geometry must hit the batch cache without GPU revision churn");
    const std::shared_ptr<MeshData> cachedStaticMesh = cached.Instances.front()->Mesh;
    for (MeshInstance& instance : scene.Instances())
        instance.BatchGroupId = 42;
    const SceneBatchBuildResult& recycled = batcher.Build(scene, 3);
    Require(recycled.Statistics.RecycledBatches == 1 &&
                recycled.Instances.front()->Mesh == cachedStaticMesh &&
                recycled.Instances.front()->Mesh->Revision > staticRevision,
            "bounded batch cache must recycle inactive entries without changing GPU handles");

    Camera camera;
    camera.Position = {0.0f, 0.0f, 2.0f};
    camera.Yaw = -90.0f;
    SceneRenderer renderer;
    const RenderFrameData& frame = renderer.PrepareFrame(scene, camera, 800, 600);
    Require(frame.Batching.DrawCallsSaved == 3 && frame.RenderCommands.size() == 1,
            "SceneRenderer must cull and submit the shared batched geometry path");

    Scene dynamic;
    dynamic.Visibility.Enabled = false;
    dynamic.PostProcess.AntiAliasing = AntiAliasingMode::None;
    dynamic.Batching.MinimumDynamicBatchSize = 2;
    dynamic.Batching.PreferInstancingForRepeatedMeshes = false;
    dynamic.Batching.SpatialCellSize = 100.0f;
    for (int index = 0; index < 2; ++index)
    {
        dynamic.AddInstance(sharedCube, material,
            glm::translate(glm::mat4(1.0f), {static_cast<float>(index), 0.0f, -4.0f}));
        dynamic.Instances().back().Mobility = MeshMobility::Movable;
        dynamic.Instances().back().TemporalId = 200u + static_cast<uint64_t>(index);
    }
    SceneBatcher dynamicBatcher;
    const SceneBatchBuildResult& dynamicFirst = dynamicBatcher.Build(dynamic, 1);
    Require(dynamicFirst.Instances.size() == 1 &&
                dynamicFirst.Statistics.DynamicBatches == 1,
            "selective dynamic batching must combine small movable meshes without TAA");
    const uint64_t dynamicRevision = dynamicFirst.Instances.front()->Mesh->Revision;
    const std::shared_ptr<MeshData> dynamicMesh = dynamicFirst.Instances.front()->Mesh;
    dynamic.Instances()[0].Transform[3].x += 0.5f;
    const SceneBatchBuildResult& dynamicMoved = dynamicBatcher.Build(dynamic, 2);
    Require(dynamicMoved.Instances.size() == 1 &&
                dynamicMoved.Instances.front()->Mesh->Revision > dynamicRevision &&
                dynamicMoved.Instances.front()->Mesh == dynamicMesh &&
                dynamicMoved.Statistics.RebuiltBatches == 1,
            "changed dynamic batches must increment the native GPU upload revision");

    dynamic.PostProcess.AntiAliasing = AntiAliasingMode::Taa;
    dynamic.Batching.DynamicStabilityFrames = 2;
    SceneBatcher temporalBatcher;
    Require(temporalBatcher.Build(dynamic, 1).Instances.size() == 2,
            "moving TAA geometry must initially retain individual transforms");
    temporalBatcher.Build(dynamic, 2);
    Require(temporalBatcher.Build(dynamic, 3).Instances.size() == 1,
            "TAA geometry may batch after its configured stability window");
    dynamic.Instances()[1].Transform[3].x += 0.25f;
    Require(temporalBatcher.Build(dynamic, 4).Instances.size() == 2,
            "TAA motion must immediately fall back from dynamic batching");
}

void TestTaskSystemScheduling()
{
    jobs::TaskSystem tasks(1);
    std::promise<void> blockerStarted;
    std::promise<void> releaseBlocker;
    std::shared_future<void> release = releaseBlocker.get_future().share();
    const jobs::TaskHandle blocker = tasks.Submit([&](const jobs::CancellationToken&) {
        blockerStarted.set_value();
        release.wait();
    }, jobs::TaskPriority::Critical);
    blockerStarted.get_future().wait();

    std::mutex orderMutex;
    std::vector<int> order;
    const jobs::TaskHandle low = tasks.Submit([&](const jobs::CancellationToken&) {
        std::scoped_lock lock(orderMutex);
        order.push_back(1);
    }, jobs::TaskPriority::Low);
    const jobs::TaskHandle high = tasks.Submit([&](const jobs::CancellationToken&) {
        std::scoped_lock lock(orderMutex);
        order.push_back(2);
    }, jobs::TaskPriority::High);
    auto cancelled = tasks.SubmitFuture([](const jobs::CancellationToken&) { return 99; }, jobs::TaskPriority::Normal);
    cancelled.Cancel();
    releaseBlocker.set_value();
    tasks.Wait(blocker);
    tasks.Wait(high);
    tasks.Wait(low);
    Require(order == std::vector<int>({2, 1}),
            "single-worker task queue must dispatch higher priorities first and preserve FIFO within a priority");
    bool cancellationObserved = false;
    try
    {
        (void)cancelled.Get();
    }
    catch (const jobs::TaskCancelled&)
    {
        cancellationObserved = true;
    }
    Require(cancellationObserved && cancelled.Status() == jobs::TaskStatus::Cancelled,
            "queued asynchronous tasks must surface cooperative cancellation");

    std::promise<void> runningStarted;
    auto running = tasks.SubmitFuture([&runningStarted](const jobs::CancellationToken& token) {
        runningStarted.set_value();
        while (!token.IsCancellationRequested())
            std::this_thread::yield();
        token.ThrowIfCancellationRequested();
    }, jobs::TaskPriority::High);
    runningStarted.get_future().wait();
    running.Cancel();
    try
    {
        running.Get();
    }
    catch (const jobs::TaskCancelled&)
    {
    }
    tasks.Wait(running.Handle());
    Require(running.Status() == jobs::TaskStatus::Cancelled,
            "already-running jobs must observe cancellation without stopping a worker thread");

    auto failed = tasks.SubmitFuture([]() -> int { throw std::runtime_error("expected failure"); });
    bool failureObserved = false;
    try
    {
        (void)failed.Get();
    }
    catch (const std::runtime_error& error)
    {
        failureObserved = std::string(error.what()) == "expected failure";
    }
    tasks.Wait(failed.Handle());
    Require(failureObserved && failed.Status() == jobs::TaskStatus::Failed,
            "typed asynchronous results must retain task exceptions and failed state");

    std::vector<std::atomic<uint32_t>> visits(2048);
    tasks.ParallelFor(visits.size(), 31, [&visits](size_t index) {
        visits[index].fetch_add(1, std::memory_order_relaxed);
    });
    for (const auto& visit : visits)
        Require(visit.load() == 1, "parallel-for must visit every element exactly once");

    auto nested = tasks.SubmitFuture([&tasks](const jobs::CancellationToken&) {
        auto child = tasks.SubmitFuture([] { return 17; }, jobs::TaskPriority::High);
        tasks.Wait(child.Handle());
        return child.Get();
    });
    Require(nested.Get() == 17, "worker-side waits must execute queued work instead of deadlocking");
    // Exercise TaskHandle::Wait directly instead of TaskSystem::Wait so the
    // condition-variable publication contract remains covered. This used to
    // lose a completion notification when the worker finished between the
    // predicate check and the main thread actually sleeping.
    for (uint32_t round = 0; round < 10'000; ++round)
    {
        const jobs::TaskHandle completed = tasks.Submit(
            [](const jobs::CancellationToken&) {}, jobs::TaskPriority::High);
        completed.Wait();
        Require(completed.IsReady(),
                "direct task waits must always observe terminal publication");
    }
    for (uint32_t round = 0; round < 128; ++round)
    {
        for (uint32_t task = 0; task < 4; ++task)
            tasks.Submit([](const jobs::CancellationToken&) {});
        tasks.WaitIdle();
        const jobs::TaskSystemStatistics idle = tasks.Statistics();
        Require(idle.Queued == 0 && idle.Active == 0,
                "WaitIdle must not miss the final active-task transition");
    }
    const jobs::TaskSystemStatistics statistics = tasks.Statistics();
    Require(statistics.Completed >= 3 && statistics.Cancelled >= 1 && statistics.Queued == 0,
            "task system statistics must report completed, cancelled and pending work");
}

void TestAsyncResourceLoadingAndStreamingBudgets()
{
    using namespace resources;
    jobs::TaskSystem tasks(2);
    ResourceManager resources;
    resources.SetTaskSystem(&tasks);
    resources.SetDevelopmentFallback(
        [](assets::AssetGuid, std::type_index type, std::string* error) -> std::shared_ptr<void> {
            if (type != std::type_index(typeid(int)))
            {
                if (error)
                    *error = "Unexpected test resource type";
                return {};
            }
            return std::make_shared<int>(42);
        }, true);
    const auto loaded = resources.LoadAsync<int>(assets::AssetGuid{1, 2},
        {jobs::TaskPriority::High, {}});
    Require(loaded.Get() && *loaded.Get() == 42,
            "typed model/texture resource path must support asynchronous cached loading");

    StreamingBudget budget;
    budget.MaxResidentCpuBytes = 100;
    budget.MaxResidentVramBytes = 100;
    budget.MaxIoBytesPerTick = 64;
    budget.MaxConcurrentLoads = 1;
    ResourceStreamingScheduler streaming(budget, &tasks);
    std::mutex orderMutex;
    std::vector<std::string> order;
    auto makeRequest = [&](std::string key, jobs::TaskPriority priority) {
        StreamingRequest request;
        request.Key = key;
        request.Priority = priority;
        request.Cost = {60, 60, 64};
        request.Load = [&, key](const jobs::CancellationToken& token) -> std::shared_ptr<void> {
            token.ThrowIfCancellationRequested();
            std::scoped_lock lock(orderMutex);
            order.push_back(key);
            return std::make_shared<std::string>(key);
        };
        return request;
    };
    const StreamingTicket low = streaming.Queue(makeRequest("low", jobs::TaskPriority::Low));
    const StreamingTicket high = streaming.Queue(makeRequest("high", jobs::TaskPriority::Critical));
    const StreamingTicket cancelled = streaming.Queue(makeRequest("cancelled", jobs::TaskPriority::Normal));
    cancelled.Cancel();
    streaming.Tick();
    for (int spin = 0; spin < 10000 && high.Status() != StreamingState::Resident; ++spin)
    {
        std::this_thread::yield();
        streaming.Tick();
    }
    Require(high.Status() == StreamingState::Resident && !order.empty() && order.front() == "high",
            "streaming scheduler must admit the highest-priority request first");
    Require(low.Status() == StreamingState::Queued && streaming.Statistics().BudgetBlocked >= 1,
            "resident CPU/VRAM budgets must block an asset that does not fit");
    Require(cancelled.Status() == StreamingState::Cancelled,
            "streaming tickets must cancel queued model/texture requests");
    Require(streaming.Evict("high"), "resident streamed assets must be evictable");
    for (int spin = 0; spin < 10000 && low.Status() != StreamingState::Resident; ++spin)
    {
        streaming.Tick();
        std::this_thread::yield();
    }
    Require(low.Status() == StreamingState::Resident && streaming.Statistics().ResidentCpuBytes == 60 &&
                streaming.Statistics().ResidentVramBytes == 60,
            "eviction must return CPU and VRAM budget to subsequent streaming requests");
}

void TestAsyncRenderResourceLifetime()
{
    using namespace render;
    const Material fallbackMaterial = MakeVisibleFallbackMaterial();
    Require(fallbackMaterial.Albedo.r == 1.0f && fallbackMaterial.Albedo.b == 1.0f &&
                fallbackMaterial.AlbedoMap,
            "asynchronous pipeline failure must have a visible magenta checker fallback material");
    FrameGpuArena arena(1024, 2);
    const ArenaAllocation first = arena.Allocate(1, 100, 256);
    const ArenaAllocation second = arena.Allocate(1, 100, 256);
    Require(first.Offset == 1024 && second.Offset == 1280 && arena.Used(1) == 356,
            "per-frame GPU arena must apply alignment inside independent frame slots");
    Require(!arena.Allocate(1, 800, 1), "frame arena must reject overflow without corrupting its cursor");
    arena.Reset(1);
    Require(arena.Allocate(1, 1024, 1).Size == 1024 && arena.Peak(1) >= 356,
            "a fence-safe frame reset must preserve peak usage while recycling capacity");

    StagingRingAllocator staging(128);
    const StagingAllocation uploadA = staging.Allocate(64, 16, 0);
    const StagingAllocation uploadB = staging.Allocate(64, 16, 0);
    Require(uploadA && uploadB && !staging.Allocate(1, 1, 0),
            "staging ring must prevent overlap with in-flight uploads");
    staging.Retire(uploadA.Serial, 4);
    staging.Reclaim(3);
    Require(!staging.Allocate(32, 16, 3), "staging memory must remain live before its timeline value");
    staging.Reclaim(4);
    Require(staging.Allocate(32, 16, 4).Offset == 0,
            "completed timeline ranges must become reusable deterministically");

    DeferredReleaseQueue releases;
    std::vector<int> released;
    releases.Enqueue(5, [&] { released.push_back(2); });
    releases.Enqueue(3, [&] { released.push_back(1); });
    Require(releases.ReleaseCompleted(4) == 1 && released == std::vector<int>({1}) && releases.Pending() == 1,
            "deferred GPU destruction must only run callbacks whose frame fence completed");
    Require(releases.Flush() == 1 && released == std::vector<int>({1, 2}),
            "deferred destruction shutdown flush must preserve safe deterministic order");

    struct TestPipeline
    {
        int Id = 0;
    };
    jobs::TaskSystem tasks(1);
    auto fallback = std::make_shared<TestPipeline>();
    fallback->Id = -1;
    AsyncPipelineLibrary<TestPipeline> pipelines(fallback, &tasks);
    std::promise<void> started;
    std::promise<void> release;
    std::shared_future<void> releaseFuture = release.get_future().share();
    const auto pipeline = pipelines.Request("pbr", [&](const jobs::CancellationToken& token) {
        started.set_value();
        releaseFuture.wait();
        token.ThrowIfCancellationRequested();
        auto result = std::make_shared<TestPipeline>();
        result->Id = 7;
        return result;
    });
    started.get_future().wait();
    Require(pipeline.UsesFallback() && pipeline.Resolve()->Id == -1,
            "background pipeline builds must expose a conspicuous fallback while compiling");
    release.set_value();
    pipeline.Wait();
    Require(pipeline.State() == AsyncPipelineState::Ready && pipeline.Resolve()->Id == 7,
            "background pipeline handle must atomically replace fallback after successful creation");
    const auto failedPipeline = pipelines.Request(
        "broken", [](const jobs::CancellationToken&) -> std::shared_ptr<TestPipeline> {
            throw std::runtime_error("shader permutation failed");
        });
    failedPipeline.Wait();
    Require(failedPipeline.State() == AsyncPipelineState::Failed &&
                failedPipeline.Resolve()->Id == -1 &&
                failedPipeline.Error().find("shader permutation failed") != std::string::npos,
            "failed background pipelines must retain the visible fallback and diagnostic");
}

void TestRenderDocCaptureFallback()
{
    debug::RenderDocCapture capture;
    debug::RenderDocCaptureConfig disabled;
    Require(!capture.Initialize(disabled) && !capture.IsRequested() && !capture.IsAvailable(),
            "disabled RenderDoc capture must have zero runtime dependency");

    debug::RenderDocCaptureConfig requested;
    requested.Enabled = true;
    requested.RequireAvailable = false;
    requested.FrameIndex = 7;
    requested.FixedDeltaSeconds = 1.0f / 120.0f;
    requested.LibraryPath = "definitely-missing-renderdoc-library";
    requested.CapturePathTemplate = "test-output/renderdoc/frame";
    const bool available = capture.Initialize(requested);
    Require(capture.IsRequested() && capture.Config().FrameIndex == 7 &&
                std::abs(capture.Config().FixedDeltaSeconds - 1.0f / 120.0f) < 1.0e-6f,
            "RenderDoc capture configuration must retain its deterministic frame schedule");
    if (!available)
    {
        Require(!capture.LastError().empty() && !capture.BeginFrame(7),
                "missing RenderDoc must produce a useful error without attempting capture");
    }
    capture.Shutdown();
}

void TestSharedRendererUtilities()
{
    const float adapted = AdaptExposure(1.0f, 0.09f, 0.18f, 0.25f, 4.0f,
                                        2.0f, 0.5f);
    Require(adapted > 1.0f && adapted < 2.0f,
            "exposure adaptation must move smoothly toward key/luminance");
    Require(std::abs(AdaptExposure(1.0f, 0.0f, 0.18f, 0.5f, 3.0f, 0.0f, 1.0f)
                     - 1.0f) < 1e-6f,
            "zero-speed exposure adaptation must preserve the current value");

    GpuTimingAccumulator timing;
    Require(!timing.Submit(100.0f, 2, 4), "GPU timing warm-up must not report");
    Require(!timing.Submit(100.0f, 2, 4), "GPU timing warm-up must cover all configured frames");
    Require(!timing.Submit(2.0f, 2, 4), "GPU timing must wait for the report interval");
    const auto summary = timing.Submit(4.0f, 2, 4);
    Require(summary.has_value(), "GPU timing must report on its interval");
    Require(std::abs(summary->AverageMilliseconds - 3.0f) < 1e-6f
                && summary->MinimumMilliseconds == 2.0f
                && summary->MaximumMilliseconds == 4.0f
                && summary->SampleCount == 2,
            "GPU timing aggregation must produce common min/average/max values");

    const std::filesystem::path shaderRoot = TEST_SHADER_DIR;
    const ShaderSourceDocument shaderDocument = LoadShaderSource(
        shaderRoot / "hlsl/opengl/common/fullscreen.vert.hlsl",
        {shaderRoot / "hlsl", shaderRoot});
    Require(shaderDocument.Dependencies.size() == 2,
            "shared HLSL loader must track includes for cache invalidation");
    Require(shaderDocument.Source.find("#include") == std::string::npos
                && shaderDocument.Source.find("EngineFullscreenNdc") != std::string::npos,
            "shared shader loader must expand backend-neutral HLSL includes");

    shader::HlslCompileRequest glRequest;
    glRequest.SourcePath = shaderRoot / "hlsl/opengl/common/fullscreen.vert.hlsl";
    glRequest.ShaderStage = shader::Stage::Vertex;
    glRequest.Target = shader::SpirvTarget::OpenGL46;
    glRequest.Optimize = false;
    const shader::HlslCompileResult glSpirv = shader::CompileHlslToSpirv(glRequest);
    Require(!glSpirv.Spirv.empty() && glSpirv.Spirv[0] == 0x07230203u &&
                glSpirv.TargetIdentity.find("opengl-4.6") != std::string::npos,
            "HLSL compiler must emit native OpenGL-compatible SPIR-V");

    shader::HlslCompileRequest vkRequest;
    vkRequest.SourcePath = shaderRoot / "hlsl/vulkan/post/fullscreen.vert.hlsl";
    vkRequest.ShaderStage = shader::Stage::Vertex;
    vkRequest.Target = shader::SpirvTarget::Vulkan13;
    vkRequest.Optimize = false;
    const shader::HlslCompileResult vkSpirv = shader::CompileHlslToSpirv(vkRequest);
    Require(!vkSpirv.Spirv.empty() && vkSpirv.Spirv[0] == 0x07230203u &&
                vkSpirv.TargetIdentity.find("vulkan-1.3") != std::string::npos,
            "HLSL compiler must emit Vulkan 1.3 SPIR-V");

    const ShaderSourceDocument glSky = LoadShaderSource(
        shaderRoot / "hlsl/opengl/environment/sky.frag.hlsl",
        {shaderRoot / "hlsl", shaderRoot});
    const ShaderSourceDocument vkSky = LoadShaderSource(
        shaderRoot / "hlsl/vulkan/environment/sky.frag.hlsl",
        {shaderRoot / "hlsl", shaderRoot});
    for (const ShaderSourceDocument* sky : {&glSky, &vkSky})
    {
        Require(sky->Source.find("EngineEvaluateProceduralStar") != std::string::npos &&
                    sky->Source.find("EngineStarCubeGrid") != std::string::npos,
                "both HLSL backend variants must retain the pole-safe procedural star field");
        Require(sky->Source.find("atan(nightDirection") == std::string::npos,
                "procedural stars must not use pole-singular equirectangular coordinates");
    }
}

void TestVisualRegressionComparison()
{
    using namespace engine::testing;
    VisualImage reference;
    reference.Width = 4;
    reference.Height = 2;
    reference.SourceWasHdr = true;
    reference.LinearRgb.assign(4 * 2 * 3, 0.25f);
    VisualImage candidate = reference;

    VisualTolerance tolerance;
    tolerance.Absolute = 0.01f;
    tolerance.Relative = 0.05f;
    tolerance.RelativeFloor = 0.05f;
    tolerance.MaximumFailingPixelFraction = 0.01f;
    tolerance.MaximumMeanNormalizedError = 0.20f;
    const VisualComparison identical = CompareVisualImages(reference, candidate, tolerance);
    Require(identical.Valid() && identical.Metrics.Passed &&
                identical.Metrics.MeanAbsoluteError == 0.0 &&
                identical.DifferenceRgba.size() == 4 * 2 * 4 &&
                identical.HeatmapRgba.size() == 4 * 2 * 4,
            "identical golden images must pass and generate complete diagnostics");

    candidate.LinearRgb[0] += 0.015f;
    const VisualComparison tolerated = CompareVisualImages(reference, candidate, tolerance);
    Require(tolerated.Metrics.Passed && tolerated.Metrics.FailingPixelCount == 0,
            "mixed absolute/relative HDR tolerance must accept small radiance drift");

    candidate = reference;
    candidate.LinearRgb[0] = candidate.LinearRgb[1] = candidate.LinearRgb[2] = 2.0f;
    const VisualComparison regression = CompareVisualImages(reference, candidate, tolerance);
    Require(!regression.Metrics.Passed && regression.Metrics.FailingPixelCount == 1 &&
                regression.Metrics.FailingPixelFraction > 0.10 &&
                regression.Metrics.MaximumAbsoluteError > 1.0,
            "a localized material/lighting regression must fail and be measured per pixel");

    const std::filesystem::path hdrPath = std::filesystem::temp_directory_path() /
                                          "source_like_visual_roundtrip.hdr";
    const std::filesystem::path diffPath = std::filesystem::temp_directory_path() /
                                           "source_like_visual_diff.png";
    std::string error;
    Require(WriteHdrImage(hdrPath, reference.Width, reference.Height,
                          reference.LinearRgb, &error),
            "visual regression must write linear Radiance HDR captures");
    VisualImage loaded;
    Require(LoadVisualImage(hdrPath, loaded, &error) && loaded.Valid() && loaded.SourceWasHdr,
            "visual regression must load its HDR captures in linear light");
    const VisualComparison roundTrip = CompareVisualImages(reference, loaded, tolerance);
    Require(roundTrip.Metrics.Passed,
            "Radiance encoding quantization must remain inside the configured HDR tolerance");
    Require(WriteRgbaImage(diffPath, regression.Width, regression.Height,
                           regression.DifferenceRgba, &error) &&
                std::filesystem::file_size(diffPath) > 0,
            "visual regression must emit a PNG difference artifact");
    std::error_code removeError;
    std::filesystem::remove(hdrPath, removeError);
    std::filesystem::remove(diffPath, removeError);
}

void TestGpuProfilerHistoryAndExport()
{
    using namespace engine::profiling;
    const std::filesystem::path reportPath = std::filesystem::temp_directory_path() /
                                             "source_like_gpu_profile_test.json";
    GpuProfiler& profiler = GpuProfiler::Get();
    profiler.SetEnabled(false);
    profiler.Reset();
    GpuProfilerConfig config;
    config.Enabled = true;
    config.RetainedFrames = 2;
    profiler.Configure(config);

    for (uint64_t frameIndex = 1; frameIndex <= 3; ++frameIndex)
    {
        GpuFrameProfile frame;
        frame.FrameIndex = frameIndex;
        frame.BackendName = "Test API";
        frame.AdapterName = "Test GPU";
        frame.TimingAvailable = true;
        frame.PipelineStatisticsAvailable = true;
        frame.MemoryBudgetAvailable = true;
        frame.Passes = {{"Shadows/Directional", static_cast<float>(frameIndex)},
                        {"Main HDR", static_cast<float>(frameIndex * 2)}};
        frame.Pipeline.DrawCalls = frameIndex * 10;
        frame.Pipeline.InputAssemblyPrimitives = frameIndex * 100;
        frame.Memory.UsageBytes = frameIndex * 1024;
        frame.Memory.BudgetBytes = 16 * 1024;
        frame.Memory.EngineOwnedBytes = frameIndex * 512;
        profiler.SubmitFrame(std::move(frame));
    }

    const GpuProfileSnapshot snapshot = profiler.Snapshot();
    Require(snapshot.Enabled && snapshot.Frames.size() == 2 &&
                snapshot.CompletedFrame == 3 && snapshot.FrameMilliseconds == 9.0f,
            "GPU profiler must retain bounded asynchronous frame history and total pass time");
    Require(snapshot.Passes.size() == 2 &&
                std::abs(snapshot.Passes[0].AverageMilliseconds - 2.5f) < 1.0e-6f &&
                snapshot.Passes[0].MinimumMilliseconds == 2.0f &&
                snapshot.Passes[0].MaximumMilliseconds == 3.0f,
            "GPU profiler must aggregate per-pass latest/average/minimum/maximum timings");
    Require(snapshot.Pipeline.DrawCalls == 30 &&
                snapshot.Pipeline.InputAssemblyPrimitives == 300 &&
                snapshot.Memory.PeakUsageBytes == 3072,
            "GPU profiler must publish latest pipeline statistics and VRAM peak usage");
    Require(profiler.WriteJsonReport(reportPath.string()),
            "GPU profiler must export a JSON report");
    std::ifstream report(reportPath);
    const std::string text((std::istreambuf_iterator<char>(report)),
                           std::istreambuf_iterator<char>());
    Require(text.find("Shadows/Directional") != std::string::npos &&
                text.find("\"pipelineStatisticsAvailable\": true") != std::string::npos &&
                text.find("\"vramBudgetBytes\"") != std::string::npos &&
                text.find("\"frames\"") != std::string::npos,
            "GPU JSON report must contain passes, pipeline counters, VRAM and history");
    std::error_code removeError;
    std::filesystem::remove(reportPath, removeError);

    profiler.SetEnabled(false);
    profiler.Reset();
    GpuFrameProfile ignored;
    ignored.FrameIndex = 99;
    profiler.SubmitFrame(std::move(ignored));
    Require(profiler.Snapshot().Frames.empty(),
            "disabled GPU profiling must discard samples with negligible overhead");
}

void TestCpuProfilerHierarchyAndTimeline()
{
    using namespace engine::profiling;
    CpuProfiler& profiler = CpuProfiler::Get();
    profiler.SetEnabled(false);
    profiler.Reset();
    CpuProfilerConfig config;
    config.Enabled = true;
    config.RetainedFrames = 8;
    config.MaximumEvents = 128;
    profiler.Configure(config);
    profiler.SetThreadName("Test main");

    profiler.BeginFrame();
    {
        CpuProfileScope outer("Outer", "Test");
        {
            CpuProfileScope inner("Inner", "Test");
        }
        std::thread worker([&profiler] {
            profiler.SetThreadName("Test worker");
            CpuProfileScope scope("Worker", "Test");
        });
        worker.join();
    }
    profiler.EndFrame();

    const CpuProfileSnapshot snapshot = profiler.Snapshot();
    Require(snapshot.Events.size() == 3,
            "CPU profiler must record nested and worker-thread zones");
    const auto findEvent = [&](const char* name) -> const CpuProfileEvent* {
        const auto found = std::find_if(snapshot.Events.begin(), snapshot.Events.end(),
            [&](const CpuProfileEvent& event) { return event.Name == name; });
        return found == snapshot.Events.end() ? nullptr : &*found;
    };
    const CpuProfileEvent* outer = findEvent("Outer");
    const CpuProfileEvent* inner = findEvent("Inner");
    const CpuProfileEvent* worker = findEvent("Worker");
    Require(outer && inner && worker, "CPU profiler must preserve zone names");
    Require(outer->ParentId == 0 && outer->Depth == 0,
            "top-level CPU zone must be a hierarchy root");
    Require(inner->ParentId == outer->Id && inner->Depth == 1,
            "nested CPU zone must reference its parent and depth");
    Require(worker->ThreadId != outer->ThreadId && worker->ParentId == 0,
            "worker timeline must be separate from the main-thread hierarchy");
    Require(snapshot.ThreadNames.size() == 2,
            "CPU profiler must retain names for each recorded thread timeline");
    Require(snapshot.Summaries.size() == 3,
            "CPU profiler must aggregate every distinct zone");

    const CpuProfileSnapshot frameSummary = profiler.FrameSummarySnapshot();
    Require(frameSummary.Events.empty() && frameSummary.Summaries.size() == 3 &&
                frameSummary.FirstFrame == 0 && frameSummary.LastFrame == 0,
            "runtime CPU overlay summary must avoid copying the retained trace");

    const std::filesystem::path tracePath =
        std::filesystem::temp_directory_path() / "source_like_cpu_profiler_test.json";
    Require(profiler.WriteChromeTrace(tracePath.string()),
            "CPU profiler must export a Chrome Trace JSON file");
    std::ifstream traceFile(tracePath);
    const std::string trace((std::istreambuf_iterator<char>(traceFile)),
                            std::istreambuf_iterator<char>());
    const bool traceValid = trace.find("\"traceEvents\"") != std::string::npos
                && trace.find("\"parent\"") != std::string::npos
                && trace.find("Test worker") != std::string::npos;
    Require(traceValid,
            "Chrome Trace export must contain events, hierarchy and thread metadata");
    traceFile.close();
    std::error_code removeError;
    std::filesystem::remove(tracePath, removeError);
    Require(!removeError, "CPU profiler trace test must clean up its temporary file");

    profiler.SetEnabled(false);
    profiler.Reset();
}

void TestCpuProfilerBounds()
{
    using namespace engine::profiling;
    CpuProfiler& profiler = CpuProfiler::Get();
    CpuProfilerConfig config;
    config.Enabled = true;
    config.RetainedFrames = 2;
    config.MaximumEvents = 3;
    profiler.Configure(config);
    for (int frame = 0; frame < 5; ++frame)
    {
        profiler.BeginFrame();
        {
            CpuProfileScope first("BoundedA", "Test");
        }
        {
            CpuProfileScope second("BoundedB", "Test");
        }
        profiler.EndFrame();
    }
    const CpuProfileSnapshot snapshot = profiler.Snapshot();
    Require(snapshot.Events.size() <= config.MaximumEvents,
            "CPU profiler event storage must respect its hard memory bound");
    Require(snapshot.FirstFrame >= 3,
            "CPU profiler must discard events outside its retained frame window");
    Require(snapshot.DroppedEvents > 0,
            "CPU profiler must report events removed by its hard bound");
    profiler.SetEnabled(false);
    profiler.Reset();

    {
        CpuProfileScope disabled("Disabled", "Test");
    }
    Require(profiler.Snapshot().Events.empty(),
            "disabled CPU profiling zones must not record events");
}

void TestDebugOverlayRasterAndState()
{
    debug::DebugOverlay overlay;
    debug::DebugOverlayMetrics metrics;
    metrics.BackendName = "Test backend";
    metrics.AntiAliasing = "TAA";
    metrics.ViewWidth = 1280;
    metrics.ViewHeight = 720;
    metrics.DeltaSeconds = 1.0f / 60.0f;
    metrics.ObjectCount = 4;
    metrics.TriangleCount = 128;
    metrics.Backend.GpuTimingAvailable = true;
    metrics.Backend.GpuFrameMilliseconds = 2.5f;
    profiling::MemoryProfileSnapshot memoryProfile;
    memoryProfile.Enabled = true;
    memoryProfile.CurrentBytes = 3 * 1024 * 1024;
    memoryProfile.PeakBytes = 5 * 1024 * 1024;
    memoryProfile.LiveAllocations = 17;
    memoryProfile.Tags.push_back({"Renderer", 2 * 1024 * 1024, 4 * 1024 * 1024,
                                  9, 20, 11, 6 * 1024 * 1024});
    memoryProfile.Frames.push_back({7, memoryProfile.CurrentBytes, memoryProfile.PeakBytes,
                                    memoryProfile.LiveAllocations, 64 * 1024, 24 * 1024, 4, 2});
    overlay.SetValue("Test group", "Custom", "42");
    overlay.SetVisible(true);
    overlay.Update(metrics, {}, memoryProfile);

    const debug::DebugOverlayImage& image = overlay.Image();
    Require(image.Pixels.size() == static_cast<size_t>(debug::DebugOverlayImage::Width)
            * debug::DebugOverlayImage::Height * 4,
            "debug overlay must produce a complete RGBA8 frame");
    Require(image.Revision == 1,
            "visible debug overlay updates must publish a new revision");
    Require(image.Pixels[4] == 65 && image.Pixels[5] == 220
            && image.Pixels[6] == 170 && image.Pixels[7] == 255,
            "debug overlay must rasterize its accent bar deterministically");
    const uint64_t visibleRevision = image.Revision;
    overlay.SetVisible(false);
    overlay.Update(metrics, {}, memoryProfile);
    Require(overlay.Image().Revision == visibleRevision,
            "hidden debug overlay must not spend work publishing frames");

    metrics.Capabilities.AdapterName = "Test GPU";
    metrics.Capabilities.DedicatedVideoMemoryBytes = 8ull * 1024 * 1024 * 1024;
    overlay.SetRuntimeMonitorsVisible(true);
    overlay.Update(metrics, {}, memoryProfile);
    Require(overlay.Image().Revision == visibleRevision + 1 &&
                overlay.Image().LayerCount == 2,
            "persistent runtime monitors must update while the detailed panel is hidden");
    Require(overlay.Image().Layers[0].Placement == debug::DebugOverlayPlacement::TopRight &&
                overlay.Image().Layers[1].Placement == debug::DebugOverlayPlacement::BottomRight,
            "CPU-memory and GPU monitors must use independent default screen placements");
    const debug::DebugOverlayDrawRect cpuRect = debug::ResolveDebugOverlayLayer(
        overlay.Image().Layers[0], 1280, 720);
    const debug::DebugOverlayDrawRect gpuRect = debug::ResolveDebugOverlayLayer(
        overlay.Image().Layers[1], 1280, 720);
    Require(cpuRect.X == 970 && cpuRect.Y == 10 && cpuRect.Width == 300 &&
                cpuRect.Height == 96 && gpuRect.X == 970 && gpuRect.Y == 562 &&
                gpuRect.Height == 148,
            "overlay placement must resolve top-right and bottom-right anchors in pixels");
    const debug::DebugOverlayDrawRect clippedCpuRect = debug::ResolveDebugOverlayLayer(
        overlay.Image().Layers[0], 200, 80);
    Require(clippedCpuRect.X == 0 && clippedCpuRect.Y == 10 &&
                clippedCpuRect.Width == 200 && clippedCpuRect.Height == 70,
            "overlay placement must clip safely when a resized viewport is smaller than a card");
    overlay.SetVisible(true);
    overlay.Update(metrics, {}, memoryProfile);
    Require(overlay.Image().LayerCount == 3 &&
                overlay.Image().Layers[0].Placement == debug::DebugOverlayPlacement::TopLeft,
            "detailed and persistent widgets must render as three independent layers");
    overlay.SetRuntimeMonitorPlacements(debug::DebugOverlayPlacement::BottomLeft,
                                        debug::DebugOverlayPlacement::TopRight);
    overlay.SetVisible(false);
    overlay.Update(metrics, {}, memoryProfile);
    Require(overlay.Image().Layers[0].Placement == debug::DebugOverlayPlacement::BottomLeft &&
                overlay.Image().Layers[1].Placement == debug::DebugOverlayPlacement::TopRight,
            "runtime monitor placement must be configurable in code");
    overlay.SetRuntimeMonitorsVisible(false);
    overlay.RemoveValue("Test group", "Custom");
    overlay.ClearValues();
}

void TestFrameDebuggerModelAndNavigation()
{
    constexpr uint64_t colorId = debug::FrameDebugId("test.color");
    constexpr uint64_t depthId = debug::FrameDebugId("test.depth");
    Require(colorId == debug::FrameDebugId("test.color") && colorId != depthId,
            "frame-debug resource ids must be deterministic and distinct");

    debug::FrameDebugSnapshot snapshot;
    snapshot.BackendName = "Test API";
    snapshot.FrameIndex = 42;
    debug::FrameDebugResource color;
    color.Id = colorId;
    color.Name = "HDR Color";
    color.Width = 1280;
    color.Height = 720;
    color.MipLevels = 2;
    color.Layers = 1;
    color.Format = "RGBA16F";
    color.EstimatedBytes = 1280ull * 720 * 8;
    debug::FrameDebugResource depth;
    depth.Id = depthId;
    depth.Name = "Shadow Array";
    depth.Kind = debug::FrameDebugResourceKind::Texture2DArray;
    depth.Visualization = debug::FrameDebugVisualization::Depth;
    depth.Width = 512;
    depth.Height = 512;
    depth.MipLevels = 1;
    depth.Layers = 3;
    depth.Format = "D32F";
    depth.EstimatedBytes = 512ull * 512 * 4 * 3;
    snapshot.Resources = {color, depth};
    snapshot.Passes.push_back({"Main HDR", {depthId}, {colorId}, 1.25f, true});
    Require(debug::FindFrameDebugResource(snapshot, depthId) != nullptr &&
                debug::FindFrameDebugResource(snapshot, 1234) == nullptr,
            "frame-debug snapshots must resolve stable resource references");

    debug::DebugOverlay overlay;
    overlay.SetFrameDebuggerVisible(true);
    overlay.SetFrameDebugSnapshot(snapshot);
    uint64_t requestId = 0;
    uint32_t mip = 0;
    uint32_t layer = 0;
    Require(overlay.GetFrameDebugCaptureRequest(requestId, mip, layer) &&
                requestId == colorId && mip == 0 && layer == 0,
            "opening the frame debugger must request the selected resource preview");
    debug::FrameDebugPreview preview;
    preview.ResourceId = colorId;
    preview.SourceWidth = 2;
    preview.SourceHeight = 1;
    preview.Width = 2;
    preview.Height = 1;
    preview.Pixels = {255, 0, 0, 255, 0, 255, 0, 255};
    overlay.SetFrameDebugPreview(std::move(preview));
    Require(!overlay.GetFrameDebugCaptureRequest(requestId, mip, layer),
            "a completed frozen preview must clear the capture request");

    debug::FrameDebugPreview pendingPreview;
    pendingPreview.ResourceId = colorId;
    pendingPreview.Pending = true;
    overlay.SetFrameDebugPreview(std::move(pendingPreview));
    Require(overlay.GetFrameDebugCaptureRequest(requestId, mip, layer) &&
                requestId == colorId,
            "an asynchronous preview must remain requested until its GPU readback completes");

    overlay.MoveFrameDebugResource(1);
    overlay.MoveFrameDebugLayer(1);
    Require(overlay.GetFrameDebugCaptureRequest(requestId, mip, layer) &&
                requestId == depthId && layer == 1,
            "resource and array-layer navigation must request the selected subresource");
    debug::FrameDebugPreview depthPreview;
    depthPreview.ResourceId = depthId;
    depthPreview.Width = depthPreview.Height = 1;
    depthPreview.Pixels = {128, 128, 128, 255};
    overlay.SetFrameDebugPreview(std::move(depthPreview));
    overlay.MoveFrameDebugResource(-1);
    overlay.MoveFrameDebugMip(1);
    Require(overlay.GetFrameDebugCaptureRequest(requestId, mip, layer) &&
                requestId == colorId && mip == 1 && layer == 0,
            "mip navigation must wrap safely within the selected texture");

    debug::DebugOverlayMetrics metrics;
    metrics.BackendName = "Test API";
    metrics.DeltaSeconds = 1.0f / 60.0f;
    overlay.Update(metrics, {});
    Require(overlay.Image().LayerCount == 1 &&
                overlay.Image().Layers[0].Width == debug::DebugOverlayImage::FrameDebuggerWidth &&
                overlay.Image().Layers[0].Height == debug::DebugOverlayImage::FrameDebuggerHeight,
            "frame debugger must publish one backend-neutral full inspection layer");
}

void TestMemoryProfilerDisabledMode()
{
    profiling::MemoryProfiler& profiler = profiling::MemoryProfiler::Get();
    profiler.SetEnabled(false);
    profiler.Reset();

    uint8_t* allocation = new uint8_t[4096];
    const profiling::MemoryProfileSnapshot snapshot = profiler.Snapshot(true);
    Require(!snapshot.Enabled && snapshot.CurrentBytes == 0 &&
                snapshot.LiveAllocations == 0 && snapshot.TotalAllocations == 0 &&
                snapshot.Leaks.empty(),
            "disabled memory profiling must not record global C++ allocations");
    delete[] allocation;

    // MSVC's max_align_t can be less strict than the alignment promised by
    // ordinary operator new. SIMD render payloads whose alignment equals the
    // default-new alignment therefore still use the non-aligned overload.
    // Exercise several vector reallocations so the global profiler allocator
    // must preserve that contract for every returned block.
    struct alignas(16) SimdPayload
    {
        std::array<uint64_t, 2> Words{};
    };
    std::vector<SimdPayload> simd;
    for (uint32_t index = 0; index < 2048; ++index)
    {
        simd.push_back({{index, index + 1u}});
        Require(reinterpret_cast<uintptr_t>(simd.data()) % alignof(SimdPayload) == 0,
                "global operator new must preserve default SIMD alignment");
    }
}

void TestMemoryProfilerTrackingAndExport()
{
    const std::filesystem::path reportPath = std::filesystem::temp_directory_path() /
                                             "source_like_memory_profile_test.json";
    profiling::MemoryProfiler& profiler = profiling::MemoryProfiler::Get();
    profiler.SetEnabled(false);
    profiler.Reset();
    profiling::MemoryProfilerConfig config;
    config.Enabled = true;
    config.RetainedFrames = 4;
    profiler.Configure(config);

    profiler.BeginFrame();
    void* small = profiler.Allocate(32, 16, "Test/Small");
    void* medium = profiler.Allocate(2048, 64, "Test/Medium");
    Require(small && medium && reinterpret_cast<uintptr_t>(medium) % 64 == 0,
            "memory profiler allocator must honor requested alignment");
    uint8_t* globalAllocation = nullptr;
    {
        profiling::MemoryTagScope tag("Test/GlobalNew");
        globalAllocation = new uint8_t[70'000];
    }

    const profiling::MemoryProfileSnapshot live = profiler.Snapshot(true);
    Require(live.LiveAllocations >= 3 && live.CurrentBytes >= 72'080 &&
                live.PeakBytes >= live.CurrentBytes,
            "memory profiler must report current bytes, live count and peak usage");
    const auto smallTag = std::find_if(live.Tags.begin(), live.Tags.end(),
                                       [](const profiling::MemoryTagStats& tag)
                                       { return tag.Name == "Test/Small"; });
    const auto globalTag = std::find_if(live.Tags.begin(), live.Tags.end(),
                                        [](const profiling::MemoryTagStats& tag)
                                        { return tag.Name == "Test/GlobalNew"; });
    Require(smallTag != live.Tags.end() && smallTag->CurrentBytes == 32 &&
                globalTag != live.Tags.end() && globalTag->CurrentBytes >= 70'000,
            "explicit and global new allocations must preserve subsystem tags");
    Require(live.SizeBuckets[0].LiveAllocations >= 1 &&
                live.SizeBuckets[3].LiveAllocations >= 1 &&
                live.SizeBuckets[6].LiveAllocations >= 1,
            "memory profiler must distribute allocations into size buckets");
    Require(std::any_of(live.Leaks.begin(), live.Leaks.end(),
                        [&](const profiling::MemoryLeakInfo& leak)
                        { return leak.Address == reinterpret_cast<uintptr_t>(medium) &&
                                 leak.Tag == "Test/Medium"; }),
            "live-allocation snapshot must expose leak address, size and tag");

    delete[] globalAllocation;
    profiler.Free(medium);
    profiler.Free(small);
    profiler.EndFrame();
    const profiling::MemoryProfileSnapshot released = profiler.Snapshot();
    Require(released.PeakBytes >= 72'080 && !released.Frames.empty() &&
                released.Frames.back().PeakBytes >= 72'080 &&
                released.Frames.back().AllocatedBytes >= 72'080 &&
                released.Frames.back().FreedBytes >= 72'080,
            "global/frame peak usage and per-frame allocation/free traffic must survive release");
    Require(profiler.WriteJsonReport(reportPath.string(), true),
            "memory profiler must export a JSON report");
    std::ifstream report(reportPath);
    const std::string reportText((std::istreambuf_iterator<char>(report)),
                                 std::istreambuf_iterator<char>());
    Require(reportText.find("\"sizeBuckets\"") != std::string::npos &&
                reportText.find("Test/Small") != std::string::npos &&
                reportText.find("\"trackingCapacity\"") != std::string::npos &&
                reportText.find("\"liveAllocations\"") != std::string::npos &&
                reportText.find("\"frames\"") != std::string::npos,
            "memory JSON report must contain tags, buckets and frame history");
    std::error_code removeError;
    std::filesystem::remove(reportPath, removeError);

    profiler.SetEnabled(false);
    profiler.Reset();
}

void TestMemoryProfilerMultithreaded()
{
    profiling::MemoryProfiler& profiler = profiling::MemoryProfiler::Get();
    profiler.SetEnabled(false);
    profiler.Reset();
    profiling::MemoryProfilerConfig config;
    config.Enabled = true;
    profiler.Configure(config);
    profiler.BeginFrame();
    constexpr int threadCount = 6;
    constexpr int allocationsPerThread = 1500;
    {
        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
        {
            workers.emplace_back(
                [&profiler, threadIndex]
                {
                    for (int allocation = 0; allocation < allocationsPerThread; ++allocation)
                    {
                        const size_t size = static_cast<size_t>(16 +
                            ((allocation + threadIndex) % 257));
                        void* memory = profiler.Allocate(size, 16, "Test/Threaded");
                        Require(memory != nullptr, "threaded profiler allocation must succeed");
                        profiler.Free(memory);
                    }
                });
        }
        for (std::thread& worker : workers)
            worker.join();
    }
    profiler.EndFrame();
    const profiling::MemoryProfileSnapshot snapshot = profiler.Snapshot();
    const auto threaded = std::find_if(snapshot.Tags.begin(), snapshot.Tags.end(),
                                        [](const profiling::MemoryTagStats& tag)
                                        { return tag.Name == "Test/Threaded"; });
    Require(threaded != snapshot.Tags.end() &&
                threaded->TotalAllocations == threadCount * allocationsPerThread &&
                threaded->LiveAllocations == 0 &&
                threaded->TotalFrees == threaded->TotalAllocations,
            "memory profiler bookkeeping must remain exact under concurrent allocation/free");
    profiler.SetEnabled(false);
    profiler.Reset();
}

void TestApplicationConfigRoundTrip()
{
    ApplicationDesc source;
    source.Window.title = "Quoted \"renderer\" sample";
    source.Window.width = 1920;
    source.Window.height = 1080;
    source.Window.api = GraphicsApi::Vulkan;
    source.Window.mode = WindowMode::BorderlessFullscreen;
    source.Window.monitor = 2;
    source.Window.centerOnMonitor = false;
    source.Window.cursor = CursorMode::Hidden;
    source.Renderer.Presentation = PresentMode::Adaptive;
    source.Renderer.MsaaSamples = 8;
    source.Renderer.MaxAnisotropy = 16.0f;
    source.Renderer.PreferDiscreteGpu = false;
    source.Renderer.PreferredAdapter = "RTX test";
    source.Renderer.EnableValidation = false;
    source.Renderer.EnableGpuTiming = false;
    source.Renderer.CapabilityPolicy = GpuCapabilityPolicy::Conservative;
    source.Renderer.EnableDriverWorkarounds = false;
    source.Renderer.EnablePipelineCache = false;
    source.Renderer.PipelineCacheDirectory = "Cache/Test Renderer";
    source.Renderer.EnableRenderGraph = false;
    source.Renderer.ValidateRenderGraph = false;
    source.Renderer.EnableTransientAliasing = false;
    source.Renderer.RenderGraphConfigPath = "Config/test-render-graph.ini";
    source.Unfocused = UnfocusedBehavior::RenderOnly;
    source.MaximumDeltaSeconds = 0.05f;
    source.FixedDeltaSeconds = 1.0f / 60.0f;
    source.FrameRateLimit = 144.0;
    source.CaptureCursorOnRightMouse = false;
    source.EnableRuntimeMonitors = false;

    const std::filesystem::path path = std::filesystem::temp_directory_path()
        / "source_like_application_config_test.cfg";
    std::string error;
    Require(SaveApplicationConfig(path, source, &error),
            "application config must save all shared settings");
    {
        std::ofstream append(path, std::ios::app);
        append << "future.unknown_key=kept_forward_compatible\n";
    }
    ApplicationDesc loaded;
    Require(LoadApplicationConfig(path, loaded, &error),
            "application config must load and ignore future unknown keys");
    Require(loaded.Window.title == source.Window.title
            && loaded.Window.width == 1920 && loaded.Window.height == 1080
            && loaded.Window.api == GraphicsApi::Vulkan
            && loaded.Window.mode == WindowMode::BorderlessFullscreen
            && loaded.Window.monitor == 2 && !loaded.Window.centerOnMonitor
            && loaded.Window.cursor == CursorMode::Hidden,
            "window configuration must survive a complete round trip");
    Require(loaded.Renderer.Presentation == PresentMode::Adaptive
            && loaded.Renderer.MsaaSamples == 8
            && loaded.Renderer.MaxAnisotropy == 16.0f
            && !loaded.Renderer.PreferDiscreteGpu
            && loaded.Renderer.PreferredAdapter == "RTX test"
            && !loaded.Renderer.EnableValidation && !loaded.Renderer.EnableGpuTiming
            && loaded.Renderer.CapabilityPolicy == GpuCapabilityPolicy::Conservative
            && !loaded.Renderer.EnableDriverWorkarounds
            && !loaded.Renderer.EnablePipelineCache
            && loaded.Renderer.PipelineCacheDirectory == "Cache/Test Renderer"
            && !loaded.Renderer.EnableRenderGraph
            && !loaded.Renderer.ValidateRenderGraph
            && !loaded.Renderer.EnableTransientAliasing
            && loaded.Renderer.RenderGraphConfigPath == "Config/test-render-graph.ini",
            "renderer configuration must survive a complete round trip");
    Require(loaded.Unfocused == UnfocusedBehavior::RenderOnly
            && std::abs(loaded.MaximumDeltaSeconds - 0.05f) < 1.0e-6f
            && std::abs(loaded.FixedDeltaSeconds - 1.0f / 60.0f) < 1.0e-6f
            && loaded.FrameRateLimit == 144.0
            && !loaded.CaptureCursorOnRightMouse
            && !loaded.EnableRuntimeMonitors,
            "application loop configuration must survive a complete round trip");

    {
        std::ofstream invalid(path, std::ios::trunc);
        invalid << "window.width=invalid\n";
    }
    const int oldWidth = loaded.Window.width;
    Require(!LoadApplicationConfig(path, loaded, &error) && !error.empty()
            && loaded.Window.width == oldWidth,
            "invalid configs must report a useful error without partially mutating state");
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
}

void TestShaderPermutationAndPipelineReport()
{
    const std::vector<ShaderDefine> first = {{"USE_FOG", "1"}, {"LIGHT_COUNT", "4"}};
    const std::vector<ShaderDefine> reordered = {{"LIGHT_COUNT", "4"}, {"USE_FOG", "1"}};
    const std::string source = "#version 460\nvoid main() {}\n";
    const std::string key = BuildShaderPermutationKey(source, first, "test-target");
    Require(key == BuildShaderPermutationKey(source, reordered, "test-target"),
            "shader permutation keys must be independent of define order");
    Require(key != BuildShaderPermutationKey(source, {{"LIGHT_COUNT", "8"}}, "test-target"),
            "shader permutation keys must change with define values");
    bool conflictingDefineRejected = false;
    try
    {
        CanonicalizeShaderDefines({{"USE_FOG", "0"}, {"USE_FOG", "1"}});
    }
    catch (const std::runtime_error&)
    {
        conflictingDefineRejected = true;
    }
    Require(conflictingDefineRejected,
            "conflicting shader permutation defines must be rejected");
    const std::string injected = ApplyShaderDefines(source, first);
    Require(injected.rfind("#version 460\n#define LIGHT_COUNT 4\n#define USE_FOG 1\n", 0) == 0,
            "shader defines must be canonical and injected after #version");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "source_like_pipeline_cache_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const uint8_t bytes[] = {1, 2, 3, 4};
    Require(WriteBinaryFileAtomically(directory / "shader.spv", bytes, sizeof(bytes)),
            "atomic shader-cache write must succeed");
    {
        std::ofstream keep(directory / "keep.txt");
        keep << "do not delete";
    }
    std::vector<uint8_t> loaded;
    Require(ReadBinaryFile(directory / "shader.spv", loaded) && loaded.size() == sizeof(bytes),
            "shader-cache binary must round-trip");
    Require(ClearRendererCacheFiles(directory),
            "cache cleanup must remove only known renderer-cache files");
    Require(std::filesystem::exists(directory / "keep.txt"),
            "cache cleanup must preserve unrelated files");

    PipelineStutterReport report;
    report.Backend = "Unit Test";
    report.Adapter = "Deterministic Adapter";
    report.ColdCacheRun = true;
    report.ApplicationStartupMilliseconds = 12.5;
    report.Cache.ShaderPermutationMisses = 2;
    report.FrameCpuMilliseconds = {1.0, 3.0, 2.0, 8.0};
    const std::filesystem::path reportPath = directory / "report.json";
    std::string error;
    Require(WritePipelineStutterReport(reportPath, report, &error),
            "pipeline stutter report export must succeed");
    std::ifstream reportFile(reportPath);
    const std::string json((std::istreambuf_iterator<char>(reportFile)),
                           std::istreambuf_iterator<char>());
    Require(json.find("\"frame_p95_ms\": 8") != std::string::npos &&
                json.find("\"shader_misses\": 2") != std::string::npos,
            "pipeline stutter report must contain percentile and cache statistics");
    std::filesystem::remove_all(directory, ignored);
}

void TestGpuCapabilityDatabaseAndFallbacks()
{
    Require(IdentifyGpuVendor(0x10de, "", "") == GpuVendor::Nvidia &&
                IdentifyGpuVendor(0, "ATI Technologies", "Radeon") == GpuVendor::AMD &&
                IdentifyGpuVendor(0, "Mesa", "llvmpipe") == GpuVendor::Mesa,
            "GPU vendor database must recognize PCI IDs and API identity strings");

    GpuRawCapabilities raw;
    raw.Device.Api = GpuApi::Vulkan;
    raw.Device.VendorId = 0x8086;
    raw.Device.Vendor = GpuVendor::Intel;
    raw.Device.DeviceName = "Synthetic GPU";
    raw.Device.DriverName = "Synthetic Driver";
    raw.MaxMsaaSamples = 4;
    raw.MaxAnisotropy = 8.0f;
    raw.TextureCompressionBc = true;
    raw.TextureCompressionBc7 = false;
    raw.GpuTimestamps = true;
    raw.PipelineStatistics = true;
    raw.ImmediatePresent = false;
    raw.DynamicRendering = true;
    raw.Synchronization2 = true;
    GpuCapabilityRequest request;
    request.MsaaSamples = 8;
    request.MaxAnisotropy = 16.0f;
    request.RequestImmediatePresent = true;
    const GpuCapabilityProfile profile = EvaluateGpuCapabilities(raw, request);
    Require(profile.SelectedMsaaSamples == 4 && profile.SelectedAnisotropy == 8.0f &&
                profile.Supports(GpuFeature::TextureCompressionBc) &&
                !profile.Supports(GpuFeature::TextureCompressionBc7) &&
                profile.Fallbacks.size() >= 3,
            "capability evaluation must clamp requests and record each safe fallback");

    TextureData compressed;
    compressed.Width = 4;
    compressed.Height = 4;
    compressed.Storage = TexturePixelStorage::Bc7Rgba;
    TextureMipData nativeMip;
    nativeMip.Width = 4;
    nativeMip.Height = 4;
    nativeMip.Pixels.resize(16);
    compressed.MipLevels.push_back(nativeMip);
    TextureMipData fallbackMip;
    fallbackMip.Width = 4;
    fallbackMip.Height = 4;
    fallbackMip.Pixels.resize(4 * 4 * 4, 127);
    compressed.Rgba8FallbackMipLevels.push_back(fallbackMip);
    TextureData scratch;
    std::string reason;
    const TextureData* selected = ResolveTextureForGpu(compressed, profile, scratch, &reason);
    Require(selected == &scratch && selected->Storage == TexturePixelStorage::Rgba8 &&
                selected->MipLevels.front().Pixels.size() == 64 && !reason.empty(),
            "unsupported block textures must select their cooked RGBA8 mip chain");

    raw.Device.DeviceName = "llvmpipe (LLVM software rasterizer)";
    raw.MaxMsaaSamples = 16;
    raw.MaxAnisotropy = 16.0f;
    request.MsaaSamples = 8;
    request.MaxAnisotropy = 16.0f;
    const GpuCapabilityProfile software = EvaluateGpuCapabilities(raw, request);
    Require(software.Device.SoftwareRenderer && software.Tier == GpuFeatureTier::Compatibility &&
                software.SelectedMsaaSamples == 1 && software.SelectedAnisotropy == 1.0f &&
                !software.Uses(GpuFeature::GpuTimestamps) && !software.AppliedRules.empty(),
            "software renderers must enter deterministic safe mode");

    const std::filesystem::path report = std::filesystem::temp_directory_path() /
        "source_like_gpu_capabilities_test.json";
    std::string error;
    Require(WriteGpuCapabilityReport(report, software, &error),
            "GPU capability report must be exportable");
    std::ifstream stream(report);
    const std::string json((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    Require(json.find("software-renderer-safe-mode") != std::string::npos &&
                json.find("\"features\"") != std::string::npos,
            "GPU capability report must include feature and applied-rule data");
    std::error_code removeError;
    std::filesystem::remove(report, removeError);
}

void TestRuntimePluginAndWorld()
{
    profiling::MemoryProfiler& memoryProfiler = profiling::MemoryProfiler::Get();
    memoryProfiler.SetEnabled(false);
    memoryProfiler.Reset();
    profiling::MemoryProfilerConfig memoryConfig;
    memoryConfig.Enabled = true;
    memoryProfiler.Configure(memoryConfig);
    runtime::ComponentRegistry components;
    plugin::PluginManager plugins(components);
    runtime::World world(components);

    std::vector<plugin::PluginEventType> events;
    plugins.AddEventListener([&](const plugin::PluginEvent& event) { events.push_back(event.Type); });
    Require(plugins.LoadPlugin(TEST_RUNTIME_PLUGIN_PATH, plugin::PluginLoadFlags::LoadCopy),
            "runtime plugin DLL must load through the ABI-checked entry point");
    Require(plugins.IsLoaded(kTestRuntimePluginName), "loaded plugin must be discoverable by name");
    Require(components.Contains(kTestRuntimeComponentName),
            "plugin startup must register its component factory");

    auto* service = static_cast<TestRuntimePluginService*>(
        plugins.Services().Find(kTestRuntimeServiceName, 1));
    Require(service && service->LoadCount == 1,
            "plugin startup must publish a versioned runtime service");

    const runtime::EntityId parent = world.CreateEntity("Parent");
    const runtime::EntityId child = world.CreateEntity("Child");
    runtime::Transform parentTransform;
    parentTransform.Position = {2.0f, 0.0f, 0.0f};
    runtime::Transform childTransform;
    childTransform.Position = {0.0f, 3.0f, 0.0f};
    Require(world.SetLocalTransform(parent, parentTransform) &&
                world.SetLocalTransform(child, childTransform) && world.SetParent(child, parent),
            "entity hierarchy must accept valid parent-child transforms");
    Require(!world.SetParent(parent, child), "entity hierarchy must reject cycles");
    const glm::vec3 childWorld = glm::vec3(world.GetWorldTransform(child)[3]);
    Require(glm::all(glm::epsilonEqual(childWorld, glm::vec3(2.0f, 3.0f, 0.0f), 1.0e-5f)),
            "child world transform must concatenate its parent transform");

    std::string error;
    Require(world.AddComponent(child, kTestRuntimeComponentName, &error),
            "plugin component factory must attach behavior to an entity");
    const profiling::MemoryProfileSnapshot pluginMemory = memoryProfiler.Snapshot();
    Require(std::any_of(pluginMemory.Tags.begin(), pluginMemory.Tags.end(),
                        [](const profiling::MemoryTagStats& tag)
                        { return tag.Name == "Plugin/TestRuntimePlugin" &&
                                 tag.LiveAllocations > 0; }),
            "plugin component allocations must use the tagged host allocator");
    Require(service->ActivateCount == 1, "active entities must activate newly attached components");
    world.Update(0.25f);
    Require(service->StartCount == 1 && service->UpdateCount == 1 &&
                std::abs(service->AccumulatedSeconds - 0.25f) < 1.0e-6f,
            "world update must run component start exactly once and update every frame");

    Require(world.SetActive(parent, false) && !world.IsActive(child),
            "inactive state must propagate through the entity hierarchy");
    Require(service->DeactivateCount == 1,
            "hierarchy deactivation must call the component lifecycle hook");
    world.Update(0.25f);
    Require(service->UpdateCount == 1, "inactive components must not update");
    Require(world.SetActive(parent, true) && world.IsActive(child),
            "reactivating a parent must reactivate its descendants");
    Require(service->ActivateCount == 2,
            "hierarchy reactivation must call the component lifecycle hook");
    world.Update(0.1f);
    Require(service->StartCount == 2 && service->UpdateCount == 2,
            "reactivated components must restart before their next update");

    plugin::PluginApplicationEvent applicationEvent;
    applicationEvent.Type = plugin::PluginApplicationEventType::BeforeRender;
    plugins.BroadcastApplicationEvent(applicationEvent);
    Require(service->ApplicationEventCount == 1,
            "loaded plugins must receive application lifecycle events");

    Require(!plugins.UnloadPlugin(kTestRuntimePluginName) &&
                plugins.LastError().find("component instance") != std::string::npos,
            "plugin unload must be blocked while callbacks are owned by live components");
    Require(world.DestroyEntity(parent) && !world.IsAlive(parent) && !world.IsAlive(child),
            "destroying an entity must recursively destroy children and their components");
    Require(service->DeactivateCount == 2,
            "entity destruction must deactivate live components before deletion");
    Require(plugins.UnloadPlugin(kTestRuntimePluginName),
            "plugin must unload after all of its component instances are gone");
    Require(!components.Contains(kTestRuntimeComponentName) &&
                plugins.Services().Find(kTestRuntimeServiceName) == nullptr,
            "plugin unload must remove all owned factories and services");
    Require(std::find(events.begin(), events.end(), plugin::PluginEventType::AfterLoading) !=
                    events.end() &&
                std::find(events.begin(), events.end(), plugin::PluginEventType::AfterUnloading) !=
                    events.end(),
            "plugin manager must publish symmetric load and unload events");
    memoryProfiler.SetEnabled(false);
}

void TestRenderGraphCompilationAndAliasing()
{
    using namespace rendergraph;
    RenderGraph graph;
    graph.Reset();
    ResourceDesc texture;
    texture.Name = "test.producer";
    texture.Format = "RGBA16F";
    texture.Width = 128;
    texture.Height = 128;
    texture.BytesPerPixel = 8;
    const ResourceHandle produced = graph.Create(texture);
    std::vector<int> execution;
    graph.AddPass("consumer", "Consumer")
        .Read(produced)
        .SetExecute([&] { execution.push_back(2); });
    graph.AddPass("producer", "Producer")
        .Write(produced, ResourceState::ColorAttachment, PipelineStage::ColorOutput)
        .SetExecute([&] { execution.push_back(1); });
    std::string error;
    Require(graph.Compile(&error), "render graph must sort a consumer declared before its producer");
    Require(graph.Passes().size() == 2 && graph.Passes()[0].Id == "producer" &&
                graph.Passes()[1].Id == "consumer",
            "render graph topological order must follow resource dependencies");
    graph.ExecutePhase(PassPhase::Render);
    Require(execution == std::vector<int>({1, 2}),
            "render graph execution must use compiled dependency order");
    Require(graph.Stats().BarrierCount >= 2,
            "render graph must synthesize write/read resource barriers");

    graph.Reset();
    texture.Name = "temporary.a";
    const ResourceHandle first = graph.Create(texture);
    texture.Name = "temporary.b";
    const ResourceHandle second = graph.Create(texture);
    graph.AddPass("write_a", "Write A")
        .Write(first, ResourceState::ColorAttachment, PipelineStage::ColorOutput)
        .SetExecute([] {});
    graph.AddPass("read_a", "Read A").Read(first).SetExecute([] {});
    graph.AddPass("write_b", "Write B")
        .Write(second, ResourceState::ColorAttachment, PipelineStage::ColorOutput)
        .SetExecute([] {});
    graph.AddPass("read_b", "Read B").Read(second).SetExecute([] {});
    Require(graph.Compile(&error), "non-overlapping transient graph must compile");
    Require(graph.Resources()[first.Index].AliasSlot == graph.Resources()[second.Index].AliasSlot &&
                graph.Stats().AliasedResourceCount == 1 && graph.Stats().AliasedBytesSaved > 0,
            "compatible non-overlapping render targets must reuse one physical alias slot");

    debug::FrameDebugSnapshot snapshot;
    snapshot.Resources.push_back({debug::FrameDebugId("temporary.a")});
    snapshot.Resources.push_back({debug::FrameDebugId("temporary.b")});
    graph.PopulateFrameDebugSnapshot(snapshot);
    Require(snapshot.Passes.size() == 4 && snapshot.GraphAliasedBytesSaved > 0 &&
                snapshot.Resources[0].Transient &&
                snapshot.Resources[0].LastUsePass < snapshot.Resources[1].FirstUsePass,
            "frame debugger must expose pass order, lifetimes, barriers and alias savings");

    graph.Reset();
    graph.AddPass("a", "A").After("b").SetExecute([] {});
    graph.AddPass("b", "B").After("a").SetExecute([] {});
    Require(!graph.Compile(&error) && error.find("cycle") != std::string::npos,
            "render graph must reject dependency cycles with a useful error");

    graph.Reset();
    texture.Name = "uninitialized";
    const ResourceHandle uninitialized = graph.Create(texture);
    graph.AddPass("bad_read", "Bad read").Read(uninitialized).SetExecute([] {});
    Require(!graph.Compile(&error) && error.find("read before") != std::string::npos,
            "render graph must reject a transient read without a producer");

    graph.Reset();
    texture.Name = "phase_order";
    const ResourceHandle phaseResource = graph.Create(texture);
    graph.AddPass("render_producer", "Render producer")
        .Write(phaseResource, ResourceState::ColorAttachment, PipelineStage::ColorOutput)
        .SetExecute([] {});
    graph.AddPass("late_prepare", "Late prepare")
        .SetPhase(PassPhase::Prepare).Read(phaseResource).SetExecute([] {});
    Require(!graph.Compile(&error) && error.find("prepare pass") != std::string::npos,
            "render graph must reject a prepare phase that depends on rendering");
}

void TestRenderGraphConfigAndScale()
{
    using namespace rendergraph;
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        "source_like_render_graph_test.ini";
    Config saved;
    saved.Validation = false;
    saved.TransientAliasing = false;
    saved.Passes.push_back({"debug_ui", false, {"post_process"}});
    std::string error;
    Require(SaveConfig(path, saved, &error), "render graph config must be exportable");
    Config loaded;
    Require(LoadConfig(path, loaded, &error) && !loaded.Validation &&
                !loaded.TransientAliasing && loaded.Passes.size() == 1 &&
                loaded.Passes[0].Enabled == false && loaded.Passes[0].After.size() == 1,
            "render graph config must round-trip pass enable and ordering overrides");
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    RenderGraph graph;
    graph.Reset();
    constexpr uint32_t passCount = 512;
    std::vector<ResourceHandle> resources;
    resources.reserve(passCount);
    for (uint32_t index = 0; index < passCount; ++index)
    {
        ResourceDesc desc;
        desc.Name = "scale." + std::to_string(index);
        desc.Type = ResourceType::Buffer;
        desc.Width = 256;
        resources.push_back(graph.Create(std::move(desc)));
    }
    for (uint32_t index = 0; index < passCount; ++index)
    {
        auto pass = graph.AddPass("scale_" + std::to_string(index),
                                  "Scale " + std::to_string(index));
        if (index > 0)
            pass.Read(resources[index - 1], ResourceState::ShaderRead, PipelineStage::Compute);
        pass.Write(resources[index], ResourceState::ShaderWrite, PipelineStage::Compute)
            .SetExecute([] {});
    }
    const auto begin = std::chrono::steady_clock::now();
    Require(graph.Compile(&error), "large render graph must compile");
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    Require(graph.Passes().size() == passCount && milliseconds < 1000.0,
            "512-pass render graph compile must remain within the regression budget");
}

void TestRendererHiZFrameGraph()
{
    using namespace rendergraph;
    RenderGraph graph;
    Config config;
    config.Validation = true;
    RendererFrameGraphFeatures features;
    features.Width = 1920;
    features.Height = 1080;
    features.MsaaSamples = 4;
    features.OcclusionCulling = true;
    features.OcclusionCandidateCount = 4096;
    features.HiZMipLevels = 11;
    RendererFrameGraphCallbacks callbacks;
    for (auto& callback : callbacks.Passes)
        callback = [] {};
    std::string error;
    Require(BuildRendererFrameGraph(graph, config, features,
                                    std::move(callbacks), &error),
            "renderer frame graph must compile with GPU Hi-Z enabled");
    const auto passIndex = [&](std::string_view id) {
        const auto found = std::find_if(graph.Passes().begin(), graph.Passes().end(),
            [id](const CompiledPass& pass) { return pass.Id == id; });
        return found == graph.Passes().end()
            ? graph.Passes().size()
            : static_cast<size_t>(std::distance(graph.Passes().begin(), found));
    };
    const size_t cull = passIndex("occlusion_cull");
    const size_t main = passIndex("main_hdr");
    const size_t build = passIndex("hiz_build");
    const size_t post = passIndex("post_process");
    Require(cull < main && main < build && build < post,
            "Hi-Z frame graph must query previous depth before drawing and build new depth before post");
    const auto hiz = std::find_if(graph.Resources().begin(), graph.Resources().end(),
        [](const CompiledResource& resource) {
            return resource.Description.Name == "visibility.hiz";
        });
    const auto results = std::find_if(graph.Resources().begin(), graph.Resources().end(),
        [](const CompiledResource& resource) {
            return resource.Description.Name == "visibility.results";
        });
    Require(hiz != graph.Resources().end() &&
                hiz->Description.MipLevels == features.HiZMipLevels &&
                results != graph.Resources().end() &&
                results->Description.Type == ResourceType::Buffer,
            "Hi-Z graph must expose the mip pyramid and visibility-result buffer to diagnostics");
}

void TestLogSinkApi()
{
    std::mutex mutex;
    std::vector<log::Record> records;
    const log::SinkId sink = log::AddSink([&](const log::Record& record) {
        std::scoped_lock lock(mutex);
        records.push_back(record);
        // Recursive logging must remain safe and must not recursively invoke
        // the same sink forever.
        if (record.Message == "outer")
            log::Info("Test", "inner");
    });
    Require(sink != 0, "log sink registration must return a stable token");
    log::Warn("Editor", "console message");
    log::Info("Test", "outer");
    Require(records.size() == 2 && records[0].Category == "Editor" &&
                records[0].Severity == log::Level::Warn &&
                records[0].Message == "console message" &&
                records[1].Sequence > records[0].Sequence,
            "log sinks must receive categorized, ordered records exactly once");
    Require(log::RemoveSink(sink), "registered log sink must be removable");
    log::Info("Editor", "after removal");
    Require(records.size() == 2, "removed log sink must not receive records");
}

void TestWorldReflectionBridgeAndSerialization()
{
    runtime::ComponentRegistry registry;
    std::string error;
    Require(runtime::RegisterBuiltinRenderComponents(registry, &error),
            "built-in render components must register with reflection metadata");
    const auto pointProperties = registry.Properties(
        std::string(runtime::kPointLightComponent));
    const auto intensityMetadata = std::find_if(
        pointProperties.begin(), pointProperties.end(),
        [](const runtime::PropertyMetadata& property) {
            return property.Name == "Intensity";
        });
    Require(intensityMetadata != pointProperties.end() &&
                runtime::HasFlag(intensityMetadata->Flags,
                                 runtime::PropertyFlags::HasRange) &&
                intensityMetadata->Maximum > intensityMetadata->Minimum,
            "numeric inspector metadata must distinguish constrained ranges");
    runtime::World world(registry);
    const assets::AssetGuid rootGuid{0x101u, 0x201u};
    const assets::AssetGuid meshGuid{0x102u, 0x202u};
    const runtime::EntityId root = world.CreateEntityWithGuid(rootGuid, "Root");
    const runtime::EntityId meshEntity = world.CreateEntityWithGuid(meshGuid, "Mesh");
    Require(root != runtime::kInvalidEntity && meshEntity != runtime::kInvalidEntity &&
                world.SetParent(meshEntity, root),
            "world must create stable-GUID parent-child entities");
    world.SetLocalTransform(root, {{2.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
    world.SetLocalTransform(meshEntity, {{0.0f, 0.0f, -3.0f}, {}, {1.0f, 1.0f, 1.0f}});
    Require(world.AddComponent(meshEntity, std::string(runtime::kMeshRendererComponent), &error),
            "mesh renderer component must be constructible");
    auto* meshRenderer = world.GetComponent<runtime::MeshRendererComponent>(
        meshEntity, std::string(runtime::kMeshRendererComponent));
    meshRenderer->Mesh = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    Require(world.SetComponentProperty(meshEntity,
                std::string(runtime::kMeshRendererComponent), "Metallic", 0.72, &error),
            "reflected component property must be type-safely writable");

    const runtime::EntityId lightEntity = world.CreateEntity("Point Light");
    world.SetLocalTransform(lightEntity, {{1.0f, 3.0f, 2.0f}, {}, {1.0f, 1.0f, 1.0f}});
    Require(world.AddComponent(lightEntity, std::string(runtime::kPointLightComponent), &error) &&
                world.SetComponentProperty(lightEntity,
                    std::string(runtime::kPointLightComponent), "Intensity", 77.0, &error),
            "point-light properties must be reflected");

    const runtime::EntityId cameraEntity = world.CreateEntity("Camera");
    Require(world.AddComponent(cameraEntity, std::string(runtime::kCameraComponent), &error),
            "camera component must be constructible");
    world.GetComponent<runtime::CameraComponent>(
        cameraEntity, std::string(runtime::kCameraComponent))->Primary = true;

    Scene scene;
    Camera camera;
    runtime::WorldRenderBridge bridge;
    const runtime::WorldRenderSyncResult sync = bridge.Synchronize(world, scene, &camera);
    Require(sync.MeshRenderers == 1 && sync.PointLights == 1 &&
                sync.ActiveCamera == cameraEntity && scene.Instances().size() == 1 &&
                scene.Instances()[0].SourceEntity == meshEntity &&
                std::abs(scene.Instances()[0].Transform[3].x - 2.0f) < 0.001f &&
                std::abs(scene.PointLights()[0].Intensity - 77.0f) < 0.001f,
            "World-to-Scene bridge must immediately propagate hierarchy and render components");
    Require(world.SetComponentEnabled(lightEntity,
                std::string(runtime::kPointLightComponent), false),
            "render components must support editor enable/disable toggles");
    bridge.Synchronize(world, scene, &camera);
    Require(scene.PointLights().empty(),
            "disabled render components must disappear from the Scene immediately");
    world.SetComponentEnabled(lightEntity, std::string(runtime::kPointLightComponent), true);

    assets::SceneAssetData serialized;
    Require(assets::SerializeWorld(world, serialized, cameraEntity, &error),
            "complete World must serialize through reflected properties");
    runtime::World loaded(registry);
    runtime::EntityId loadedCamera = runtime::kInvalidEntity;
    Require(assets::DeserializeWorld(serialized, loaded, &loadedCamera, &error),
            "serialized World must deserialize without data loss");
    const runtime::EntityId loadedMesh = loaded.FindEntity(meshGuid);
    Require(loadedMesh != runtime::kInvalidEntity &&
                loaded.GetGuid(loaded.GetParent(loadedMesh)) == rootGuid &&
                loaded.GetGuid(loadedCamera) == world.GetGuid(cameraEntity),
            "scene round-trip must preserve stable GUID hierarchy and active camera");
    runtime::PropertyValue metallic;
    Require(loaded.GetComponentProperty(loadedMesh,
                std::string(runtime::kMeshRendererComponent), "Metallic", metallic, &error) &&
                std::abs(std::get<double>(metallic) - 0.72) < 0.001,
            "scene round-trip must preserve reflected component values");

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "engine_world_roundtrip.sla-scene";
    std::filesystem::remove(scenePath);
    Require(assets::SaveWorldScene(scenePath, world, cameraEntity, &error),
            "World scene must save to the versioned scene descriptor format");
    runtime::World fileLoaded(registry);
    runtime::EntityId fileCamera = runtime::kInvalidEntity;
    Require(assets::LoadWorldScene(scenePath, fileLoaded, &fileCamera, &error) &&
                fileLoaded.FindEntity(meshGuid) != runtime::kInvalidEntity &&
                fileLoaded.GetGuid(fileCamera) == world.GetGuid(cameraEntity),
            "World scene file load must preserve entities and active camera");
    std::filesystem::remove(scenePath);
}

void TestPickingSelectionAndDebugDraw()
{
    Scene scene;
    Material material;
    auto cube = std::make_shared<MeshData>(primitives::MakeCube(1.0f));
    scene.AddInstance(cube, material, glm::mat4(1.0f));
    scene.Instances().back().SourceEntity = 42;
    scene.Visibility.GpuOcclusionCulling = false;
    Camera camera;
    camera.Position = {0.0f, 0.0f, 6.0f};
    camera.Yaw = -90.0f;
    camera.Pitch = 0.0f;
    const PickingResult hit = PickScene(scene, camera, 400.0f, 300.0f, 800, 600);
    Require(hit.Hit && hit.Entity == 42 && hit.Distance > 0.0f,
            "viewport center picking ray must resolve the source entity AABB");
    ApplyPickingSelection(scene, hit);
    scene.DebugDraw().Grid(2.0f, 1.0f);
    scene.DebugDraw().Aabb(glm::vec3(-1.0f), glm::vec3(1.0f),
                           glm::vec3(0.0f, 1.0f, 0.0f));
    scene.DebugDraw().Icon(DebugIconType::Camera, {0.0f, 2.0f, 0.0f});
    SceneRenderer renderer;
    const RenderFrameData& frame = renderer.PrepareFrame(scene, camera, 800, 600);
    Require(frame.DebugLines.size() >= 40,
            "debug draw must resolve grid, AABB, icon and selection highlight into render lines");
    const PickingResult miss = PickScene(scene, camera, 0.0f, 0.0f, 800, 600);
    ApplyPickingSelection(scene, miss);
    Require(scene.SelectedEntity == 0,
            "empty viewport clicks must clear selection");
}

void TestRuntimePluginDependencies()
{
    runtime::ComponentRegistry components;
    plugin::PluginManager plugins(components);
    Require(plugins.LoadPlugin(TEST_DEPENDENT_PLUGIN_PATH, plugin::PluginLoadFlags::LoadCopy),
            "loading a plugin must recursively load declared dependencies first");
    const auto loaded = plugins.LoadedPlugins();
    Require(loaded.size() == 2 && loaded[0].Name == kTestRuntimePluginName &&
                loaded[1].Name == kTestDependentPluginName,
            "plugin dependency startup order must be provider before consumer");
    Require(plugins.Services().Find(kTestRuntimeServiceName, 1) != nullptr &&
                plugins.Services().Find(kTestDependentServiceName, 1) != nullptr,
            "dependent plugin startup must be able to query provider services");
    Require(!plugins.UnloadPlugin(kTestRuntimePluginName) &&
                plugins.LastError().find("depends on it") != std::string::npos,
            "a provider plugin must not unload while a loaded plugin depends on it");
    Require(plugins.UnloadAll(), "bulk plugin shutdown must use reverse dependency order");
    Require(plugins.LoadedPlugins().empty(), "bulk plugin shutdown must unload every module");
}

} // namespace

int main()
{
    TestSplitDistribution();
    TestCoverageAndResolution();
    TestTexelStabilization();
    TestProceduralLightCookie();
    TestHdrEnvironmentLoadingAndCache();
    TestDayNightTransitions();
    TestColorGradingLut();
    TestTemporalSamplingAndCuts();
    TestVulkanMaterialPacking();
    TestSharedSceneRendererFrame();
    TestVisibilityCulling();
    TestScreenSpaceMeshLods();
    TestTemporalHiZOcclusionPolicy();
    TestGpuInstancingAndHism();
    TestGeometryCombiningAndBatching();
    TestTaskSystemScheduling();
    TestAsyncResourceLoadingAndStreamingBudgets();
    TestAsyncRenderResourceLifetime();
    TestRenderDocCaptureFallback();
    TestSharedRendererUtilities();
    TestVisualRegressionComparison();
    TestGpuProfilerHistoryAndExport();
    TestCpuProfilerHierarchyAndTimeline();
    TestCpuProfilerBounds();
    TestDebugOverlayRasterAndState();
    TestFrameDebuggerModelAndNavigation();
    TestMemoryProfilerDisabledMode();
    TestMemoryProfilerTrackingAndExport();
    TestMemoryProfilerMultithreaded();
    TestGpuCapabilityDatabaseAndFallbacks();
    TestApplicationConfigRoundTrip();
    TestShaderPermutationAndPipelineReport();
    TestRenderGraphCompilationAndAliasing();
    TestRenderGraphConfigAndScale();
    TestRendererHiZFrameGraph();
    TestLogSinkApi();
    TestWorldReflectionBridgeAndSerialization();
    TestPickingSelectionAndDebugDraw();
    TestRuntimePluginAndWorld();
    TestRuntimePluginDependencies();
    std::puts("Renderer tests passed");
    return EXIT_SUCCESS;
}
