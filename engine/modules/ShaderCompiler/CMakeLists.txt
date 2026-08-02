# The LunarG Vulkan SDK uses "Include"/"Lib" on Windows and "include"/"lib"
# on Linux/macOS; hint both so a sourced VULKAN_SDK works on every platform.
# On Linux, apt-installed libshaderc-dev is found via the default system
# search paths regardless of these hints.
find_path(ENGINE_SHADERC_INCLUDE_DIR shaderc/shaderc.hpp
    HINTS ${Vulkan_INCLUDE_DIRS} "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
set(ENGINE_SHADERC_LINK_LIBRARIES)

# Recent Windows Vulkan SDKs no longer ship a Debug shaderc_combined archive.
# Prefer shaderc's C-ABI DLL there: it is safe across Debug/Release CRT and STL
# boundaries, unlike linking the release static C++ archive into a Debug DLL.
if(WIN32)
    find_library(ENGINE_SHADERC_SHARED_LIBRARY_RELEASE shaderc_shared
        HINTS "$ENV{VULKAN_SDK}/Lib")
    find_library(ENGINE_SHADERC_SHARED_LIBRARY_DEBUG shaderc_sharedd
        HINTS "$ENV{VULKAN_SDK}/Lib")
    find_file(ENGINE_SHADERC_SHARED_RUNTIME_RELEASE shaderc_shared.dll
        HINTS "$ENV{VULKAN_SDK}/Bin")
    find_file(ENGINE_SHADERC_SHARED_RUNTIME_DEBUG shaderc_sharedd.dll
        HINTS "$ENV{VULKAN_SDK}/Bin")
    if(ENGINE_SHADERC_SHARED_LIBRARY_RELEASE AND ENGINE_SHADERC_SHARED_RUNTIME_RELEASE)
        add_library(engine_shaderc_runtime SHARED IMPORTED GLOBAL)
        set_target_properties(engine_shaderc_runtime PROPERTIES
            IMPORTED_IMPLIB_RELEASE "${ENGINE_SHADERC_SHARED_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_RELEASE "${ENGINE_SHADERC_SHARED_RUNTIME_RELEASE}"
            IMPORTED_IMPLIB_RELWITHDEBINFO "${ENGINE_SHADERC_SHARED_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${ENGINE_SHADERC_SHARED_RUNTIME_RELEASE}"
            IMPORTED_IMPLIB_MINSIZEREL "${ENGINE_SHADERC_SHARED_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_MINSIZEREL "${ENGINE_SHADERC_SHARED_RUNTIME_RELEASE}")
        if(ENGINE_SHADERC_SHARED_LIBRARY_DEBUG AND ENGINE_SHADERC_SHARED_RUNTIME_DEBUG)
            set_target_properties(engine_shaderc_runtime PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${ENGINE_SHADERC_SHARED_LIBRARY_DEBUG}"
                IMPORTED_LOCATION_DEBUG "${ENGINE_SHADERC_SHARED_RUNTIME_DEBUG}")
        else()
            set_target_properties(engine_shaderc_runtime PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${ENGINE_SHADERC_SHARED_LIBRARY_RELEASE}"
                IMPORTED_LOCATION_DEBUG "${ENGINE_SHADERC_SHARED_RUNTIME_RELEASE}")
        endif()
        list(APPEND ENGINE_SHADERC_LINK_LIBRARIES engine_shaderc_runtime)
        set_property(GLOBAL PROPERTY ENGINE_SHADERC_RUNTIME_TARGET engine_shaderc_runtime)
    endif()
endif()

if(NOT ENGINE_SHADERC_LINK_LIBRARIES)
    find_library(ENGINE_SHADERC_LIBRARY_RELEASE shaderc_combined
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
    find_library(ENGINE_SHADERC_LIBRARY_DEBUG shaderc_combinedd
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
    if(NOT ENGINE_SHADERC_LIBRARY_RELEASE)
        message(FATAL_ERROR
            "The HLSL shader pipeline requires shaderc (normally supplied by the Vulkan SDK).")
    endif()
    if(NOT ENGINE_SHADERC_LIBRARY_DEBUG)
        set(ENGINE_SHADERC_LIBRARY_DEBUG ${ENGINE_SHADERC_LIBRARY_RELEASE})
    endif()
    list(APPEND ENGINE_SHADERC_LINK_LIBRARIES
        optimized ${ENGINE_SHADERC_LIBRARY_RELEASE}
        debug ${ENGINE_SHADERC_LIBRARY_DEBUG})
endif()

if(NOT ENGINE_SHADERC_INCLUDE_DIR OR NOT ENGINE_SHADERC_LINK_LIBRARIES)
    message(FATAL_ERROR
        "The HLSL shader pipeline requires shaderc (normally supplied by the Vulkan SDK).")
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
        ${ENGINE_SHADERC_LINK_LIBRARIES}
        ${ENGINE_SHADERC_EXTRA_LIBRARIES}
    PRIVATE_INCLUDE_DIRS
        ${ENGINE_SHADERC_INCLUDE_DIR})
