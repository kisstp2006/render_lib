engine_add_module(engine_asset_core
    FOLDER "Assets/AssetCore"
    ALIAS asset_core
    SOURCES
        ${ENGINE_ROOT}/include/engine/asset/AssetDatabase.h
        ${ENGINE_ROOT}/include/engine/asset/AssetDescriptor.h
        ${ENGINE_ROOT}/include/engine/asset/AssetFileSystem.h
        ${ENGINE_ROOT}/include/engine/asset/AssetGuid.h
        ${ENGINE_ROOT}/include/engine/asset/AssetHash.h
        ${ENGINE_ROOT}/include/engine/asset/AssetTypeRegistry.h
        ${ENGINE_ROOT}/src/asset/AssetDatabase.cpp
        ${ENGINE_ROOT}/src/asset/AssetDescriptor.cpp
        ${ENGINE_ROOT}/src/asset/AssetFileSystem.cpp
        ${ENGINE_ROOT}/src/asset/AssetGuid.cpp
        ${ENGINE_ROOT}/src/asset/AssetHash.cpp
        ${ENGINE_ROOT}/src/asset/AssetTypeRegistry.cpp
    PUBLIC_DEPS
        engine_foundation)
