cmake_minimum_required(VERSION 3.21)
project(SourceLikeEngine VERSION 0.1.0 LANGUAGES C CXX)

# ON when this is the top-level build (an application built directly from
# this repo); automatically OFF when add_subdirectory()'d into another
# project. Guards every setting below that would otherwise leak into or
# fight with an embedding parent's own global build configuration.
option(ENGINE_STANDALONE "Build the renderer as a standalone top-level project" ${PROJECT_IS_TOP_LEVEL})
if(NOT PROJECT_IS_TOP_LEVEL)
    set(ENGINE_STANDALONE OFF)
endif()

if(ENGINE_STANDALONE)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep C and C++ dependencies on the same MSVC CRT. GLAD is generated as C;
# without enabling/configuring C explicitly it fell back to static LIBCMT,
# while the engine used the dynamic debug CRT, producing LNK4098. Only
# forced standalone: embedded, the parent's own CRT choice already applies
# to this subtree, and overriding it here risks a CRT mismatch against the
# parent's other targets.
if(ENGINE_STANDALONE AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(ENGINE_STANDALONE AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(_engine_standalone_default ${ENGINE_STANDALONE})
option(ENGINE_BUILD_VULKAN "Build the Vulkan backend" ON)
option(ENGINE_BUILD_EXTRAS
       "Build the mini-engine, runtime, editor and asset-pipeline support used by examples"
       ${_engine_standalone_default})
set(_engine_extras_default ${ENGINE_BUILD_EXTRAS})
option(ENGINE_BUILD_RENDERER_EXAMPLES "Build minimal renderer SDK examples"
       ${_engine_standalone_default})
option(ENGINE_BUILD_RENDERER_TESTS "Build renderer SDK ABI tests"
       ${_engine_standalone_default})
option(ENGINE_BUILD_CSHARP_BINDINGS "Build the managed binding and C# examples" OFF)
option(ENGINE_BUILD_SANDBOX "Build the legacy mini-engine sandbox example"
       ${_engine_extras_default})
option(ENGINE_BUILD_TESTS "Build full mini-engine unit tests" ${_engine_extras_default})
option(ENGINE_BUILD_ASSET_TOOLS "Build the asset pipeline command-line tools"
       ${_engine_extras_default})
option(ENGINE_ENABLE_GPU_VISUAL_TESTS
       "Run OpenGL/Vulkan golden-image tests through CTest (requires a display and GPU)" OFF)
option(ENGINE_ENABLE_GPU_STABILITY_TESTS
       "Run long-lived OpenGL/Vulkan window and resource stress tests through CTest" OFF)
option(ENGINE_ENABLE_GPU_PIPELINE_CACHE_TESTS
       "Run cold/warm pipeline-cache stutter benchmarks through CTest" OFF)
option(ENGINE_ENABLE_RUNTIME_MONITORS
       "Enable the always-on CPU-memory and GPU runtime monitor widgets" ON)
option(ENGINE_ENABLE_IMGUI
       "Build the Dear ImGui docking integration used by editor examples"
       ${_engine_extras_default})

if(ENGINE_BUILD_SANDBOX OR ENGINE_BUILD_TESTS OR ENGINE_BUILD_ASSET_TOOLS OR ENGINE_ENABLE_IMGUI)
    set(ENGINE_BUILD_EXTRAS ON)
endif()

set(ENGINE_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

include(FetchContent)

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched at configure time, no vendoring)
# ---------------------------------------------------------------------------

# No FORCE on any of these: they set our preferred defaults for the first
# configure without clobbering a value an embedding parent project or a
# command-line -D already gave the cache.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
set(GLFW_INSTALL OFF CACHE BOOL "")
# Wayland support needs wayland-protocols/wayland-scanner in addition to
# libwayland-dev; X11 alone (which also covers XWayland-compatible Wayland
# sessions) keeps the Linux dependency list short. Set GLFW_BUILD_WAYLAND=ON
# in a local cache if native Wayland windowing is specifically needed.
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "")
# GLFW's own switch to pin its library type independent of the ambient
# BUILD_SHARED_LIBS, which an embedding parent may have set ON for its own
# targets without meaning to also turn our fetched GLFW into a shared lib.
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "")

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

if(ENGINE_BUILD_EXTRAS)
    FetchContent_Declare(
        cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG        v1.15
        GIT_SHALLOW    TRUE
    )

    FetchContent_Declare(
        meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG        73583c335e541c139821d0de2bf5f12960a04941 # v1.0
        GIT_SHALLOW    TRUE
    )
endif()

if(ENGINE_BUILD_EXTRAS AND ENGINE_ENABLE_IMGUI)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.9b-docking
        GIT_SHALLOW    TRUE
    )
endif()

if(ENGINE_BUILD_EXTRAS)
    set(ASTCENC_CLI OFF CACHE BOOL "")
    set(ASTCENC_UNITTEST OFF CACHE BOOL "")
    set(ASTCENC_WERROR OFF CACHE BOOL "")
    set(ASTCENC_ISA_NATIVE ON CACHE BOOL "")
    FetchContent_Declare(
        astc_encoder
        GIT_REPOSITORY https://github.com/ARM-software/astc-encoder.git
        GIT_TAG        30aabb3f42406df45a910d8496f9bee17eeba9bb # 5.3.0
        GIT_SHALLOW    TRUE
    )

    # These two dependencies are source/header packages rather than reusable
    # CMake projects. SOURCE_SUBDIR keeps FetchContent from importing their
    # sample apps; small engine-owned targets are declared below instead.
    FetchContent_Declare(
        bc7enc_rdo
        GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git
        GIT_TAG        dbe416d28a5530b4e8cc45b14bf034dc6b96bbde
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  engine_no_cmake_project
    )
    FetchContent_Declare(
        vhacd
        GIT_REPOSITORY https://github.com/kmammou/v-hacd.git
        GIT_TAG        22ec20a7f8ea221ab600df869369b9c6a258cb10 # 4.1.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  engine_no_cmake_project
    )
endif()

FetchContent_MakeAvailable(glfw glm glad stb)
if(ENGINE_BUILD_EXTRAS)
    FetchContent_MakeAvailable(cgltf meshoptimizer astc_encoder bc7enc_rdo vhacd)
endif()
if(ENGINE_BUILD_EXTRAS AND ENGINE_ENABLE_IMGUI)
    FetchContent_MakeAvailable(imgui)
endif()

if(ENGINE_BUILD_EXTRAS)
    add_library(engine_bc_encoder STATIC
        ${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp
        ${bc7enc_rdo_SOURCE_DIR}/rgbcx.cpp)
    target_include_directories(engine_bc_encoder PUBLIC ${bc7enc_rdo_SOURCE_DIR})
    set_target_properties(engine_bc_encoder PROPERTIES FOLDER "Dependencies")

    add_library(engine_vhacd INTERFACE)
    target_include_directories(engine_vhacd INTERFACE ${vhacd_SOURCE_DIR}/include)
endif()

# glad2 generates loader sources at configure time via its cmake helper
glad_add_library(glad_gl_core46 STATIC API gl:core=4.6)
set_target_properties(glad_gl_core46 PROPERTIES FOLDER "Dependencies")
if(TARGET glfw)
    set_target_properties(glfw PROPERTIES FOLDER "Dependencies")
endif()
if(TARGET glm)
    set_target_properties(glm PROPERTIES FOLDER "Dependencies")
endif()
if(ENGINE_BUILD_EXTRAS AND TARGET meshoptimizer)
    set_target_properties(meshoptimizer PROPERTIES FOLDER "Dependencies")
endif()
if(TARGET astcenc-native-static)
    set_target_properties(astcenc-native-static PROPERTIES FOLDER "Dependencies")
endif()

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

if(ENGINE_BUILD_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING "Vulkan SDK not found - disabling ENGINE_BUILD_VULKAN. Install the LunarG Vulkan SDK to enable it.")
        set(ENGINE_BUILD_VULKAN OFF)
    endif()
endif()

add_subdirectory(engine)

if(ENGINE_BUILD_EXTRAS AND ENGINE_BUILD_ASSET_TOOLS)
    add_subdirectory(tools/assetc)
endif()

if(ENGINE_BUILD_RENDERER_EXAMPLES)
    add_subdirectory(examples/renderer_embed)
endif()

if(ENGINE_BUILD_RENDERER_TESTS)
    enable_testing()
    add_subdirectory(tests/renderer_sdk)
endif()

if(ENGINE_BUILD_EXTRAS AND ENGINE_BUILD_SANDBOX)
    add_subdirectory(examples/sandbox)
endif()

if(ENGINE_BUILD_EXTRAS AND ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ENGINE_BUILD_CSHARP_BINDINGS)
    add_subdirectory(bindings/csharp)
endif()
