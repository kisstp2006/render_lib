find_path(ENGINE_SHADERC_INCLUDE_DIR shaderc/shaderc.hpp
    HINTS ${Vulkan_INCLUDE_DIRS} "$ENV{VULKAN_SDK}/Include")
find_library(ENGINE_SHADERC_LIBRARY_RELEASE shaderc_combined
    HINTS "$ENV{VULKAN_SDK}/Lib")
find_library(ENGINE_SHADERC_LIBRARY_DEBUG shaderc_combinedd
    HINTS "$ENV{VULKAN_SDK}/Lib")
if(NOT ENGINE_SHADERC_INCLUDE_DIR OR NOT ENGINE_SHADERC_LIBRARY_RELEASE)
    message(FATAL_ERROR
        "ENGINE_BUILD_VULKAN=ON requires the Vulkan SDK shaderc library for runtime GLSL compilation.")
endif()
if(NOT ENGINE_SHADERC_LIBRARY_DEBUG)
    set(ENGINE_SHADERC_LIBRARY_DEBUG ${ENGINE_SHADERC_LIBRARY_RELEASE})
endif()

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
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Performance.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Pipeline.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Post.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.SceneResources.cpp
        ${ENGINE_ROOT}/src/backend/vk/VulkanRenderBackend.Shadows.cpp
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
        engine_render_tools
        engine_third_party
        optimized ${ENGINE_SHADERC_LIBRARY_RELEASE}
        debug ${ENGINE_SHADERC_LIBRARY_DEBUG}
    PRIVATE_INCLUDE_DIRS
        ${ENGINE_SHADERC_INCLUDE_DIR})
