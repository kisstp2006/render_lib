add_executable(sandbox main.cpp)
target_link_libraries(sandbox PRIVATE engine)
target_compile_definitions(sandbox PRIVATE ENGINE_ASSET_DIR="${CMAKE_SOURCE_DIR}/examples/assets")

if(MSVC)
    target_compile_options(sandbox PRIVATE /W4 /permissive-)
else()
    target_compile_options(sandbox PRIVATE -Wall -Wextra)
endif()
