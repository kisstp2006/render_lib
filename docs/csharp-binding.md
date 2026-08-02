# C# binding

`bindings/csharp/RenderingEngine` is a .NET 8 P/Invoke wrapper over the stable
`rendering/renderer_c.h` ABI. It does not expose C++ STL types and does not
copy an ECS into the renderer.

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

## Use from another C# project

```xml
<ItemGroup>
  <ProjectReference Include="path/to/rendering-engine/bindings/csharp/RenderingEngine/RenderingEngine.csproj" />
</ItemGroup>
```

Pass `NativeRendererDirectory` and `RendererShaderDirectory` as MSBuild
properties, or copy the native library and `shaders/` beside the application.

```csharp
using System.Numerics;
using RenderingEngine;

using var renderer = new Renderer(new RendererOptions { Backend = Backend.Vulkan });
var material = PbrMaterial.Default;
material.Metallic = 0.8f;
material.Roughness = 0.2f;
var sphere = renderer.AddObject(renderer.CreateSphere(),
    renderer.CreateMaterial(material),
    Renderer.Transform(Vector3.Zero, Vector3.Zero, Vector3.One));

while (renderer.PumpEvents())
    renderer.RenderFrame();
```

`Renderer` is thread-affine and must be disposed deterministically on the
thread that created it. Use `using`/`Dispose`; the binding deliberately does
not destroy a GLFW/Vulkan/OpenGL device from the .NET finalizer thread.

Examples:

- `Examples/BasicPbr`: interactive rotating PBR scene;
- `Examples/BackendSmoke`: hidden finite-frame OpenGL/Vulkan test with an
  optional screenshot.

