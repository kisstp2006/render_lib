# Engine module architecture

The engine is split into independently compiled CMake targets. This follows
the useful dependency direction of ezEngine's separate `Foundation`, `Core`,
`RendererFoundation`, `RendererCore`, API renderer, and `GameEngine` projects,
scaled down for this renderer. ezEngine also groups renderer implementation by
responsibility such as Pipeline, Material, Lights, Meshes and Textures; this
project keeps those smaller areas as source subdirectories until their size
justifies another binary boundary.

References:

- [ezEngine engine projects](https://github.com/ezEngine/ezEngine/tree/dev/Code/Engine)
- [ezEngine RendererCore layout](https://github.com/ezEngine/ezEngine/tree/dev/Code/Engine/RendererCore)

## Dependency direction

```text
Application -> Runtime -> Foundation
           |-> AssetTools -> AssetPipeline -> Resources -> AssetCore -> Foundation
           |              -> Assets -> AssetCookers -> Scene -> Foundation
           |-> RendererOpenGL -> RendererCore -> Core -> Foundation
           |                   |               -> Scene
           |-> RendererVulkan -> RendererCore

RendererOpenGL/Vulkan -> RenderTools -> ThirdParty
engine compatibility facade -> every enabled module
```

Arrows mean "may depend on" from the higher-level target toward a lower-level
target. Native backends consume the same `Scene`, `Camera` and
`RenderFrameData`; they never own duplicate scene implementations.

## Targets

| CMake target | Alias | Responsibility |
| --- | --- | --- |
| `engine_foundation` | `engine::foundation` | Logging and CPU/GPU/memory profiling primitives |
| `engine_asset_core` | `engine::asset_core` | GUIDs, descriptor serialization/migration, hashes and dependency database |
| `engine_resources` | `engine::resources` | Cooked resource format, runtime registry, typed handles/loaders and weak cache |
| `engine_asset_pipeline` | `engine::asset_pipeline` | Import dispatch, validation, dependency-ordered transform and dirty-state policy |
| `engine_asset_tools` | `engine::asset_tools` | UI-independent inspector/browser/drop/preview models |
| `engine_core` | `engine::core` | Window, input, camera and RenderDoc integration |
| `engine_scene` | `engine::scene` | Backend-independent meshes, materials, lights and environment |
| `engine_asset_cookers` | `engine::asset_cookers` | BC/ASTC encoding, meshoptimizer LOD/geometry optimization and collision BVH/convex cooking |
| `engine_assets` | `engine::assets` | Texture, color-LUT, cubemap, skybox, material, glTF static-mesh and scene asset types plus compatibility source loaders |
| `engine_runtime` | `engine::runtime` | World hierarchy, components and runtime C++ plugins |
| `engine_renderer_core` | `engine::renderer_core` | Shared CSM, exposure, TAA, frame preparation and debug UI |
| `engine_renderer_opengl` | `engine::renderer_opengl` | OpenGL resource ownership, passes and synchronization |
| `engine_renderer_vulkan` | `engine::renderer_vulkan` | Vulkan resource ownership, pipelines, descriptors and synchronization |
| `engine_application` | `engine::application` | Main loop, configuration and backend selection |
| `engine_render_tools` | `engine::render_tools` | Linear HDR/LDR image comparison and report generation |
| `engine_third_party` | `engine::third_party` | Third-party implementation translation units |
| `engine` | `engine::engine` | Backward-compatible facade aggregating every enabled module |

Each target is a separate Visual Studio project under an `Engine/...` solution
folder. Samples live under `Samples`, test executables/plugins under `Tests`,
and standalone developer tools under `Tools`.

## Source layout

Public include paths remain stable under `engine/include/engine`. Implementation
files remain under the matching `engine/src` category. The module manifests are
isolated under `engine/modules/<Module>/CMakeLists.txt`; this gives every module
clear ownership without breaking downstream include paths merely for a build
system reorganization.

`engine/cmake/EngineModule.cmake` applies common compiler warnings, include
paths, source grouping, aliases and IDE folders. New modules should use
`engine_add_module` instead of duplicating target policy.

## Linking

Applications that need the complete runtime should keep the compatible link:

```cmake
target_link_libraries(my_app PRIVATE engine)
```

Narrow tools can avoid pulling renderer backends:

```cmake
target_link_libraries(asset_tool PRIVATE engine::assets engine::scene)
target_link_libraries(image_diff PRIVATE engine::render_tools)
```

Do not link a lower module upward merely to access one helper. Move a genuinely
general helper down to Foundation, or introduce a small neutral module. In
particular:

- Scene and Assets must not include OpenGL or Vulkan types.
- RendererCore must not include a native backend header.
- OpenGL and Vulkan must not link to each other.
- Application is the only engine module that selects a concrete backend.
- Tools and tests may depend on engine modules; engine runtime modules must not
  depend on sample or test targets.
