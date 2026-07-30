# Mesh LOD chains

The engine keeps mesh LOD policy on the CPU side, ahead of either graphics
API. OpenGL and Vulkan therefore receive identical `PreparedRenderCommand`
objects: the selected `Geometry`, `LodLevel`, world bounds and index count are
all decided once by `SceneRenderer`.

## Content paths

`static-mesh` assets already support offline generated LODs through their
descriptor settings:

```ini
generate_lods=true
lod_count=4
lod_ratio=0.42
lod_error=0.04
lod_aggressive=true
```

The meshoptimizer cooker preserves normals, tangents and UVs, optimizes the
index/vertex order of every level, and serializes the chain with the cooked
asset. `SampleAssetPipeline::AddStaticMesh()` turns those cooked levels into
runtime `MeshInstance::LodLevels` automatically.

Applications can also provide an authored chain directly. `Mesh` is LOD0;
each following level needs its own CPU mesh plus its triangle ratio and
measured relative geometric error:

```cpp
instance.LodLevels.push_back({lod1, 0.42f, 0.01f});
instance.LodLevels.push_back({lod2, 0.18f, 0.04f});
```

## Runtime policy

`Scene::Lods` controls selection:

- `TargetScreenSpaceErrorPixels` is the maximum projected geometric error.
- `HysteresisFraction` creates a transition band, avoiding LOD flicker while
  a camera is near a threshold.
- `Bias` trades detail for cost without recooking content.
- `ForcedLevel` pins a level for visual QA (`UINT32_MAX` restores automatic
  choice).

The renderer uses the LOD0 bounding sphere for conservative frustum, distance
and Hi-Z bounds. It then chooses the coarsest level below the target error.
Main rendering and every directional/point/spot/area shadow pass consume the
same selected mesh. GPU instancing includes the selected geometry in its key,
so objects at different levels never end up in one draw.

Geometry batching deliberately leaves LOD-chain instances uncombined: a
combined static mesh cannot change level independently. They still take the
HISM and LOD-aware GPU-instancing path. HLOD is the later roadmap feature for
combining distant clusters safely.

## Test scene

`sample_visibility --opengl --debug-ui` or `--vulkan --debug-ui` includes a
four-level sphere population. The `MESH LOD` debug section reports candidates,
per-level selections, transitions, submitted triangles and the equivalent
LOD0 triangle count. The automated renderer test covers screen-space
selection, hysteresis, forced levels, shadow-command identity and the rule
that distinct selected levels cannot be instanced together.

For clean automated screenshots, pass `--no-visibility-debug`; it retains the
LOD, HISM and occlusion workloads but removes the intentionally dense AABB
diagnostic lines.
