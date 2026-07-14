# Multithreaded renderer and asynchronous data path

The engine uses one persistent `engine::concurrency::TaskSystem` instead of
creating threads per request. Its five FIFO priority queues support cooperative
cancellation, typed results, failure propagation, worker-safe nested waits,
parallel-for ranges and bounded worker counts. The global instance defaults to
`hardware_concurrency - 1` workers (clamped to 1–32); tools and tests may own a
separate instance with an explicit worker count.

## Frame preparation

`SceneRenderer::PrepareFrame` is the API-neutral CPU front end. Animation and
particle callbacks run in parallel first. After that barrier, visibility work,
cascade construction, point/spot/area-light preparation and stable render-command
generation run concurrently. The resulting `RenderFrameData::RenderCommands`
preserves scene order and is consumed by both OpenGL and Vulkan. Worker jobs never
touch an OpenGL context, Vulkan command pool or backend GPU cache.

Applications can attach an engine or plugin subsystem without depending on a
graphics API:

```cpp
const uint64_t token = app.GetSceneRenderer().AddFrameWork(
    engine::FrameWorkStage::Particles,
    [](const engine::FrameWorkContext& frame,
       const engine::concurrency::CancellationToken& cancellation)
    {
        cancellation.ThrowIfCancellationRequested();
        // Update subsystem-owned CPU simulation/output buffers here.
    });
```

Renderer hot reload resets temporal frame history but preserves registered
subsystems.

## Asynchronous resources and streaming

Every registered `ResourceManager` type—including cooked model and texture
resources—can use `LoadAsync<T>`. Requests carry a priority and cancellation
token, return a typed `AsyncResult<std::shared_ptr<T>>`, and reuse the same
thread-safe runtime cache as synchronous loads. Asset preview generation also
uses the shared task system.

Application-owned resource managers should share the application pool with
`resources.SetTaskSystem(&app.GetTaskSystem())`; standalone tools may use their
own `TaskSystem` or the lazy global fallback.

`ResourceStreamingScheduler` adds admission control around asynchronous loads:

- maximum resident CPU bytes;
- maximum estimated resident VRAM bytes;
- maximum I/O bytes admitted per tick;
- maximum concurrent loads;
- priority changes, cancellation, failure diagnostics and explicit eviction.

Budget is reserved before dispatch and only becomes resident after a successful
load, so concurrent requests cannot oversubscribe it accidentally.

## GPU lifetime and uploads

The common renderer layer provides three fence-aware primitives:

- `FrameGpuArena`: aligned linear allocations in independent frame slots;
- `StagingRingAllocator`: timeline-retired persistent staging ranges;
- `DeferredReleaseQueue`: deterministic destruction after a completed frame.

Vulkan uses these primitives directly. It selects a dedicated transfer queue
when available and safely falls back to the graphics queue otherwise. Mesh vertex
and index buffers plus material texture mip chains are copied from a persistently
mapped 64 MiB staging ring, submitted with `vkQueueSubmit2`, signalled through a
timeline semaphore and waited by the graphics submission at `ALL_GRAPHICS`.
Buffers and uploadable images use concurrent queue-family sharing when transfer
and graphics families differ. Raw RGBA textures receive a CPU-generated mip chain,
because transfer-only queue families are not required to support image blits;
cooked block-compressed textures keep their imported mip chain. Command buffers
and staging ranges are reclaimed only after the timeline counter has completed.
A persistently mapped 4 MiB-per-frame GPU arena owns frame constants and prepared
object data.

## Background pipelines and fallback

`AsyncPipelineLibrary<T>` builds backend-native pipeline objects on the task
system. Until a build succeeds, `Resolve()` returns the required visible fallback
object (normally the engine's magenta/error material); failures retain that
fallback and expose their diagnostic. Vulkan runtime shader groups are compiled
in parallel and cached as content-addressed SPIR-V permutations before their
pipelines are installed.

## Tests

`engine_tests` covers priority/FIFO behavior, queued cancellation, nested waits,
parallel-for exactness, frame-stage barriers and stable commands, typed async
resource loads, streaming priority/budgets/eviction/cancellation, staging timeline
reuse, arena alignment/overflow/reset, deferred release order and fallback-to-ready
pipeline transitions. OpenGL and Vulkan sample smoke tests exercise the common
frame path; Vulkan validation additionally exercises transfer queue synchronization
and timeline lifetime rules.
