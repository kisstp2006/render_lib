#include "engine/core/Input.h"

#include <GLFW/glfw3.h>

namespace engine {

void Input::Attach(GLFWwindow* window)
{
    m_window = window;
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, &Input::ScrollCallback);
}

void Input::NewFrame()
{
    if (!m_window)
        return;

    double x, y;
    glfwGetCursorPos(m_window, &x, &y);
    m_mousePos = {static_cast<float>(x), static_cast<float>(y)};

    if (m_firstFrame)
    {
        m_lastMousePos = m_mousePos;
        m_firstFrame = false;
    }

    m_mouseDelta = m_mousePos - m_lastMousePos;
    m_lastMousePos = m_mousePos;

    m_scrollDelta = m_scrollAccum;
    m_scrollAccum = 0.0f;
}

bool Input::IsKeyDown(int glfwKeyCode) const
{
    return m_window && glfwGetKey(m_window, glfwKeyCode) == GLFW_PRESS;
}

bool Input::IsMouseButtonDown(int glfwButton) const
{
    return m_window && glfwGetMouseButton(m_window, glfwButton) == GLFW_PRESS;
}

void Input::ScrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
    auto* self = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (self)
        self->m_scrollAccum += static_cast<float>(yoffset);
}

} // namespace engine
