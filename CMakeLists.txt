cmake_minimum_required(VERSION 3.21)
project(SourceLikeEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ENGINE_BUILD_VULKAN "Build the Vulkan backend" ON)
option(ENGINE_BUILD_SANDBOX "Build the sandbox example app" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

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

FetchContent_MakeAvailable(glfw glm glad stb)

# glad2 generates loader sources at configure time via its cmake helper
glad_add_library(glad_gl_core46 STATIC API gl:core=4.6)

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

if(ENGINE_BUILD_SANDBOX)
    add_subdirectory(examples/sandbox)
endif()
