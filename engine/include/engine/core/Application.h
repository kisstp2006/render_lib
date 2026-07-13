#pragma once

#include <functional>
#include <memory>

#include "engine/backend/IRenderBackend.h"
#include "engine/core/ApplicationDesc.h"
#include "engine/core/Camera.h"
#include "engine/core/Input.h"
#include "engine/core/Window.h"
#include "engine/debug/DebugOverlay.h"
#include "engine/plugin/PluginManager.h"
#include "engine/render/SceneRenderer.h"
#include "engine/runtime/World.h"
#include "engine/scene/Scene.h"

namespace engine
{

// Owns the window, input, camera, active render backend and scene, and drives
// the main loop. Construct one, populate BuildScene, call Run().
class Application
{
  public:
    explicit Application(const WindowDesc& desc);
    explicit Application(const ApplicationDesc& desc);
    ~Application();

    Scene& GetScene() { return m_scene; }
    Camera& GetCamera() { return m_camera; }
    const Input& GetInput() const { return m_input; }
    Window& GetWindow() { return *m_window; }
    IRenderBackend& GetBackend() { return *m_backend; }
    debug::DebugOverlay& GetDebugOverlay() { return m_debugOverlay; }
    plugin::PluginManager& GetPlugins() { return m_plugins; }
    const plugin::PluginManager& GetPlugins() const { return m_plugins; }
    runtime::World& GetWorld() { return m_world; }
    const runtime::World& GetWorld() const { return m_world; }
    const ApplicationDesc& GetDescription() const { return m_desc; }

    // Called once per frame after input polling, before rendering. Use for
    // animation, sun changes, screenshot triggers, etc.
    using UpdateCallback = std::function<void(float deltaTime)>;
    void SetUpdateCallback(UpdateCallback callback) { m_updateCallback = std::move(callback); }

    using EventCallback = std::function<void(const ApplicationEvent& event)>;
    void SetEventCallback(EventCallback callback) { m_eventCallback = std::move(callback); }

    void Run();
    bool RunOneFrame();
    void RequestQuit() { m_window->RequestClose(); }
    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }
    void SetFrameRateLimit(double framesPerSecond);
    bool SetPresentMode(PresentMode mode);

  private:
    void EmitEvent(ApplicationEventType type, float deltaSeconds);

    ApplicationDesc m_desc;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<IRenderBackend> m_backend;
    Input m_input;
    Camera m_camera;
    Scene m_scene;
    SceneRenderer m_sceneRenderer;
    debug::DebugOverlay m_debugOverlay;
    runtime::ComponentRegistry m_componentRegistry;
    plugin::PluginManager m_plugins{m_componentRegistry};
    runtime::World m_world{m_componentRegistry};
    UpdateCallback m_updateCallback;
    EventCallback m_eventCallback;
    bool m_debugToggleWasDown = false;
    bool m_paused = false;
    bool m_wasFocused = true;
    bool m_hasFrameClock = false;
    bool m_profilerOwnedByDebugOverlay = false;
    double m_lastFrameTime = 0.0;
    uint64_t m_frameCounter = 0;
};

} // namespace engine
