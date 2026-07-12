add_executable(engine_tests CascadedShadowsTests.cpp)
target_link_libraries(engine_tests PRIVATE engine)
target_compile_definitions(engine_tests PRIVATE TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/examples/assets")

if(MSVC)
    target_compile_options(engine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME engine_cascaded_shadows COMMAND engine_tests)
