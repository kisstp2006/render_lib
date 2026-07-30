#include "SandboxApp.h"

#include "SampleAssetPipeline.h"
#include "SandboxControls.h"
#include "SandboxScene.h"

#include "engine/core/Application.h"
#include "engine/core/ApplicationConfig.h"
#include "engine/core/Log.h"
#include "engine/diagnostics/StabilityStress.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/profiling/GpuProfiler.h"
#include "engine/profiling/MemoryProfiler.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace engine;

namespace {

void ApplyPreset(SandboxPreset preset, WindowDesc& window, SandboxSceneConfig& config)
{
    const std::string assets = ENGINE_ASSET_DIR;
    switch (preset)
    {
    case SandboxPreset::Generic:
        window.title = "Source-Like PBR Sandbox";
        break;
    case SandboxPreset::Materials:
        window.title = "Renderer Sample - PBR Materials";
        break;
    case SandboxPreset::Gltf:
        window.title = "Renderer Sample - glTF Water Bottle";
        config.SampleGltf = true;
        config.GltfPath = assets + "/WaterBottle.glb";
        break;
    case SandboxPreset::Lights:
        window.title = "Renderer Sample - Local Lights";
        config.LocalLightShowcase = true;
        break;
    case SandboxPreset::HdriStudio:
        window.title = "Renderer Sample - HDRI Studio";
        config.HdriStudio = true;
        config.HdriPath = assets + "/studio_small_09_1k.hdr";
        break;
    case SandboxPreset::DayNight:
        window.title = "Renderer Sample - Day and Night";
        config.DayNightShowcase = true;
        config.SampleGltf = true;
        config.GltfPath = assets + "/WaterBottle.glb";
        break;
    case SandboxPreset::PostProcessing:
        window.title = "Renderer Sample - Post Processing";
        config.PostShowcase = true;
        config.CinematicLut = true;
        break;
    case SandboxPreset::Stability:
        window.title = "Renderer Sample - Stability Stress";
        window.width = 1280;
        window.height = 720;
        config.SampleGltf = true;
        config.GltfPath = assets + "/WaterBottle.glb";
        config.HdriPath = assets + "/studio_small_09_1k.hdr";
        break;
    case SandboxPreset::Visibility:
        window.title = "Renderer Sample - Visibility Culling";
        config.VisibilityShowcase = true;
        break;
    }
}

const char* PresetName(SandboxPreset preset)
{
    switch (preset)
    {
    case SandboxPreset::Materials: return "PBR MATERIALS";
    case SandboxPreset::Gltf: return "GLTF";
    case SandboxPreset::Lights: return "LOCAL LIGHTS";
    case SandboxPreset::HdriStudio: return "HDRI STUDIO";
    case SandboxPreset::DayNight: return "DAY NIGHT";
    case SandboxPreset::PostProcessing: return "POST PROCESSING";
    case SandboxPreset::Stability: return "STABILITY STRESS";
    case SandboxPreset::Visibility: return "VISIBILITY CULLING";
    default: return "SANDBOX";
    }
}

void ParseCommandLine(int argc, char** argv, ApplicationDesc& application,
                      SandboxSceneConfig& sceneConfig,
                      std::string& screenshotPath, std::string& hdrScreenshotPath,
                      int& screenshotFrame,
                      std::string& cpuProfilePath, bool& cpuProfileLog,
                      uint32_t& cpuProfileRetainedFrames,
                       std::string& memoryProfilePath, bool& memoryLeakReport,
                       uint32_t& memoryProfileRetainedFrames,
                       std::string& gpuProfilePath, uint32_t& gpuProfileRetainedFrames,
                       std::string& gpuCapabilitiesPath,
                       bool& debugUi, bool& frameDebugger,
                      std::string& saveConfigPath,
                      std::string& pipelineBenchmarkPath,
                      uint32_t& pipelineBenchmarkFrames,
                      bool& stabilityStress,
                      diagnostics::StabilityStressConfig& stabilityConfig,
                      std::string& stabilityReportPath)
{
    WindowDesc& window = application.Window;
    RenderBackendConfig& renderer = application.Renderer;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--vulkan") window.api = GraphicsApi::Vulkan;
        else if (argument == "--opengl") window.api = GraphicsApi::OpenGL;
        else if (argument == "--config" && i + 1 < argc)
        {
            std::string error;
            if (!LoadApplicationConfig(argv[++i], application, &error))
                throw std::runtime_error(error);
        }
        else if (argument == "--save-config" && i + 1 < argc) saveConfigPath = argv[++i];
        else if (argument == "--windowed") window.mode = WindowMode::WindowedResizable;
        else if (argument == "--fixed-window") window.mode = WindowMode::WindowedFixed;
        else if (argument == "--borderless") window.mode = WindowMode::BorderlessFullscreen;
        else if (argument == "--fullscreen") window.mode = WindowMode::ExclusiveFullscreen;
        else if (argument == "--title" && i + 1 < argc) window.title = argv[++i];
        else if (argument == "--resolution" && i + 2 < argc)
        {
            window.width = std::max(std::atoi(argv[++i]), 1);
            window.height = std::max(std::atoi(argv[++i]), 1);
        }
        else if (argument == "--monitor" && i + 1 < argc) window.monitor = std::atoi(argv[++i]);
        else if (argument == "--position" && i + 2 < argc)
        {
            window.positionX = std::atoi(argv[++i]);
            window.positionY = std::atoi(argv[++i]);
            window.centerOnMonitor = false;
        }
        else if (argument == "--no-center") window.centerOnMonitor = false;
        else if (argument == "--hidden-window") window.visible = false;
        else if (argument == "--no-focus") window.focusOnShow = false;
        else if (argument == "--cursor" && i + 1 < argc)
        {
            const std::string value = argv[++i];
            window.cursor = value == "captured" ? CursorMode::Captured
                          : value == "hidden" ? CursorMode::Hidden : CursorMode::Normal;
        }
        else if (argument == "--vsync") renderer.Presentation = PresentMode::VSync;
        else if (argument == "--no-vsync") renderer.Presentation = PresentMode::Immediate;
        else if (argument == "--adaptive-vsync") renderer.Presentation = PresentMode::Adaptive;
        else if (argument == "--msaa" && i + 1 < argc)
            renderer.MsaaSamples = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 1));
        else if (argument == "--anisotropy" && i + 1 < argc)
            renderer.MaxAnisotropy = std::max(std::strtof(argv[++i], nullptr), 1.0f);
        else if (argument == "--adapter" && i + 1 < argc) renderer.PreferredAdapter = argv[++i];
        else if (argument == "--no-prefer-discrete") renderer.PreferDiscreteGpu = false;
        else if (argument == "--validation") renderer.EnableValidation = true;
        else if (argument == "--no-validation") renderer.EnableValidation = false;
        else if (argument == "--no-gpu-timing") renderer.EnableGpuTiming = false;
        else if (argument == "--gpu-policy" && i + 1 < argc)
        {
            const std::string value = argv[++i];
            if (value == "default") renderer.CapabilityPolicy = GpuCapabilityPolicy::Default;
            else if (value == "conservative") renderer.CapabilityPolicy = GpuCapabilityPolicy::Conservative;
            else throw std::runtime_error("Unknown GPU policy: " + value);
        }
        else if (argument == "--no-driver-workarounds") renderer.EnableDriverWorkarounds = false;
        else if (argument == "--gpu-capabilities" && i + 1 < argc) gpuCapabilitiesPath = argv[++i];
        else if (argument == "--pipeline-cache-dir" && i + 1 < argc)
            renderer.PipelineCacheDirectory = argv[++i];
        else if (argument == "--no-pipeline-cache") renderer.EnablePipelineCache = false;
        else if (argument == "--clear-pipeline-cache") renderer.ClearPipelineCache = true;
        else if (argument == "--render-graph" && i + 1 < argc)
            renderer.RenderGraphConfigPath = argv[++i];
        else if (argument == "--no-render-graph-validation") renderer.ValidateRenderGraph = false;
        else if (argument == "--no-transient-aliasing") renderer.EnableTransientAliasing = false;
        else if (argument == "--pipeline-cache-benchmark" && i + 1 < argc)
            pipelineBenchmarkPath = argv[++i];
        else if (argument == "--pipeline-benchmark-frames" && i + 1 < argc)
            pipelineBenchmarkFrames = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 1));
        else if (argument == "--no-runtime-monitors") application.EnableRuntimeMonitors = false;
        else if (argument == "--renderdoc-capture" && i + 1 < argc)
        {
            application.FrameCapture.Enabled = true;
            application.FrameCapture.RequireAvailable = true;
            application.FrameCapture.CapturePathTemplate = argv[++i];
        }
        else if (argument == "--renderdoc-frame" && i + 1 < argc)
            application.FrameCapture.FrameIndex = static_cast<uint64_t>(
                std::max(std::strtoll(argv[++i], nullptr, 10), 0ll));
        else if (argument == "--renderdoc-library" && i + 1 < argc)
            application.FrameCapture.LibraryPath = argv[++i];
        else if (argument == "--renderdoc-fixed-delta" && i + 1 < argc)
            application.FrameCapture.FixedDeltaSeconds =
                std::max(std::strtof(argv[++i], nullptr), 0.0001f);
        else if (argument == "--renderdoc-keep-running")
            application.FrameCapture.QuitAfterCapture = false;
        else if (argument == "--renderdoc-api-validation")
            application.FrameCapture.ApiValidation = true;
        else if (argument == "--max-fps" && i + 1 < argc)
            application.FrameRateLimit = std::max(std::strtod(argv[++i], nullptr), 0.0);
        else if (argument == "--max-delta" && i + 1 < argc)
            application.MaximumDeltaSeconds = std::max(std::strtof(argv[++i], nullptr), 0.001f);
        else if (argument == "--fixed-delta" && i + 1 < argc)
            application.FixedDeltaSeconds = std::max(std::strtof(argv[++i], nullptr), 0.0001f);
        else if (argument == "--unfocused" && i + 1 < argc)
        {
            const std::string value = argv[++i];
            application.Unfocused = value == "pause" ? UnfocusedBehavior::Pause
                                  : value == "render" ? UnfocusedBehavior::RenderOnly
                                  : UnfocusedBehavior::Continue;
        }
        else if (argument == "--no-look-capture") application.CaptureCursorOnRightMouse = false;
        else if (argument == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
        else if (argument == "--hdr-screenshot" && i + 1 < argc) hdrScreenshotPath = argv[++i];
        else if (argument == "--frames" && i + 1 < argc) screenshotFrame = std::atoi(argv[++i]);
        else if (argument == "--cpu-profile" && i + 1 < argc) cpuProfilePath = argv[++i];
        else if (argument == "--cpu-profile-log") cpuProfileLog = true;
        else if (argument == "--cpu-profile-retain" && i + 1 < argc)
            cpuProfileRetainedFrames = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 1));
        else if (argument == "--memory-profile" && i + 1 < argc) memoryProfilePath = argv[++i];
        else if (argument == "--memory-leak-report") memoryLeakReport = true;
        else if (argument == "--memory-profile-retain" && i + 1 < argc)
            memoryProfileRetainedFrames = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 1));
        else if (argument == "--gpu-profile" && i + 1 < argc) gpuProfilePath = argv[++i];
        else if (argument == "--gpu-profile-retain" && i + 1 < argc)
            gpuProfileRetainedFrames = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 1));
        else if (argument == "--debug-ui") debugUi = true;
        else if (argument == "--frame-debugger") frameDebugger = true;
        else if (argument == "--stability-stress") stabilityStress = true;
        else if (argument == "--stress-cycles" && i + 1 < argc)
            stabilityConfig.Cycles = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 1));
        else if (argument == "--stress-stage-frames" && i + 1 < argc)
            stabilityConfig.FramesPerStage = static_cast<uint32_t>(std::max(std::atoi(argv[++i]), 2));
        else if (argument == "--stress-report" && i + 1 < argc)
            stabilityReportPath = argv[++i];
        else if (argument == "--stress-no-exclusive")
            stabilityConfig.ExerciseExclusiveFullscreen = false;
        else if (argument == "--stress-max-cpu-growth-mb" && i + 1 < argc)
            stabilityConfig.MaximumCpuGrowthBytes = static_cast<uint64_t>(
                std::max(std::strtoll(argv[++i], nullptr, 10), 0ll)) * 1024ull * 1024ull;
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
}

} // namespace

int RunSandboxApp(int argc, char** argv, SandboxPreset preset)
{
    ApplicationDesc application;
    WindowDesc& window = application.Window;
    window.width = 1600;
    window.height = 900;
    SandboxSceneConfig sceneConfig;
    ApplyPreset(preset, window, sceneConfig);

    std::string screenshotPath;
    std::string hdrScreenshotPath;
    std::string cpuProfilePath;
    bool cpuProfileLog = false;
    uint32_t cpuProfileRetainedFrames = 600;
    std::string memoryProfilePath;
    bool memoryLeakReport = false;
    uint32_t memoryProfileRetainedFrames = 240;
    std::string gpuProfilePath;
    std::string gpuCapabilitiesPath;
    uint32_t gpuProfileRetainedFrames = 240;
    bool debugUi = false;
    bool frameDebugger = false;
    bool stabilityStress = preset == SandboxPreset::Stability;
    diagnostics::StabilityStressConfig stabilityConfig;
    std::string stabilityReportPath;
    std::string saveConfigPath;
    std::string pipelineBenchmarkPath;
    uint32_t pipelineBenchmarkFrames = 120;
    int screenshotFrame = 10;
    try
    {
        ParseCommandLine(argc, argv, application, sceneConfig, screenshotPath, hdrScreenshotPath,
                         screenshotFrame,
                         cpuProfilePath, cpuProfileLog, cpuProfileRetainedFrames,
                         memoryProfilePath, memoryLeakReport, memoryProfileRetainedFrames,
                         gpuProfilePath, gpuProfileRetainedFrames, gpuCapabilitiesPath,
                         debugUi, frameDebugger,
                         saveConfigPath, pipelineBenchmarkPath, pipelineBenchmarkFrames,
                         stabilityStress, stabilityConfig, stabilityReportPath);
        if (stabilityStress)
        {
            application.Unfocused = UnfocusedBehavior::Continue;
            application.FixedDeltaSeconds = application.FixedDeltaSeconds > 0.0f
                ? application.FixedDeltaSeconds : 1.0f / 60.0f;
            if (stabilityReportPath.empty())
            {
                stabilityReportPath = (std::filesystem::path("build") / "stability" /
                    (application.Window.api == GraphicsApi::OpenGL
                        ? "stability.opengl.json" : "stability.vulkan.json")).string();
            }
        }
        if (!saveConfigPath.empty())
        {
            std::string error;
            if (!SaveApplicationConfig(saveConfigPath, application, &error))
                throw std::runtime_error(error);
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }

    profiling::CpuProfiler& profiler = profiling::CpuProfiler::Get();
    profiler.Reset();
    profiling::CpuProfilerConfig profilerConfig;
    profilerConfig.Enabled = !cpuProfilePath.empty() || cpuProfileLog;
    profilerConfig.RetainedFrames = cpuProfileRetainedFrames;
    profilerConfig.LogIntervalFrames = cpuProfileLog ? 120u : 0u;
    profiler.Configure(profilerConfig);

    profiling::MemoryProfiler& memoryProfiler = profiling::MemoryProfiler::Get();
    memoryProfiler.SetEnabled(false);
    memoryProfiler.Reset();
    profiling::MemoryProfilerConfig memoryConfig;
    // The runtime monitor uses the inexpensive process-memory snapshot. Full
    // allocation tracking is intentionally opt-in so opening the Debug UI
    // cannot perturb rendering performance.
    memoryConfig.Enabled = !memoryProfilePath.empty() || memoryLeakReport || stabilityStress;
    memoryConfig.LeakReportOnShutdown = memoryLeakReport;
    memoryConfig.RetainedFrames = memoryProfileRetainedFrames;
    memoryProfiler.Configure(memoryConfig);

    profiling::GpuProfiler& gpuProfiler = profiling::GpuProfiler::Get();
    gpuProfiler.Reset();
    profiling::GpuProfilerConfig gpuConfig;
    gpuConfig.Enabled = application.Renderer.EnableGpuTiming;
    gpuConfig.RetainedFrames = gpuProfileRetainedFrames;
    gpuProfiler.Configure(gpuConfig);

    try
    {
        {
            ENGINE_MEMORY_TAG_SCOPE("Core");
            const auto startupBegin = std::chrono::steady_clock::now();
            Application app(application);
            const double startupMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startupBegin).count();
            std::vector<double> benchmarkFrameTimes;
            benchmarkFrameTimes.reserve(pipelineBenchmarkFrames);
            SampleAssetPipeline sampleAssets;
            app.GetDebugOverlay().SetVisible(debugUi);
            app.GetDebugOverlay().SetFrameDebuggerVisible(frameDebugger);
            app.GetDebugOverlay().SetValue("APPLICATION", "SAMPLE", PresetName(preset));
            PopulateSandboxScene(app, sceneConfig, sampleAssets);
            app.GetDebugOverlay().SetValue("ASSET PIPELINE", "REGISTERED", std::to_string(sampleAssets.AssetCount()));
            app.GetDebugOverlay().SetValue("ASSET PIPELINE", "RUNTIME RESOURCES", std::to_string(sampleAssets.RuntimeResourceCount()));
            app.GetDebugOverlay().SetValue("ASSET PIPELINE", "LOADED", std::to_string(sampleAssets.LoadedResourceCount()));
            app.GetDebugOverlay().SetValue("ASSET PIPELINE", "CACHE HITS", std::to_string(sampleAssets.CacheHitCount()));
            SandboxControls controls(app, sceneConfig.FlashlightOn,
                                     !sceneConfig.LocalLightShowcase && !sceneConfig.HdriStudio,
                                     sceneConfig.LocalLightShowcase, sceneConfig.DayNightShowcase,
                                     sceneConfig.PostShowcase, sceneConfig.DayNightCycleSeconds,
                                     sceneConfig.SunAzimuthDegrees, sceneConfig.SunElevationDegrees,
                                     screenshotPath, hdrScreenshotPath, screenshotFrame);
            std::unique_ptr<diagnostics::StabilityStressRunner> stabilityRunner;
            if (stabilityStress)
            {
                stabilityRunner = std::make_unique<diagnostics::StabilityStressRunner>(
                    app, stabilityConfig,
                    [&app, &sceneConfig, &sampleAssets]
                    {
                        const size_t invalidated = sampleAssets.InvalidateImportedResources();
                        app.GetScene() = Scene{};
                        PopulateSandboxScene(app, sceneConfig, sampleAssets);
                        app.GetDebugOverlay().SetValue("ASSET PIPELINE", "HOT RELOAD INVALIDATED",
                                                       std::to_string(invalidated));
                        app.GetDebugOverlay().SetValue("ASSET PIPELINE", "LOADED",
                            std::to_string(sampleAssets.LoadedResourceCount()));
                    });
            }
            app.SetUpdateCallback([&app, &controls, &stabilityRunner,
                                   &pipelineBenchmarkPath, pipelineBenchmarkFrames,
                                   &benchmarkFrameTimes](float deltaTime)
            {
                controls.Update(deltaTime);
                if (stabilityRunner)
                    stabilityRunner->Update(deltaTime);
                if (!pipelineBenchmarkPath.empty() && app.LastFrameCpuMilliseconds() > 0.0)
                {
                    benchmarkFrameTimes.push_back(app.LastFrameCpuMilliseconds());
                    if (benchmarkFrameTimes.size() >= pipelineBenchmarkFrames)
                        app.RequestQuit();
                }
            });
            app.Run();
            if (!pipelineBenchmarkPath.empty())
            {
                PipelineStutterReport report;
                report.Backend = app.GetBackend().Name();
                report.Adapter = app.GetBackend().GetCapabilities().AdapterName;
                report.ColdCacheRun = application.Renderer.ClearPipelineCache;
                report.ApplicationStartupMilliseconds = startupMilliseconds;
                report.Cache = app.GetBackend().GetPipelineCacheStats();
                report.FrameCpuMilliseconds = benchmarkFrameTimes;
                std::string benchmarkError;
                if (!WritePipelineStutterReport(pipelineBenchmarkPath, report, &benchmarkError))
                    throw std::runtime_error(benchmarkError);
                log::Info("Saved pipeline-cache benchmark: " + pipelineBenchmarkPath);
            }
            if (!gpuCapabilitiesPath.empty())
            {
                std::string capabilityError;
                if (!WriteGpuCapabilityReport(gpuCapabilitiesPath,
                                              app.GetBackend().GetCapabilities().Gpu,
                                              &capabilityError))
                    throw std::runtime_error(capabilityError);
                log::Info("Saved GPU capability report: " + gpuCapabilitiesPath);
            }
            if (stabilityRunner)
            {
                if (!stabilityRunner->WriteJsonReport(stabilityReportPath))
                    throw std::runtime_error("Failed to write stability report: " + stabilityReportPath);
                log::Info("Saved stability report: " + stabilityReportPath);
                if (!stabilityRunner->Succeeded())
                    throw std::runtime_error("Renderer stability stress failed; see " + stabilityReportPath);
            }
            if (application.FrameCapture.Enabled && !app.GetFrameCapture().CaptureCompleted())
            {
                throw std::runtime_error(app.GetFrameCapture().LastError().empty()
                    ? "The requested RenderDoc frame capture did not complete."
                    : app.GetFrameCapture().LastError());
            }
        }
        if (!cpuProfilePath.empty())
        {
            if (!profiler.WriteChromeTrace(cpuProfilePath))
                throw std::runtime_error("Failed to write CPU profile: " + cpuProfilePath);
            log::Info("Saved CPU profile: " + cpuProfilePath);
        }
        if (!memoryProfilePath.empty())
        {
            if (!memoryProfiler.WriteJsonReport(memoryProfilePath, true))
                throw std::runtime_error("Failed to write memory profile: " + memoryProfilePath);
            log::Info("Saved memory profile: " + memoryProfilePath);
        }
        if (!gpuProfilePath.empty())
        {
            if (!gpuProfiler.WriteJsonReport(gpuProfilePath))
                throw std::runtime_error("Failed to write GPU profile: " + gpuProfilePath);
            log::Info("Saved GPU profile: " + gpuProfilePath);
        }
        profiler.SetEnabled(false);
        memoryProfiler.SetEnabled(false);
        gpuProfiler.SetEnabled(false);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        profiler.SetEnabled(false);
        memoryProfiler.SetEnabled(false);
        gpuProfiler.SetEnabled(false);
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
