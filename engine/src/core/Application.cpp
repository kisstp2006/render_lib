#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/backend/IRenderBackend.h"
#include "engine/backend/gl/GLRenderBackend.h"

#if ENGINE_HAS_VULKAN
#include "engine/backend/vk/VulkanRenderBackend.h"
#endif

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace engine {

Application::Application(const WindowDesc& desc)
    : m_desc(desc)
{
    m_window = std::make_unique<Window>(desc);
    m_input.Attach(*m_window);

    if (desc.api == GraphicsApi::OpenGL)
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

    m_backend->Init(*m_window);

    m_window->SetResizeCallback([this](int w, int h) {
        if (w > 0 && h > 0)
            m_backend->Resize(w, h);
    });

    log::Info(std::string("Using render backend: ") + m_backend->Name());
}

Application::~Application()
{
    if (m_backend)
        m_backend->Shutdown();
}

void Application::Run()
{
    double lastTime = glfwGetTime();

    while (!m_window->ShouldClose())
    {
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastTime);
        lastTime = now;

        m_window->PollEvents();
        m_input.NewFrame();

        const bool lookEnabled = m_input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
        glfwSetInputMode(m_window->Handle(), GLFW_CURSOR, lookEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        m_camera.Update(m_input, deltaTime, lookEnabled);

        if (m_updateCallback)
            m_updateCallback(deltaTime);

        m_backend->RenderFrame(m_scene, m_camera);

        if (m_desc.api == GraphicsApi::OpenGL)
            m_window->SwapBuffers();
    }
}

} // namespace engine
