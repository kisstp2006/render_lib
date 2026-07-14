# GPU capability database and safe fallbacks

The renderer discovers the selected device once during backend initialization
and converts OpenGL and Vulkan data to the same `GpuCapabilityProfile`. The
profile contains API/vendor/device and driver identity, a capability tier,
native support and active state for every tracked feature, applied workaround
rules, and an explicit list of fallback decisions.

## Native discovery

OpenGL records `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`, the shading-language
version and relevant extension/core-version support. Vulkan records PCI vendor
and device IDs, `VkPhysicalDeviceDriverProperties` (driver ID/name/info and
conformance version), physical-device features, queue timestamp support,
present modes, memory-budget support and per-format sampled-image support.

The shared vendor database recognizes the standard PCI IDs and API identity
strings for NVIDIA, AMD, Intel, Apple, Qualcomm, Arm, Imagination, Microsoft
and Mesa. Driver workaround entries have API/vendor/name predicates and bounded
driver-version fields. The initial safety rules cover software implementations
(llvmpipe, lavapipe, SwiftShader, softpipe and Microsoft Basic Render Driver).
They select 1x MSAA, 1x anisotropy and disable unreliable native GPU timing.
Do not add a vendor-specific rule without a reproducible failing test and a
bounded affected driver interval.

## Fallback behavior

- Unsupported or over-requested MSAA selects the highest supported power-of-two
  sample count, down to 1x.
- Anisotropy is clamped to the hardware/policy limit; 1x is always valid.
- Unsupported immediate/adaptive presentation selects FIFO VSync (Vulkan may
  use mailbox for an immediate request while still reporting that it was not
  an exact match).
- Missing GPU timestamps fall back to CPU frame timing. Missing pipeline or
  memory queries retain engine-owned counters and allocation estimates.
- Cooked BC1/BC3/BC5, BC7 and ASTC textures contain an RGBA8 fallback mip
  chain. Both backends select it before GPU upload if the native format family
  cannot be sampled. Older cooked resources without that payload use the
  material default texture and log one actionable warning instead of crashing.

The default policy only applies proven driver rules and hardware limits. A
more defensive deployment can cap optional quality features:

```cpp
engine::ApplicationDesc config;
config.Renderer.CapabilityPolicy = engine::GpuCapabilityPolicy::Conservative;
config.Renderer.EnableDriverWorkarounds = true;
```

Equivalent sample switches are `--gpu-policy default|conservative` and
`--no-driver-workarounds`. Application config keys are
`renderer.gpu_policy` and `renderer.driver_workarounds`.

## Diagnostics and export

The detailed debug UI reports vendor, driver, capability tier and fallback
count. The persistent GPU card also shows the tier and count. Export the full,
deterministic JSON profile from any sample:

```powershell
sample_materials.exe --vulkan --gpu-capabilities build/reports/gpu.vulkan.json
sample_materials.exe --opengl --gpu-capabilities build/reports/gpu.opengl.json
```

The JSON contains raw identity, selected MSAA/anisotropy, every feature's
supported/enabled state and fallback, all matched database rules, and every
automatic selection decision. Synthetic unit tests cover vendor detection,
hardware clamping, software-renderer safe mode, JSON export and compressed
texture fallback. Asset-pipeline tests verify that BC7 and ASTC resources carry
a complete RGBA8 safety mip chain.
