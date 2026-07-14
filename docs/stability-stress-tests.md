# Renderer stability stress tests

The stability suite drives the real `Application`, `Window`, asset pipeline and
selected native render backend. OpenGL and Vulkan execute the same state
machine and produce the same JSON schema.

Each cycle performs:

1. resize to 800x450, 960x720 and back to 1280x720;
2. minimize to a real 0x0 framebuffer, then restore;
3. enter and leave borderless fullscreen;
4. enter and leave exclusive fullscreen;
5. invalidate imported glTF/HDRI resources, rebuild the shared CPU `Scene`,
   shut down the backend, recompile/reload shaders and recreate all GPU state;
6. compare CPU memory, GPU memory, native allocation and mesh/texture/material
   cache counts with the warm baseline.

Rendering is deliberately skipped while GLFW reports a minimized or zero-sized
framebuffer. Updates continue under `UnfocusedBehavior::Continue`, allowing the
same deterministic state machine to restore the window without a helper thread.

## Running it

```powershell
# Interactive/default four-cycle runs
.\build\examples\sandbox\Release\sample_stability.exe --opengl
.\build\examples\sandbox\Release\sample_stability.exe --vulkan

# A custom soak run and report
.\build\examples\sandbox\Release\sample_stability.exe --vulkan `
  --stress-cycles 50 --stress-stage-frames 8 `
  --stress-report build/stability/manual.vulkan.json

# Three-cycle OpenGL + Vulkan integration gate
cmake --build build --config Release --target stability_stress

# 100-cycle soak target for overnight/driver validation
cmake --build build --config Release --target stability_stress_long
```

Set `ENGINE_ENABLE_GPU_STABILITY_TESTS=ON` at configure time to register the
three-cycle gate with CTest. It is disabled by default because it needs a real
display/GPU and intentionally changes the active window mode.

Useful switches:

- `--stress-cycles N`: number of complete state-machine cycles;
- `--stress-stage-frames N`: rendered/update frames allowed to settle per state;
- `--stress-report path.json`: output report;
- `--stress-max-cpu-growth-mb N`: permitted retained CPU-memory envelope;
- `--stress-no-exclusive`: omit exclusive fullscreen where CI policy forbids it.

The JSON report contains every stage and its dimensions/minimized state, CPU
bytes, backend resource counters, per-cycle result, baseline/final values and
failure messages. Vulkan bytes/allocation counts come directly from its native
allocator. OpenGL uses deterministic estimates plus explicit engine-owned GL
object/cache counts because portable OpenGL has no allocator query.
