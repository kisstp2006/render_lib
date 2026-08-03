# Renderer SDK

The repository can now be consumed as a renderer library without importing
the ECS, editor, asset pipeline or legacy sandbox. The public native target is
`engine::renderer`; it produces `RenderingEngine.dll` on Windows and
`libRenderingEngine.so` on Linux.

## Public layers

- `engine/renderer/Renderer.h` is the convenient C++20 facade. It owns its
  GLFW window, selected OpenGL/Vulkan backend, render scene and camera.
- `engine/renderer/GraphicsDevice.h` is the backend-neutral programmable GPU
  layer. It exposes custom shader modules, reflected graphics/compute
  pipelines, buffers, textures, samplers, vertex layouts and render passes.
- `rendering/renderer_c.h` is the stable C ABI. It uses opaque integer handles,
  fixed-layout structures, explicit create/destroy calls and thread-local
  error text. This is the supported boundary for C#, other languages and
  compiler-independent plugins.
- The old `engine::engine` mini-engine target still exists when
  `ENGINE_BUILD_EXTRAS=ON`, but is not required by the SDK.

The facade supports custom indexed meshes, cube/sphere primitives, PBR
materials, object transforms, directional and point lights, camera, exposure,
sky colors, frame statistics, resize/close handling and screenshots. The same
scene data is sent to either native backend.

## Programmable rendering

Call `Renderer::GetGraphicsDevice()` when an application needs rendering that
is not represented by the small built-in scene facade. `GraphicsDevice`
supports:

- HLSL and GLSL source compiled to SPIR-V, plus precompiled SPIR-V modules;
- vertex, fragment and compute shader modules with entry points and defines;
- reflection of vertex inputs, uniform/storage buffers, sampled/storage
  textures and samplers, including descriptor set and binding numbers;
- explicit graphics and compute pipelines with custom per-vertex/per-instance
  layouts, topology, culling, depth and blend state;
- vertex, index, uniform and storage buffers plus sampled/storage textures;
- offscreen color/depth render targets and explicit load/store/clear actions;
- file-backed hot reload and canonical define-based shader permutations.

Commands are recorded on the renderer thread and consumed by the next
`RenderFrame`, immediately before UI and presentation. A typical custom frame
is `BindComputePipeline`/`Dispatch`, followed by one or more
`BeginRenderPass`/bind/draw/`EndRenderPass` groups. Render target handle zero
selects the renderer-owned presentation target.

The complete backend-parity example is in `examples/custom_shader`. It renders
through an offscreen target, samples that result in the presentation pass and
also exercises a compute storage buffer.

File-backed modules can be checked with `ReloadChangedShaders`; changed source
or included files are recompiled and dependent pipelines are rebuilt. A failed
compile keeps the previous working shader and reports text through
`LastShaderError`. `CreateShaderPermutation` merges additional defines with a
base module and reuses an existing canonical permutation when possible.

Inline source uses `SourcePath` as a virtual filename and diagnostic/include
root, but the current inline-source path does not expand includes. Use a
file-backed module when includes or automatic hot reload are required.

## Add as a submodule

```cmake
set(ENGINE_BUILD_VULKAN ON CACHE BOOL "" FORCE)
set(ENGINE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(ENGINE_BUILD_RENDERER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENGINE_BUILD_RENDERER_TESTS OFF CACHE BOOL "" FORCE)
set(ENGINE_BUILD_CSHARP_BINDINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/rendering-engine)

target_link_libraries(my_application PRIVATE engine::renderer)
```

When this repository is included with `add_subdirectory`, extras, examples,
tests and tools default to `OFF`. It does not change the parent project's C++
standard, compiler warning policy, CRT selection or `BUILD_SHARED_LIBS`.

## Install and find_package

```bash
cmake -S . -B build-sdk -DENGINE_BUILD_EXTRAS=OFF \
  -DENGINE_BUILD_RENDERER_EXAMPLES=OFF
cmake --build build-sdk --config Release --target engine_renderer_sdk
cmake --install build-sdk --config Release --prefix out/sdk
```

Consumer:

```cmake
find_package(RenderingEngine 0.1 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE engine::renderer)
```

`RenderingEngine_SHADER_DIR` points to the installed shader directory. A
complete installed-package consumer is in `examples/package_consumer`.

## Runtime assets

Shaders are runtime assets, not compiled into the DLL. A normal build copies
them next to the renderer library in `shaders/`. If `RendererDesc::ShaderDirectory`
is empty, the SDK searches in this order:

1. `RENDERING_ENGINE_SHADER_DIR` environment variable;
2. `shaders/` beside the native renderer library;
3. the installed `../share/RenderingEngine/shaders` layout;
4. `shaders/` below the current working directory;
5. the source-tree path compiled into a developer build.

An application may set `RendererDesc::PipelineCacheDirectory` to control
where native shader and pipeline caches are stored.

## Lifetime and threading

- Create, render and destroy a renderer on the same thread. OpenGL contexts
  and GLFW window operations are thread-affine.
- Use the `rendering::RendererPtr` returned by `Renderer::Create`; its custom
  deleter performs destruction inside the shared library and is safe across
  the Windows Debug CRT boundary.
- Multiple facade windows share a reference-counted GLFW lifetime. All active
  instances must use the same runtime shader root.
- Destroying a mesh or material invalidates its handle for new objects;
  existing scene objects retain their underlying resource until removed.
- Call `PumpEvents` once per host frame, then update scene state and call
  `RenderFrame`. `Tick` is only the convenience combination of those calls.
- Create resources and record `GraphicsDevice` commands on the renderer thread.
  Do not destroy handles still referenced by commands waiting for the next
  frame. Vulkan resource destruction waits for submitted GPU work so immediate
  post-frame cleanup is validation-safe; batch destruction outside latency-
  sensitive frame code.
- Destroy render targets before their attachment textures, pipelines before
  their shader modules, and all custom handles before destroying the renderer.

The current facade owns a top-level window. Rendering directly into a foreign
native window or editor control is intentionally a later SDK extension; no
Edu ECS adapter is included in this layer.

## Build switches

| Option | Purpose | Embedded default |
| --- | --- | --- |
| `ENGINE_BUILD_VULKAN` | Vulkan backend in addition to OpenGL | `ON` when SDK is available |
| `ENGINE_BUILD_EXTRAS` | ECS/runtime/editor/assets/legacy app | `OFF` |
| `ENGINE_BUILD_RENDERER_EXAMPLES` | Small C++ SDK example | `OFF` |
| `ENGINE_BUILD_RENDERER_TESTS` | C/C++ ABI tests | `OFF` |
| `ENGINE_BUILD_CSHARP_BINDINGS` | Managed binding and C# examples | `OFF` |
