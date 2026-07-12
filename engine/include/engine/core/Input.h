#pragma once

#include <array>
#include <glm/glm.hpp>

struct GLFWwindow;

namespace engine {

class Window;

// Polls keyboard/mouse state each frame from a GLFW window. Kept independent
// of any render backend so both the GL and Vulkan paths can share it.
class Input
{
public:
    ~Input();

    void Attach(Window& window);
    void NewFrame();

    bool IsKeyDown(int glfwKeyCode) const;
    bool IsMouseButtonDown(int glfwButton) const;

    glm::vec2 GetMousePosition() const { return m_mousePos; }
    glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
    float GetScrollDelta() const { return m_scrollDelta; }

private:
    Window* m_ownerWindow = nullptr;
    GLFWwindow* m_window = nullptr;
    glm::vec2 m_mousePos{0.0f};
    glm::vec2 m_lastMousePos{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    float m_scrollDelta = 0.0f;
    float m_scrollAccum = 0.0f;
    bool m_firstFrame = true;
};

} // namespace engine
