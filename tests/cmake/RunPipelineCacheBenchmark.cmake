cmake_minimum_required(VERSION 3.21)

foreach(required BENCHMARK_EXECUTABLE SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "RunPipelineCacheBenchmark.cmake requires -D${required}=...")
    endif()
endforeach()
if(NOT EXISTS "${BENCHMARK_EXECUTABLE}")
    message(FATAL_ERROR "Benchmark executable is missing: ${BENCHMARK_EXECUTABLE}")
endif()
if(NOT DEFINED RUN_VULKAN)
    set(RUN_VULKAN OFF)
endif()
if(NOT DEFINED BENCHMARK_FRAMES)
    set(BENCHMARK_FRAMES 120)
endif()
if(NOT DEFINED MAX_WARM_P95_MS)
    set(MAX_WARM_P95_MS 100)
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(run_cache_pass backend switch pass clear)
    set(report "${OUTPUT_DIR}/${backend}.${pass}.json")
    set(log "${OUTPUT_DIR}/${backend}.${pass}.log")
    set(clear_argument)
    if(clear)
        set(clear_argument --clear-pipeline-cache)
    endif()
    execute_process(
        COMMAND "${BENCHMARK_EXECUTABLE}" "${switch}"
            --resolution 640 360 --fixed-window --hidden-window --no-focus
            --unfocused continue --no-vsync --no-validation
            --no-runtime-monitors --no-gpu-timing --msaa 1
            --pipeline-cache-dir "${OUTPUT_DIR}/cache"
            --pipeline-cache-benchmark "${report}"
            --pipeline-benchmark-frames "${BENCHMARK_FRAMES}"
            ${clear_argument}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_FILE "${log}"
        ERROR_FILE "${log}")
    if(NOT result EQUAL 0 OR NOT EXISTS "${report}")
        message(FATAL_ERROR
            "${backend} ${pass} pipeline-cache benchmark failed (${result}). See ${log}")
    endif()
endfunction()

function(validate_backend backend switch)
    run_cache_pass("${backend}" "${switch}" cold TRUE)
    run_cache_pass("${backend}" "${switch}" warm FALSE)
    file(READ "${OUTPUT_DIR}/${backend}.cold.json" cold_json)
    file(READ "${OUTPUT_DIR}/${backend}.warm.json" warm_json)
    string(JSON cold_misses GET "${cold_json}" cache shader_misses)
    string(JSON warm_hits GET "${warm_json}" cache shader_hits)
    string(JSON warm_misses GET "${warm_json}" cache shader_misses)
    string(JSON warm_loaded GET "${warm_json}" cache persistent_cache_loaded)
    string(JSON cold_startup GET "${cold_json}" startup_ms)
    string(JSON warm_startup GET "${warm_json}" startup_ms)
    string(JSON cold_pipeline GET "${cold_json}" cache pipeline_create_ms)
    string(JSON warm_pipeline GET "${warm_json}" cache pipeline_create_ms)
    string(JSON warm_p95 GET "${warm_json}" frame_p95_ms)
    string(JSON warm_max GET "${warm_json}" frame_max_ms)
    if(cold_misses LESS 1)
        message(FATAL_ERROR "${backend}: cold cache did not compile any shader permutation")
    endif()
    if(warm_hits LESS 1 OR NOT warm_misses EQUAL 0 OR NOT warm_loaded)
        message(FATAL_ERROR
            "${backend}: warm cache regression (hits=${warm_hits}, misses=${warm_misses}, loaded=${warm_loaded})")
    endif()
    if(warm_p95 GREATER MAX_WARM_P95_MS)
        message(FATAL_ERROR
            "${backend}: warm-cache p95 ${warm_p95} ms exceeds ${MAX_WARM_P95_MS} ms")
    endif()
    message(STATUS
        "${backend}: startup ${cold_startup} -> ${warm_startup} ms; "
        "pipeline ${cold_pipeline} -> ${warm_pipeline} ms; "
        "warm p95/max ${warm_p95}/${warm_max} ms; shader hits ${warm_hits}")
endfunction()

validate_backend(opengl --opengl)
if(RUN_VULKAN)
    validate_backend(vulkan --vulkan)
endif()
