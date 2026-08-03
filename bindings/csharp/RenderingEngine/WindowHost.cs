using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace RenderingEngine;

/// <summary>
/// Presentation callbacks for a window owned by the embedding application.
/// Implement only the methods used by the selected graphics backend.
/// </summary>
public interface IRendererWindowHost
{
    bool ShouldClose => false;
    void RequestClose() { }
    void PollEvents() { }

    void MakeContextCurrent() =>
        throw new NotSupportedException("This host has no OpenGL context.");
    nint GetOpenGLProcAddress(string name) =>
        throw new NotSupportedException("This host has no OpenGL context.");
    void SwapBuffers() =>
        throw new NotSupportedException("This host has no OpenGL swapchain.");
    void SetSwapInterval(int interval) { }

    IReadOnlyList<string> GetRequiredVulkanInstanceExtensions() =>
        throw new NotSupportedException("This host has no Vulkan surface.");
    bool CreateVulkanSurface(nint instance, nint allocator, out ulong surface)
    {
        surface = 0;
        throw new NotSupportedException("This host has no Vulkan surface.");
    }
}

internal sealed unsafe class ExternalWindowBridge : IDisposable
{
    private readonly IRendererWindowHost _host;
    private GCHandle _self;
    private Native.ExternalWindowDesc* _native;
    private nint* _vulkanExtensions;
    private readonly List<nint> _vulkanStrings = [];
    private Exception? _pendingException;

    internal nint NativeDescription => (nint)_native;

    internal ExternalWindowBridge(IRendererWindowHost host, Backend backend)
    {
        _host = host;
        _self = GCHandle.Alloc(this);
        try
        {
            if (backend == Backend.Vulkan)
                CacheVulkanExtensions(host.GetRequiredVulkanInstanceExtensions());

            _native = (Native.ExternalWindowDesc*)NativeMemory.Alloc(
                (nuint)sizeof(Native.ExternalWindowDesc));
            *_native = new Native.ExternalWindowDesc
            {
                UserData = GCHandle.ToIntPtr(_self),
                ShouldClose = (nint)(delegate* unmanaged[Cdecl]<nint, int>)&ShouldClose,
                RequestClose = (nint)(delegate* unmanaged[Cdecl]<nint, void>)&RequestClose,
                PollEvents = (nint)(delegate* unmanaged[Cdecl]<nint, void>)&PollEvents,
                MakeContextCurrent = (nint)(delegate* unmanaged[Cdecl]<nint, int>)&MakeContextCurrent,
                GetGlProcAddress = (nint)(delegate* unmanaged[Cdecl]<nint, byte*, nint>)&GetGlProcAddress,
                SwapBuffers = (nint)(delegate* unmanaged[Cdecl]<nint, void>)&SwapBuffers,
                SetSwapInterval = (nint)(delegate* unmanaged[Cdecl]<nint, int, void>)&SetSwapInterval,
                GetVulkanInstanceExtensions = (nint)(delegate* unmanaged[Cdecl]<nint, uint*, nint>)&GetVulkanInstanceExtensions,
                CreateVulkanSurface = (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, ulong*, int>)&CreateVulkanSurface
            };
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    private void CacheVulkanExtensions(IReadOnlyList<string> extensions)
    {
        if (extensions.Count == 0)
            throw new InvalidOperationException("The window host returned no Vulkan instance extensions.");
        _vulkanExtensions = (nint*)NativeMemory.Alloc((nuint)extensions.Count, (nuint)sizeof(nint));
        for (int index = 0; index < extensions.Count; ++index)
        {
            nint value = Marshal.StringToCoTaskMemUTF8(extensions[index]);
            _vulkanStrings.Add(value);
            _vulkanExtensions[index] = value;
        }
    }

    private static ExternalWindowBridge Get(nint userData) =>
        (ExternalWindowBridge)(GCHandle.FromIntPtr(userData).Target ??
            throw new InvalidOperationException("Renderer window host was released."));

    private void Capture(Exception exception) =>
        Interlocked.CompareExchange(ref _pendingException, exception, null);

    internal void ThrowPendingException()
    {
        Exception? exception = Interlocked.Exchange(ref _pendingException, null);
        if (exception is not null)
            throw new InvalidOperationException("A renderer window-host callback failed.", exception);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShouldClose(nint userData)
    {
        try { return Get(userData)._host.ShouldClose ? 1 : 0; }
        catch (Exception exception) { Get(userData).Capture(exception); return 1; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RequestClose(nint userData)
    {
        try { Get(userData)._host.RequestClose(); }
        catch (Exception exception) { Get(userData).Capture(exception); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void PollEvents(nint userData)
    {
        try { Get(userData)._host.PollEvents(); }
        catch (Exception exception) { Get(userData).Capture(exception); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int MakeContextCurrent(nint userData)
    {
        try { Get(userData)._host.MakeContextCurrent(); return 1; }
        catch (Exception exception) { Get(userData).Capture(exception); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint GetGlProcAddress(nint userData, byte* name)
    {
        try
        {
            string symbol = Marshal.PtrToStringUTF8((nint)name) ?? string.Empty;
            return Get(userData)._host.GetOpenGLProcAddress(symbol);
        }
        catch (Exception exception) { Get(userData).Capture(exception); return 0; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SwapBuffers(nint userData)
    {
        try { Get(userData)._host.SwapBuffers(); }
        catch (Exception exception) { Get(userData).Capture(exception); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetSwapInterval(nint userData, int interval)
    {
        try { Get(userData)._host.SetSwapInterval(interval); }
        catch (Exception exception) { Get(userData).Capture(exception); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint GetVulkanInstanceExtensions(nint userData, uint* count)
    {
        ExternalWindowBridge bridge = Get(userData);
        *count = (uint)bridge._vulkanStrings.Count;
        return (nint)bridge._vulkanExtensions;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CreateVulkanSurface(nint userData, nint instance,
        nint allocator, ulong* surface)
    {
        try
        {
            bool success = Get(userData)._host.CreateVulkanSurface(
                instance, allocator, out ulong value);
            *surface = value;
            return success ? 1 : 0;
        }
        catch (Exception exception)
        {
            Get(userData).Capture(exception);
            *surface = 0;
            return 0;
        }
    }

    public void Dispose()
    {
        if (_native is not null)
        {
            NativeMemory.Free(_native);
            _native = null;
        }
        if (_vulkanExtensions is not null)
        {
            NativeMemory.Free(_vulkanExtensions);
            _vulkanExtensions = null;
        }
        foreach (nint value in _vulkanStrings)
            Marshal.FreeCoTaskMem(value);
        _vulkanStrings.Clear();
        if (_self.IsAllocated)
            _self.Free();
    }
}
