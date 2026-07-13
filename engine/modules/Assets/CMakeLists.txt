engine_add_module(engine_assets
    FOLDER "Assets"
    ALIAS assets
    SOURCES
        ${ENGINE_ROOT}/include/engine/asset/ColorGrading.h
        ${ENGINE_ROOT}/include/engine/asset/GltfLoader.h
        ${ENGINE_ROOT}/src/asset/ColorGrading.cpp
        ${ENGINE_ROOT}/src/asset/GltfLoader.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_scene
    PRIVATE_DEPS
        engine_third_party
    PRIVATE_INCLUDE_DIRS
        ${cgltf_SOURCE_DIR})
