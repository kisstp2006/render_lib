add_library(sandbox_support STATIC
    src/SandboxApp.cpp
    src/SampleAssetPipeline.cpp
    src/SandboxScene.cpp
    src/SandboxControls.cpp
)
set_target_properties(sandbox_support PROPERTIES FOLDER "Samples/Shared")
target_link_libraries(sandbox_support PUBLIC engine)
target_include_directories(sandbox_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sandbox_support PRIVATE
    ENGINE_ASSET_DIR="${CMAKE_SOURCE_DIR}/examples/assets"
    ENGINE_SAMPLE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    ENGINE_SAMPLE_ASSET_ROOT="${CMAKE_BINARY_DIR}/sample-assets/descriptors"
    ENGINE_SAMPLE_CACHE_ROOT="${CMAKE_BINARY_DIR}/sample-assets/cache")

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
add_renderer_sample(sample_stability ../samples/stability/main.cpp)
add_renderer_sample(sample_visibility ../samples/visibility/main.cpp)

if(ENGINE_ENABLE_IMGUI)
    function(add_editor_sample target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE engine)
        set_target_properties(${target} PROPERTIES FOLDER "Samples/Editor Foundation")
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra)
        endif()
    endfunction()

    add_editor_sample(sample_editor_viewports ../samples/editor_viewports/main.cpp)
    add_editor_sample(sample_world_editor ../samples/world_editor/main.cpp)
endif()
