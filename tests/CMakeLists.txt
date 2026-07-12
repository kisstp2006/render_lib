add_executable(engine_tests CascadedShadowsTests.cpp)
target_link_libraries(engine_tests PRIVATE engine)

if(MSVC)
    target_compile_options(engine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME engine_cascaded_shadows COMMAND engine_tests)
