# Persistent runtime monitors

The runtime displays two compact diagnostic cards by default, independently
of the larger F3 debug panel:

- CPU memory at the top-right: current and peak tracked bytes, live allocation
  count, per-frame allocation/free traffic and the largest subsystem tag;
- GPU at the bottom-right: adapter, asynchronous pass timings, theoretical GPU
  FPS, VRAM usage/budget/peak, draw/primitive and shader-invocation counters,
  active API and anti-aliasing.

The debug UI uses one small RGBA8 atlas and a bounded three-layer draw list.
Each layer carries a source rectangle and a `TopLeft`, `TopRight`, `BottomLeft`
or `BottomRight` placement. OpenGL and Vulkan upload the atlas once and draw
only the active rectangles after post-processing.

## Disable or move in code

Disable both persistent monitors and their automatic CPU-memory collection
before constructing the application:

```cpp
engine::ApplicationDesc desc;
desc.EnableRuntimeMonitors = false;
engine::Application app(desc);
```

The detailed F3 panel is still available. Monitor placement can be changed at
runtime:

```cpp
app.GetDebugOverlay().SetRuntimeMonitorPlacements(
    engine::debug::DebugOverlayPlacement::BottomLeft,
    engine::debug::DebugOverlayPlacement::TopRight);
```

`app.SetRuntimeMonitorsEnabled(false)` hides the cards dynamically and stops
the automatically owned memory profiler. The lower-level
`SetRuntimeMonitorsVisible(false)` only hides the cards, which is useful when
an explicit memory capture should continue in the background.

## Config, command line and build flag

Application config:

```ini
diagnostics.runtime_monitors=false
```

Every sample accepts `--no-runtime-monitors`. To disable automatic runtime
monitors for every application in a build:

```powershell
cmake -S . -B build -DENGINE_ENABLE_RUNTIME_MONITORS=OFF
```

Explicit `--memory-profile` and programmatic `MemoryProfiler::Configure()`
remain available even when the persistent cards are disabled.
GPU profiling is controlled independently by `Renderer.EnableGpuTiming` or
`--no-gpu-timing`; see [gpu-profiler.md](gpu-profiler.md).

The layout follows ezEngine's useful separation between hierarchical runtime
statistics and placement-aware debug text groups, while keeping this renderer's
backend-neutral CPU rasterization and identical OpenGL/Vulkan appearance.
