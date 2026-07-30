#include "engine/core/Application.h"
#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/core/Log.h"
#include "engine/profiling/CpuProfiler.h"
#include "engine/profiling/GpuProfiler.h"
#include "engine/profiling/MemoryProfiler.h"
#if ENGINE_ENABLE_IMGUI
#include "engine/editor/ImGuiLayer.h"
#endif

#if ENGINE_HAS_VULKAN
#include "engine/backend/vk/VulkanRenderBackend.h"
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace engine
{

namespace
{

const char* PresentModeName(PresentMode mode)
{
    switch (mode)
    {
    case PresentMode::Immediate:
        return "IMMEDIATE";
    case PresentMode::Adaptive:
        return "ADAPTIVE";
    default:
        return "VSYNC";
    }
}

const char* WindowModeName(WindowMode mode)
{
    switch (mode)
    {
    case WindowMode::WindowedFixed:
        return "WINDOWED FIXED";
    case WindowMode::BorderlessFullscreen:
        return "BORDERLESS";
    case WindowMode::ExclusiveFullscreen:
        return "FULLSCREEN";
    default:
        return "WINDOWED RESIZABLE";
    }
}

std::unique_ptr<IRenderBackend> CreateRenderBackend(GraphicsApi api)
{
    if (api == GraphicsApi::OpenGL)
        return std::make_unique<GLRenderBackend>();
#if ENGINE_HAS_VULKAN
    return std::make_unique<VulkanRenderBackend>();
#else
    throw std::runtime_error("Engine was built without Vulkan support (ENGINE_BUILD_VULKAN=OFF)");
#endif
}

} // namespace

Application::Application(const WindowDesc& desc) : Application(ApplicationDesc{desc}) {}

Application::Application(const ApplicationDesc& desc)
    : m_desc(desc), m_taskSystem(std::make_unique<concurrency::TaskSystem>()),
      m_sceneRenderer(m_taskSystem.get()),
      m_worldRenderingEnabled(desc.SynchronizeWorldToScene)
{
    ENGINE_MEMORY_TAG_SCOPE("Core");
    if (m_desc.FrameCapture.CaptureTitle.empty())
    {
        m_desc.FrameCapture.CaptureTitle = m_desc.Window.title + " - " +
            (m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL" : "Vulkan");
    }
    m_desc.FrameCapture.PrepareVulkanLayer =
        m_desc.Window.api == GraphicsApi::Vulkan;
    // RenderDoc must be discovered/loaded before the graphics API is created so
    // it can install its hooks deterministically.
    m_frameCapture.Initialize(m_desc.FrameCapture);
    SetRuntimeMonitorsEnabled(m_desc.EnableRuntimeMonitors);
    m_desc.MaximumDeltaSeconds = std::max(m_desc.MaximumDeltaSeconds, 0.001f);
    m_desc.FrameRateLimit = std::max(m_desc.FrameRateLimit, 0.0);
    m_desc.Renderer.MsaaSamples = std::max(m_desc.Renderer.MsaaSamples, 1u);
    m_desc.Renderer.MaxAnisotropy = std::max(m_desc.Renderer.MaxAnisotropy, 1.0f);
    profiling::GpuProfiler::Get().SetEnabled(m_desc.Renderer.EnableGpuTiming);

    std::string componentError;
    if (!runtime::RegisterBuiltinRenderComponents(m_componentRegistry, &componentError))
        throw std::runtime_error("Failed to register built-in render components: " +
                                 componentError);

    m_window = std::make_unique<Window>(m_desc.Window);
    m_input.Attach(m_window->Handle());

    m_backend = CreateRenderBackend(m_desc.Window.api);

    {
        profiling::MemoryTagScope tag(m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL"
                                                                              : "Vulkan");
        m_backend->Init(*m_window, m_desc.Renderer);
    }

#if ENGINE_ENABLE_IMGUI
    if (m_desc.EnableImGui)
    {
        m_imgui = std::make_unique<editor::ImGuiLayer>();
        editor::ImGuiLayerConfig uiConfig;
        uiConfig.PlatformViewports = m_desc.EnableImGuiPlatformViewports;
        uiConfig.IniFilename = m_desc.ImGuiIniFilename;
        if (!m_imgui->Initialize(*m_window, *m_backend, uiConfig))
            throw std::runtime_error("Failed to initialize Dear ImGui");
    }
#else
    if (m_desc.EnableImGui)
        throw std::runtime_error(
            "Dear ImGui was requested, but ENGINE_ENABLE_IMGUI=OFF in this build");
#endif

    m_window->SetResizeCallback(
        [this](int w, int h)
        {
            if (w > 0 && h > 0)
            {
                profiling::MemoryTagScope tag(m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL"
                                                                                      : "Vulkan");
                m_backend->Resize(w, h);
            }
        });

    m_wasFocused = m_window->IsFocused();

    const BackendCapabilities capabilities = m_backend->GetCapabilities();
    m_debugOverlay.SetValue("APPLICATION", "WINDOW", WindowModeName(m_desc.Window.mode));
    m_debugOverlay.SetValue("APPLICATION", "PRESENT", PresentModeName(m_desc.Renderer.Presentation));
    m_debugOverlay.SetValue("APPLICATION", "FOCUS", m_wasFocused ? "FOREGROUND" : "BACKGROUND");
    m_debugOverlay.SetValue("RENDERER", "ADAPTER", capabilities.AdapterName);
    m_debugOverlay.SetValue("RENDERER", "GPU VENDOR",
                            GpuVendorName(capabilities.Gpu.Device.Vendor));
    m_debugOverlay.SetValue("RENDERER", "DRIVER",
                            capabilities.Gpu.Device.DriverName.empty()
                                ? capabilities.Gpu.Device.ApiVersion
                                : capabilities.Gpu.Device.DriverName);
    m_debugOverlay.SetValue("RENDERER", "CAPABILITY TIER",
                            GpuFeatureTierName(capabilities.Gpu.Tier));
    m_debugOverlay.SetValue("RENDERER", "FALLBACKS",
                            std::to_string(capabilities.Gpu.Fallbacks.size()));
    m_debugOverlay.SetValue("RENDERER", "MSAA", std::to_string(capabilities.ActiveMsaaSamples) + "X");
    m_debugOverlay.SetValue("RENDERER", "ANISOTROPY",
                            std::to_string(static_cast<int>(capabilities.ActiveAnisotropy)) + "X");
    if (capabilities.DedicatedVideoMemoryBytes > 0)
        m_debugOverlay.SetValue("RENDERER", "DEDICATED VRAM",
                                std::to_string(capabilities.DedicatedVideoMemoryBytes / (1024 * 1024)) +
                                    " MB");
    const PipelineCacheStatistics pipelineCache = m_backend->GetPipelineCacheStats();
    m_debugOverlay.SetValue("RENDERER", "PIPELINE CACHE",
                            pipelineCache.Enabled
                                ? (pipelineCache.PersistentCacheLoaded ? "WARM" : "COLD")
                                : "DISABLED");
    m_debugOverlay.SetValue("RENDERER", "SHADER CACHE",
                            std::to_string(pipelineCache.ShaderPermutationHits) + " HIT / " +
                            std::to_string(pipelineCache.ShaderPermutationMisses) + " MISS");

    log::Info(std::string("Using render backend: ") + m_backend->Name());
}

Application::~Application()
{
    ENGINE_MEMORY_TAG_SCOPE("Core");
    // Components contain callbacks into their owning plugin. Destroy them
    // before unloading modules, while renderer services are still alive.
    m_world.Clear();
    m_plugins.UnloadAll();
    if (m_taskSystem)
    {
        m_taskSystem->WaitIdle();
        m_taskSystem.reset();
    }
#if ENGINE_ENABLE_IMGUI
    m_imgui.reset();
#endif
    if (m_backend)
    {
        profiling::MemoryTagScope tag(m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL"
                                                                              : "Vulkan");
        m_backend->Shutdown();
    }
    if (m_profilerOwnedByDebugOverlay)
        profiling::CpuProfiler::Get().SetEnabled(false);
}

#if ENGINE_ENABLE_IMGUI
void Application::SetImGuiCallback(std::function<void()> callback)
{
    m_imguiCallback = std::move(callback);
    if (m_imgui)
        m_imgui->SetBuildCallback(m_imguiCallback);
}
#endif

void Application::SetRuntimeMonitorsEnabled(bool enabled)
{
#if !ENGINE_ENABLE_RUNTIME_MONITORS
    enabled = false;
#endif
    m_desc.EnableRuntimeMonitors = enabled;
    m_debugOverlay.SetRuntimeMonitorsVisible(enabled);
    // Always-on monitor cards use the OS process-memory sample exposed by
    // MemoryProfiler::Snapshot. Full global new/delete tracking remains under
    // explicit control of the embedding application.
}

void Application::Run()
{
    profiling::CpuProfiler& profiler = profiling::CpuProfiler::Get();
    if (m_debugOverlay.IsVisible() && !profiler.IsEnabled())
    {
        profiler.SetEnabled(true);
        m_profilerOwnedByDebugOverlay = true;
    }
    profiler.SetThreadName("Main");
    // Runtime memory cards read inexpensive OS process counters. Allocation
    // tracking remains opt-in (--memory-profile / leak diagnostics) because
    // globally locking every allocation to draw an overlay damages frame
    // pacing, especially in Debug builds.

    while (RunOneFrame())
    {
    }

    if (m_profilerOwnedByDebugOverlay)
    {
        profiler.SetEnabled(false);
        m_profilerOwnedByDebugOverlay = false;
    }
}

bool Application::RunOneFrame()
{
    ENGINE_MEMORY_TAG_SCOPE("Core");
    if (m_window->ShouldClose())
        return false;

    profiling::CpuProfiler& profiler = profiling::CpuProfiler::Get();
    profiling::MemoryProfiler& memoryProfiler = profiling::MemoryProfiler::Get();
    if (m_debugOverlay.IsVisible() && !profiler.IsEnabled())
    {
        profiler.SetEnabled(true);
        profiler.SetThreadName("Main");
        m_profilerOwnedByDebugOverlay = true;
    }

    const double frameStart = glfwGetTime();
    if (!m_hasFrameClock)
    {
        m_lastFrameTime = frameStart;
        m_hasFrameClock = true;
    }
    const float fixedDelta = m_frameCapture.IsRequested()
        ? m_frameCapture.Config().FixedDeltaSeconds : m_desc.FixedDeltaSeconds;
    const float deltaTime = fixedDelta > 0.0f
        ? fixedDelta
        : std::clamp(static_cast<float>(frameStart - m_lastFrameTime), 0.0f,
                     m_desc.MaximumDeltaSeconds);
    m_lastFrameTime = frameStart;

    bool pauseCompletely = false;
    bool surfaceUnavailable = false;
    bool frameDebugCapturePending = false;
    uint64_t frameDebugResourceId = 0;
    uint32_t frameDebugMipLevel = 0;
    uint32_t frameDebugLayer = 0;
    profiler.BeginFrame();
    memoryProfiler.BeginFrame();
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Application.Frame", "Frame");
        EmitEvent(ApplicationEventType::BeginFrame, deltaTime);

        {
            ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PollEvents", "Application");
            m_window->PollEvents();
            m_input.NewFrame();
#if ENGINE_ENABLE_IMGUI
            if (m_imgui)
            {
                m_imgui->BeginFrame();
                m_input.SetUiCapture(m_imgui->WantsKeyboard(), m_imgui->WantsMouse());
            }
            else
#endif
            {
                m_input.SetUiCapture(false, false);
            }
        }

        const bool focused = m_window->IsFocused();
        if (focused != m_wasFocused)
        {
            EmitEvent(focused ? ApplicationEventType::EnteredForeground
                              : ApplicationEventType::EnteredBackground,
                      deltaTime);
            m_wasFocused = focused;
            m_debugOverlay.SetValue("APPLICATION", "FOCUS", focused ? "FOREGROUND" : "BACKGROUND");
        }

        const bool debugToggleDown = m_input.IsKeyDown(GLFW_KEY_F3);
        if (debugToggleDown && !m_debugToggleWasDown)
        {
            m_debugOverlay.Toggle();
            log::Info(std::string("Debug overlay: ") + (m_debugOverlay.IsVisible() ? "ON" : "OFF"));
        }
        m_debugToggleWasDown = debugToggleDown;

        const auto frameDebugPressed = [&](size_t state, int key) {
            const bool down = m_input.IsKeyDown(key);
            const bool pressed = down && !m_frameDebuggerKeyStates[state];
            m_frameDebuggerKeyStates[state] = down;
            return pressed;
        };
        if (frameDebugPressed(0, GLFW_KEY_F4))
        {
            m_debugOverlay.ToggleFrameDebugger();
            log::Info(std::string("Frame debugger: ") +
                      (m_debugOverlay.FrameDebuggerVisible() ? "ON" : "OFF"));
        }
        const bool frameDebuggerVisible = m_debugOverlay.FrameDebuggerVisible();
        if (frameDebugPressed(1, GLFW_KEY_LEFT) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugPass(-1);
        if (frameDebugPressed(2, GLFW_KEY_RIGHT) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugPass(1);
        if (frameDebugPressed(3, GLFW_KEY_UP) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugResource(-1);
        if (frameDebugPressed(4, GLFW_KEY_DOWN) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugResource(1);
        if (frameDebugPressed(5, GLFW_KEY_COMMA) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugMip(-1);
        if (frameDebugPressed(6, GLFW_KEY_PERIOD) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugMip(1);
        if (frameDebugPressed(7, GLFW_KEY_LEFT_BRACKET) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugLayer(-1);
        if (frameDebugPressed(8, GLFW_KEY_RIGHT_BRACKET) && frameDebuggerVisible)
            m_debugOverlay.MoveFrameDebugLayer(1);
        if (frameDebugPressed(9, GLFW_KEY_R) && frameDebuggerVisible)
            m_debugOverlay.RequestFrameDebugRefresh();

        pauseCompletely = !focused && m_desc.Unfocused == UnfocusedBehavior::Pause;
        if (!pauseCompletely)
        {
            const bool updateEnabled =
                !m_paused && (focused || m_desc.Unfocused == UnfocusedBehavior::Continue);
            const bool lookEnabled = updateEnabled && m_desc.CaptureCursorOnRightMouse &&
                                     m_input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
            m_window->SetCursorMode(lookEnabled ? CursorMode::Captured : m_window->DefaultCursorMode());

            if (updateEnabled)
            {
                EmitEvent(ApplicationEventType::BeforeUpdate, deltaTime);
                {
                    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("CameraUpdate", "Application");
                    m_camera.Update(m_input, deltaTime, lookEnabled);
                }
                if (m_updateCallback)
                {
                    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("UserUpdate", "Application");
                    m_updateCallback(deltaTime);
                }
                {
                    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("World.Update", "Application");
                    m_world.Update(deltaTime);
                }
                EmitEvent(ApplicationEventType::AfterUpdate, deltaTime);
            }

            if (m_worldRenderingEnabled)
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("WorldRenderBridge", "Scene");
                m_worldRenderBridge.Synchronize(m_world, m_scene, &m_camera);
            }

#if ENGINE_ENABLE_IMGUI
            if (m_imgui)
                m_imgui->EndFrame();
#endif

            // GLFW reports a zero-sized framebuffer while a window is
            // minimized. Keep simulation and stress-control callbacks alive,
            // but never ask either backend to create or render 0x0 targets.
            surfaceUnavailable = m_window->IsMinimized() ||
                                 m_window->Width() <= 0 || m_window->Height() <= 0;
            if (!surfaceUnavailable)
            {
            // Runtime monitor cards do not need to rebuild and snapshot every
            // frame. Their CPU rasterization and the full memory snapshot
            // allocate enough temporary data to perturb the timings they are
            // meant to observe. Detailed/debugger views remain responsive at
            // 15 Hz; the always-on corner cards update at 5 Hz while the same
            // cached overlay image is composited every frame.
            const double overlayInterval =
                (m_debugOverlay.IsVisible() || m_debugOverlay.FrameDebuggerVisible())
                    ? (1.0 / 15.0) : 0.2;
            const bool updateDebugOverlay = m_debugOverlay.HasVisibleContent() &&
                (frameStart >= m_nextDebugOverlayUpdateTime || m_frameCounter == 0);
            if (updateDebugOverlay)
            {
                m_nextDebugOverlayUpdateTime = frameStart + overlayInterval;
                ENGINE_MEMORY_TAG_SCOPE("Debug");
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("DebugOverlay.Update", "Debug");
                const profiling::GpuProfileSnapshot gpuProfile =
                    profiling::GpuProfiler::Get().Snapshot();
                if (m_debugOverlay.FrameDebuggerVisible())
                {
                    m_debugOverlay.SetFrameDebugSnapshot(m_backend->GetFrameDebugSnapshot());
                    frameDebugCapturePending = m_debugOverlay.GetFrameDebugCaptureRequest(
                        frameDebugResourceId, frameDebugMipLevel, frameDebugLayer);
                }
                debug::DebugOverlayMetrics metrics;
                metrics.BackendName = m_backend->Name();
                metrics.ViewWidth = m_window->Width();
                metrics.ViewHeight = m_window->Height();
                metrics.DeltaSeconds = deltaTime;
                metrics.Exposure = m_scene.PostProcess.Exposure;
                metrics.FrameIndex = m_frameCounter;
                metrics.ObjectCount = static_cast<uint32_t>(m_scene.Instances().size());
                metrics.Backend = m_backend->GetFrameStats();
                switch (m_scene.PostProcess.AntiAliasing)
                {
                case AntiAliasingMode::Fxaa:
                    metrics.AntiAliasing = "FXAA";
                    break;
                case AntiAliasingMode::Taa:
                    metrics.AntiAliasing = "TAA";
                    break;
                default:
                    metrics.AntiAliasing = "NONE";
                    break;
                }
                for (const MeshInstance& instance : m_scene.Instances())
                    if (instance.Mesh)
                        metrics.TriangleCount += instance.Mesh->Indices.size() / 3;
                metrics.PointLightCount = static_cast<uint32_t>(
                    std::count_if(m_scene.PointLights().begin(), m_scene.PointLights().end(),
                                  [](const PointLight& light) { return light.Enabled; }));
                metrics.SpotLightCount = static_cast<uint32_t>(
                    std::count_if(m_scene.SpotLights().begin(), m_scene.SpotLights().end(),
                                  [](const SpotLight& light) { return light.Enabled; }));
                metrics.AreaLightCount = static_cast<uint32_t>(
                    std::count_if(m_scene.AreaLights().begin(), m_scene.AreaLights().end(),
                                  [](const AreaLight& light) { return light.Enabled; }));
                metrics.Capabilities = m_backend->GetCapabilities();
                const concurrency::TaskSystemStatistics taskStatistics = m_taskSystem->Statistics();
                m_debugOverlay.SetValue("JOBS", "WORKERS", std::to_string(taskStatistics.WorkerCount));
                m_debugOverlay.SetValue("JOBS", "QUEUED", std::to_string(taskStatistics.Queued));
                m_debugOverlay.SetValue("JOBS", "ACTIVE", std::to_string(taskStatistics.Active));
                m_debugOverlay.SetValue("JOBS", "COMPLETED", std::to_string(taskStatistics.Completed));
                const VisibilityStatistics& visibility = m_sceneRenderer.GetVisibilityStatistics();
                m_debugOverlay.SetValue("VISIBILITY", "TESTED", std::to_string(visibility.Tested));
                m_debugOverlay.SetValue("VISIBILITY", "VISIBLE", std::to_string(visibility.Visible));
                m_debugOverlay.SetValue("VISIBILITY", "FRUSTUM CULLED",
                                        std::to_string(visibility.FrustumCulled));
                m_debugOverlay.SetValue("VISIBILITY", "DISTANCE CULLED",
                                        std::to_string(visibility.DistanceCulled));
                m_debugOverlay.SetValue("VISIBILITY", "SHADOW CASTERS",
                                        std::to_string(visibility.ShadowCasters));
                m_debugOverlay.SetValue("GPU HI-Z", "ACTIVE",
                                        metrics.Backend.GpuOcclusionActive ? "YES" : "NO");
                m_debugOverlay.SetValue("GPU HI-Z", "CANDIDATES",
                                        std::to_string(metrics.Backend.GpuOcclusionCandidates));
                m_debugOverlay.SetValue("GPU HI-Z", "CULLED",
                                        std::to_string(metrics.Backend.GpuOcclusionCulled));
                m_debugOverlay.SetValue("GPU HI-Z", "RESULTS",
                                        std::to_string(metrics.Backend.GpuOcclusionResultsConsumed));
                m_debugOverlay.SetValue("GPU HI-Z", "MIP LEVELS",
                                        std::to_string(metrics.Backend.HiZMipLevels));
                m_debugOverlay.SetValue("GPU HI-Z", "LATENCY",
                                        std::to_string(metrics.Backend.OcclusionReadbackLatencyFrames) + " FR");
                m_debugOverlay.SetValue("GPU HI-Z", "GPU TIME",
                                        std::to_string(metrics.Backend.GpuOcclusionMilliseconds) + " MS");
                m_debugOverlay.SetValue("GPU HI-Z", "HISTORY RESET",
                                        metrics.Backend.GpuOcclusionHistoryReset ? "YES" : "NO");
                m_debugOverlay.SetValue("GPU INSTANCING", "ACTIVE",
                                        metrics.Backend.GpuInstancingActive ? "YES" : "NO");
                m_debugOverlay.SetValue("GPU INSTANCING", "INSTANCES",
                                        std::to_string(metrics.Backend.GpuInstanceCount));
                m_debugOverlay.SetValue("GPU INSTANCING", "DRAW BATCHES",
                                        std::to_string(metrics.Backend.GpuInstanceBatchCount));
                m_debugOverlay.SetValue("GPU INSTANCING", "INSTANCED BATCHES",
                                        std::to_string(metrics.Backend.GpuInstancedBatchCount));
                m_debugOverlay.SetValue("GPU INSTANCING", "DRAWS SAVED",
                                        std::to_string(metrics.Backend.GpuDrawCallsSaved));
                m_debugOverlay.SetValue("HISM", "GROUPS",
                                        std::to_string(visibility.HismGroups));
                m_debugOverlay.SetValue("HISM", "NODES TESTED",
                                        std::to_string(visibility.HismNodesTested));
                m_debugOverlay.SetValue("HISM", "NODES CULLED",
                                        std::to_string(visibility.HismNodesCulled));
                m_debugOverlay.SetValue("HISM", "INSTANCES CULLED",
                                        std::to_string(visibility.HismInstancesCulled));
                m_debugOverlay.SetValue("HISM", "LEAF TESTS",
                                        std::to_string(visibility.HismLeafTests));
                const GeometryBatchingStatistics& batching =
                    m_sceneRenderer.GetBatchingStatistics();
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "SOURCE INSTANCES",
                                        std::to_string(batching.SourceInstances));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "EFFECTIVE INSTANCES",
                                        std::to_string(batching.EffectiveInstances));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "STATIC BATCHES",
                                        std::to_string(batching.StaticBatches));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "DYNAMIC BATCHES",
                                        std::to_string(batching.DynamicBatches));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "DRAWS SAVED",
                                        std::to_string(batching.DrawCallsSaved));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "CACHE HITS",
                                        std::to_string(batching.CacheHits));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "REBUILT",
                                        std::to_string(batching.RebuiltBatches));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "REJECTED",
                                        std::to_string(batching.RejectedBatches));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "RECYCLED",
                                        std::to_string(batching.RecycledBatches));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "TO INSTANCING",
                                        std::to_string(batching.DeferredToInstancing));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "VERTICES",
                                        std::to_string(batching.CombinedVertices));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "INDICES",
                                        std::to_string(batching.CombinedIndices));
                m_debugOverlay.SetValue("GEOMETRY BATCHING", "CPU TIME",
                                        std::to_string(batching.BuildMilliseconds) + " MS");
                m_debugOverlay.Update(metrics, profiler.FrameSummarySnapshot(), memoryProfiler.Snapshot(),
                                      gpuProfile);
            }

            EmitEvent(ApplicationEventType::BeforeRender, deltaTime);
            const RenderFrameData* frame = nullptr;
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PrepareFrame", "Renderer");
                ENGINE_MEMORY_TAG_SCOPE("Renderer");
                frame = &m_sceneRenderer.PrepareFrame(
                    m_scene, m_camera, m_window->Width(), m_window->Height(),
                    m_debugOverlay.HasVisibleContent() ? &m_debugOverlay.Image() : nullptr,
                    fixedDelta > 0.0f
                        ? static_cast<float>(m_frameCounter) * fixedDelta
                        : static_cast<float>(frameStart),
                    deltaTime);
            }
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RenderBackend", "Renderer");
                profiling::MemoryTagScope memoryTag(
                    m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL" : "Vulkan");
                const bool captureStarted = m_frameCapture.BeginFrame(m_frameCounter);
                if (m_frameCapture.IsRequested() && m_frameCapture.Config().RequireAvailable &&
                    m_frameCounter == m_frameCapture.Config().FrameIndex && !captureStarted)
                {
                    throw std::runtime_error(m_frameCapture.LastError().empty()
                        ? "The requested RenderDoc frame capture could not be started."
                        : m_frameCapture.LastError());
                }
                m_backend->RenderFrame(*frame);
            }

            if (m_desc.Window.api == GraphicsApi::OpenGL)
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Present", "Renderer");
                m_window->SwapBuffers();
            }
            if (frameDebugCapturePending)
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("FrameDebugger.Capture", "Debug");
                debug::FrameDebugPreview preview;
                preview.ResourceId = frameDebugResourceId;
                preview.MipLevel = frameDebugMipLevel;
                preview.Layer = frameDebugLayer;
                if (!m_backend->CaptureFrameDebugResource(
                        frameDebugResourceId, frameDebugMipLevel, frameDebugLayer, preview) &&
                    preview.Error.empty())
                    preview.Error = "Backend could not capture this resource";
                m_debugOverlay.SetFrameDebugPreview(std::move(preview));
            }
            if (m_frameCapture.IsCapturing())
            {
                m_frameCapture.EndFrame();
                if (m_frameCapture.CaptureCompleted() &&
                    m_frameCapture.Config().QuitAfterCapture)
                    RequestQuit();
            }
            EmitEvent(ApplicationEventType::AfterRender, deltaTime);
            }
            EmitEvent(ApplicationEventType::EndFrame, deltaTime);
        }
        else
        {
            EmitEvent(ApplicationEventType::EndFrame, deltaTime);
        }
    }
#if ENGINE_ENABLE_IMGUI
    // Completes a UI frame while the application is background-paused. It is
    // a no-op when the normal render path already ended it above.
    if (m_imgui)
        m_imgui->EndFrame();
#endif
    profiler.EndFrame();
    memoryProfiler.EndFrame();
    m_lastFrameCpuMilliseconds = (glfwGetTime() - frameStart) * 1000.0;
    if (pauseCompletely)
    {
        m_window->WaitEvents(0.05);
        return !m_window->ShouldClose();
    }
    ++m_frameCounter;

    if (surfaceUnavailable)
        m_window->WaitEvents(0.01);

    if (m_desc.FrameRateLimit > 0.0)
    {
        const double targetSeconds = 1.0 / m_desc.FrameRateLimit;
        const double elapsed = glfwGetTime() - frameStart;
        if (elapsed < targetSeconds)
            std::this_thread::sleep_for(std::chrono::duration<double>(targetSeconds - elapsed));
    }
    return !m_window->ShouldClose();
}

void Application::SetFrameRateLimit(double framesPerSecond)
{
    m_desc.FrameRateLimit = std::max(framesPerSecond, 0.0);
}

bool Application::SetPresentMode(PresentMode mode)
{
    profiling::MemoryTagScope tag(m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL" : "Vulkan");
    m_desc.Renderer.Presentation = mode;
    const bool exact = m_backend->SetPresentMode(mode);
    m_debugOverlay.SetValue("APPLICATION", "PRESENT", PresentModeName(mode));
    if (!exact)
        log::Warn(std::string("Requested present mode is unavailable; backend "
                              "selected a safe fallback: ") +
                  PresentModeName(mode));
    return exact;
}

void Application::SetWindowMode(WindowMode mode, int monitor, int width, int height)
{
    m_window->SetMode(mode, monitor, width, height);
    m_desc.Window.mode = mode;
    m_desc.Window.monitor = monitor;
    m_debugOverlay.SetValue("APPLICATION", "WINDOW", WindowModeName(mode));
}

void Application::ReloadRenderer()
{
    ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Renderer.HotReload", "Renderer");
    ENGINE_MEMORY_TAG_SCOPE("Renderer");
    log::Info(std::string("Reloading render backend: ") + m_backend->Name());

#if ENGINE_ENABLE_IMGUI
    // The ImGui renderer backend owns API-native pipelines and descriptors,
    // and its callback points at the current IRenderBackend. Tear it down
    // before that backend and bind it again after recreation.
    if (m_imgui)
    {
        m_imgui.reset();
    }
#endif
    std::unique_ptr<IRenderBackend> previous = std::move(m_backend);
    previous->Shutdown();
    const BackendResourceStats released = previous->GetResourceStats();
    previous.reset();
    const bool leakedResources = released.LiveNativeAllocations != 0 ||
        released.MeshResources != 0 || released.TextureResources != 0 ||
        released.MaterialResources != 0;

    m_backend = CreateRenderBackend(m_desc.Window.api);
    profiling::MemoryTagScope backendTag(
        m_desc.Window.api == GraphicsApi::OpenGL ? "OpenGL" : "Vulkan");
    m_backend->Init(*m_window, m_desc.Renderer);
#if ENGINE_ENABLE_IMGUI
    if (m_desc.EnableImGui)
    {
        m_imgui = std::make_unique<editor::ImGuiLayer>();
        editor::ImGuiLayerConfig uiConfig;
        uiConfig.PlatformViewports = m_desc.EnableImGuiPlatformViewports;
        uiConfig.IniFilename = m_desc.ImGuiIniFilename;
        if (!m_imgui->Initialize(*m_window, *m_backend, uiConfig))
            throw std::runtime_error("Failed to reinitialize Dear ImGui after renderer reload");
        m_imgui->SetBuildCallback(m_imguiCallback);
    }
#endif
    m_sceneRenderer.ResetFrameHistory();

    const BackendCapabilities capabilities = m_backend->GetCapabilities();
    m_debugOverlay.SetValue("RENDERER", "ADAPTER", capabilities.AdapterName);
    m_debugOverlay.SetValue("RENDERER", "GPU VENDOR",
                            GpuVendorName(capabilities.Gpu.Device.Vendor));
    m_debugOverlay.SetValue("RENDERER", "DRIVER",
                            capabilities.Gpu.Device.DriverName.empty()
                                ? capabilities.Gpu.Device.ApiVersion
                                : capabilities.Gpu.Device.DriverName);
    m_debugOverlay.SetValue("RENDERER", "CAPABILITY TIER",
                            GpuFeatureTierName(capabilities.Gpu.Tier));
    m_debugOverlay.SetValue("RENDERER", "FALLBACKS",
                            std::to_string(capabilities.Gpu.Fallbacks.size()));
    m_debugOverlay.SetValue("RENDERER", "MSAA",
                            std::to_string(capabilities.ActiveMsaaSamples) + "X");
    m_debugOverlay.SetValue("RENDERER", "ANISOTROPY",
                            std::to_string(static_cast<int>(capabilities.ActiveAnisotropy)) + "X");
    const PipelineCacheStatistics pipelineCache = m_backend->GetPipelineCacheStats();
    m_debugOverlay.SetValue("RENDERER", "PIPELINE CACHE",
                            pipelineCache.Enabled
                                ? (pipelineCache.PersistentCacheLoaded ? "WARM" : "COLD")
                                : "DISABLED");
    m_debugOverlay.SetValue("RENDERER", "SHADER CACHE",
                            std::to_string(pipelineCache.ShaderPermutationHits) + " HIT / " +
                            std::to_string(pipelineCache.ShaderPermutationMisses) + " MISS");
    if (leakedResources)
        throw std::runtime_error("Renderer hot reload detected unreleased backend resources");
    log::Info(std::string("Render backend hot reload complete: ") + m_backend->Name());
}

void Application::EmitEvent(ApplicationEventType type, float deltaSeconds)
{
    plugin::PluginApplicationEvent pluginEvent;
    pluginEvent.Type = static_cast<plugin::PluginApplicationEventType>(type);
    pluginEvent.DeltaSeconds = deltaSeconds;
    pluginEvent.FrameIndex = m_frameCounter;
    m_plugins.BroadcastApplicationEvent(pluginEvent);
    if (m_eventCallback)
        m_eventCallback({type, deltaSeconds, m_frameCounter});
}

} // namespace engine
