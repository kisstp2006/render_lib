#include "engine/core/Application.h"
#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLRenderBackend.h"
#include "engine/core/Log.h"
#include "engine/profiling/CpuProfiler.h"

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

} // namespace

Application::Application(const WindowDesc& desc) : Application(ApplicationDesc{desc}) {}

Application::Application(const ApplicationDesc& desc) : m_desc(desc)
{
    m_desc.MaximumDeltaSeconds = std::max(m_desc.MaximumDeltaSeconds, 0.001f);
    m_desc.FrameRateLimit = std::max(m_desc.FrameRateLimit, 0.0);
    m_desc.Renderer.MsaaSamples = std::max(m_desc.Renderer.MsaaSamples, 1u);
    m_desc.Renderer.MaxAnisotropy = std::max(m_desc.Renderer.MaxAnisotropy, 1.0f);

    m_window = std::make_unique<Window>(m_desc.Window);
    m_input.Attach(m_window->Handle());

    if (m_desc.Window.api == GraphicsApi::OpenGL)
    {
        m_backend = std::make_unique<GLRenderBackend>();
    }
    else
    {
#if ENGINE_HAS_VULKAN
        m_backend = std::make_unique<VulkanRenderBackend>();
#else
        throw std::runtime_error("Engine was built without Vulkan support (ENGINE_BUILD_VULKAN=OFF)");
#endif
    }

    m_backend->Init(*m_window, m_desc.Renderer);

    m_window->SetResizeCallback(
        [this](int w, int h)
        {
            if (w > 0 && h > 0)
                m_backend->Resize(w, h);
        });

    m_wasFocused = m_window->IsFocused();

    const BackendCapabilities capabilities = m_backend->GetCapabilities();
    m_debugOverlay.SetValue("APPLICATION", "WINDOW", WindowModeName(m_desc.Window.mode));
    m_debugOverlay.SetValue("APPLICATION", "PRESENT", PresentModeName(m_desc.Renderer.Presentation));
    m_debugOverlay.SetValue("APPLICATION", "FOCUS", m_wasFocused ? "FOREGROUND" : "BACKGROUND");
    m_debugOverlay.SetValue("RENDERER", "ADAPTER", capabilities.AdapterName);
    m_debugOverlay.SetValue("RENDERER", "MSAA", std::to_string(capabilities.ActiveMsaaSamples) + "X");
    m_debugOverlay.SetValue("RENDERER", "ANISOTROPY",
                            std::to_string(static_cast<int>(capabilities.ActiveAnisotropy)) + "X");
    if (capabilities.DedicatedVideoMemoryBytes > 0)
        m_debugOverlay.SetValue("RENDERER", "DEDICATED VRAM",
                                std::to_string(capabilities.DedicatedVideoMemoryBytes / (1024 * 1024)) +
                                    " MB");

    log::Info(std::string("Using render backend: ") + m_backend->Name());
}

Application::~Application()
{
    // Components contain callbacks into their owning plugin. Destroy them
    // before unloading modules, while renderer services are still alive.
    m_world.Clear();
    m_plugins.UnloadAll();
    if (m_backend)
        m_backend->Shutdown();
    if (m_profilerOwnedByDebugOverlay)
        profiling::CpuProfiler::Get().SetEnabled(false);
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
    if (m_window->ShouldClose())
        return false;

    profiling::CpuProfiler& profiler = profiling::CpuProfiler::Get();
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
    const float deltaTime =
        std::clamp(static_cast<float>(frameStart - m_lastFrameTime), 0.0f, m_desc.MaximumDeltaSeconds);
    m_lastFrameTime = frameStart;

    bool pauseCompletely = false;
    profiler.BeginFrame();
    {
        ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Application.Frame", "Frame");
        EmitEvent(ApplicationEventType::BeginFrame, deltaTime);

        {
            ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PollEvents", "Application");
            m_window->PollEvents();
            m_input.NewFrame();
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

            if (m_debugOverlay.IsVisible())
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("DebugOverlay.Update", "Debug");
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
                m_debugOverlay.Update(metrics, profiler.Snapshot());
            }

            EmitEvent(ApplicationEventType::BeforeRender, deltaTime);
            const RenderFrameData* frame = nullptr;
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("PrepareFrame", "Renderer");
                frame = &m_sceneRenderer.PrepareFrame(
                    m_scene, m_camera, m_window->Width(), m_window->Height(),
                    m_debugOverlay.IsVisible() ? &m_debugOverlay.Image() : nullptr);
            }
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("RenderBackend", "Renderer");
                m_backend->RenderFrame(*frame);
            }

            if (m_desc.Window.api == GraphicsApi::OpenGL)
            {
                ENGINE_CPU_PROFILE_SCOPE_CATEGORY("Present", "Renderer");
                m_window->SwapBuffers();
            }
            EmitEvent(ApplicationEventType::AfterRender, deltaTime);
            EmitEvent(ApplicationEventType::EndFrame, deltaTime);
        }
        else
        {
            EmitEvent(ApplicationEventType::EndFrame, deltaTime);
        }
    }
    profiler.EndFrame();
    if (pauseCompletely)
    {
        m_window->WaitEvents(0.05);
        return !m_window->ShouldClose();
    }
    ++m_frameCounter;

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
    m_desc.Renderer.Presentation = mode;
    const bool exact = m_backend->SetPresentMode(mode);
    m_debugOverlay.SetValue("APPLICATION", "PRESENT", PresentModeName(mode));
    if (!exact)
        log::Warn(std::string("Requested present mode is unavailable; backend "
                              "selected a safe fallback: ") +
                  PresentModeName(mode));
    return exact;
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
