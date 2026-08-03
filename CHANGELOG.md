# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
(see the "Versioning" section in [README.md](README.md) for what that means
before 1.0.0).

## [Unreleased]

### Added

- Installable `engine::renderer` shared-library SDK with a small C++ facade,
  stable versioned C ABI, relocatable CMake package and runtime shader policy.
- .NET 8 P/Invoke binding plus interactive PBR and backend smoke examples.
- Backend-neutral programmable `GraphicsDevice` for HLSL/GLSL/SPIR-V shader
  modules, reflection, shader permutations and hot reload, custom vertex
  layouts, graphics/compute pipelines, resource binding and render passes.
- Matching C ABI and C# custom-shader API with OpenGL/Vulkan parity example.
- Renderer-only embedded build mode that leaves ECS, editor, asset pipeline,
  tools and legacy sandbox code out of consumer projects.

## [0.1.0] - 2026-07-30

First tagged release. A from-scratch C++20/CMake renderer targeting the
*look* of Valve's Source 2 renderer, with OpenGL 4.6 and Vulkan 1.3 backends
sharing one HLSL shader family and one `RenderFrameData` frame packet.

### Added

- HDR PBR pipeline: Cook-Torrance GGX, image-based lighting (procedural sky
  or HDR equirect), Source-style day/night sky, cascaded sun shadows, local
  point/spot/area lights and shadows, gradient fog, auto-exposure, 3D LUT
  color grading, FXAA/TAA, Jimenez bloom and the Uncharted tonemapper.
- glTF 2.0 scene loading with generated normals/tangents.
- GPU-driven scene management: asynchronous Hi-Z occlusion culling, GPU
  instancing with hierarchical cluster culling (HISM), mesh LOD chains with
  screen-space error selection, and geometry batching.
- Editor runtime foundation: resize-safe offscreen OpenGL/Vulkan viewports,
  Dear ImGui docking, reflected world components, stable-GUID scene
  save/load, picking and debug drawing.
- CPU/GPU profiling with Chrome-trace export, a persistent shader/pipeline
  cache, and a golden-image visual-regression suite covering both backends.
- Headless PNG/HDR capture and a cooked-asset command-line pipeline
  (`assetc`).
- Cross-platform build: verified on Windows (MSVC) and Linux (GCC, Ubuntu
  24.04), via `CMakePresets.json` and CI on both platforms.
