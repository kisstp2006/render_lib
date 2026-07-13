engine_add_module(engine_renderer_core
    FOLDER "Renderer/RendererCore"
    ALIAS renderer_core
    SOURCES
        ${ENGINE_ROOT}/include/engine/backend/IRenderBackend.h
        ${ENGINE_ROOT}/include/engine/debug/DebugOverlay.h
        ${ENGINE_ROOT}/include/engine/debug/FrameDebugger.h
        ${ENGINE_ROOT}/include/engine/render/CascadedShadows.h
        ${ENGINE_ROOT}/include/engine/render/Exposure.h
        ${ENGINE_ROOT}/include/engine/render/GpuTiming.h
        ${ENGINE_ROOT}/include/engine/render/SceneRenderer.h
        ${ENGINE_ROOT}/include/engine/render/ShaderSource.h
        ${ENGINE_ROOT}/include/engine/render/TemporalAA.h
        ${ENGINE_ROOT}/src/debug/DebugOverlay.cpp
        ${ENGINE_ROOT}/src/debug/FrameDebugger.cpp
        ${ENGINE_ROOT}/src/render/CascadedShadows.cpp
        ${ENGINE_ROOT}/src/render/Exposure.cpp
        ${ENGINE_ROOT}/src/render/GpuTiming.cpp
        ${ENGINE_ROOT}/src/render/SceneRenderer.cpp
        ${ENGINE_ROOT}/src/render/ShaderSource.cpp
        ${ENGINE_ROOT}/src/render/TemporalAA.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_core
        engine_scene)
