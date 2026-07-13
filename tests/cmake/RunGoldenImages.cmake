cmake_minimum_required(VERSION 3.21)

foreach(required COMPARE_EXECUTABLE MATERIALS_EXECUTABLE GLTF_EXECUTABLE
                 LIGHTS_EXECUTABLE HDRI_EXECUTABLE SOURCE_DIR BASELINE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "RunGoldenImages.cmake requires -D${required}=...")
    endif()
endforeach()
if(NOT DEFINED UPDATE_GOLDENS)
    set(UPDATE_GOLDENS OFF)
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(capture_scene scene executable backend)
    set(scene_dir "${OUTPUT_DIR}/${scene}")
    file(MAKE_DIRECTORY "${scene_dir}")
    set(stem "${scene_dir}/${backend}")
    execute_process(
        COMMAND "${executable}"
            "--${backend}"
            --resolution 640 360
            --fixed-window --hidden-window --no-focus
            --unfocused continue --no-vsync --no-validation
            --no-runtime-monitors --no-gpu-timing
            --msaa 1 --fixed-delta 0.016666667 --frames 16
            --screenshot "${stem}.png"
            --hdr-screenshot "${stem}.hdr"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_FILE "${stem}.log"
        ERROR_FILE "${stem}.log")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${scene} ${backend} capture failed (${result}). See ${stem}.log")
    endif()
    foreach(extension png hdr)
        if(NOT EXISTS "${stem}.${extension}")
            message(FATAL_ERROR
                "${scene} ${backend} did not produce ${stem}.${extension}")
        endif()
    endforeach()
endfunction()

function(compare_images scene label reference candidate profile)
    set(prefix "${OUTPUT_DIR}/${scene}/${label}")
    if(profile STREQUAL "parity")
        # Rasterization coverage and transcendental instructions differ slightly
        # between APIs. Keep the same per-channel HDR tolerance but permit those
        # localized edge/highlight pixels to cover at most 5% of the frame.
        set(maximum_failing_fraction 0.05)
        set(maximum_mean_normalized 0.25)
    else()
        set(maximum_failing_fraction 0.01)
        set(maximum_mean_normalized 0.20)
    endif()
    execute_process(
        COMMAND "${COMPARE_EXECUTABLE}" "${reference}" "${candidate}"
            --absolute 0.015
            --relative 0.04
            --relative-floor 0.05
            --max-failing-fraction ${maximum_failing_fraction}
            --max-mean-normalized ${maximum_mean_normalized}
            --diff-gain 8.0
            --diff "${prefix}.diff.png"
            --heatmap "${prefix}.heatmap.png"
            --report "${prefix}.json"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    string(STRIP "${output}" output)
    if(NOT output STREQUAL "")
        message(STATUS "${scene}/${label}: ${output}")
    endif()
    if(result GREATER 1)
        message(FATAL_ERROR "${scene}/${label} comparison error: ${error}")
    elseif(result EQUAL 1)
        message(SEND_ERROR
            "${scene}/${label} exceeded visual tolerance. See ${prefix}.heatmap.png")
        set(GOLDEN_COMPARISON_FAILED TRUE PARENT_SCOPE)
    endif()
endfunction()

set(scenes materials gltf lights hdri)
set(executables
    "${MATERIALS_EXECUTABLE}"
    "${GLTF_EXECUTABLE}"
    "${LIGHTS_EXECUTABLE}"
    "${HDRI_EXECUTABLE}")

list(LENGTH scenes scene_count)
math(EXPR last_scene "${scene_count} - 1")
foreach(index RANGE ${last_scene})
    list(GET scenes ${index} scene)
    list(GET executables ${index} executable)
    message(STATUS "Capturing ${scene} with OpenGL and Vulkan")
    capture_scene("${scene}" "${executable}" opengl)
    capture_scene("${scene}" "${executable}" vulkan)

    set(opengl_baseline_png "${BASELINE_DIR}/${scene}.opengl.png")
    set(opengl_baseline_hdr "${BASELINE_DIR}/${scene}.opengl.hdr")
    set(vulkan_baseline_png "${BASELINE_DIR}/${scene}.vulkan.png")
    set(vulkan_baseline_hdr "${BASELINE_DIR}/${scene}.vulkan.hdr")
    if(UPDATE_GOLDENS)
        file(MAKE_DIRECTORY "${BASELINE_DIR}")
        file(COPY_FILE "${OUTPUT_DIR}/${scene}/opengl.png" "${opengl_baseline_png}" ONLY_IF_DIFFERENT)
        file(COPY_FILE "${OUTPUT_DIR}/${scene}/opengl.hdr" "${opengl_baseline_hdr}" ONLY_IF_DIFFERENT)
        file(COPY_FILE "${OUTPUT_DIR}/${scene}/vulkan.png" "${vulkan_baseline_png}" ONLY_IF_DIFFERENT)
        file(COPY_FILE "${OUTPUT_DIR}/${scene}/vulkan.hdr" "${vulkan_baseline_hdr}" ONLY_IF_DIFFERENT)
        message(STATUS "Updated ${scene} OpenGL and Vulkan golden images")
    endif()
    if(NOT EXISTS "${opengl_baseline_png}" OR NOT EXISTS "${opengl_baseline_hdr}" OR
       NOT EXISTS "${vulkan_baseline_png}" OR NOT EXISTS "${vulkan_baseline_hdr}")
        message(FATAL_ERROR
            "Missing ${scene} baseline. Build the update_golden_images target first.")
    endif()

    compare_images("${scene}" baseline-opengl-ldr
        "${opengl_baseline_png}" "${OUTPUT_DIR}/${scene}/opengl.png" regression)
    compare_images("${scene}" baseline-vulkan-ldr
        "${vulkan_baseline_png}" "${OUTPUT_DIR}/${scene}/vulkan.png" regression)
    compare_images("${scene}" opengl-vulkan-ldr
        "${OUTPUT_DIR}/${scene}/opengl.png" "${OUTPUT_DIR}/${scene}/vulkan.png" parity)
    compare_images("${scene}" baseline-opengl-hdr
        "${opengl_baseline_hdr}" "${OUTPUT_DIR}/${scene}/opengl.hdr" regression)
    compare_images("${scene}" baseline-vulkan-hdr
        "${vulkan_baseline_hdr}" "${OUTPUT_DIR}/${scene}/vulkan.hdr" regression)
    compare_images("${scene}" opengl-vulkan-hdr
        "${OUTPUT_DIR}/${scene}/opengl.hdr" "${OUTPUT_DIR}/${scene}/vulkan.hdr" parity)
endforeach()

if(GOLDEN_COMPARISON_FAILED)
    message(FATAL_ERROR "One or more golden-image comparisons failed")
endif()
message(STATUS "All golden-image and OpenGL/Vulkan parity comparisons passed")
