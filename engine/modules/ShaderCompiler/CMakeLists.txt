find_path(ENGINE_SHADERC_INCLUDE_DIR shaderc/shaderc.hpp
    HINTS ${Vulkan_INCLUDE_DIRS} "$ENV{VULKAN_SDK}/Include")
find_library(ENGINE_SHADERC_LIBRARY_RELEASE shaderc_combined
    HINTS "$ENV{VULKAN_SDK}/Lib")
find_library(ENGINE_SHADERC_LIBRARY_DEBUG shaderc_combinedd
    HINTS "$ENV{VULKAN_SDK}/Lib")
if(NOT ENGINE_SHADERC_INCLUDE_DIR OR NOT ENGINE_SHADERC_LIBRARY_RELEASE)
    message(FATAL_ERROR
        "The HLSL shader pipeline requires shaderc (normally supplied by the Vulkan SDK).")
endif()
if(NOT ENGINE_SHADERC_LIBRARY_DEBUG)
    set(ENGINE_SHADERC_LIBRARY_DEBUG ${ENGINE_SHADERC_LIBRARY_RELEASE})
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
    PRIVATE_INCLUDE_DIRS
        ${ENGINE_SHADERC_INCLUDE_DIR})
