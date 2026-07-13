engine_add_module(engine_runtime
    FOLDER "Runtime"
    ALIAS runtime
    SOURCES
        ${ENGINE_ROOT}/include/engine/plugin/PluginApi.h
        ${ENGINE_ROOT}/include/engine/plugin/PluginManager.h
        ${ENGINE_ROOT}/include/engine/plugin/ServiceRegistry.h
        ${ENGINE_ROOT}/include/engine/runtime/Component.h
        ${ENGINE_ROOT}/include/engine/runtime/ComponentRegistry.h
        ${ENGINE_ROOT}/include/engine/runtime/World.h
        ${ENGINE_ROOT}/src/plugin/PluginManager.cpp
        ${ENGINE_ROOT}/src/plugin/ServiceRegistry.cpp
        ${ENGINE_ROOT}/src/runtime/ComponentRegistry.cpp
        ${ENGINE_ROOT}/src/runtime/World.cpp
    PUBLIC_DEPS
        engine_foundation
    PRIVATE_DEPS
        ${CMAKE_DL_LIBS})
