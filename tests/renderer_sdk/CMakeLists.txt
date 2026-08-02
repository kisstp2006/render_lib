add_executable(renderer_sdk_abi_tests RendererAbiTests.cpp)
target_link_libraries(renderer_sdk_abi_tests PRIVATE engine::renderer)
target_compile_features(renderer_sdk_abi_tests PRIVATE cxx_std_20)
engine_apply_target_compile_options(renderer_sdk_abi_tests)
set_target_properties(renderer_sdk_abi_tests PROPERTIES
    FOLDER "Tests/Renderer SDK"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_test(NAME renderer_sdk_abi COMMAND renderer_sdk_abi_tests)
set_tests_properties(renderer_sdk_abi PROPERTIES LABELS "renderer;sdk;abi")
