engine_add_module(engine_third_party
    FOLDER "ThirdParty"
    ALIAS third_party
    SOURCES
        ${ENGINE_ROOT}/src/third_party/StbImpl.cpp
    PUBLIC_DEPS
        stb_image)
