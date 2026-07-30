engine_add_module(engine_renderer_vulkan
    FOLDER "Renderer/RendererVulkan"
    ALIAS renderer_vulkan
    SOURCES
        ${ENGINE_ROOT}/include/engine/backend/vk/VulkanRenderBackend.h
        ${ENGINE_ROOT}/include/engine/backend/vk/VulkanResources.h
        ${ENGINE_ROOT}/include/engine/backend/vk/VulkanPipelineCache.h
        ${ENGINE_ROOT}/include/engine/backend/vk/VulkanShaderCompiler.h
        ${ENGINE_ROOT}/include/engine/backend/vk/VulkanShaderInterop.h
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Debug.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Environment.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.FrameDebugger.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.FrameResources.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.LocalLights.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Occlusion.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Performance.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Pipeline.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Post.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.SceneResources.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Shadows.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Uploads.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Viewports.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanResources.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanPipelineCache.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanShaderCompiler.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_core
        engine_scene
        engine_renderer_core
        Vulkan::Vulkan
    PRIVATE_DEPS
        engine_shader_compiler
        engine_render_tools
        engine_third_party
        # For color_grading::MakeIdentity() (default color LUT). This was an
        # undeclared transitive dependency that only linked by accident of
        # static-library ordering on MSVC; GNU ld's single-pass archive
        # resolution exposed it as an undefined reference on Linux.
        engine_assets
        )
