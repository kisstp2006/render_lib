engine_add_module(engine_runtime
    FOLDER "Runtime"
    ALIAS runtime
    SOURCES
        ${ENGINE_ROOT}/include/engine/plugin/PluginApi.h
        ${ENGINE_ROOT}/include/engine/plugin/PluginManager.h
        ${ENGINE_ROOT}/include/engine/plugin/ServiceRegistry.h
        ${ENGINE_ROOT}/include/engine/runtime/Component.h
        ${ENGINE_ROOT}/include/engine/runtime/ComponentRegistry.h
        ${ENGINE_ROOT}/include/engine/runtime/Property.h
        ${ENGINE_ROOT}/include/engine/runtime/RenderComponents.h
        ${ENGINE_ROOT}/include/engine/runtime/World.h
        ${ENGINE_ROOT}/include/engine/runtime/WorldRenderBridge.h
        ${ENGINE_ROOT}/src/plugin/PluginManager.cpp
        ${ENGINE_ROOT}/src/plugin/ServiceRegistry.cpp
        ${ENGINE_ROOT}/src/runtime/ComponentRegistry.cpp
        ${ENGINE_ROOT}/src/runtime/Property.cpp
        ${ENGINE_ROOT}/src/runtime/RenderComponents.cpp
        ${ENGINE_ROOT}/src/runtime/World.cpp
        ${ENGINE_ROOT}/src/runtime/WorldRenderBridge.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_asset_core
        engine_core
        engine_scene
    PRIVATE_DEPS
        ${CMAKE_DL_LIBS})
