#include "engine/core/Window.h"
#include "engine/core/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>

namespace engine
{

namespace
{

GLFWmonitor* SelectMonitor(int requestedIndex)
{
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (!monitors || count == 0)
        return nullptr;
    if (requestedIndex < 0)
        return glfwGetPrimaryMonitor();
    return monitors[std::clamp(requestedIndex, 0, count - 1)];
}

int CursorValue(CursorMode mode)
{
    switch (mode)
    {
    case CursorMode::Hidden:
        return GLFW_CURSOR_HIDDEN;
    case CursorMode::Captured:
        return GLFW_CURSOR_DISABLED;
    default:
        return GLFW_CURSOR_NORMAL;
    }
}

} // namespace

Window::Window(const WindowDesc& desc)
    : m_width(desc.width), m_height(desc.height), m_windowedWidth(desc.width), m_windowedHeight(desc.height),
      m_mode(desc.mode), m_defaultCursorMode(desc.cursor)
{
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, desc.focusOnShow ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, desc.mode == WindowMode::WindowedResizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, desc.mode == WindowMode::BorderlessFullscreen ? GLFW_FALSE : GLFW_TRUE);

    if (desc.api == GraphicsApi::OpenGL)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    }
    else
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    GLFWmonitor* monitor = SelectMonitor(desc.monitor);
    int createWidth = desc.width;
    int createHeight = desc.height;
    GLFWmonitor* createMonitor = nullptr;
    if (desc.mode == WindowMode::BorderlessFullscreen && monitor)
    {
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        createWidth = videoMode->width;
        createHeight = videoMode->height;
    }
    else if (desc.mode == WindowMode::ExclusiveFullscreen)
    {
        createMonitor = monitor;
    }

    m_handle = glfwCreateWindow(createWidth, createHeight, desc.title.c_str(), createMonitor, nullptr);
    if (!m_handle)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(m_handle, this);
    glfwSetFramebufferSizeCallback(m_handle, &Window::FramebufferSizeCallback);
    glfwSetWindowFocusCallback(m_handle, &Window::FocusCallbackThunk);

    if (desc.mode == WindowMode::BorderlessFullscreen && monitor)
    {
        int x = 0, y = 0;
        glfwGetMonitorPos(monitor, &x, &y);
        glfwSetWindowPos(m_handle, x, y);
    }
    else if (desc.mode == WindowMode::WindowedFixed || desc.mode == WindowMode::WindowedResizable)
    {
        if (desc.positionX != std::numeric_limits<int>::min() &&
            desc.positionY != std::numeric_limits<int>::min())
        {
            glfwSetWindowPos(m_handle, desc.positionX, desc.positionY);
        }
        else if (desc.centerOnMonitor && monitor)
        {
            int x = 0, y = 0, workWidth = 0, workHeight = 0;
            glfwGetMonitorWorkarea(monitor, &x, &y, &workWidth, &workHeight);
            glfwSetWindowPos(m_handle, x + (workWidth - createWidth) / 2,
                             y + (workHeight - createHeight) / 2);
        }
        RememberWindowedPlacement();
    }

    glfwGetFramebufferSize(m_handle, &m_width, &m_height);
    SetCursorMode(desc.cursor);

    if (desc.api == GraphicsApi::OpenGL)
    {
        glfwMakeContextCurrent(m_handle);
    }

    log::Info("Window created: " + desc.title + " (" + std::to_string(desc.width) + "x" +
              std::to_string(desc.height) + ")");
}

Window::~Window()
{
    if (m_handle)
        glfwDestroyWindow(m_handle);
    glfwTerminate();
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(m_handle); }

void Window::RequestClose() const { glfwSetWindowShouldClose(m_handle, GLFW_TRUE); }

void Window::PollEvents() const { glfwPollEvents(); }

void Window::WaitEvents(double timeoutSeconds) const { glfwWaitEventsTimeout(std::max(timeoutSeconds, 0.0)); }

void Window::SwapBuffers() const { glfwSwapBuffers(m_handle); }

void Window::SetSwapInterval(int interval) const
{
    glfwMakeContextCurrent(m_handle);
    glfwSwapInterval(interval);
}

void Window::SetTitle(const std::string& title) { glfwSetWindowTitle(m_handle, title.c_str()); }

void Window::SetCursorMode(CursorMode mode) { glfwSetInputMode(m_handle, GLFW_CURSOR, CursorValue(mode)); }

void Window::SetSize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    if (m_mode != WindowMode::WindowedFixed && m_mode != WindowMode::WindowedResizable)
        SetMode(WindowMode::WindowedResizable, -1, width, height);
    else
        glfwSetWindowSize(m_handle, width, height);
}

void Window::Minimize()
{
    glfwIconifyWindow(m_handle);
}

void Window::Restore()
{
    glfwRestoreWindow(m_handle);
}

void Window::RememberWindowedPlacement()
{
    if (m_mode != WindowMode::WindowedFixed && m_mode != WindowMode::WindowedResizable)
        return;
    glfwGetWindowPos(m_handle, &m_windowedX, &m_windowedY);
    glfwGetWindowSize(m_handle, &m_windowedWidth, &m_windowedHeight);
}

void Window::SetMode(WindowMode mode, int monitorIndex, int width, int height)
{
    if (mode == m_mode && width <= 0 && height <= 0)
        return;

    RememberWindowedPlacement();
    GLFWmonitor* monitor = SelectMonitor(monitorIndex);
    const GLFWvidmode* videoMode = monitor ? glfwGetVideoMode(monitor) : nullptr;

    if (mode == WindowMode::ExclusiveFullscreen && monitor && videoMode)
    {
        const int targetWidth = width > 0 ? width : videoMode->width;
        const int targetHeight = height > 0 ? height : videoMode->height;
        glfwSetWindowAttrib(m_handle, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(m_handle, monitor, 0, 0, targetWidth, targetHeight, videoMode->refreshRate);
    }
    else if (mode == WindowMode::BorderlessFullscreen && monitor && videoMode)
    {
        int x = 0, y = 0;
        glfwGetMonitorPos(monitor, &x, &y);
        glfwSetWindowMonitor(m_handle, nullptr, x, y, videoMode->width, videoMode->height, GLFW_DONT_CARE);
        glfwSetWindowAttrib(m_handle, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowAttrib(m_handle, GLFW_RESIZABLE, GLFW_FALSE);
    }
    else
    {
        const int targetWidth = width > 0 ? width : m_windowedWidth;
        const int targetHeight = height > 0 ? height : m_windowedHeight;
        glfwSetWindowMonitor(m_handle, nullptr, m_windowedX, m_windowedY, targetWidth, targetHeight,
                             GLFW_DONT_CARE);
        glfwSetWindowAttrib(m_handle, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowAttrib(m_handle, GLFW_RESIZABLE,
                            mode == WindowMode::WindowedResizable ? GLFW_TRUE : GLFW_FALSE);
    }
    m_mode = mode;
}

bool Window::IsFocused() const { return glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) == GLFW_TRUE; }

bool Window::IsVisible() const
{
    return glfwGetWindowAttrib(m_handle, GLFW_VISIBLE) == GLFW_TRUE && !IsMinimized();
}

bool Window::IsMinimized() const { return glfwGetWindowAttrib(m_handle, GLFW_ICONIFIED) == GLFW_TRUE; }

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

void Window::FocusCallbackThunk(GLFWwindow* window, int focused)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_focusCallback)
        self->m_focusCallback(focused == GLFW_TRUE);
}

} // namespace engine
