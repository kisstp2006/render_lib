engine_add_module(engine_core
    FOLDER "Core"
    ALIAS core
    SOURCES
        ${ENGINE_ROOT}/include/engine/core/Camera.h
        ${ENGINE_ROOT}/include/engine/core/Input.h
        ${ENGINE_ROOT}/include/engine/core/Window.h
        ${ENGINE_ROOT}/include/engine/debug/RenderDocCapture.h
        ${ENGINE_ROOT}/src/core/Camera.cpp
        ${ENGINE_ROOT}/src/core/Input.cpp
        ${ENGINE_ROOT}/src/core/Window.cpp
        ${ENGINE_ROOT}/src/debug/RenderDocCapture.cpp
    PUBLIC_DEPS
        engine_foundation
        glfw
    PRIVATE_DEPS
        ${CMAKE_DL_LIBS})
