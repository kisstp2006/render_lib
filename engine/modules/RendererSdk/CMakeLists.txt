add_library(engine_renderer_sdk SHARED
    ${ENGINE_ROOT}/include/engine/renderer/Export.h
    ${ENGINE_ROOT}/include/engine/renderer/Renderer.h
    ${ENGINE_ROOT}/include/rendering/renderer_c.h
    ${ENGINE_ROOT}/src/renderer/Renderer.cpp
    ${ENGINE_ROOT}/src/renderer/RendererC.cpp)

target_link_libraries(engine_renderer_sdk PRIVATE
    engine_foundation
    engine_core
    engine_scene
    engine_renderer_core
    engine_renderer_opengl
    ${CMAKE_DL_LIBS})
if(ENGINE_BUILD_VULKAN)
    target_link_libraries(engine_renderer_sdk PRIVATE engine_renderer_vulkan)
endif()
target_include_directories(engine_renderer_sdk PUBLIC
    $<BUILD_INTERFACE:${ENGINE_ROOT}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_definitions(engine_renderer_sdk PRIVATE ENGINE_RENDERER_SDK_BUILD)
target_compile_features(engine_renderer_sdk PUBLIC cxx_std_20)
engine_apply_target_compile_options(engine_renderer_sdk)
target_compile_options(engine_renderer_sdk INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
set_target_properties(engine_renderer_sdk PROPERTIES
    OUTPUT_NAME RenderingEngine
    EXPORT_NAME renderer
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    FOLDER "Engine/Renderer/SDK"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
set_target_properties(engine_renderer_sdk PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

add_library(engine::renderer ALIAS engine_renderer_sdk)

add_custom_command(TARGET engine_renderer_sdk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ENGINE_SHADER_SOURCE_DIR}"
            "$<TARGET_FILE_DIR:engine_renderer_sdk>/shaders"
    COMMENT "Deploying renderer HLSL assets")

get_property(ENGINE_SHADERC_RUNTIME_TARGET GLOBAL PROPERTY ENGINE_SHADERC_RUNTIME_TARGET)
if(ENGINE_SHADERC_RUNTIME_TARGET)
    add_custom_command(TARGET engine_renderer_sdk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${ENGINE_SHADERC_RUNTIME_TARGET}>"
                "$<TARGET_FILE_DIR:engine_renderer_sdk>"
        COMMENT "Deploying shaderc runtime")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS engine_renderer_sdk
    EXPORT RenderingEngineTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(ENGINE_SHADERC_RUNTIME_TARGET)
    install(IMPORTED_RUNTIME_ARTIFACTS ${ENGINE_SHADERC_RUNTIME_TARGET}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY ${ENGINE_ROOT}/include/engine/renderer
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/engine)
install(FILES ${ENGINE_ROOT}/include/rendering/renderer_c.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rendering)
install(DIRECTORY ${ENGINE_SHADER_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/RenderingEngine/shaders)
install(EXPORT RenderingEngineTargets
    FILE RenderingEngineTargets.cmake
    NAMESPACE engine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderingEngine)

configure_package_config_file(
    ${ENGINE_ROOT}/../cmake/RenderingEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RenderingEngineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderingEngine
    PATH_VARS CMAKE_INSTALL_DATADIR)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/RenderingEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/RenderingEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/RenderingEngineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderingEngine)
