set(ENGINE_SOURCES
    # Core/platform-agnostic application layer
    src/core/Log.cpp
    src/core/ApplicationConfig.cpp
    src/core/Window.cpp
    src/core/Input.cpp
    src/core/Application.cpp
    src/core/Camera.cpp

    # Runtime extension and behavior layer
    src/plugin/PluginManager.cpp
    src/plugin/ServiceRegistry.cpp
    src/runtime/ComponentRegistry.cpp
    src/runtime/World.cpp

    # Backend-neutral scene data and asset import
    src/scene/Mesh.cpp
    src/scene/Scene.cpp
    src/scene/Environment.cpp
    src/scene/Texture.cpp
    src/asset/ColorGrading.cpp
    src/asset/GltfLoader.cpp

    # Renderer algorithms shared by backends
    src/render/CascadedShadows.cpp
    src/render/Exposure.cpp
    src/render/GpuTiming.cpp
    src/render/SceneRenderer.cpp
    src/render/ShaderSource.cpp
    src/render/TemporalAA.cpp

    # Profiling and diagnostics
    src/profiling/CpuProfiler.cpp
    src/debug/DebugOverlay.cpp

    # Third-party implementation translation units
    src/third_party/StbImpl.cpp

    # OpenGL backend
    src/backend/gl/GLRenderBackend.cpp
    src/backend/gl/GLRenderBackend.Frame.cpp
    src/backend/gl/GLRenderBackend.Targets.cpp
    src/backend/gl/GLRenderBackend.Post.cpp
    src/backend/gl/GLRenderBackend.LocalLights.cpp
    src/backend/gl/GLRenderBackend.Debug.cpp
    src/backend/gl/GLShader.cpp
    src/backend/gl/GLMesh.cpp
    src/backend/gl/GLTexture.cpp
    src/backend/gl/GLEnvironment.cpp
)

add_library(engine STATIC ${ENGINE_SOURCES})

target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine
    PUBLIC
        glfw
        glm::glm
    PRIVATE
        glad_gl_core46
        stb_image
        ${CMAKE_DL_LIBS}
)

target_include_directories(engine PRIVATE ${cgltf_SOURCE_DIR})

target_compile_definitions(engine PUBLIC
    ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

if(ENGINE_BUILD_VULKAN)
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

    target_sources(engine PRIVATE
        src/backend/vk/VulkanRenderBackend.cpp
        src/backend/vk/VulkanRenderBackend.Pipeline.cpp
        src/backend/vk/VulkanRenderBackend.FrameResources.cpp
        src/backend/vk/VulkanRenderBackend.SceneResources.cpp
        src/backend/vk/VulkanRenderBackend.Shadows.cpp
        src/backend/vk/VulkanRenderBackend.Environment.cpp
        src/backend/vk/VulkanRenderBackend.LocalLights.cpp
        src/backend/vk/VulkanRenderBackend.Post.cpp
        src/backend/vk/VulkanRenderBackend.Performance.cpp
        src/backend/vk/VulkanRenderBackend.Debug.cpp
        src/backend/vk/VulkanResources.cpp
        src/backend/vk/VulkanShaderCompiler.cpp
    )
    target_include_directories(engine PRIVATE ${ENGINE_SHADERC_INCLUDE_DIR})
    target_link_libraries(engine PRIVATE Vulkan::Vulkan
        optimized ${ENGINE_SHADERC_LIBRARY_RELEASE}
        debug ${ENGINE_SHADERC_LIBRARY_DEBUG})
    target_compile_definitions(engine PUBLIC ENGINE_HAS_VULKAN=1)
    target_compile_definitions(engine PRIVATE
        ENGINE_VULKAN_SHADER_CACHE_DIR="${CMAKE_BINARY_DIR}/runtime_shaders/vk/$<CONFIG>"
    )
else()
    target_compile_definitions(engine PUBLIC ENGINE_HAS_VULKAN=0)
endif()

if(MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra)
endif()
