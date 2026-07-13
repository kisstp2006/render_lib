# GPU profiler

The GPU profiler has one backend-neutral history and two native collectors.
OpenGL uses `GL_TIME_ELAPSED` plus `GL_ARB_pipeline_statistics_query`; Vulkan
uses timestamp and pipeline-statistics query pools. Query results are read only
after they are available (or after the corresponding Vulkan frame fence), so
profiling never adds a synchronous GPU wait to the frame.

Every completed sample contains these top-level timings:

- `Shadows/Directional`
- `Shadows/Local lights`
- `Main HDR`
- `Post process`
- `Debug UI`

Slash-separated names form a hierarchy in JSON and the debug UI. The retained
history produces latest, average, minimum and maximum time for every pass.
The first frames include shader/cache warm-up and are intentionally preserved;
benchmark tools can discard them during analysis.

## Pipeline and memory counters

When the device supports native pipeline queries, the report contains input
assembly vertices/primitives and vertex, fragment and compute shader
invocations. Draw calls and compute dispatches are counted by command recording,
so they remain meaningful next to the native counters.

Vulkan reads the driver heap usage and budget through `VK_EXT_memory_budget`
and also reports exact engine-owned device-local allocations maintained by the
Vulkan resource allocator. OpenGL reads usage and available memory through
`GL_NVX_gpu_memory_info` when the driver exposes it. Unsupported counters are
reported as unavailable instead of being guessed.

## Capture and export

All sandbox-based samples accept:

```powershell
./build/examples/sandbox/Release/sample_day_night.exe --vulkan `
  --gpu-profile traces/daynight-vulkan.json --gpu-profile-retain 600
```

The equivalent OpenGL capture only changes `--vulkan` to `--opengl`. Reports
contain capability flags, pass summaries, every retained frame, pipeline
counters, VRAM usage/budget/peak and engine-owned bytes.

Code can access or export the same data directly:

```cpp
#include <engine/profiling/GpuProfiler.h>

auto snapshot = engine::profiling::GpuProfiler::Get().Snapshot();
engine::profiling::GpuProfiler::Get().WriteJsonReport("gpu-profile.json");
```

`GpuProfilerConfig::RetainedFrames` bounds memory use. Set
`ApplicationDesc::Renderer.EnableGpuTiming` to `false`, or pass
`--no-gpu-timing`, to avoid creating and recording native query pools.
