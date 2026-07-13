engine_add_module(engine_render_tools
    FOLDER "Tools"
    ALIAS render_tools
    SOURCES
        ${ENGINE_ROOT}/include/engine/testing/VisualRegression.h
        ${ENGINE_ROOT}/src/testing/VisualRegression.cpp
    PUBLIC_DEPS
        engine_third_party)
