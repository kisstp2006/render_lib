# ezEngine application and renderer configuration study

Reference revision: `ezEngine/ezEngine@2fe8e9b1f5605862f67d7bc1e6c8ee780b8b3ef1`.

Primary references:

- [Application layers and lifecycle](https://ezengine.net/pages/docs/runtime/application/application.html)
- [Window project settings](https://ezengine.net/pages/docs/projects/project-settings.html)
- [Render pipeline](https://ezengine.net/pages/docs/graphics/render-pipeline/render-pipeline-overview.html)
- [Shader permutations and render state](https://ezengine.net/pages/docs/graphics/shaders/shader-render-state.html)
- `Code/Engine/Foundation/Application/Application.h`
- `Code/Engine/Core/GameApplication/GameApplicationBase.h`
- `Code/Engine/Core/System/Window.h`
- `Code/Engine/GameEngine/GameApplication/GameApplication.h`
- `Code/Engine/RendererFoundation/Device/DeviceCapabilities.h`
- `Code/Engine/RendererCore/Pipeline/Passes/*`

## What ezEngine exposes

ezEngine separates the platform application, game application and render
pipeline. The useful public controls in those layers are:

- application name, quit request, return code and command-line access;
- startup/shutdown, background/foreground and per-frame execution hooks;
- skip, render-only and update-input-and-render loop modes;
- fixed/resizable window, borderless or exclusive fullscreen, monitor,
  position, resolution, cursor clipping/visibility, initial focus and centering;
- persistent runtime CVars, including VSync and the FPS overlay;
- renderer/backend selection and custom device creation;
- adapter/device capabilities and swapchain present-mode changes;
- data-driven render-pipeline passes that can be activated and configured;
- shader permutations with complete rasterizer, depth/stencil and blend state.

Its standard render passes also expose effect-specific parameters for render
target format/MSAA/clear, bloom, tonemapping/color grading, AO, screen-space
shadows, light shafts and lens effects. Those parameters only make sense when
the corresponding pass exists.

## Implemented in this renderer

The equivalent configuration is deliberately smaller and strongly typed:

- `WindowDesc`: title, size, API, four window modes, monitor, explicit or
  centered position, visibility, initial focus and cursor mode;
- `ApplicationDesc`: background behavior (`Continue`, `RenderOnly`, `Pause`),
  clamped maximum delta time, optional FPS limit and right-mouse cursor capture;
- `RenderBackendConfig`: present mode, requested MSAA, maximum anisotropy,
  discrete-GPU preference, adapter-name filter, validation and GPU timing;
- `ApplicationEvent`: begin/end frame, before/after update, before/after render,
  and foreground/background transitions;
- `RunOneFrame`, `RequestQuit`, pause, runtime FPS limit and runtime present-mode
  changes;
- `BackendCapabilities`: selected adapter, VRAM where available, active/maximum
  MSAA and anisotropy, GPU timestamp support and present-mode support;
- a version-tolerant text config through `LoadApplicationConfig` and
  `SaveApplicationConfig`;
- identical CLI controls in every sample, because all samples share the same
  application implementation.

Both OpenGL and Vulkan consume the same settings. OpenGL translates present
mode to swap interval; Vulkan selects FIFO, FIFO-relaxed or immediate and
recreates the swapchain when changed. Requested MSAA and anisotropy are clamped
to the selected device's capabilities. Vulkan adapter preference is applied
before device creation. Validation and timestamp resources are only created
when enabled.

Scene-level image settings remain in `Scene`: shadows, fog, sky/environment,
exposure, bloom, tonemap, LUT, FXAA and TAA. This avoids mixing one scene's
art direction with process/window settings.

## Intentionally not copied yet

- A generic CVar/console and reflection system: useful later, but a much larger
  subsystem than application settings. The typed config covers current needs.
- A configurable render graph: it is already roadmap item 8 and should be
  designed around measured resource lifetimes, not copied around the current
  hard-coded pass chain.
- AO, lens effects, light shafts and other absent ezEngine passes: adding a
  setting without the rendering implementation would be misleading. These
  remain in their existing roadmap positions.
- Editor project profiles, multi-world game-state switching and DX11 device
  injection are still outside this renderer. Runtime plugin startup and one
  behavior world are now implemented separately; see
  [runtime C++ plugins](runtime-cpp-plugins.md).
- Exclusive frame-capture integration and memory reporting: already explicit
  roadmap diagnostics work rather than configuration-only switches.

This keeps the architectural lesson from ezEngine—separate lifecycle, window,
device and scene settings—without turning this renderer into an ezEngine clone.
