# GPU instancing and HISM

The shared renderer front end groups camera-visible commands with identical
mesh and material state. OpenGL and Vulkan consume the same batch order and the
same 128-byte `GpuInstanceData` layout containing current and previous model
matrices. A batch becomes one indexed base-instance draw; single objects use
the same path with an instance count of one.

This is used by the main PBR pass, all four directional cascades, point-light
cubemap faces, and projected spot/area shadow passes. Alpha-mask state and
textures remain material-batch state, and previous transforms preserve TAA
motion vectors without falling back to per-object draws.

## Configuration

```cpp
scene.Instancing.Enabled = true;
scene.Instancing.MinimumBatchSize = 2;
scene.Instancing.HierarchicalCulling = true;
scene.Instancing.HismMinimumGroupSize = 32;
scene.Instancing.HismLeafSize = 8;

instance.AllowInstancing = true;
instance.HismGroupId = 0; // automatic group by mesh; non-zero partitions trees
```

Material compatibility is exact: factors, alpha mode/cutoff and texture asset
identities must match. This prevents an optimization from silently changing
the image. Opted-out or undersized groups remain singleton batches.

## HISM culling

HISM groups valid instances by mesh (or explicit group id) and effective draw
distance. A median split along the widest center axis builds an AABB tree.
Distance and frustum rejection first test parent nodes; rejected subtrees skip
all leaf tests. Accepted leaves run the same conservative per-instance tests as
the normal visibility path, so the result is equivalent to brute-force
culling. Nearby off-screen shadow casters remain in `ShadowCommands`, while
distance-rejected objects are omitted.

## Diagnostics

The runtime debug UI provides:

- `GPU INSTANCING`: source instances, draw batches, instanced batches and
  saved draw calls;
- `HISM`: groups, nodes tested/rejected, subtree instances rejected and leaf
  tests;
- native GPU pipeline statistics still count the expanded instance geometry,
  while draw-call counters report actual submitted batches.

`sample_visibility` contains four shared-material cube populations and enables
both HISM and GPU Hi-Z. `AllowInstancing = false` and
`scene.Instancing.Enabled = false` provide deterministic fallback paths for
debugging. Indirect rendering and GPU-generated draw lists remain separate
later roadmap items; this stage intentionally keeps batch construction on the
shared CPU front end.
