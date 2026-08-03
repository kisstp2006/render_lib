# C# binding

`bindings/csharp/RenderingEngine` is a .NET 8 P/Invoke wrapper over the stable
`rendering/renderer_c.h` ABI. It does not expose C++ STL types and does not
copy an ECS into the renderer.

`bindings/csharp/RenderingEngine.Silk` is the optional Silk.NET 2.23 adapter.
It lets the application own the window and event loop while the native renderer
owns the OpenGL/Vulkan device and rendering resources.

## Build

```powershell
cmake -S . -B build -DENGINE_BUILD_CSHARP_BINDINGS=ON
cmake --build build --config Release --target renderer_csharp_examples
```

The build places three runtime pieces beside each C# example:

- `RenderingEngine.Managed.dll` — managed API;
- `RenderingEngine.dll` or `libRenderingEngine.so` — native renderer;
- `shaders/` — runtime HLSL sources.

The repository pins only this subtree to the installed .NET 8 SDK through
`bindings/csharp/global.json`; it does not change another project's SDK.

## Use with a Silk.NET window

```xml
<ItemGroup>
  <ProjectReference Include="path/to/rendering-engine/bindings/csharp/RenderingEngine/RenderingEngine.csproj" />
  <ProjectReference Include="path/to/rendering-engine/bindings/csharp/RenderingEngine.Silk/RenderingEngine.Silk.csproj" />
</ItemGroup>
```

Pass `NativeRendererDirectory` and `RendererShaderDirectory` as MSBuild
properties, or copy the native library and `shaders/` beside the application.

```csharp
using RenderingEngine;
using RenderingEngine.Silk;
using Silk.NET.Windowing;

Backend backend = Backend.Vulkan;
WindowOptions options = SilkRendererWindow.CreateOptions(backend, "My renderer host");
using IWindow window = Window.Create(options);
Renderer? renderer = null;

window.Load += () => renderer = new Renderer(new RendererOptions
{
    Backend = backend,
    Width = (uint)window.FramebufferSize.X,
    Height = (uint)window.FramebufferSize.Y,
    WindowHost = new SilkWindowHost(window)
});
window.FramebufferResize += size =>
{
    if (renderer is not null && size.X > 0 && size.Y > 0)
        renderer.Resize((uint)size.X, (uint)size.Y);
};
window.Render += dt => renderer?.RenderFrame((float)dt);
window.Closing += () => renderer?.Dispose();
window.Run();
```

For OpenGL, Silk.NET creates and owns the context; the adapter supplies GL
function addresses and buffer presentation to the renderer. For Vulkan,
Silk.NET supplies the required instance extensions and creates the surface for
the renderer-owned Vulkan instance. The callback ABI does not pass a
`GLFWwindow*` between two GLFW binaries.

The core binding can still create its standalone GLFW window when `WindowHost`
is null. Other window toolkits can implement `IRendererWindowHost` without
depending on Silk.NET.

## NativeAOT

The binding and Silk adapter are marked AOT/trimming compatible. Every C#
example enables `PublishAot`, explicitly registers Silk's GLFW platform, and
publishes the native renderer, shader compiler and `shaders/` tree beside the
native executable.

```powershell
dotnet publish bindings/csharp/Examples/BackendSmoke/BackendSmoke.csproj `
  -c Release -r win-x64 --self-contained true `
  -p:NativeRendererDirectory=build/bin/Release `
  -p:RendererShaderDirectory=build/bin/Release/shaders
```

Use `linux-x64` (or the matching architecture RID) with a Linux-built
`libRenderingEngine.so`. NativeAOT compiles the managed application and
bindings into an executable; the C++ renderer intentionally remains a separate
native shared library.

## Custom shaders and passes

`renderer.GraphicsDevice` mirrors the native programmable rendering API. It
accepts HLSL, GLSL and SPIR-V shader modules and exposes shader reflection,
define-based permutations, graphics/compute pipelines, custom vertex layouts,
buffers, textures, samplers and render targets.

```csharp
GraphicsDevice gpu = renderer.GraphicsDevice;
ShaderModule vertex = gpu.CreateShaderModule(new ShaderModuleOptions
{
    Stage = ShaderStage.Vertex,
    SourcePath = "shaders/custom.hlsl",
    EntryPoint = "VSMain",
    Defines = [new ShaderDefine("SKINNED", "0")]
});
ShaderReflection reflection = gpu.GetShaderReflection(vertex);

gpu.BeginRenderPass(new RenderPassOptions { ColorLoad = LoadAction.Clear });
gpu.BindGraphicsPipeline(pipeline);
gpu.BindVertexBuffer(0, vertices);
gpu.BindUniformBuffer(0, 0, frameConstants);
gpu.BindTexture(0, 1, albedo, linearSampler);
gpu.Draw(vertexCount);
gpu.EndRenderPass();
renderer.RenderFrame();
```

Recorded commands execute during the next `RenderFrame`. The same C# code runs
on OpenGL and Vulkan; backend-specific shader compilation and descriptor
binding stay behind the native ABI. `ReloadChangedShaders()` rebuilds changed
file-backed modules and their dependent pipelines while retaining the last
working version after a compile error.

The binding targets `net8.0`, so it can be referenced by applications using
.NET 8 or a later compatible runtime, including .NET 10 applications. Silk.NET
is isolated in the optional adapter package; the core managed binding has no
Silk.NET dependency.

`Renderer` is thread-affine and must be disposed deterministically on the
thread that created it. Use `using`/`Dispose`; the binding deliberately does
not destroy a GLFW/Vulkan/OpenGL device from the .NET finalizer thread.

Examples:

- `Examples/BasicPbr`: interactive rotating PBR scene hosted by Silk.NET;
- `Examples/BackendSmoke`: hidden finite-frame OpenGL/Vulkan test with an
  optional screenshot, also used for NativeAOT verification;
- `Examples/CustomShader`: HLSL reflection, compute dispatch, custom vertex
  layout, uniform/storage buffers and offscreen render-target parity sample.
