# Geometry batching

Geometry batching reduces CPU submission and state changes before the shared
OpenGL/Vulkan instancing stage. It operates on backend-neutral `MeshData`,
`Material`, and `MeshInstance` values, so both render backends receive the same
combined geometry and command order.

## Runtime modes

- Static batching combines compatible `MeshMobility::Static` instances once
  and reuses the result while mesh revisions and transforms remain unchanged.
- Selective dynamic batching accepts only small `MeshMobility::Movable`
  sources. Without TAA it refreshes changed batches each frame. With TAA and
  `PreserveMotionVectors` enabled, objects stay on the normal per-object path
  until their transforms have been stable for `DynamicStabilityFrames`; any
  later movement immediately removes them from the batch.
- Spatial cells limit the bounds of a combined mesh, retaining useful frustum,
  distance, HISM and Hi-Z culling granularity.

Compatibility includes the complete metallic-roughness material and texture
set, alpha mode, shadow participation, maximum draw distance, always-visible
state, cell and explicit batch group. Incompatible instances and batches below
the configured minimum use the ordinary renderer path.

`Scene::Batching` controls minimum group sizes, cell size, per-batch vertex and
index limits, cache capacity, dynamic source size and temporal stability. Per
instance, `AllowBatching`, `Mobility`, and `BatchGroupId` provide opt-out and
grouping control. World `MeshRenderer` components expose the same controls as
reflected properties.

Repeated instances of one mesh are handed to GPU instancing/HISM by default,
avoiding duplicated vertex memory. Set `PreferInstancingForRepeatedMeshes` to
false only when draw-call reduction is more important than that memory and
vertex-fetch advantage.

Cache identity follows stable source membership rather than the current
spatial-cell coordinate, so moving groups reuse their existing CPU/GPU mesh.
After `MaximumCachedBatches`, inactive entries are recycled least-recently-used;
active batches are never evicted mid-frame.

## GPU resource lifetime

Combined meshes keep a stable CPU pointer. A changed dynamic batch increments
`MeshData::Revision`. OpenGL refreshes the existing VBO/EBO; Vulkan creates new
device-local buffers and defers destruction of the old buffers until every
in-flight frame is safe. Static cache hits therefore cause no GPU upload.

The runtime debug UI's `GEOMETRY BATCHING` group reports source/effective
instances, static/dynamic batches, saved draws, cache hits, rebuilds, rejected
and recycled batches, instancing hand-offs, combined vertex/index counts and
CPU build time.

## Offline combining

The static-mesh asset setting `merge_meshes=true` bakes node transforms and
combines primitives with equivalent PBR materials during cooking. The merged
LOD0 is then passed through mesh optimization and LOD generation, followed by
collision cooking. Cook statistics include `source_parts`, `parts`, and
`merged_parts`. This path has no per-frame rebuild cost and is preferred for
immutable environment geometry.

Mirrored transforms reverse triangle winding and tangent handedness. Normals
use inverse-transpose transformation, while tangents are transformed and
re-orthogonalized, preserving correct normal mapping under non-uniform scale.
