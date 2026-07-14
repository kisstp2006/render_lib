# Pipeline and shader permutation cache

The renderer keeps shader and native pipeline work out of normal frame
submission. Both backends use the same canonical shader-define list, expanded
shader source and stable content hash, so a permutation is invalidated when an
included file, define value, compiler target or relevant driver identity
changes.

## Backend behavior

- OpenGL requests retrievable program binaries, stores the linked binary plus
  its native binary format, and restores it with `glProgramBinary`. A failed or
  rejected binary is treated as a miss and compiled normally.
- Vulkan stores content-addressed runtime-compiled SPIR-V permutations and
  supplies one `VkPipelineCache` to every graphics and compute pipeline create.
  `vkGetPipelineCacheData` persists the native cache at clean shutdown. Its
  vendor ID, device ID and pipeline-cache UUID are checked before reuse; an
  incompatible blob is ignored safely. When
  `VK_EXT_pipeline_creation_feedback` is available, reported native hits and
  misses come from the driver's exact per-pipeline feedback flags.

Cache files live below an engine-owned API and hashed-device subtree. Cache
cleanup only removes known `.glbin`, `.spv`, `.vkc` and temporary files. It
does not recursively delete arbitrary user content.

The application-level controls are:

```cpp
engine::ApplicationDesc desc;
desc.Renderer.EnablePipelineCache = true;
desc.Renderer.ClearPipelineCache = false;
desc.Renderer.PipelineCacheDirectory = "Cache/Renderer"; // empty = build default
```

Equivalent sample command-line controls are `--pipeline-cache-dir <path>`,
`--clear-pipeline-cache` and `--no-pipeline-cache`. The settings can also be
saved as `renderer.pipeline_cache` and `renderer.pipeline_cache_directory` in
an application config file.

## Adding a permutation

Pass a list of `ShaderDefine` values to `GLShader` or `LoadShader`. The common
code sorts and validates the definitions before key generation and injects
them immediately after GLSL's `#version` directive. Call-site order therefore
does not create duplicate permutations.

```cpp
const std::vector<engine::ShaderDefine> defines = {
    {"MATERIAL_CLEAR_COAT", "1"},
    {"SHADOW_CASCADE_COUNT", "4"},
};
```

Conflicting duplicate names and invalid preprocessor identifiers fail with a
useful exception instead of silently selecting the wrong shader.

## Cold/warm stutter benchmark

Build and run the automated two-process benchmark:

```powershell
cmake --build build --config Debug --target pipeline_cache_benchmark
```

For both OpenGL and Vulkan it clears the isolated benchmark cache, records a
cold run, then records a warm run. JSON reports in
`build/pipeline-cache-benchmark` contain startup time, shader/native cache
hits and misses, shader compile/load time, pipeline creation time, and every
sampled CPU frame time with average, p95 and maximum values. The test fails if
the warm run recompiles a permutation, fails to restore persistent data, or
exceeds the configurable warm-frame p95 ceiling.

CI machines with a display and GPU can register this target in CTest by
configuring `-DENGINE_ENABLE_GPU_PIPELINE_CACHE_TESTS=ON`.
