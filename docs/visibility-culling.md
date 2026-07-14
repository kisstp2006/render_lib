# Visibility culling

The backend-neutral `SceneRenderer` computes a conservative world-space AABB
for every mesh instance, then applies distance and camera-frustum tests during
parallel frame preparation. OpenGL and Vulkan consume the same filtered
`RenderFrameData`; the CPU classification and bounds generation have one
backend-neutral implementation.

After those CPU tests, both backends optionally run the same GPU Hi-Z policy.
The native compute paths build a maximum-depth mip pyramid from resolved scene
depth, test world-space AABBs against the previous completed pyramid, and read
visibility bits back asynchronously. The CPU never waits for the current GPU
frame to decide its draw list.

## Scene configuration

```cpp
scene.Visibility.Enabled = true;
scene.Visibility.FrustumCulling = true;
scene.Visibility.DistanceCulling = true;
scene.Visibility.MaxDistance = 80.0f;       // 0 uses Camera::Far
scene.Visibility.DebugBounds = true;
scene.Visibility.DebugCulledBounds = true;
scene.Visibility.GpuOcclusionCulling = true;
scene.Visibility.OcclusionConfirmationFrames = 2;
scene.Visibility.OcclusionMaxHiddenFrames = 30;
scene.Visibility.OcclusionDepthBias = 0.0001f; // normalized device depth
scene.Visibility.OcclusionBoundsInflation = 0.08f;
scene.Visibility.DebugOcclusion = true;

instance.MaxDrawDistance = 35.0f;           // 0 inherits the scene limit
instance.AlwaysVisible = false;              // bypasses CPU and GPU culling
```

The per-instance draw distance can only reduce the scene limit. This avoids an
individual object silently extending the configured visibility range. Mesh
vertex data is treated as immutable after publication; local bounds are cached
by mesh identity and vertex allocation and transformed conservatively for
rotation and non-uniform scale.

## GPU Hi-Z pipeline and temporal safety

`Visibility/Hi-Z Occlusion Cull` runs before the main HDR pass and consumes the
previous frame's pyramid. `Visibility/Build Hi-Z Pyramid` runs after resolved
depth is available and writes an R32F chain using maximum-depth 2x2 reduction.
The two passes and the `visibility.hiz`/`visibility.results` resources are
declared in the common render graph, while buffer ownership, barriers and
readback synchronization remain native to OpenGL and Vulkan.

Raw GPU rejection does not immediately hide an object. The shared temporal
policy requires at least two consecutive occluded results, rejects stale
generations after camera, FOV or structural scene changes, invalidates moved
objects individually, inflates tested bounds, and periodically forces hidden
objects visible for a fresh query. This
prevents permanent false negatives and visible popping. `Visibility.Enabled`
or `GpuOcclusionCulling` can disable the path; `AlwaysVisible` objects never
enter the GPU candidate list.

`BackendFrameStats` and the runtime `GPU HI-Z` debug group report candidate and
culled counts, results consumed, pyramid mip count, readback latency and history
resets. Native timestamp queries expose the cull and pyramid-build costs as
separate GPU-profiler/frame-debugger passes and as a combined debug-UI time.
The in-engine frame debugger can inspect every mip of the live R32F Hi-Z
pyramid on either backend.

## Camera and shadow lists

`RenderCommands` contains camera-visible instances in stable scene order.
`ShadowCommands` is separate: a nearby off-screen caster remains available to
directional shadow passes, while a distance-culled object is omitted from both
lists. Empty or non-finite meshes are rejected and counted as invalid bounds.

`VisibilityStatistics` reports tested, visible, frustum-culled,
distance-culled, invalid and shadow-caster counts. The runtime debug UI exposes
the previous completed frame under the `VISIBILITY` group.

## Bounds visualization

Set `DebugBounds` to draw bounds in both backends:

- green: camera-visible;
- red: frustum-culled;
- yellow: distance-culled;
- purple: GPU Hi-Z occluded.

`DebugCulledBounds = false` limits the overlay to visible instances. Debug
lines deliberately ignore scene depth so rejected bounds can still be
inspected. Run `sample_visibility --opengl` or
`sample_visibility --vulkan`; its large foreground wall provides a stable
occlusion case and both APIs consume the identical scene and temporal policy.
