# Source-Like Rendering Engine

A from-scratch C++20 / CMake renderer aiming at the *look* of Valve's Source 2
renderer: physically based metallic/roughness materials, Cook-Torrance GGX
specular, image-based lighting, HDR bloom and Source's tonemapper — built up
incrementally with an OpenGL 4.6 backend first and a Vulkan backend growing
alongside it behind a shared, small interface (`IRenderBackend`).

Current feature set (OpenGL backend):

- **HDR pipeline**: MSAA RGBA16F scene target, resolve, post pass to backbuffer
- **PBR**: Cook-Torrance GGX with the same D/G/F terms as VRF's `pbr.slang`;
  albedo/normal/MRAO texture maps (Source 2 channel convention) + scalars
- **IBL**: procedural sky or linear Radiance `.hdr` equirectangular panorama
  converted to a cubemap, irradiance convolution, GGX-prefiltered specular
  mips and split-sum BRDF LUT (VRF `EnvBRDF` shape); cached CPU/GPU source
  data and key-based re-baking only when an IBL input changes. The low-
  resolution procedural IBL cubemap is never displayed as the background;
  the visible procedural sky is evaluated directly at screen resolution.
- **Procedural sun**: screen-resolution anti-aliased HDR disk and halo driven
  by the same directional-light vector as direct lighting and CSM shadows
- **Day/night sky**: solar-elevation-driven sunset and civil/nautical/
  astronomical twilight, warm fading direct sun, dark night IBL and a stable
  procedural HDR star field; artist controls cover night brightness/horizon,
  star density/intensity/size/twinkle and procedural Milky Way color/strength
- **Local lights**: up to 8 point, 4 spot and 4 Source 2 barn/rect-inspired
  area lights with finite-luminaire response, soft rectangular edges and
  per-light cookies
- **Local shadows**: up to 4 omnidirectional point shadows in a cubemap array;
  all active spot and area shadows packed into a shared 4096 px atlas
- **Cascaded sun shadows**: four camera-fitted, texel-stabilized cascades with
  logarithmic/linear split blending, PCF, transition blending and a debug view
- **Gradient fog**: distance x height ramps with exponents, mirroring VRF's
  `ApplyGradientFog` (fog.slang)
- **Auto-exposure**: Source 2 tonemap-controller style adaptation
  (Key/avgLuminance clamped to min/max, smoothed over time)
- **Color grading**: backend-independent `.cube` 3D LUT loading with domain
  metadata, cached RGB16F 3D textures, Source 2/VRF half-texel addressing,
  runtime blending, plus saturation/contrast/tint and a built-in cinematic LUT
- **Anti-aliasing**: selectable none/FXAA/TAA modes. TAA uses an 8-sample
  Halton jitter, per-object and camera motion vectors, ping-pong HDR/depth
  history, YCoCg variance clipping, disocclusion rejection, camera-cut/resize
  resets and configurable sharpening; FXAA runs after tonemap/color grading
- **Bloom**: Jimenez 13-tap downsample with Karis average + threshold
  (mirroring VRF's `downsample_bloomthreshold`), tent-filter upsample chain
- **Post**: exposure, ADD-bloom composite, Uncharted tonemapper with VRF's
  exact parameterization (shoulder/linear/toe + precomputed white point
  scale), exact linear->sRGB, banding dither
- **glTF 2.0 scenes**: `.gltf` and `.glb`, hierarchical transforms,
  multi-primitive meshes, metallic-roughness materials, embedded/external
  textures, alpha masking, and generated normals/tangents when absent
- **Render to PNG**: `--screenshot out.png` headless-ish capture or F12 in
  the sandbox — usable as a library for offline rendering

Current Vulkan renderer (core OpenGL image parity complete):

- Vulkan 1.3 device with dynamic rendering and synchronization2 enabled
- swapchain, resize handling, two frames in flight and validation in Debug
- runtime shaderc compilation of source GLSL into dependency-aware cached SPIR-V;
  editing a Vulkan shader or any of its includes takes effect on the next launch
- indexed mesh rendering, glTF metallic-roughness textures, normal mapping,
  alpha-mask materials, procedural sky, directional/point/spot/area PBR lights
- four camera-fitted directional shadow cascades with PCF and boundary blending;
  point cubemap shadows, projected spot/area shadows and light cookies
- procedural day/night sky and cached HDRI cubemap, irradiance, GGX-prefilter and
  BRDF-LUT compute baking
- RGBA16F HDR main pass, 4x MSAA resolve, asynchronous auto-exposure, Jimenez
  bloom, Uncharted tonemap, 3D color LUT, FXAA and velocity/depth-history TAA
- validated shader modules, per-frame-safe material/shadow descriptor sets and
  per-object push constants
- backend-owned buffer/image allocator, mipmapped anisotropic material textures,
  per-frame uniforms and resize-safe swapchain-dependent render targets
- asynchronous OpenGL/Vulkan GPU profiling for directional/local shadows,
  main HDR, post and debug UI, with native pipeline statistics, bounded
  history, driver VRAM budgets, engine-owned Vulkan allocation tracking and
  JSON export

The platform layer is cross-platform by construction (GLFW, no Win32-only
code paths) and the current OpenGL 4.6/Vulkan targets support Windows and
Linux. macOS needs a future OpenGL 4.1 compatibility path or Vulkan through
MoltenVK; the current renderer does not build there as-is.

## Why this shape

- **One renderer front end, native back ends.** `SceneRenderer` prepares one
  immutable `RenderFrameData` packet per application frame: camera matrices,
  day/night sun state, CSM data, selected local lights and tonemap constants.
  OpenGL and Vulkan consume that exact packet. Native allocation, descriptors,
  synchronization and command recording remain backend-specific because their
  APIs have materially different lifetime models.
- **CPU-side scene is backend-agnostic.** `MeshData`, `Material`, `Scene`,
  `Camera` don't know about GL or Vulkan; both backends consume the same data.
- **Shared shading math.** Both backends include common Cook-Torrance/GGX,
  tonemap/color-space, TAA and bloom math from `shaders/common`, preventing
  visually important equations from drifting. API-specific shader files only
  adapt bindings, clip-space conventions and native render-target contracts.
- **Shared renderer utilities.** Shader source/include loading, auto-exposure
  adaptation, temporal sampling, cascade preparation and GPU timing aggregation
  are backend-neutral; only native queries, resource ownership and command
  recording remain inside the OpenGL/Vulkan implementations.
- **Reference for the PBR math and Source "feel":** the lighting model
  (`shaders/gl/lighting/pbr.frag`) mirrors the D/G/F terms in Valve's own
  `ValveResourceFormat/Renderer/Shaders/common/pbr.slang` (GGX distribution,
  Schlick-Smith visibility, Schlick Fresnel) so material response reads the
  same way Source 2 materials do, without depending on that project's code.

## Layout

```
engine/                  Modular engine libraries plus compatibility facade
  cmake/                  Shared target policy and module helper
  modules/                One CMake/IDE project per architectural module
    Foundation/           Logging and CPU/GPU/memory profiling
    AssetCore/            GUIDs, descriptors, migration and dependency database
    Resources/            Validated cooked files and typed runtime resource cache
    AssetPipeline/        Import, fingerprint, validation and transform orchestration
    AssetTools/           Inspector/browser/drag/preview models without a GUI
    Core/                 Window, input, camera and RenderDoc host integration
    Scene/                Backend-neutral render scene data
    Assets/               Concrete texture/environment/material/model/scene asset types
    Runtime/              Components, world hierarchy and C++ plugins
    RendererCore/         Shared frame preparation and renderer algorithms
    RendererOpenGL/       Native OpenGL backend
    RendererVulkan/       Native Vulkan backend
    Application/          Main loop and backend selection
    RenderTools/          Visual comparison and HDR image tooling
    ThirdParty/           Third-party implementation translation units
  include/engine/
    asset/                glTF import and color-grading LUT assets/loaders
    core/                 Window, Input, Camera, Application, Log
    debug/                Dependency-free engine statistics overlay
    profiling/            Hierarchical CPU zones, timelines and trace export
    render/               Shared frame preparation, CSM and temporal-AA algorithms
    scene/                Mesh, materials, lights, environment and render settings
    backend/
      IRenderBackend.h     Shared Init/Resize/RenderFrame/Shutdown contract
      gl/                  OpenGL 4.6 backend (full renderer)
      vk/                  Vulkan resources, shader contract and backend scaffold
  src/                     Implementations mirroring the stable public include tree

shaders/
  common/                  PBR/BRDF math shared by OpenGL and Vulkan
  gl/common/               Shared fullscreen vertex stage
  gl/lighting/             PBR and directional/local shadow stages
  gl/environment/          Visible sky and IBL baking stages
  gl/post/                 Bloom, tonemap/LUT, FXAA and TAA
  vk/                      Runtime-compiled and cached Vulkan shader stages

examples/sandbox/src/      Shared sample application, scenes and runtime controls
examples/samples/          One small entry point per standalone renderer sample
examples/assets/           Bundled CC0 test assets

tests/src/                 Renderer CPU/regression test entry point
tools/assetc/              Headless asset import/transform/validation utility
```

The historical `engine` static target remains as a tiny compatibility facade,
so existing consumers do not need changes. New code may link narrower aliases
such as `engine::foundation`, `engine::scene`, `engine::renderer_core`,
`engine::renderer_opengl` or `engine::renderer_vulkan`. Module responsibilities,
allowed dependency directions and extension rules are documented in the
[engine module architecture guide](docs/engine-modules.md).

## Building

Requires CMake >= 3.21, Python 3 with Jinja2 for GLAD code generation, and a
C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+).
Dependencies (GLFW, GLAD2, GLM, stb) are fetched at configure time via
`FetchContent` — nothing to install beyond a compiler and CMake. The Vulkan
backend additionally requires the LunarG Vulkan SDK (including shaderc); if it
isn't found, `ENGINE_BUILD_VULKAN` is automatically disabled and only the
OpenGL backend is built. Vulkan GLSL is compiled when the application starts.
SPIR-V is cached under `build/runtime_shaders/vk/<config>` and automatically
rebuilt when its source or a transitive include changes.

The engine also builds `assetc`, a headless source-to-cooked asset tool. See
the [asset pipeline guide](docs/asset-pipeline.md) for descriptor formats,
runtime handles, supported asset types and command examples.

```powershell
cmake -B build -S . -G "Visual Studio 18 2026"
cmake --build build --config RelWithDebInfo
.\build\examples\sandbox\RelWithDebInfo\sandbox.exe
```

On Linux/macOS with Ninja:

```bash
cmake -B build -S . -G Ninja
cmake --build build
./build/examples/sandbox/sandbox
```

The build creates a generic command-line sandbox and eight focused sample
executables. They share the same scene/control implementation, so the samples
stay small while each can be launched directly:

| Executable | Demonstration |
| --- | --- |
| `sandbox` | Generic sandbox; all command-line scene switches remain available |
| `sample_materials` | Metallic-roughness PBR material grid |
| `sample_gltf` | Bundled glTF Water Bottle asset |
| `sample_lights` | Point, spot and area lights with shadowed/unshadowed pairs |
| `sample_hdri` | HDRI studio/product-lighting scene |
| `sample_day_night` | Animated procedural day/sunset/night sky cycle |
| `sample_post` | Color-grading LUT, FXAA and TAA validation scene |
| `sample_stability` | Resize/minimize/fullscreen, hot-reload and resource-lifetime stress suite |
| `sample_visibility` | CPU frustum/distance culling and color-coded world-bounds visualization |

For example, on Windows run
`.\build\examples\sandbox\RelWithDebInfo\sample_lights.exe`. Every focused
sample still accepts the common options below, including `--screenshot` and
`--frames`, for automated visual tests.

Every sample is one executable backed by one scene implementation. Use
`--vulkan` or `--opengl` to select the backend (default is OpenGL).
`sample_stability` runs the shared stability suite automatically. The same
suite can be enabled on any sample with `--stability-stress`; configure it with
`--stress-cycles`, `--stress-stage-frames`, `--stress-report`,
`--stress-max-cpu-growth-mb` and optionally `--stress-no-exclusive`. See the
[stability stress-test guide](docs/stability-stress-tests.md).
Run with `--sample-gltf` for the bundled Khronos Water Bottle, or with
`--gltf path/to/scene.glb` to load another static glTF 2.0 scene.
Use `--hdri path/to/environment.hdr` with any scene, or `--hdri-studio` for
the bundled CC0 Poly Haven product-lighting showcase.
Use `--day-night-showcase` for an automatically rotating directional light
and a complete day/sunset/twilight/starry-night/sunrise loop. Its default
cycle is 24 seconds; override it with `--day-night-seconds N`.
Use `--post-showcase` for the animated LUT/FXAA/TAA validation scene. Select
the initial mode with `--aa none|fxaa|taa`, load an industry-standard LUT with
`--color-lut path/to/look.cube`, or use `--cinematic-lut`; `--lut-weight N`
sets its initial blend. `--post-benchmark` reports asynchronous GPU timings
for the complete TAA/FXAA + bloom + tonemap + grading stack.
Advanced overrides are `--fxaa-subpixel`, `--fxaa-edge-threshold`,
`--fxaa-edge-threshold-min`, `--taa-history`, `--taa-sharpen`, `--taa-jitter`
and `--taa-depth-threshold`; the same fields are available through
`Scene::PostProcess` without backend-specific data.

**Controls:** right-click + mouse to look around, WASD to move, Q/E for
down/up, hold Shift to move faster. I/K/J/L move the sun (sky + IBL re-bake
live), F toggles the flashlight, C toggles cascade debug colors, F12 saves a
PNG render into `renders/`. The compact CPU-memory and GPU monitor cards are
visible by default; F3 toggles the larger detailed debug panel and `--debug-ui`
starts that panel visible. `--no-runtime-monitors` disables the two persistent
cards. F4 opens the in-engine frame debugger (`--frame-debugger` starts it
visible): left/right selects a render pass, up/down selects a texture,
comma/period selects a mip, brackets select an array layer or cubemap face,
and R refreshes the deliberately frozen GPU preview. The panel shows pass
order/timing, input/output relationships, format, dimensions, samples and
estimated memory identically on OpenGL and Vulkan. See the
[frame debugger guide](docs/frame-debugger.md). `--shadow-stress
--shadow-benchmark` runs the CSM
stress scene with asynchronous GPU timing. `--light-showcase` displays point,
spot and barn/area lights side by side, with shadowed examples on the left and
unshadowed examples on the right. In this showcase, T toggles shadows for all
three right-side lights while the left-side reference remains shadowed. When
an HDRI is loaded, H switches between it and the procedural sky, `[`/`]`
rotate it by 15 degrees, and `-`/`=` change IBL exposure by 0.25 EV.
For deterministic sky tests, `--sun-azimuth degrees` and
`--sun-elevation degrees` set the initial procedural-sun position; I/K can
move the sun down to -30 degrees for a complete sunset-to-night transition.
In the automatic showcase, P pauses/resumes time and comma/period halve or
double the cycle speed. The visible sun, direct light and CSM move every
frame, while direction-only IBL rebuilds are throttled to 350 ms.
Night-sky controls in the same showcase are B for stars, N for the Milky Way,
M to freeze/resume sky rotation and R to cycle natural/cool/warm/fantasy color
presets. Up/down change star density, while left/right change star intensity.
Post controls work in every scene: V cycles none/FXAA/TAA, G toggles the loaded
color LUT, and semicolon/apostrophe decrease/increase LUT weight by 0.1.

CPU profiling is opt-in, so disabled zones only perform a cheap runtime flag
check. `--cpu-profile <path.json>` records bounded hierarchical events and
per-thread timelines in Chrome Trace/Perfetto-compatible JSON;
`--cpu-profile-retain N` controls the retained frame window, while
`--cpu-profile-log` prints rolling top-zone statistics every 120 frames. For
example:

```powershell
.\build\examples\sandbox\Release\sample_day_night.exe --vulkan `
  --cpu-profile traces/daynight_vk.json --cpu-profile-log `
  --cpu-profile-retain 600
```

Engine and application code can add RAII instrumentation with
`ENGINE_CPU_PROFILE_SCOPE`, `ENGINE_CPU_PROFILE_SCOPE_CATEGORY` and
`ENGINE_CPU_PROFILE_FUNCTION` from `engine/profiling/CpuProfiler.h`. The main
application loop and the important OpenGL/Vulkan render passes are already
instrumented.

The CPU memory profiler tracks global C++ `new/delete` plus plugin allocations
made through the ABI v2 host allocator. It records current/peak usage,
allocation counts, subsystem tags, size buckets, bounded frame history and
live leak records. `--memory-profile <path.json>` exports a report,
`--memory-profile-retain N` controls retained frames and
`--memory-leak-report` prints remaining allocations at process shutdown.
The default runtime CPU-memory card enables collection automatically and
displays live memory, peak, largest tag and per-frame allocation/free traffic.
Engine code can classify work with
`ENGINE_MEMORY_TAG_SCOPE("Streaming")`. See the
[memory profiler guide](docs/memory-profiler.md).

The GPU profiler collects native, non-blocking timestamp and pipeline queries
on both backends. It retains per-pass history and min/average/max summaries,
tracks draw/dispatch and shader-invocation counts, reads Vulkan
`VK_EXT_memory_budget` or OpenGL `GL_NVX_gpu_memory_info`, and separately tracks
engine-owned Vulkan device allocations. `--gpu-profile <path.json>` exports a
report and `--gpu-profile-retain N` sets its bounded history. Disable all native
GPU queries with `--no-gpu-timing` or `Renderer.EnableGpuTiming = false`. See
the [GPU profiler guide](docs/gpu-profiler.md).

GPU discovery is also backend-neutral: vendor/device/driver identity, native
format and query support, active feature tier and every automatic fallback are
stored in one capability profile. Proven driver rules and hardware limits
gracefully reduce MSAA, anisotropy, presentation and profiling features;
unsupported BC/BC7/ASTC assets use their cooked RGBA8 safety mip chain instead
of failing at upload. `--gpu-capabilities <path.json>` exports the complete
profile, `--gpu-policy conservative` enables defensive caps and
`--no-driver-workarounds` is available for diagnosis. See the
[GPU capability and fallback guide](docs/gpu-capabilities.md).

Shader permutations and native pipelines have persistent, driver-safe caches:
OpenGL restores linked program binaries, while Vulkan caches content-addressed
SPIR-V and validates/persists one native pipeline cache. The
`pipeline_cache_benchmark` target runs isolated cold/warm passes on both APIs
and exports startup, cache and frame-time/p95 JSON metrics. See the
[pipeline cache and stutter benchmark guide](docs/pipeline-cache.md).

RenderDoc capture is deterministic and backend-neutral. `--renderdoc-capture
<path-template> --renderdoc-frame N` captures the same fixed-step frame from
OpenGL or Vulkan and exits after saving; `--renderdoc-library <path>` supports
direct launches when the process was not started from RenderDoc. Both backends
emit the same named frame/pass hierarchy and semantic GPU resource names. See
the [RenderDoc capture guide](docs/renderdoc-capture.md).

Golden-image regression tests capture both the final sRGB PNG and the linear,
pre-tonemap HDR scene color for the materials, glTF, local-lights and HDRI
samples. Separate OpenGL/Vulkan baselines catch backend-specific regressions;
a direct cross-backend comparison verifies parity with a tolerance designed
for localized rasterization-edge and highlight differences. Every comparison
emits a diff PNG, heatmap and JSON metrics report. Run
`cmake --build build --config Release --target golden_images`; see the
[visual regression guide](docs/visual-regression.md) before intentionally
updating baselines.

The small debug UI is engine-owned and has no ImGui dependency. Its atlas can
draw independent layers at any screen corner. By default the CPU-memory card
is at the top-right and the GPU adapter/timing/VRAM card is at the bottom-right;
the detailed F3 panel remains at the top-left. The detailed panel displays the
active API, resolution, FPS/frame time history, scene/triangle/light counts,
post settings and the hottest CPU profiler zones. Applications can add
persistent grouped values or move the monitor cards without backend-specific
code:

```cpp
app.GetDebugOverlay().SetValue("STREAMING", "VISIBLE CHUNKS", "24");
app.GetDebugOverlay().SetRuntimeMonitorPlacements(
    engine::debug::DebugOverlayPlacement::TopRight,
    engine::debug::DebugOverlayPlacement::BottomRight);
```

The architecture follows the useful parts of
[ezEngine's debug renderer](https://ezengine.net/pages/docs/debugging/debug-rendering.html):
backend-neutral collection, context-local frame data and a separate unlit
screen-space pass after image-quality processing. The implementation here is
purpose-built for this renderer and rasterizes its own tiny bitmap font.

Application/window/device settings now follow the same useful separation. Use
`ApplicationDesc` for process and window behavior, and keep image/art settings
on `Scene`. The common device configuration works on both OpenGL and Vulkan:

```cpp
engine::ApplicationDesc config;
config.Window.title = "Product Viewer";
config.Window.mode = engine::WindowMode::WindowedResizable;
config.Renderer.Presentation = engine::PresentMode::VSync;
config.Renderer.MsaaSamples = 4;
config.Renderer.MaxAnisotropy = 16.0f;
config.Unfocused = engine::UnfocusedBehavior::RenderOnly;
config.FrameRateLimit = 144.0;
// Default is true. This one flag disables both persistent runtime cards and
// their automatic CPU-memory collection for this Application.
config.EnableRuntimeMonitors = false;

engine::Application app(config);
app.Run();
```

Every sample accepts `--windowed`, `--fixed-window`, `--borderless`,
`--fullscreen`, `--title NAME`, `--resolution W H`, `--monitor N`,
`--position X Y`, `--cursor MODE`, `--vsync`,
`--no-vsync`, `--adaptive-vsync`, `--msaa N`, `--anisotropy N`, `--adapter NAME`,
`--no-prefer-discrete`, `--validation`, `--no-validation`, `--no-gpu-timing`,
`--gpu-policy default|conservative`, `--no-driver-workarounds`,
`--gpu-capabilities path.json`,
`--no-runtime-monitors`,
`--max-fps N`, `--max-delta N` and `--unfocused continue|render|pause`.
Profiling switches shared by every sample are `--cpu-profile path.json`,
`--cpu-profile-log`, `--cpu-profile-retain N`, `--memory-profile path.json`,
`--memory-profile-retain N`, `--memory-leak-report` and `--debug-ui`.
`--save-config path.cfg` stores the resolved settings; `--config path.cfg`
loads them, and later command-line switches override loaded values. See the
[ezEngine configuration study](docs/ezengine-application-renderer-settings.md)
for the source comparison and intentionally deferred renderer-pass features.
The build-wide `-DENGINE_ENABLE_RUNTIME_MONITORS=OFF` option removes automatic
runtime monitor activation from every application. See the
[runtime monitor guide](docs/runtime-monitors.md) for code, config and placement
examples.

Runtime C++ plugins now use an ABI-checked DLL/SO entry point rather than
sharing STL ownership across module boundaries. Plugins can declare
dependencies, receive application frame/update/render events, publish
versioned services and register entity components. Copy-on-load keeps the
original DLL buildable during development; shutdown is dependency-safe and a
module cannot unload while one of its component instances is alive.

```cpp
app.GetPlugins().AddSearchPath("plugins");
app.GetPlugins().LoadPlugin("ExamplePlugin",
    engine::plugin::PluginLoadFlags::LoadCopy);

auto entity = app.GetWorld().CreateEntity("Product");
app.GetWorld().AddComponent(entity, "example.Rotator");
```

The new `runtime::World` supplies generation-checked entities, name/tag/layer,
parent-child transforms, inherited active state and component lifecycle. It is
a behavior layer beside the common renderer `Scene`, not an OpenGL/Vulkan
scene duplicate. See the complete [runtime plugin and component guide](docs/runtime-cpp-plugins.md).

Night-sky command-line overrides are `--star-density`, `--star-intensity`,
`--star-size`, `--star-twinkle`, `--milky-way`, `--night-brightness` and
`--night-horizon-glow`. Animation and appearance can also be configured with
`--star-twinkle-speed`, `--night-sky-speed`, and `--night-preset` (natural,
cool, warm or fantasy). `--no-stars`, `--no-milky-way` and
`--static-night-sky` disable the corresponding effects. The same switches,
colors and numeric values are available to applications through `Scene::Sky`
(`SkySettings`) without depending on the OpenGL backend.

## Roadmap

Rough order, each step buildable/testable on its own:

1. **glTF scene loading** (initial static metallic-roughness support complete) so real test scenes/assets can be brought in instead
   of only procedural primitives — the biggest step toward "make pretty
   renders of real content".
2. **Cascaded shadow maps** — complete: four stabilized cascades, blended
   transitions, PCF, debug visualization and GPU benchmark mode.
3. **HDR equirect environment loading** — complete: linear float Radiance HDR
   loading, cubemap conversion, irradiance/GGX bake, yaw and separate IBL/
   background exposure, source switching, caching, diagnostics and a CC0
   studio product-render test scene.
4. **Color grading LUT + FXAA/TAA** — complete: `.cube` CPU loading and RGB16F
   3D texture cache, Source 2-compatible LUT addressing/blending, post-tonemap
   FXAA, jittered HDR TAA with camera/object motion vectors, depth history,
   variance clipping, disocclusion/camera-cut/resize handling, runtime controls,
   animated visual test scene and asynchronous GPU benchmark mode.
5. **Vulkan PBR parity** — core image parity complete: the shared Scene/Camera
   frame drives indexed PBR meshes, mipmapped anisotropic textures, CSM and
   local-light shadows/cookies, procedural/HDRI IBL, RGBA16F + 4x MSAA,
   auto-exposure, bloom, tonemap, 3D LUT, FXAA/TAA and PNG capture. All seven
   executables run on both backends without validation errors. The automated
   golden suite currently measures at most 0.0032 mean linear LDR error and
   0.0073 mean absolute HDR radiance error across its four static scenes.
6. **More local-light features** — complete: point cubemap shadows, shared
   spot/area shadow atlas, cookie atlas and Source 2-style barn/rect area lights.
7. Only *then* revisit whether a shared RHI abstraction actually pays for
   itself between the two backends.

Done so far: HDR+MSAA pipeline, procedural/HDRI IBL (irradiance/prefilter/BRDF LUT),
sun shadow mapping with PCF, Jimenez bloom, VRF-parameterized Uncharted
tonemap, textured materials with normal mapping, PNG capture, static glTF/GLB
scene loading with the CC0 Khronos Water Bottle sample.

### Extended roadmap after the current items

The order below deliberately prioritizes measurable performance, Source 2-like
material/lighting quality, AAA image stability and production robustness. A
feature is only considered complete when it has automated correctness tests,
a representative visual scene, resize/device-loss coverage where applicable,
and CPU/GPU timing plus memory-budget measurements. Items already implemented
above are not repeated here.

8. **Performance, diagnostics and stability baseline (P0)**
   - [x] CPU profiler with hierarchical RAII zones, per-thread timelines, bounded capture, rolling statistics and Chrome Trace export
   - [x] Memory profiler with global C++ and plugin-host allocation tracking, subsystem tags, current/peak and size-bucket statistics, bounded frame history, JSON export, debug UI, shutdown leak reports and multithreaded tests
   - [x] GPU profiler with asynchronous per-pass timestamps, pipeline statistics, VRAM budgets/peaks, bounded history, debug UI, JSON export and automated OpenGL/Vulkan runtime validation
   - [x] RenderDoc integration with named OpenGL/Vulkan resources, matching pass markers, fixed-step automatic capture and backend-neutral CLI validation mode
   - [x] In-engine frame debugger with unified pass order/timing, input/output resource links, texture metadata/memory and frozen mip/layer previews for OpenGL and Vulkan
   - [x] Golden-image visual regression tests with deterministic OpenGL/Vulkan LDR+HDR capture, mixed absolute/relative tolerance, diff images, heatmaps and JSON reports
   - [x] Long-running resize/minimize/fullscreen, hot-reload and resource-lifetime stress tests
   - [x] GPU vendor/driver capability database with shared OpenGL/Vulkan discovery, capability tiers, software-driver rules, JSON/debug-UI diagnostics and graceful MSAA/anisotropy/present/profiler/BC/ASTC fallback paths
   - [x] Persistent OpenGL program-binary and Vulkan pipeline caches, content-addressed canonical shader permutation caching, and automated cold/warm stutter regression benchmarks
   - [x] Configurable OpenGL/Vulkan render graph with declared inputs/outputs, deterministic dependency ordering, automatic barriers/layouts, explicit transient lifetimes and conservative target alias slots; integrated frame-debugger memory/timing diagnostics and regression tests ([guide](docs/render-graph.md))

9. **Multithreaded renderer and asynchronous data path (P0)**
   - [x] Job system and general thread pool
   - [x] Multithreaded render preparation and command generation
   - [x] Parallel visibility, animation and particle updates
   - [x] Asynchronous model/texture loading with cancellation and priorities
   - [x] Vulkan transfer queue uploads, staging-ring allocator and synchronization2 barriers
   - [x] Background shader/pipeline creation with a visible fallback material
   - [x] Frame-safe deferred destruction and per-frame GPU memory arenas
   - [x] Asset streaming governed by CPU, VRAM and I/O budgets

   Implementation and safety contracts: [multithreaded/asynchronous rendering guide](docs/asynchronous-rendering.md).

10. **Visibility, batching and GPU-driven rendering (P0)**
    - [x] CPU frustum and distance culling with bounds/debug visualization ([guide](docs/visibility-culling.md))
    - [ ] GPU Hi-Z occlusion culling with temporal conservatism to prevent popping
    - [ ] GPU instancing and hierarchical instancing (HISM)
    - [ ] Static batching, selective dynamic batching and offline mesh combining
    - [ ] Authored and generated LOD chains with screen-space error selection
    - [ ] HLOD cluster generation, impostors and streaming integration
    - [ ] Indirect rendering and Multi Draw Indirect
    - [ ] GPU-generated draw lists and fully GPU-driven submission
    - [ ] Bindless textures/material resources with non-bindless fallback
    - [ ] Meshlet generation and hierarchical cluster culling
    - [ ] Visibility-buffer rendering path for extremely high geometry density
    - [ ] Mesh shader path on capable GPUs with classic vertex/index fallback

11. **Forward+ and large-light scalability (P0)**
    - [ ] Profile-driven Forward+ versus deferred evaluation; prefer a hybrid Forward+ path
      unless measured content demonstrates a clear deferred advantage
    - [ ] Compute-built clustered light lists with depth slicing
    - [ ] Thousands-of-lights stress scene with strict frame-time and overflow diagnostics
    - [ ] Decal and projected-material integration without excessive G-buffer bandwidth
    - [ ] Transparent-material lighting path consistent with opaque PBR

12. **AAA shadow quality and scalability (P0/P1)**
    - [ ] Variance Shadow Maps and EVSM as optional filtered-shadow techniques
    - [ ] Contact shadows for small near-field details
    - [ ] PCSS-style source-size-aware soft shadows with stable temporal filtering
    - [ ] Virtual Shadow Maps with cached pages, invalidation and residency debugging
    - [ ] Shadow caster culling, per-light update budgets and static shadow caching
    - [ ] Alpha-tested foliage shadows and bias/leak regression scenes

13. **Source 2-style advanced materials (P1)**
    - [ ] Height mapping and parallax occlusion mapping
    - [ ] Clear-coat lobe with independent normal and roughness
    - [ ] Subsurface scattering and wrapped/transmission lighting for skin and foliage
    - [ ] Anisotropic BRDF for brushed metal and hair-like surfaces
    - [ ] Layered materials, detail normal maps, masks, tint regions and material blending
    - [ ] Decals, wetness, dirt and environment-dependent material parameters
    - [ ] Material feature permutations that avoid a single oversized uber-shader
    - [ ] Reference-material validation against captured spheres and known renderers

14. **AAA screen-space and volumetric rendering (P1)**
    - [ ] GTAO as the primary ambient-occlusion path, plus a cheaper SSAO fallback
    - [ ] Hierarchical, roughness-aware Screen Space Reflections with probe fallback
    - [ ] Temporally stable volumetric fog with froxel lighting and height/density volumes
    - [ ] Volumetric lighting, light shafts and local-light shadow injection
    - [ ] Compute-shader versions of suitable post and lighting effects
    - [ ] Physically motivated motion blur with camera and per-object velocity
    - [ ] Cinematic depth of field with stable near/far bokeh
    - [ ] Optional vignette, film grain, chromatic aberration and lens distortion
    - [ ] SMAA as an additional non-temporal anti-aliasing option
    - [ ] DLSS, FSR and XeSS integration behind one upscaler interface with native fallback

15. **Baked, probe-based and dynamic global illumination (P1/P2)**
    - [ ] Lightmaps with UV validation, baking metadata and streaming
    - [ ] Irradiance/light probes with spatial interpolation and visibility handling
    - [ ] Local reflection probes with parallax correction, blending and priority volumes
    - [ ] SSGI with temporal accumulation and denoising
    - [ ] DDGI probe volumes with relocation/classification and update budgets
    - [ ] VXGI research path only after profiling its memory/performance trade-offs
    - [ ] Hardware ray-traced GI with denoising and raster fallback
    - [ ] Lumen-like software/hardware hybrid GI research after DDGI and RT foundations

16. **Ray tracing and offline-quality rendering (P2)**
    - [ ] Acceleration-structure build/update and instance management
    - [ ] Ray-traced reflections with roughness-aware denoising
    - [ ] Ray-traced shadows with raster/virtual-shadow fallback
    - [ ] Hybrid raster/ray-traced rendering presets
    - [ ] Progressive path tracer sharing the production material/light model
    - [ ] Ray-traced GI quality/performance tiers and robust temporal denoisers

17. **Virtualized geometry, textures and world streaming (P1/P2)**
    - [ ] Virtual texturing with feedback, page cache and residency visualization
    - [ ] Nanite-like virtual geometry research using meshlets and hierarchical clusters
    - [ ] Scene graph with parent-child transforms and stable object identities
    - [ ] Entity/component layer, tags, layers and prefab instances
    - [ ] Chunked streaming world and World Partition-style spatial database
    - [ ] Predictive streaming based on camera velocity and memory/I/O budgets
    - [ ] Save/serialization foundation: versioned binary format, JSON diagnostics and scene serialization

18. **Terrain, foliage and water rendering (P2)**
    - [ ] Heightmap terrain with continuous LOD and crack-free transitions
    - [ ] Terrain chunk streaming, splat maps and layered texture blending
    - [ ] GPU-instanced grass and trees with wind, culling, LOD and HLOD impostors
    - [ ] Water reflection/refraction with depth-aware shoreline blending
    - [ ] Gerstner-wave water for smaller surfaces
    - [ ] FFT ocean, foam, wakes and caustics for large water bodies

19. **Animation and deformable geometry (P2)**
    - [ ] Skeletal animation, GPU skinning and compute-based skinning option
    - [ ] Morph targets/blend shapes and GPU evaluation
    - [ ] Animation state machine and blend trees
    - [ ] FK, IK, root motion and animation events
    - [ ] Animation retargeting with validation tools
    - [ ] Tessellation where supported and demonstrably useful
    - [ ] Geometry-shader compatibility path only for effects where modern alternatives are unsuitable

20. **GPU particle and effects system (P2)**
    - [ ] CPU reference particle system
    - [ ] GPU simulation, culling, sorting and indirect particle draws
    - [ ] Mesh particles, trails and ribbon particles
    - [ ] Niagara-like node graph/runtime compiled to efficient GPU kernels
    - [ ] Particle-light, fog and collision integration with strict budgets

21. **Additional modern graphics APIs (P2)**
    - [ ] DirectX 12 backend after Vulkan PBR/performance parity stabilizes the resource model
    - [ ] Metal backend, preferably through a measured native path or a proven portability layer
    - [ ] Capability-based feature tiers shared across Vulkan, DirectX 12 and Metal
    - [ ] Cross-backend golden-image and frame-time parity suite

22. **Asset production pipeline (P2)**
    - [ ] Asset database with stable GUIDs, dependency tracking and derived-data cache
    - [ ] Offline texture/model/shader cooking and platform-specific compression
    - [ ] Material asset format and inheritance/instance support
    - [ ] Safe asset and shader hot reload with error fallback
    - [ ] Streaming audio and large-asset package/archive support

23. **Supporting engine systems (P3, after renderer goals)**
    - [ ] Rigid-body physics, collision detection, triggers, raycasts and shape casts
    - [ ] Character controller, continuous collision detection and vehicle physics
    - [ ] Cloth, rope and soft-body simulation as later optional modules
    - [ ] 2D/3D audio, mixer, reverb, occlusion and streaming playback
    - [ ] Canvas UI, text/font rendering, images, buttons, sliders and DPI scaling
    - [ ] Script layer selected from measured integration needs; C#, Lua and Python are
      candidates, not simultaneous requirements
    - [ ] Script hot reload and versioned save-game serialization

24. **Editor and content tools (P4, intentionally near the end)**
    - [ ] Scene editor with multi-viewport and perspective/orthographic cameras
    - [ ] Multiple runtime/editor cameras and renderable viewport targets
    - [ ] Transform gizmos, hierarchy, inspector, tags and layers
    - [ ] Asset browser, material editor, console and profiling panels
    - [ ] Visual frame debugger and render-graph/resource inspection UI
    - [ ] Prefab authoring, world-partition visualization and streaming controls

25. **Networking (P4, last)**
    - [ ] Client/server transport and replication model
    - [ ] Snapshot interpolation, client prediction and reconciliation
    - [ ] Rollback netcode for appropriate deterministic gameplay
    - [ ] Matchmaking/service integration only after the runtime/game layer requires it
