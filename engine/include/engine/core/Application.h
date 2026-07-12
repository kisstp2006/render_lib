#pragma once

#include <functional>
#include <memory>

#include "engine/core/Camera.h"
#include "engine/core/Input.h"
#include "engine/core/Window.h"
#include "engine/scene/Scene.h"

namespace engine {

class IRenderBackend;

// Owns the window, input, camera, active render backend and scene, and drives
// the main loop. Construct one, populate BuildScene, call Run().
class Application
{
public:
    explicit Application(const WindowDesc& desc);
    ~Application();

    Scene& GetScene() { return m_scene; }
    Camera& GetCamera() { return m_camera; }

    void Run();

private:
    WindowDesc m_desc;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<IRenderBackend> m_backend;
    Input m_input;
    Camera m_camera;
    Scene m_scene;
};

} // namespace engine
