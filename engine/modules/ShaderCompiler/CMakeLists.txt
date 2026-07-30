# The LunarG Vulkan SDK uses "Include"/"Lib" on Windows and "include"/"lib"
# on Linux/macOS; hint both so a sourced VULKAN_SDK works on every platform.
# On Linux, apt-installed libshaderc-dev is found via the default system
# search paths regardless of these hints.
find_path(ENGINE_SHADERC_INCLUDE_DIR shaderc/shaderc.hpp
    HINTS ${Vulkan_INCLUDE_DIRS} "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
find_library(ENGINE_SHADERC_LIBRARY_RELEASE shaderc_combined
    HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
find_library(ENGINE_SHADERC_LIBRARY_DEBUG shaderc_combinedd
    HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
if(NOT ENGINE_SHADERC_INCLUDE_DIR OR NOT ENGINE_SHADERC_LIBRARY_RELEASE)
    message(FATAL_ERROR
        "The HLSL shader pipeline requires shaderc (normally supplied by the Vulkan SDK).")
endif()
if(NOT ENGINE_SHADERC_LIBRARY_DEBUG)
    set(ENGINE_SHADERC_LIBRARY_DEBUG ${ENGINE_SHADERC_LIBRARY_RELEASE})
endif()

# The LunarG SDK's shaderc_combined is a single self-contained archive, but
# distro packages (e.g. Ubuntu/Debian libshaderc-dev) ship a thinner
# "combined" archive that still depends on separate SPIRV-Tools and glslang
# static libraries at link time. Add them if present, wrapped in a linker
# group since their internal dependency order is not worth hardcoding; on
# the LunarG SDK none of these are found separately and nothing changes.
find_library(ENGINE_SPIRV_TOOLS_OPT_LIBRARY SPIRV-Tools-opt)
find_library(ENGINE_SPIRV_TOOLS_LIBRARY SPIRV-Tools)
find_library(ENGINE_GLSLANG_LIBRARY glslang)
find_library(ENGINE_GLSLANG_SPIRV_LIBRARY SPIRV)
find_library(ENGINE_GLSLANG_DEFAULT_RESOURCE_LIMITS_LIBRARY glslang-default-resource-limits)
find_library(ENGINE_GLSLANG_MACHINE_INDEPENDENT_LIBRARY MachineIndependent)
find_library(ENGINE_GLSLANG_GENERIC_CODEGEN_LIBRARY GenericCodeGen)
find_library(ENGINE_GLSLANG_OS_DEPENDENT_LIBRARY OSDependent)
set(ENGINE_SHADERC_EXTRA_LIBRARIES)
if(ENGINE_SPIRV_TOOLS_OPT_LIBRARY AND ENGINE_SPIRV_TOOLS_LIBRARY)
    set(ENGINE_SHADERC_EXTRA_LIBRARIES
        "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--start-group>"
        ${ENGINE_SPIRV_TOOLS_OPT_LIBRARY} ${ENGINE_SPIRV_TOOLS_LIBRARY}
        ${ENGINE_GLSLANG_LIBRARY} ${ENGINE_GLSLANG_SPIRV_LIBRARY}
        ${ENGINE_GLSLANG_DEFAULT_RESOURCE_LIMITS_LIBRARY}
        ${ENGINE_GLSLANG_MACHINE_INDEPENDENT_LIBRARY}
        ${ENGINE_GLSLANG_GENERIC_CODEGEN_LIBRARY}
        ${ENGINE_GLSLANG_OS_DEPENDENT_LIBRARY}
        "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--end-group>")
endif()

engine_add_module(engine_shader_compiler
    FOLDER "Renderer/ShaderCompiler"
    ALIAS shader_compiler
    SOURCES
        ${ENGINE_ROOT}/include/engine/shader/HlslCompiler.h
        ${ENGINE_ROOT}/src/shader/HlslCompiler.cpp
    PUBLIC_DEPS
        engine_renderer_core
    PRIVATE_DEPS
        optimized ${ENGINE_SHADERC_LIBRARY_RELEASE}
        debug ${ENGINE_SHADERC_LIBRARY_DEBUG}
        ${ENGINE_SHADERC_EXTRA_LIBRARIES}
    PRIVATE_INCLUDE_DIRS
        ${ENGINE_SHADERC_INCLUDE_DIR})
