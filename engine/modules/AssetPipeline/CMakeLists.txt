engine_add_module(engine_asset_pipeline
    FOLDER "Assets/Pipeline"
    ALIAS asset_pipeline
    SOURCES
        ${ENGINE_ROOT}/include/engine/asset/AssetPipeline.h
        ${ENGINE_ROOT}/src/asset/AssetPipeline.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_asset_core
        engine_resources)
