add_executable(renderer_embed_cpp main.cpp)
target_link_libraries(renderer_embed_cpp PRIVATE engine::renderer)
target_compile_features(renderer_embed_cpp PRIVATE cxx_std_20)
engine_apply_target_compile_options(renderer_embed_cpp)
set_target_properties(renderer_embed_cpp PROPERTIES
    FOLDER "Examples/Renderer SDK"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
