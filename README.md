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
- **IBL**: procedural HDR sky baked to a cubemap, irradiance convolution,
  GGX-prefiltered specular mips, split-sum BRDF LUT (VRF `EnvBRDF` shape);
  re-baked automatically whenever the sun/sky settings change
- **Lights**: directional sun, point lights, Source 2-style spots with
  inner/outer cone (see VRF `SceneLight`); the first shadow-casting spot gets
  a 2048 px perspective shadow map — the sandbox binds it to `F` as a
  camera-attached flashlight
- **Local shadows**: 2048 px perspective shadow map for the first shadow-casting spot
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

**Controls:** right-click + mouse to look around, WASD to move, Q/E for
down/up, hold Shift to move faster. I/K/J/L move the sun (sky + IBL re-bake
live), F toggles the flashlight, C toggles cascade debug colors, F12 saves a
PNG render into `renders/`. `--shadow-stress --shadow-benchmark` runs the CSM
stress scene with asynchronous GPU timing.

## Roadmap

Rough order, each step buildable/testable on its own:

1. **glTF scene loading** (initial static metallic-roughness support complete) so real test scenes/assets can be brought in instead
   of only procedural primitives — the biggest step toward "make pretty
   renders of real content".
2. **Cascaded shadow maps** — complete: four stabilized cascades, blended
   transitions, PCF, debug visualization and GPU benchmark mode.
3. **HDR equirect environment loading** (stb_image supports .hdr) as an
   alternative to the procedural sky for studio-style product renders.
4. **Color grading LUT + FXAA/TAA** to finish the Source 2 post stack — see
   `ValveResourceFormat/Renderer/Shaders/post_processing.frag.slang`
   (`g_tColorCorrectionLUT`) for the reference behavior.
5. **Vulkan PBR parity** — shader modules (compile the GLSL to SPIR-V at
   build time via `glslang`/`glslc`), pipeline + descriptor layout, depth
   buffer, shadow pass — matching what `GLRenderBackend` already does.
6. **More local-light features**: shadow maps for all spots (atlas), light
   cookies/projected textures, barn-door area lights (VRF `lighting.barn`).
7. Only *then* revisit whether a shared RHI abstraction actually pays for
   itself between the two backends.

Done so far: HDR+MSAA pipeline, IBL (irradiance/prefilter/BRDF LUT),
sun shadow mapping with PCF, Jimenez bloom, VRF-parameterized Uncharted
tonemap, textured materials with normal mapping, PNG capture, static glTF/GLB
scene loading with the CC0 Khronos Water Bottle sample.
