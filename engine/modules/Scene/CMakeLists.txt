engine_add_module(engine_scene
    FOLDER "Scene"
    ALIAS scene
    SOURCES
        ${ENGINE_ROOT}/include/engine/scene/Environment.h
        ${ENGINE_ROOT}/include/engine/scene/ColorGrading.h
        ${ENGINE_ROOT}/include/engine/scene/DebugDraw.h
        ${ENGINE_ROOT}/include/engine/scene/Lights.h
        ${ENGINE_ROOT}/include/engine/scene/Material.h
        ${ENGINE_ROOT}/include/engine/scene/Mesh.h
        ${ENGINE_ROOT}/include/engine/scene/MeshCombiner.h
        ${ENGINE_ROOT}/include/engine/scene/RenderSettings.h
        ${ENGINE_ROOT}/include/engine/scene/Scene.h
        ${ENGINE_ROOT}/include/engine/scene/Texture.h
        ${ENGINE_ROOT}/src/scene/Environment.cpp
        ${ENGINE_ROOT}/src/asset/ColorGrading.cpp
        ${ENGINE_ROOT}/src/scene/DebugDraw.cpp
        ${ENGINE_ROOT}/src/scene/Material.cpp
        ${ENGINE_ROOT}/src/scene/Mesh.cpp
        ${ENGINE_ROOT}/src/scene/MeshCombiner.cpp
        ${ENGINE_ROOT}/src/scene/Scene.cpp
        ${ENGINE_ROOT}/src/scene/Texture.cpp
    PUBLIC_DEPS
        engine_foundation
    PRIVATE_DEPS
        engine_third_party)
