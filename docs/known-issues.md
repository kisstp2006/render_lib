# Known issues

## 2026-07-14 — Engine freeze shortly after startup

**Status:** Open; investigate on 2026-07-15. No fix attempted yet.

**Reported symptom:** The engine freezes approximately 1–2 seconds after it
starts.

**Additional symptom:** One of the applications may have crashed while the
user was interacting with it. It is not confirmed yet which action triggered
the crash or whether the freeze and crash have the same root cause.

**Context:** This was reported after launching `sample_editor_viewports` and
`sample_world_editor` on OpenGL. It is not confirmed yet whether one or both
samples freeze, whether the process stops responding or only rendering stops,
or whether Vulkan is affected.

**Investigation checklist:**

1. Reproduce each sample separately on OpenGL and Vulkan in a sustained run.
   Exercise viewport clicks, panel docking/resizing and inspector controls
   separately while recording the last successful input event.
2. When frozen, break all threads and inspect the main, render and worker
   thread call stacks for a mutex/fence/device-idle wait.
3. Check whether continuous ImGui viewport resizing is recreating offscreen
   targets every frame because of fractional panel dimensions.
4. Check the ImGui render callback, Vulkan fences and OpenGL `glFinish` paths.
5. Temporarily disable the World bridge, console log sink, second viewport and
   animated updates independently to isolate the trigger.
6. Record CPU/GPU profiler data and validation/debug output for the last frame
   before the freeze.
