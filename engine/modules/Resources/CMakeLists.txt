engine_add_module(engine_resources
    FOLDER "Assets/Resources"
    ALIAS resources
    SOURCES
        ${ENGINE_ROOT}/include/engine/resource/BinaryIO.h
        ${ENGINE_ROOT}/include/engine/resource/CookedResource.h
        ${ENGINE_ROOT}/include/engine/resource/ResourceManager.h
        ${ENGINE_ROOT}/include/engine/resource/ResourceStreaming.h
        ${ENGINE_ROOT}/include/engine/resource/RuntimeAssetRegistry.h
        ${ENGINE_ROOT}/src/resource/BinaryIO.cpp
        ${ENGINE_ROOT}/src/resource/CookedResource.cpp
        ${ENGINE_ROOT}/src/resource/ResourceManager.cpp
        ${ENGINE_ROOT}/src/resource/ResourceStreaming.cpp
        ${ENGINE_ROOT}/src/resource/RuntimeAssetRegistry.cpp
    PUBLIC_DEPS
        engine_foundation
        engine_asset_core)
