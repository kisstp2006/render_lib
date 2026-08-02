cmake_minimum_required(VERSION 3.21)
project(RenderingEnginePackageConsumer LANGUAGES CXX)

find_package(RenderingEngine 0.1 CONFIG REQUIRED)

add_executable(rendering_engine_package_consumer main.cpp)
target_link_libraries(rendering_engine_package_consumer PRIVATE engine::renderer)
target_compile_features(rendering_engine_package_consumer PRIVATE cxx_std_20)
target_compile_definitions(rendering_engine_package_consumer PRIVATE
    RENDERING_ENGINE_PACKAGE_SHADER_DIR="${RenderingEngine_SHADER_DIR}")
