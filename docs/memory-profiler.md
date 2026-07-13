# CPU memory profiler

The memory profiler tracks C++ allocations made through global `new/delete`
inside the executable and allocations made by runtime plugins through the
host allocator. The profiler object starts disabled, but `Application` enables
it by default for the persistent CPU-memory runtime card. Set
`ApplicationDesc::EnableRuntimeMonitors` to false or configure the build with
`ENGINE_ENABLE_RUNTIME_MONITORS=OFF` to opt out. When disabled, the hot path is
an atomic enabled check followed by the platform allocation; no tracking lock
or hash-table operation is performed.

## Captured data

- current and peak live bytes;
- live, total allocated and total freed allocation counts;
- total allocation traffic in bytes;
- per-tag current/peak bytes and allocation counters;
- eight size buckets from 0–64 bytes through allocations above 256 KB;
- bounded per-frame allocation/free traffic and live-memory history;
- allocation ID, address, size, thread ID and tag for live/leak records;
- dropped-record count if the fixed 262,144-allocation table is exhausted.

The profiler uses a fixed-capacity internal table allocated outside the
tracked allocator, so collecting an allocation does not recursively allocate.
Snapshots and report construction are also excluded from tracking.

## Tags

Use an RAII tag around allocations that belong to a subsystem:

```cpp
#include <engine/profiling/MemoryProfiler.h>

{
    ENGINE_MEMORY_TAG_SCOPE("Streaming");
    auto chunk = std::make_unique<WorldChunk>();
}
```

The engine currently applies `Core`, `Renderer`, `Scene`, `Asset`, `Plugin`,
`OpenGL`, `Vulkan`, `Debug` and `Profiling` tags at the major entry points.
Nested scopes restore the previous tag automatically and tags are
thread-local.

## Runtime and export

Programmatic setup:

```cpp
auto& memory = engine::profiling::MemoryProfiler::Get();
memory.Reset();
engine::profiling::MemoryProfilerConfig config;
config.Enabled = true;
config.RetainedFrames = 240;
config.LeakReportOnShutdown = true;
memory.Configure(config);

// Run frames...
memory.WriteJsonReport("captures/memory.json", true);
```

Every sample supports:

```powershell
sample_day_night.exe --vulkan --debug-ui `
  --memory-profile captures/day-night-memory.json `
  --memory-profile-retain 300 --memory-leak-report
```

The persistent top-right runtime card shows current and peak memory, live
allocation count, the largest tag and the last frame's allocated/freed
traffic; `--debug-ui` controls only the larger detailed panel.
`--memory-profile` writes the complete JSON report
after the application and renderer have shut down. `--memory-leak-report`
prints up to 32 remaining allocations at process shutdown.

## Runtime plugins

Plugin ABI version 2 adds host-owned allocation functions:

```cpp
void* data = host->Allocate(host->HostContext, bytes, alignment, "Plugin/MySystem");
host->Free(host->HostContext, data);
```

The `MakeComponentType<T>()` adapter uses these functions automatically, so
plugin component objects appear under `Plugin/<plugin-name>` and are destroyed
by the same host allocator. Plugins must not mix the host allocator with their
DLL-local `delete`, `free` or another allocator.

## Scope and limitations

The global override covers C++ `new/delete`; direct third-party `malloc/free`,
GPU allocations and operating-system/driver memory are outside this CPU
profiler. GPU/VRAM budgets belong to the next GPU-profiler roadmap item.
Because capture begins when the profiler is enabled, allocations that already
existed before that point are intentionally absent. `Reset()` starts a new
capture and forgets existing records; use it only at a controlled capture
boundary.
