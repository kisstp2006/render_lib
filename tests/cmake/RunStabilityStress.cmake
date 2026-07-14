if(NOT DEFINED STABILITY_EXECUTABLE OR NOT EXISTS "${STABILITY_EXECUTABLE}")
    message(FATAL_ERROR "STABILITY_EXECUTABLE is missing: ${STABILITY_EXECUTABLE}")
endif()
if(NOT DEFINED OUTPUT_DIR)
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/stability")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
if(NOT DEFINED STRESS_CYCLES)
    set(STRESS_CYCLES 3)
endif()
if(NOT DEFINED STRESS_STAGE_FRAMES)
    set(STRESS_STAGE_FRAMES 6)
endif()

function(run_backend backend switch)
    set(report "${OUTPUT_DIR}/stability.${backend}.json")
    execute_process(
        COMMAND "${STABILITY_EXECUTABLE}" "${switch}"
                --stability-stress
                --stress-cycles "${STRESS_CYCLES}"
                --stress-stage-frames "${STRESS_STAGE_FRAMES}"
                --stress-report "${report}"
                --fixed-delta 0.016666667
                --no-vsync
                --validation
                --no-runtime-monitors
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${backend} stability stress failed (${result})\n${output}\n${error}")
    endif()
    if(NOT EXISTS "${report}")
        message(FATAL_ERROR "${backend} stability stress did not produce ${report}")
    endif()
    file(READ "${report}" report_json)
    string(FIND "${report_json}" "\"passed\":true" passed_at)
    string(FIND "${report_json}" "\"completedCycles\":${STRESS_CYCLES}" cycles_at)
    if(passed_at EQUAL -1 OR cycles_at EQUAL -1)
        message(FATAL_ERROR "${backend} stability report is incomplete or failed: ${report}")
    endif()
    message(STATUS "${backend} stability stress passed: ${report}")
endfunction()

run_backend(opengl --opengl)
if(RUN_VULKAN)
    run_backend(vulkan --vulkan)
endif()
