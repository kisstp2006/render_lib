set(ENGINE_CORE_SOURCES
    src/core/Log.cpp
    src/core/Window.cpp
    src/core/Input.cpp
    src/core/Application.cpp
    src/core/Camera.cpp
    src/scene/Mesh.cpp
    src/scene/Scene.cpp
    src/scene/Environment.cpp
    src/scene/Texture.cpp
    src/scene/GltfLoader.cpp
    src/render/CascadedShadows.cpp
    src/third_party/StbImpl.cpp
    src/backend/gl/GLRenderBackend.Frame.cpp
    src/backend/gl/GLRenderBackend.Resources.cpp
    src/backend/gl/GLRenderBackend.Targets.cpp
    src/backend/gl/GLRenderBackend.Post.cpp
    src/backend/gl/GLRenderBackend.LocalLights.cpp
    src/backend/gl/GLShader.cpp
    src/backend/gl/GLMesh.cpp
    src/backend/gl/GLTexture.cpp
    src/backend/gl/GLEnvironment.cpp
)

add_library(engine STATIC ${ENGINE_CORE_SOURCES})

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
)

target_include_directories(engine PRIVATE ${cgltf_SOURCE_DIR})

target_compile_definitions(engine PUBLIC
    ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
)

if(ENGINE_BUILD_VULKAN)
    target_sources(engine PRIVATE src/backend/vk/VulkanRenderBackend.cpp)
    target_link_libraries(engine PRIVATE Vulkan::Vulkan)
    target_compile_definitions(engine PUBLIC ENGINE_HAS_VULKAN=1)
else()
    target_compile_definitions(engine PUBLIC ENGINE_HAS_VULKAN=0)
endif()

if(MSVC)
    target_compile_options(engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra)
endif()
