#pragma once

#include <functional>
#include <string>

struct GLFWwindow;

namespace engine {

enum class GraphicsApi { OpenGL, Vulkan };

struct WindowDesc
{
    std::string title = "Rendering Engine";
    int width = 1600;
    int height = 900;
    GraphicsApi api = GraphicsApi::OpenGL;
};

// Thin GLFW wrapper. Owns the native window handle and exposes just enough
// to let a render backend attach a context/surface to it.
class Window
{
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void RequestClose() const;
    void PollEvents() const;
    void SwapBuffers() const;

    GLFWwindow* Handle() const { return m_handle; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    using ResizeCallback = std::function<void(int, int)>;
    void SetResizeCallback(ResizeCallback callback) { m_resizeCallback = std::move(callback); }

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* m_handle = nullptr;
    int m_width;
    int m_height;
    ResizeCallback m_resizeCallback;
};

} // namespace engine
