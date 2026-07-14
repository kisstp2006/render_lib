cmake_minimum_required(VERSION 3.21)
project(SourceLikeEngine LANGUAGES C CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep C and C++ dependencies on the same MSVC CRT. GLAD is generated as C;
# without enabling/configuring C explicitly it fell back to static LIBCMT,
# while the engine used the dynamic debug CRT, producing LNK4098.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ENGINE_BUILD_VULKAN "Build the Vulkan backend" ON)
option(ENGINE_BUILD_SANDBOX "Build the sandbox example app" ON)
option(ENGINE_BUILD_TESTS "Build engine unit tests" ON)
option(ENGINE_BUILD_ASSET_TOOLS "Build the asset pipeline command-line tools" ON)
option(ENGINE_ENABLE_GPU_VISUAL_TESTS
       "Run OpenGL/Vulkan golden-image tests through CTest (requires a display and GPU)" OFF)
option(ENGINE_ENABLE_GPU_STABILITY_TESTS
       "Run long-lived OpenGL/Vulkan window and resource stress tests through CTest" OFF)
option(ENGINE_ENABLE_GPU_PIPELINE_CACHE_TESTS
       "Run cold/warm pipeline-cache stutter benchmarks through CTest" OFF)
option(ENGINE_ENABLE_RUNTIME_MONITORS
       "Enable the always-on CPU-memory and GPU runtime monitor widgets" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched at configure time, no vendoring)
# ---------------------------------------------------------------------------

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

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

set(ASTCENC_CLI OFF CACHE BOOL "" FORCE)
set(ASTCENC_UNITTEST OFF CACHE BOOL "" FORCE)
set(ASTCENC_WERROR OFF CACHE BOOL "" FORCE)
set(ASTCENC_ISA_NATIVE ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    astc_encoder
    GIT_REPOSITORY https://github.com/ARM-software/astc-encoder.git
    GIT_TAG        30aabb3f42406df45a910d8496f9bee17eeba9bb # 5.3.0
    GIT_SHALLOW    TRUE
)

# These two dependencies are source/header packages rather than reusable CMake
# projects. SOURCE_SUBDIR keeps FetchContent from importing their sample apps;
# small engine-owned targets are declared below instead.
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

FetchContent_MakeAvailable(glfw glm glad stb cgltf meshoptimizer astc_encoder bc7enc_rdo vhacd)

add_library(engine_bc_encoder STATIC
    ${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp
    ${bc7enc_rdo_SOURCE_DIR}/rgbcx.cpp)
target_include_directories(engine_bc_encoder PUBLIC ${bc7enc_rdo_SOURCE_DIR})
set_target_properties(engine_bc_encoder PROPERTIES FOLDER "Dependencies")

add_library(engine_vhacd INTERFACE)
target_include_directories(engine_vhacd INTERFACE ${vhacd_SOURCE_DIR}/include)

# glad2 generates loader sources at configure time via its cmake helper
glad_add_library(glad_gl_core46 STATIC API gl:core=4.6)
set_target_properties(glad_gl_core46 PROPERTIES FOLDER "Dependencies")
if(TARGET glfw)
    set_target_properties(glfw PROPERTIES FOLDER "Dependencies")
endif()
if(TARGET glm)
    set_target_properties(glm PROPERTIES FOLDER "Dependencies")
endif()
if(TARGET meshoptimizer)
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

if(ENGINE_BUILD_ASSET_TOOLS)
    add_subdirectory(tools/assetc)
endif()

if(ENGINE_BUILD_SANDBOX)
    add_subdirectory(examples/sandbox)
endif()

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
