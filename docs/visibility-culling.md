# CPU visibility culling

The backend-neutral `SceneRenderer` computes a conservative world-space AABB
for every mesh instance, then applies distance and camera-frustum tests during
parallel frame preparation. OpenGL and Vulkan consume the same filtered
`RenderFrameData`; neither backend has a separate visibility implementation.

## Scene configuration

```cpp
scene.Visibility.Enabled = true;
scene.Visibility.FrustumCulling = true;
scene.Visibility.DistanceCulling = true;
scene.Visibility.MaxDistance = 80.0f;       // 0 uses Camera::Far
scene.Visibility.DebugBounds = true;
scene.Visibility.DebugCulledBounds = true;

instance.MaxDrawDistance = 35.0f;           // 0 inherits the scene limit
instance.AlwaysVisible = false;              // bypasses both culling tests
```

The per-instance draw distance can only reduce the scene limit. This avoids an
individual object silently extending the configured visibility range. Mesh
vertex data is treated as immutable after publication; local bounds are cached
by mesh identity and vertex allocation and transformed conservatively for
rotation and non-uniform scale.

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
- yellow: distance-culled.

`DebugCulledBounds = false` limits the overlay to visible instances. Debug
lines deliberately ignore scene depth so rejected bounds can still be
inspected. Run `sample_visibility --opengl` or
`sample_visibility --vulkan`; the sample provides visible, off-frustum and
distance-rejected geometry with identical scene data for both APIs.
