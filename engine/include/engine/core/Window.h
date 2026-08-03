#pragma once

#include <functional>
#include <cstdint>
#include <limits>
#include <string>

struct GLFWwindow;

namespace engine
{

enum class GraphicsApi
{
    OpenGL,
    Vulkan
};

enum class WindowMode
{
    WindowedFixed,
    WindowedResizable,
    BorderlessFullscreen,
    ExclusiveFullscreen
};

enum class CursorMode
{
    Normal,
    Hidden,
    Captured
};

struct WindowDesc
{
    std::string title = "Rendering Engine";
    int width = 1600;
    int height = 900;
    GraphicsApi api = GraphicsApi::OpenGL;
    WindowMode mode = WindowMode::WindowedResizable;
    int monitor = -1; // -1 selects the primary monitor.
    int positionX = std::numeric_limits<int>::min();
    int positionY = std::numeric_limits<int>::min();
    bool centerOnMonitor = true;
    bool focusOnShow = true;
    bool visible = true;
    CursorMode cursor = CursorMode::Normal;
};

// Host-owned presentation surface. This keeps the renderer independent from
// the host's windowing library (Silk.NET, SDL, Qt, an editor, ...). All
// callbacks are invoked on the thread which created and drives the renderer.
struct ExternalWindowDesc
{
    void* userData = nullptr;
    int width = 1;
    int height = 1;
    GraphicsApi api = GraphicsApi::OpenGL;
    int (*shouldClose)(void*) = nullptr;
    void (*requestClose)(void*) = nullptr;
    void (*pollEvents)(void*) = nullptr;
    int (*makeContextCurrent)(void*) = nullptr;
    void* (*getGlProcAddress)(void*, const char*) = nullptr;
    void (*swapBuffers)(void*) = nullptr;
    void (*setSwapInterval)(void*, int) = nullptr;
    const char* const* (*getVulkanInstanceExtensions)(void*, uint32_t*) = nullptr;
    int (*createVulkanSurface)(void*, void*, const void*, uint64_t*) = nullptr;
};

// Thin GLFW wrapper. Owns the native window handle and exposes just enough
// to let a render backend attach a context/surface to it.
class Window
{
  public:
    explicit Window(const WindowDesc& desc);
    explicit Window(const ExternalWindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    void RequestClose() const;
    void PollEvents() const;
    void WaitEvents(double timeoutSeconds) const;
    void SwapBuffers() const;
    void SetSwapInterval(int interval) const;
    bool MakeContextCurrent() const;
    void* GetGlProcAddress(const char* name) const;
    const char* const* GetRequiredVulkanInstanceExtensions(uint32_t* count) const;
    bool CreateVulkanSurface(void* instance, const void* allocator,
                             uint64_t* surface) const;

    void SetTitle(const std::string& title);
    void SetCursorMode(CursorMode mode);
    void SetSize(int width, int height);
    void SetFramebufferSize(int width, int height);
    void SetMode(WindowMode mode, int monitor = -1, int width = 0, int height = 0);
    void Minimize();
    void Restore();

    bool IsFocused() const;
    bool IsVisible() const;
    bool IsMinimized() const;
    WindowMode Mode() const { return m_mode; }
    CursorMode DefaultCursorMode() const { return m_defaultCursorMode; }

    GLFWwindow* Handle() const { return m_handle; }
    bool IsExternal() const { return m_external; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    using ResizeCallback = std::function<void(int, int)>;
    void SetResizeCallback(ResizeCallback callback) { m_resizeCallback = std::move(callback); }
    using FocusCallback = std::function<void(bool)>;
    void SetFocusCallback(FocusCallback callback) { m_focusCallback = std::move(callback); }

  private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void FocusCallbackThunk(GLFWwindow* window, int focused);

    void RememberWindowedPlacement();

    GLFWwindow* m_handle = nullptr;
    ExternalWindowDesc m_externalDesc{};
    bool m_external = false;
    int m_width;
    int m_height;
    int m_windowedX = 0;
    int m_windowedY = 0;
    int m_windowedWidth = 1600;
    int m_windowedHeight = 900;
    WindowMode m_mode = WindowMode::WindowedResizable;
    CursorMode m_defaultCursorMode = CursorMode::Normal;
    ResizeCallback m_resizeCallback;
    FocusCallback m_focusCallback;
};

} // namespace engine
