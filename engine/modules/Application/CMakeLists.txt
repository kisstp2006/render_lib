set(application_dependencies
    engine_foundation
    engine_core
    engine_scene
    engine_runtime
    engine_renderer_core
    engine_renderer_opengl)
if(ENGINE_BUILD_VULKAN)
    list(APPEND application_dependencies engine_renderer_vulkan)
endif()

engine_add_module(engine_application
    FOLDER "Application"
    ALIAS application
    SOURCES
        ${ENGINE_ROOT}/include/engine/core/Application.h
        ${ENGINE_ROOT}/include/engine/core/ApplicationConfig.h
        ${ENGINE_ROOT}/include/engine/core/ApplicationDesc.h
        ${ENGINE_ROOT}/src/core/Application.cpp
        ${ENGINE_ROOT}/src/core/ApplicationConfig.cpp
    PUBLIC_DEPS
        ${application_dependencies})
