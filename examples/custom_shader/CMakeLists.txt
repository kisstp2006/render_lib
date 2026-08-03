add_executable(custom_shader_example main.cpp)
target_link_libraries(custom_shader_example PRIVATE engine::renderer)
target_compile_features(custom_shader_example PRIVATE cxx_std_20)
target_compile_definitions(custom_shader_example PRIVATE
    CUSTOM_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set_target_properties(custom_shader_example PROPERTIES
    FOLDER "Examples/Renderer SDK"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
