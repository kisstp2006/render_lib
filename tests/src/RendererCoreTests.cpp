#include "engine/core/Camera.h"
#include "engine/core/ApplicationConfig.h"
#include "engine/debug/DebugOverlay.h"
#include "engine/render/CascadedShadows.h"
#include "engine/render/Exposure.h"
#include "engine/render/GpuTiming.h"
#include "engine/render/SceneRenderer.h"
#include "engine/render/ShaderSource.h"
#include "engine/render/TemporalAA.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/plugin/PluginManager.h"
#include "engine/runtime/World.h"
#include "engine/asset/ColorGrading.h"
#include "engine/backend/vk/VulkanShaderInterop.h"
#include "engine/scene/Texture.h"
#include "engine/scene/Environment.h"
#include "engine/scene/Scene.h"
#include "TestRuntimePluginShared.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>

#include <glm/gtc/epsilon.hpp>

using namespace engine;

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

    Camera camera;
    camera.Position = {2.0f, 3.0f, 7.0f};
    SceneRenderer renderer;
    const RenderFrameData first = renderer.PrepareFrame(scene, camera, 1600, 900);
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

    const RenderFrameData second = renderer.PrepareFrame(scene, camera, 800, 800);
    Require(second.FrameIndex == first.FrameIndex + 1,
            "shared scene renderer frame index must advance exactly once per application frame");
    Require(std::abs(second.AspectRatio - 1.0f) < 1.0e-6f,
            "shared frame must react to viewport resize independently of the backend");
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
    const ShaderSourceDocument shader = LoadShaderSource(
        shaderRoot / "vk/lighting/pbr.frag", {shaderRoot / "vk", shaderRoot});
    Require(shader.Dependencies.size() >= 3,
            "shared shader loader must track transitive includes for cache invalidation");
    Require(shader.Source.find("#include") == std::string::npos
                && shader.Source.find("D_GGX") != std::string::npos,
            "shared shader loader must expand backend and common GLSL includes");
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
    overlay.SetValue("Test group", "Custom", "42");
    overlay.SetVisible(true);
    overlay.Update(metrics, {});

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
    overlay.Update(metrics, {});
    Require(overlay.Image().Revision == visibleRevision,
            "hidden debug overlay must not spend work publishing frames");
    overlay.RemoveValue("Test group", "Custom");
    overlay.ClearValues();
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
    source.Unfocused = UnfocusedBehavior::RenderOnly;
    source.MaximumDeltaSeconds = 0.05f;
    source.FrameRateLimit = 144.0;
    source.CaptureCursorOnRightMouse = false;

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
            && !loaded.Renderer.EnableValidation && !loaded.Renderer.EnableGpuTiming,
            "renderer configuration must survive a complete round trip");
    Require(loaded.Unfocused == UnfocusedBehavior::RenderOnly
            && std::abs(loaded.MaximumDeltaSeconds - 0.05f) < 1.0e-6f
            && loaded.FrameRateLimit == 144.0
            && !loaded.CaptureCursorOnRightMouse,
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

void TestRuntimePluginAndWorld()
{
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
    TestSharedRendererUtilities();
    TestCpuProfilerHierarchyAndTimeline();
    TestCpuProfilerBounds();
    TestDebugOverlayRasterAndState();
    TestApplicationConfigRoundTrip();
    TestRuntimePluginAndWorld();
    TestRuntimePluginDependencies();
    std::puts("Renderer tests passed");
    return EXIT_SUCCESS;
}
