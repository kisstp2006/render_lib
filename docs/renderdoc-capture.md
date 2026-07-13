# RenderDoc capture and GPU debug names

The renderer exposes the same capture workflow for every sample and both
backends. It uses RenderDoc's in-application API dynamically, so normal builds
do not link against or require RenderDoc.

## Automated capture

When the executable is already launched by RenderDoc:

```powershell
.\build\examples\sandbox\Release\sample_day_night.exe --opengl `
  --renderdoc-capture captures/day-night-gl --renderdoc-frame 8

.\build\examples\sandbox\Release\sample_day_night.exe --vulkan `
  --renderdoc-capture captures/day-night-vk --renderdoc-frame 8
```

For a direct command-line launch, point the application at RenderDoc's library
before either graphics API is initialized. The engine also exposes a portable
RenderDoc folder's `renderdoc.json` to the Vulkan loader automatically:

```powershell
.\build\examples\sandbox\Release\sample_lights.exe --vulkan `
  --renderdoc-library "C:\Program Files\RenderDoc\renderdoc.dll" `
  --renderdoc-capture captures/lights-vk --renderdoc-frame 8
```

The capture mode advances simulation and shader animation with a fixed 1/60 s
step, captures exactly the zero-based frame selected by `--renderdoc-frame`,
and exits successfully after the `.rdc` is written. Useful options:

- `--renderdoc-fixed-delta <seconds>` changes the deterministic time step.
- `--renderdoc-keep-running` leaves the sample open after the capture.
- `--renderdoc-api-validation` asks RenderDoc to record API validation output.
- `--hidden-window` makes automated capture jobs unobtrusive.

If RenderDoc cannot be found, the explicit CLI capture mode exits with a useful
error. Programmatic users can leave `FrameCapture.RequireAvailable` false for a
graceful no-capture fallback.

## Capture structure

OpenGL uses `GL_KHR_debug` groups and `glObjectLabel`. Vulkan enables
`VK_EXT_debug_utils` whenever the extension is present, independently of the
validation layers, and uses command labels plus object names. Both backends
present the same high-level event tree:

```text
Frame
├─ Environment / IBL Update or Bake
├─ Shadows / Directional Cascades
│  ├─ Directional Cascade 0
│  ├─ Directional Cascade 1
│  ├─ Directional Cascade 2
│  └─ Directional Cascade 3
├─ Shadows / Local Lights
├─ Main HDR / Geometry + Sky + Resolve
├─ Post Process
│  ├─ Post / Auto Exposure
│  ├─ Post / TAA Resolve
│  ├─ Post / Bloom Pyramid
│  └─ Post / Tonemap + Color Grade + AA
└─ Debug UI
```

Render targets, shadow maps, IBL cubemaps, bloom/TAA histories, query pools,
pipelines, shader modules, scene mesh buffers and material textures also carry
semantic names. This keeps the Event Browser, Pipeline State and Resource
Inspector usable without matching anonymous native handles to C++ code.

The implementation follows RenderDoc's official recommendation to discover
`RENDERDOC_GetAPI` dynamically and start/end the selected frame through the
in-application API. See the
[official in-application API documentation](https://renderdoc.org/docs/in_application_api.html)
and [official API header](https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/api/app/renderdoc_app.h).
