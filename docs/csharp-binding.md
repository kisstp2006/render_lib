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
.NET 8 or a later compatible runtime, including a future .NET 10 application.
No Silk.NET dependency is required: window, OpenGL and Vulkan ownership remain
inside the native renderer.

`Renderer` is thread-affine and must be disposed deterministically on the
thread that created it. Use `using`/`Dispose`; the binding deliberately does
not destroy a GLFW/Vulkan/OpenGL device from the .NET finalizer thread.

Examples:

- `Examples/BasicPbr`: interactive rotating PBR scene;
- `Examples/BackendSmoke`: hidden finite-frame OpenGL/Vulkan test with an
  optional screenshot;
- `Examples/CustomShader`: HLSL reflection, compute dispatch, custom vertex
  layout, uniform/storage buffers and offscreen render-target parity sample.
