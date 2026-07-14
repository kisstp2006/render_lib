# In-engine frame debugger

The frame debugger is a backend-neutral frozen view of the most recently
completed frame. Press **F4**, or launch a sample with `--frame-debugger`.

- Left/right selects a render pass; the list follows execution order and uses
  the asynchronous GPU-profiler result when a timestamp is available.
- Up/down selects a render resource. Input resources are marked `I`, outputs
  are marked `O`, and the selected pass shows both lists explicitly.
- Comma/period selects a mip; brackets select an array layer or cubemap face.
- **R** takes a new frozen preview. Captures also occur when the selected
  resource or subresource changes.

The common snapshot records name, resource kind, dimensions, format, sample
count, mip/layer count and native allocation estimate. Both backends publish
the directional and local shadow targets, point-shadow cube array, cookie
atlas, HDR color, depth, velocity, TAA histories, bloom pyramid, post LDR and
IBL cubemaps/LUT. Native multisampled renderbuffers/images are listed as
metadata-only; their resolved textures are previewable.

Pass order and input/output links now come directly from the compiled render
graph. The view also reports per-pass barrier counts, transient first/last-use
intervals and alias slots, plus logical/physical transient memory, bytes saved
by aliasing and graph compile time. See the [render-graph guide](render-graph.md).

Preview capture is intentionally not live. OpenGL performs an explicit texture
readback; Vulkan transitions only the selected subresource, copies it to a
retained host-visible diagnostic buffer, and restores the original layout.
This can synchronize the GPU, but normal rendering has no readback overhead
while the debugger is closed or its frozen preview is unchanged. HDR previews
use a display-only compression, depth gets contrast expansion, and velocity is
remapped around neutral gray; the underlying render resources are not changed.

Custom backends implement `IRenderBackend::GetFrameDebugSnapshot()` and
`IRenderBackend::CaptureFrameDebugResource()`. Stable resource references use
`debug::FrameDebugId("qualified.resource.name")`, so the common UI never stores
OpenGL object names or Vulkan handles.
