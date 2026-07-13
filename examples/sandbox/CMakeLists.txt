add_library(sandbox_support STATIC
    src/SandboxApp.cpp
    src/SandboxScene.cpp
    src/SandboxControls.cpp
)
set_target_properties(sandbox_support PROPERTIES FOLDER "Samples/Shared")
target_link_libraries(sandbox_support PUBLIC engine)
target_include_directories(sandbox_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sandbox_support PRIVATE ENGINE_ASSET_DIR="${CMAKE_SOURCE_DIR}/examples/assets")

if(MSVC)
    target_compile_options(sandbox_support PRIVATE /W4 /permissive-)
else()
    target_compile_options(sandbox_support PRIVATE -Wall -Wextra)
endif()

function(add_renderer_sample target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE sandbox_support)
    set_target_properties(${target} PROPERTIES FOLDER "Samples")

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

add_renderer_sample(sandbox src/main.cpp)
add_renderer_sample(sample_materials ../samples/materials/main.cpp)
add_renderer_sample(sample_gltf ../samples/gltf/main.cpp)
add_renderer_sample(sample_lights ../samples/lights/main.cpp)
add_renderer_sample(sample_hdri ../samples/hdri/main.cpp)
add_renderer_sample(sample_day_night ../samples/day_night/main.cpp)
add_renderer_sample(sample_post ../samples/post_processing/main.cpp)
