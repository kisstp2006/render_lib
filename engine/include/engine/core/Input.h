#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace engine {

// Polls keyboard/mouse state each frame from a GLFW window. Kept independent
// of any render backend so both the GL and Vulkan paths can share it.
class Input
{
public:
    void Attach(GLFWwindow* window);
    void NewFrame();

    bool IsKeyDown(int glfwKeyCode) const;
    bool IsMouseButtonDown(int glfwButton) const;
    bool IsKeyDownRaw(int glfwKeyCode) const;
    bool IsMouseButtonDownRaw(int glfwButton) const;
    void SetUiCapture(bool keyboard, bool mouse)
    {
        m_uiCapturesKeyboard = keyboard;
        m_uiCapturesMouse = mouse;
    }

    glm::vec2 GetMouseDelta() const
    {
        return m_uiCapturesMouse ? glm::vec2(0.0f) : m_mouseDelta;
    }

private:
    GLFWwindow* m_window = nullptr;
    glm::vec2 m_mousePos{0.0f};
    glm::vec2 m_lastMousePos{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    bool m_firstFrame = true;
    bool m_uiCapturesKeyboard = false;
    bool m_uiCapturesMouse = false;
};

} // namespace engine
