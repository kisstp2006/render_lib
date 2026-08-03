add_executable(renderer_sdk_abi_tests RendererAbiTests.cpp)
target_link_libraries(renderer_sdk_abi_tests PRIVATE engine::renderer)
target_compile_features(renderer_sdk_abi_tests PRIVATE cxx_std_20)
engine_apply_target_compile_options(renderer_sdk_abi_tests)
set_target_properties(renderer_sdk_abi_tests PROPERTIES
    FOLDER "Tests/Renderer SDK"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_test(NAME renderer_sdk_abi COMMAND renderer_sdk_abi_tests)
set_tests_properties(renderer_sdk_abi PROPERTIES LABELS "renderer;sdk;abi")

if(ENGINE_ENABLE_GPU_VISUAL_TESTS AND TARGET custom_shader_example)
    add_test(NAME renderer_sdk_custom_opengl
        COMMAND custom_shader_example --hidden --frames=3)
    set_tests_properties(renderer_sdk_custom_opengl PROPERTIES
        LABELS "renderer;sdk;gpu;opengl" TIMEOUT 120)
    if(ENGINE_BUILD_VULKAN)
        add_test(NAME renderer_sdk_custom_vulkan
            COMMAND custom_shader_example --vulkan --hidden --frames=3)
        set_tests_properties(renderer_sdk_custom_vulkan PROPERTIES
            LABELS "renderer;sdk;gpu;vulkan" TIMEOUT 120)
    endif()
endif()
