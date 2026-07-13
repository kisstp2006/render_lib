add_library(engine_test_runtime_plugin MODULE plugins/TestRuntimePlugin.cpp)
target_include_directories(engine_test_runtime_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_compile_definitions(engine_test_runtime_plugin PRIVATE ENGINE_PLUGIN_BUILD=1)
set_target_properties(engine_test_runtime_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TestRuntimePlugin")

add_library(engine_test_dependent_plugin MODULE plugins/TestDependentPlugin.cpp)
target_include_directories(engine_test_dependent_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_compile_definitions(engine_test_dependent_plugin PRIVATE ENGINE_PLUGIN_BUILD=1)
set_target_properties(engine_test_dependent_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TestDependentPlugin")

add_executable(engine_tests src/RendererCoreTests.cpp)
target_link_libraries(engine_tests PRIVATE engine)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_compile_definitions(engine_tests PRIVATE
    TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/examples/assets"
    TEST_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    TEST_RUNTIME_PLUGIN_PATH="$<TARGET_FILE:engine_test_runtime_plugin>"
    TEST_DEPENDENT_PLUGIN_PATH="$<TARGET_FILE:engine_test_dependent_plugin>")
add_dependencies(engine_tests engine_test_runtime_plugin engine_test_dependent_plugin)

if(MSVC)
    target_compile_options(engine_tests PRIVATE /W4 /permissive-)
    target_compile_options(engine_test_runtime_plugin PRIVATE /W4 /permissive-)
    target_compile_options(engine_test_dependent_plugin PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_tests PRIVATE -Wall -Wextra)
    target_compile_options(engine_test_runtime_plugin PRIVATE -Wall -Wextra)
    target_compile_options(engine_test_dependent_plugin PRIVATE -Wall -Wextra)
endif()

add_test(NAME engine_renderer_core COMMAND engine_tests)
