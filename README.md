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
- **Color grading**: post-tonemap saturation/contrast/tint (LUT loading is
  on the roadmap; this covers the same stage in the pipeline)
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

The platform layer is cross-platform by construction (GLFW, no Win32-only
code paths) and the current OpenGL 4.6/Vulkan targets support Windows and
Linux. macOS needs a future OpenGL 4.1 compatibility path or Vulkan through
MoltenVK; the current renderer does not build there as-is.

## Why this shape

- **No premature RHI abstraction.** OpenGL and Vulkan have very different
  resource/sync models. Rather than guess at a generic buffer/pipeline/command
  abstraction before either backend has real requirements, the two backends
  are separate concrete classes sharing only `engine/scene` (mesh/material/
  light data) and the thin per-frame `IRenderBackend` contract. Once the
  Vulkan backend actually needs to share pipeline/resource code with GL,
  that's the informed time to extract a real RHI layer.
- **CPU-side scene is backend-agnostic.** `MeshData`, `Material`, `Scene`,
  `Camera` don't know about GL or Vulkan; both backends consume the same data.
- **Reference for the PBR math and Source "feel":** the lighting model
  (`shaders/gl/pbr.frag`) mirrors the D/G/F terms in Valve's own
  `ValveResourceFormat/Renderer/Shaders/common/pbr.slang` (GGX distribution,
  Schlick-Smith visibility, Schlick Fresnel) so material response reads the
  same way Source 2 materials do, without depending on that project's code.

## Layout

```
engine/                  Static library: everything backend-agnostic + both backends
  include/engine/
    core/                 Window, Input, Camera, Application, Log
    scene/                Mesh (+ procedural primitives), Scene, Material, Lights
    backend/
      IRenderBackend.h     Shared Init/Resize/RenderFrame/Shutdown contract
      gl/                  OpenGL 4.6 backend (functional: PBR + shadow map)
      vk/                  Vulkan backend (swapchain scaffold; PBR pipeline WIP)
  src/                     .cpp implementations mirroring the include tree

shaders/
  gl/                      GLSL 460 core: pbr.vert/frag, shadow.vert/frag

examples/sandbox/          Metallic x roughness sphere-grid demo app

cmake/                     Reserved for custom find modules if needed
```

## Building

Requires CMake >= 3.21 and a C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+).
Dependencies (GLFW, GLAD2, GLM, stb) are fetched at configure time via
`FetchContent` — nothing to install beyond a compiler and CMake. The Vulkan
backend additionally requires the LunarG Vulkan SDK; if it isn't found,
`ENGINE_BUILD_VULKAN` is automatically disabled and only the OpenGL backend
is built.

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

Run with `--vulkan` to use the Vulkan backend once it's further along
(currently it only proves out the swapchain round-trip with a clear color).
Run with `--sample-gltf` for the bundled Khronos Water Bottle, or with
`--gltf path/to/scene.glb` to load another static glTF 2.0 scene.
Use `--hdri path/to/environment.hdr` with any scene, or `--hdri-studio` for
the bundled CC0 Poly Haven product-lighting showcase.
Use `--day-night-showcase` for an automatically rotating directional light
and a complete day/sunset/twilight/starry-night/sunrise loop. Its default
cycle is 24 seconds; override it with `--day-night-seconds N`.

**Controls:** right-click + mouse to look around, WASD to move, Q/E for
down/up, hold Shift to move faster. I/K/J/L move the sun (sky + IBL re-bake
live), F toggles the flashlight, C toggles cascade debug colors, F12 saves a
PNG render into `renders/`. `--shadow-stress --shadow-benchmark` runs the CSM
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
4. **Color grading LUT + FXAA/TAA** to finish the Source 2 post stack — see
   `ValveResourceFormat/Renderer/Shaders/post_processing.frag.slang`
   (`g_tColorCorrectionLUT`) for the reference behavior.
5. **Vulkan PBR parity** — shader modules (compile the GLSL to SPIR-V at
   build time via `glslang`/`glslc`), pipeline + descriptor layout, depth
   buffer, shadow pass — matching what `GLRenderBackend` already does.
6. **More local-light features** — complete: point cubemap shadows, shared
   spot/area shadow atlas, cookie atlas and Source 2-style barn/rect area lights.
7. Only *then* revisit whether a shared RHI abstraction actually pays for
   itself between the two backends.

Done so far: HDR+MSAA pipeline, procedural/HDRI IBL (irradiance/prefilter/BRDF LUT),
sun shadow mapping with PCF, Jimenez bloom, VRF-parameterized Uncharted
tonemap, textured materials with normal mapping, PNG capture, static glTF/GLB
scene loading with the CC0 Khronos Water Bottle sample.
