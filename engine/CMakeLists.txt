set(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EngineModule.cmake)

# Shared usage requirements only. Concrete code lives in the modules below so
# each layer becomes its own IDE project and can be built/tested independently.
add_library(engine_build_config INTERFACE)
target_include_directories(engine_build_config INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_build_config INTERFACE glm::glm)
target_compile_definitions(engine_build_config INTERFACE
    ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    ENGINE_ENABLE_RUNTIME_MONITORS=$<BOOL:${ENGINE_ENABLE_RUNTIME_MONITORS}>
    ENGINE_HAS_VULKAN=$<BOOL:${ENGINE_BUILD_VULKAN}>)
set_target_properties(engine_build_config PROPERTIES FOLDER "Engine/Build")

# ezEngine-inspired dependency direction:
# Foundation -> Core/Scene/Runtime -> RendererCore -> API backends -> Application.
add_subdirectory(modules/ThirdParty)
add_subdirectory(modules/Foundation)
add_subdirectory(modules/AssetCore)
add_subdirectory(modules/Resources)
add_subdirectory(modules/AssetPipeline)
add_subdirectory(modules/AssetTools)
add_subdirectory(modules/Core)
add_subdirectory(modules/Scene)
add_subdirectory(modules/AssetCookers)
add_subdirectory(modules/Assets)
add_subdirectory(modules/Runtime)
add_subdirectory(modules/RenderTools)
add_subdirectory(modules/RendererCore)
add_subdirectory(modules/RendererOpenGL)
if(ENGINE_BUILD_VULKAN)
    add_subdirectory(modules/RendererVulkan)
endif()
add_subdirectory(modules/Application)

# Backward-compatible facade. Existing applications keep linking `engine`, but
# the target itself contains no engine implementation; it only aggregates the
# independently compiled modules above.
add_library(engine STATIC src/Engine.cpp)
target_link_libraries(engine PUBLIC
    engine_build_config
    engine_foundation
    engine_asset_core
    engine_resources
    engine_asset_pipeline
    engine_asset_tools
    engine_core
    engine_scene
    engine_asset_cookers
    engine_assets
    engine_runtime
    engine_render_tools
    engine_renderer_core
    engine_renderer_opengl
    engine_application)
if(ENGINE_BUILD_VULKAN)
    target_link_libraries(engine PUBLIC engine_renderer_vulkan)
endif()
engine_apply_target_defaults(engine "Umbrella")
add_library(engine::engine ALIAS engine)
