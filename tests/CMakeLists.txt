add_library(engine_test_runtime_plugin MODULE plugins/TestRuntimePlugin.cpp)
target_include_directories(engine_test_runtime_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_compile_definitions(engine_test_runtime_plugin PRIVATE ENGINE_PLUGIN_BUILD=1)
set_target_properties(engine_test_runtime_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TestRuntimePlugin"
    FOLDER "Tests/Plugins")

add_library(engine_test_dependent_plugin MODULE plugins/TestDependentPlugin.cpp)
target_include_directories(engine_test_dependent_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_compile_definitions(engine_test_dependent_plugin PRIVATE ENGINE_PLUGIN_BUILD=1)
set_target_properties(engine_test_dependent_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TestDependentPlugin"
    FOLDER "Tests/Plugins")

add_executable(engine_tests src/RendererCoreTests.cpp)
set_target_properties(engine_tests PROPERTIES FOLDER "Tests")
target_link_libraries(engine_tests PRIVATE engine)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_compile_definitions(engine_tests PRIVATE
    TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/examples/assets"
    TEST_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    TEST_RUNTIME_PLUGIN_PATH="$<TARGET_FILE:engine_test_runtime_plugin>"
    TEST_DEPENDENT_PLUGIN_PATH="$<TARGET_FILE:engine_test_dependent_plugin>")
add_dependencies(engine_tests engine_test_runtime_plugin engine_test_dependent_plugin)

if(MSVC)
    target_compile_options(engine_tests PRIVATE /W4 /permissive-)
    target_compile_options(engine_test_runtime_plugin PRIVATE /W4 /permissive-)
    target_compile_options(engine_test_dependent_plugin PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_tests PRIVATE -Wall -Wextra)
    target_compile_options(engine_test_runtime_plugin PRIVATE -Wall -Wextra)
    target_compile_options(engine_test_dependent_plugin PRIVATE -Wall -Wextra)
endif()

add_test(NAME engine_renderer_core COMMAND engine_tests)

add_executable(engine_visual_compare src/VisualCompare.cpp)
set_target_properties(engine_visual_compare PROPERTIES FOLDER "Tools")
target_link_libraries(engine_visual_compare PRIVATE engine::render_tools)
if(MSVC)
    target_compile_options(engine_visual_compare PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_visual_compare PRIVATE -Wall -Wextra)
endif()

if(ENGINE_BUILD_SANDBOX AND ENGINE_BUILD_VULKAN)
    set(GOLDEN_IMAGE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunGoldenImages.cmake)
    set(GOLDEN_IMAGE_BASELINES ${CMAKE_CURRENT_SOURCE_DIR}/golden/baselines)
    set(GOLDEN_IMAGE_OUTPUT ${CMAKE_BINARY_DIR}/visual-regression)
    set(GOLDEN_IMAGE_COMMAND
        ${CMAKE_COMMAND}
        -DCOMPARE_EXECUTABLE=$<TARGET_FILE:engine_visual_compare>
        -DMATERIALS_EXECUTABLE=$<TARGET_FILE:sample_materials>
        -DGLTF_EXECUTABLE=$<TARGET_FILE:sample_gltf>
        -DLIGHTS_EXECUTABLE=$<TARGET_FILE:sample_lights>
        -DHDRI_EXECUTABLE=$<TARGET_FILE:sample_hdri>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBASELINE_DIR=${GOLDEN_IMAGE_BASELINES}
        -DOUTPUT_DIR=${GOLDEN_IMAGE_OUTPUT})

    add_custom_target(golden_images
        COMMAND ${GOLDEN_IMAGE_COMMAND} -DUPDATE_GOLDENS=OFF -P ${GOLDEN_IMAGE_SCRIPT}
        DEPENDS engine_visual_compare sample_materials sample_gltf sample_lights sample_hdri
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Running tolerant HDR/LDR golden-image and backend-parity tests")
    set_target_properties(golden_images PROPERTIES FOLDER "Tests/Visual")

    add_custom_target(update_golden_images
        COMMAND ${GOLDEN_IMAGE_COMMAND} -DUPDATE_GOLDENS=ON -P ${GOLDEN_IMAGE_SCRIPT}
        DEPENDS engine_visual_compare sample_materials sample_gltf sample_lights sample_hdri
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Updating OpenGL golden images, then verifying Vulkan parity")
    set_target_properties(update_golden_images PROPERTIES FOLDER "Tests/Visual")

    if(ENGINE_ENABLE_GPU_VISUAL_TESTS)
        add_test(NAME engine_golden_images
            COMMAND ${GOLDEN_IMAGE_COMMAND} -DUPDATE_GOLDENS=OFF -P ${GOLDEN_IMAGE_SCRIPT})
        set_tests_properties(engine_golden_images PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            LABELS "gpu;visual"
            TIMEOUT 600)
    endif()
endif()
