using System.Runtime.InteropServices;
using Silk.NET.Core.Native;
using Silk.NET.Vulkan;
using Silk.NET.Windowing;
using Silk.NET.Windowing.Glfw;

namespace RenderingEngine.Silk;

/// <summary>
/// Connects a Silk.NET window to Rendering Engine without transferring window
/// or event-loop ownership to the native renderer.
/// </summary>
public sealed unsafe class SilkWindowHost(IWindow window) : IRendererWindowHost
{
    public IWindow Window { get; } = window;

    public bool ShouldClose => Window.IsClosing;
    public void RequestClose() => Window.Close();

    // Silk.NET's Run/DoEvents loop owns event dispatch. The callback is kept a
    // no-op so Renderer.PumpEvents remains safe when embedded in that loop.
    public void PollEvents() { }

    public void MakeContextCurrent() =>
        (Window.GLContext ?? throw new InvalidOperationException(
            "The Silk.NET window has no OpenGL context.")).MakeCurrent();

    public nint GetOpenGLProcAddress(string name)
    {
        var context = Window.GLContext ?? throw new InvalidOperationException(
            "The Silk.NET window has no OpenGL context.");
        return context.TryGetProcAddress(name, out nint address) ? address : 0;
    }

    public void SwapBuffers() =>
        (Window.GLContext ?? throw new InvalidOperationException(
            "The Silk.NET window has no OpenGL context.")).SwapBuffers();

    public void SetSwapInterval(int interval) =>
        (Window.GLContext ?? throw new InvalidOperationException(
            "The Silk.NET window has no OpenGL context.")).SwapInterval(interval);

    public IReadOnlyList<string> GetRequiredVulkanInstanceExtensions()
    {
        var vkSurface = Window.VkSurface ?? throw new InvalidOperationException(
            "The Silk.NET window has no Vulkan surface provider.");
        byte** names = vkSurface.GetRequiredExtensions(out uint count);
        string[] result = new string[count];
        for (uint index = 0; index < count; ++index)
            result[index] = Marshal.PtrToStringUTF8((nint)names[index]) ??
                throw new InvalidOperationException("Silk.NET returned an invalid Vulkan extension name.");
        return result;
    }

    public bool CreateVulkanSurface(nint instance, nint allocator,
        out ulong surface)
    {
        var vkSurface = Window.VkSurface ?? throw new InvalidOperationException(
            "The Silk.NET window has no Vulkan surface provider.");
        VkNonDispatchableHandle handle = vkSurface.Create<AllocationCallbacks>(
            new VkHandle(instance), (AllocationCallbacks*)allocator);
        surface = handle.Handle;
        return surface != 0;
    }
}

public static class SilkRendererWindow
{
    public static WindowOptions CreateOptions(Backend backend, string title,
        int width = 1280, int height = 720, bool visible = true)
    {
        // Explicit registration is required when reflection-free NativeAOT
        // removes Windowing's platform discovery path.
        GlfwWindowing.Use();
        WindowOptions options = backend == Backend.Vulkan
            ? WindowOptions.DefaultVulkan
            : WindowOptions.Default;
        if (backend == Backend.OpenGL)
            options.API = new GraphicsAPI(ContextAPI.OpenGL, ContextProfile.Core,
                ContextFlags.ForwardCompatible, new APIVersion(4, 6));
        options.Size = new global::Silk.NET.Maths.Vector2D<int>(width, height);
        options.Title = title;
        options.IsVisible = visible;
        options.VSync = false;
        options.ShouldSwapAutomatically = false;
        return options;
    }
}
