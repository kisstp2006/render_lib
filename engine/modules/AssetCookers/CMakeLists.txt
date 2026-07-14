engine_add_module(engine_asset_cookers
    FOLDER "AssetCookers"
    ALIAS asset_cookers
    SOURCES
        ${ENGINE_ROOT}/include/engine/asset/cook/MeshProcessing.h
        ${ENGINE_ROOT}/include/engine/asset/cook/TextureBlockCompression.h
        ${ENGINE_ROOT}/src/asset/cook/CollisionCooker.cpp
        ${ENGINE_ROOT}/src/asset/cook/MeshProcessing.cpp
        ${ENGINE_ROOT}/src/asset/cook/TextureBlockCompression.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_scene
    PRIVATE_DEPS
        meshoptimizer
        astcenc-native-static
        engine_bc_encoder
        engine_vhacd)
