# Render graph / frame graph

The renderer uses one backend-neutral graph definition for OpenGL and Vulkan.
The design follows ezEngine's configurable pass/pin pipeline and transient
resource lifetime model, while retaining Stride's separation between frame
preparation and ordered render execution.

References:

- [ezEngine render-pipeline overview](https://ezengine.net/pages/docs/graphics/render-pipeline/render-pipeline-overview.html)
- [ezEngine RenderGraph source](https://github.com/ezEngine/ezEngine/tree/dev/Code/Engine/RendererCore/RenderGraph)
- [Stride rendering pipeline](https://doc.stride3d.net/latest/en/manual/graphics/rendering-pipeline/index.html)

## What the compiler does

Every pass declares stable ID, display name, phase, input/output resources,
required state/stage, optional explicit ordering and an execute callback.
`RenderGraph::Compile` then:

1. validates pass IDs, handles and configuration overrides;
2. derives RAW, WAR and WAW dependencies from resource access;
3. performs a deterministic topological sort and reports cycles;
4. rejects transient reads without a producer;
5. calculates first/last use for every HDR, depth, velocity, bloom and history
   target;
6. assigns compatible non-overlapping transient resources to the same physical
   alias slot;
7. emits backend-neutral barriers from the previous and requested state.

OpenGL converts the barriers to the required `glMemoryBarrier` mask. Vulkan
converts top-level image transitions to `VkImageMemoryBarrier2`, including CSM,
HDR/MSAA, depth, velocity, TAA history and swapchain layouts. Operations that
contain several native subpasses inside one graph node (IBL bake, local-light
shadow bake and bloom pyramid) retain their internal barriers.

The graph has `Prepare` and `Render` phases. IBL/cache updates run in prepare;
GPU render passes run in compiled dependency order.

## Runtime configuration

Use `--render-graph <path>` in any sample, or set
`renderer.render_graph_config` in the application configuration. The file is a
small, versioned pipeline asset:

```ini
version=1
enabled=true
validation=true
transient_aliasing=true

# Optional pass overrides. IDs are stable API names.
pass.directional_shadows.enabled=true
pass.local_shadows.enabled=true
pass.debug_ui.enabled=true
pass.debug_ui.after=post_process
```

Available built-in IDs are `environment`, `directional_shadows`,
`local_shadows`, `main_hdr`, `post_process` and `debug_ui`. Resource
dependencies always remain authoritative: an override that creates a cycle is
rejected. Disabling a required producer is rejected when a transient consumer
would read uninitialized data.

Application-level switches:

- `renderer.render_graph`: automatic dependency ordering; when false, declared
  order is retained while required barriers are still generated;
- `renderer.render_graph_validation` / `--no-render-graph-validation`;
- `renderer.transient_aliasing` / `--no-transient-aliasing`;
- `renderer.render_graph_config` / `--render-graph <path>`.

## Adding a pass

```cpp
rendergraph::ResourceDesc desc;
desc.Name = "effects.half_res";
desc.Format = "RGBA16F";
desc.Width = width / 2;
desc.Height = height / 2;
desc.BytesPerPixel = 8;
const auto target = graph.Create(desc); // transient

graph.AddPass("my_effect", "My effect")
    .Read(sceneHdr, rendergraph::ResourceState::ShaderRead,
          rendergraph::PipelineStage::Fragment)
    .Write(target, rendergraph::ResourceState::ColorAttachment,
           rendergraph::PipelineStage::ColorOutput)
    .SetExecute([&] { RecordMyEffect(); });
```

Use `Import` for persistent or externally owned resources and `Create` for
frame-local resources. Transient aliasing is conservative: formats,
dimensions, layers, mip counts and sample counts must match, and lifetime
intervals must not overlap.

## Diagnostics and tests

The in-engine frame debugger consumes the compiled graph directly. It displays
compiled pass order, declaration index, barrier count, input/output links,
resource first/last use, alias slot, logical transient bytes, physical bytes,
saved bytes and graph compile time.

`engine_renderer_core` tests cover declaration-independent sorting, callback
execution order, barrier generation, cycle errors, transient read validation,
config round-tripping, lifetime/alias-slot reuse, frame-debugger metadata and a
512-pass compile-time regression budget. The day/night sample is also exercised
on OpenGL and Vulkan with Vulkan validation enabled.
