engine_add_module(engine_asset_tools
    FOLDER "Asset Pipeline"
    ALIAS asset_tools
    SOURCES
        ${ENGINE_ROOT}/include/engine/asset/AssetWorkspace.h
        ${ENGINE_ROOT}/src/asset/AssetWorkspace.cpp
    PUBLIC_DEPS
        engine_asset_pipeline
        engine_resources)
