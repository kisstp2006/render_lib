#include "engine/core/Window.h"
#include "engine/core/Log.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace engine {

Window::Window(const WindowDesc& desc)
    : m_width(desc.width)
    , m_height(desc.height)
{
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    if (desc.api == GraphicsApi::OpenGL)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }
    else
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    m_handle = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!m_handle)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(m_handle, this);
    glfwSetFramebufferSizeCallback(m_handle, &Window::FramebufferSizeCallback);

    if (desc.api == GraphicsApi::OpenGL)
    {
        glfwMakeContextCurrent(m_handle);
        glfwSwapInterval(1);
    }

    log::Info("Window created: " + desc.title + " (" + std::to_string(desc.width) + "x" + std::to_string(desc.height) + ")");
}

Window::~Window()
{
    if (m_handle)
        glfwDestroyWindow(m_handle);
    glfwTerminate();
}

bool Window::ShouldClose() const
{
    return glfwWindowShouldClose(m_handle);
}

void Window::PollEvents() const
{
    glfwPollEvents();
}

void Window::SwapBuffers() const
{
    glfwSwapBuffers(m_handle);
}

void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self)
        return;

    self->m_width = width;
    self->m_height = height;

    if (self->m_resizeCallback)
        self->m_resizeCallback(width, height);
}

} // namespace engine
