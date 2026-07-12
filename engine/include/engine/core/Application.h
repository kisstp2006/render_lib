#pragma once

#include <functional>
#include <memory>

#include "engine/backend/IRenderBackend.h"
#include "engine/core/Camera.h"
#include "engine/core/Input.h"
#include "engine/core/Window.h"
#include "engine/scene/Scene.h"

namespace engine {

// Owns the window, input, camera, active render backend and scene, and drives
// the main loop. Construct one, populate BuildScene, call Run().
class Application
{
public:
    explicit Application(const WindowDesc& desc);
    ~Application();

    Scene& GetScene() { return m_scene; }
    Camera& GetCamera() { return m_camera; }
    const Input& GetInput() const { return m_input; }
    Window& GetWindow() { return *m_window; }
    IRenderBackend& GetBackend() { return *m_backend; }

    // Called once per frame after input polling, before rendering. Use for
    // animation, sun changes, screenshot triggers, etc.
    using UpdateCallback = std::function<void(float deltaTime)>;
    void SetUpdateCallback(UpdateCallback callback) { m_updateCallback = std::move(callback); }

    void Run();

private:
    WindowDesc m_desc;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<IRenderBackend> m_backend;
    Input m_input;
    Camera m_camera;
    Scene m_scene;
    UpdateCallback m_updateCallback;
};

} // namespace engine
