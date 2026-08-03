find_program(DOTNET_EXECUTABLE dotnet)
if(NOT DOTNET_EXECUTABLE)
    message(FATAL_ERROR "ENGINE_BUILD_CSHARP_BINDINGS=ON requires the .NET 8 SDK (dotnet)")
endif()

include(GNUInstallDirs)

set(CSHARP_COMMON_ARGS
    -p:NativeRendererDirectory=$<TARGET_FILE_DIR:engine_renderer_sdk>
    -p:RendererShaderDirectory=$<TARGET_FILE_DIR:engine_renderer_sdk>/shaders)
set(CSHARP_DOTNET_COMMAND
    ${CMAKE_COMMAND} -E env
    DOTNET_CLI_HOME=${CMAKE_BINARY_DIR}/dotnet-home
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
    ${DOTNET_EXECUTABLE})

add_custom_target(renderer_csharp_binding
    COMMAND ${CSHARP_DOTNET_COMMAND} build
            ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine/RenderingEngine.csproj
            --configuration $<CONFIG> ${CSHARP_COMMON_ARGS}
    COMMAND ${CSHARP_DOTNET_COMMAND} build
            ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine.Silk/RenderingEngine.Silk.csproj
            --configuration $<CONFIG> ${CSHARP_COMMON_ARGS}
    DEPENDS engine_renderer_sdk
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building RenderingEngine C# binding")
set_target_properties(renderer_csharp_binding PROPERTIES FOLDER "Bindings/CSharp")

add_custom_target(renderer_csharp_examples
    COMMAND ${CSHARP_DOTNET_COMMAND} build
            ${CMAKE_CURRENT_SOURCE_DIR}/Examples/BasicPbr/BasicPbr.csproj
            --configuration $<CONFIG> ${CSHARP_COMMON_ARGS}
    COMMAND ${CSHARP_DOTNET_COMMAND} build
            ${CMAKE_CURRENT_SOURCE_DIR}/Examples/BackendSmoke/BackendSmoke.csproj
            --configuration $<CONFIG> ${CSHARP_COMMON_ARGS}
    COMMAND ${CSHARP_DOTNET_COMMAND} build
            ${CMAKE_CURRENT_SOURCE_DIR}/Examples/CustomShader/CustomShader.csproj
            --configuration $<CONFIG> ${CSHARP_COMMON_ARGS}
    DEPENDS renderer_csharp_binding
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building RenderingEngine C# examples")
set_target_properties(renderer_csharp_examples PROPERTIES FOLDER "Bindings/CSharp")

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/Directory.Build.targets
    ${CMAKE_CURRENT_SOURCE_DIR}/global.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/RenderingEngine/csharp)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine/Native.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine/Renderer.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine/WindowHost.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine/GraphicsDevice.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine/RenderingEngine.csproj
    DESTINATION ${CMAKE_INSTALL_DATADIR}/RenderingEngine/csharp/RenderingEngine)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine.Silk/SilkWindowHost.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderingEngine.Silk/RenderingEngine.Silk.csproj
    DESTINATION ${CMAKE_INSTALL_DATADIR}/RenderingEngine/csharp/RenderingEngine.Silk)
