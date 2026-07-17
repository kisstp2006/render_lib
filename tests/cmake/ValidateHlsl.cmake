if(NOT DEFINED HLSL_ROOT OR NOT IS_DIRECTORY "${HLSL_ROOT}")
    message(FATAL_ERROR "HLSL_ROOT must point at the engine HLSL shader directory")
endif()
if(NOT DEFINED DXC_EXECUTABLE OR NOT EXISTS "${DXC_EXECUTABLE}")
    message(FATAL_ERROR "DXC is required for DirectX 12 HLSL validation")
endif()
if(NOT DEFINED OUTPUT_DIR)
    set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/hlsl-validation")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

file(GLOB_RECURSE HLSL_ENTRY_POINTS "${HLSL_ROOT}/*.hlsl")
list(SORT HLSL_ENTRY_POINTS)
set(DXC_COUNT 0)
set(FXC_COUNT 0)

foreach(SHADER IN LISTS HLSL_ENTRY_POINTS)
    if(SHADER MATCHES "\\.vert\\.hlsl$")
        set(DXC_PROFILE vs_6_0)
        set(FXC_PROFILE vs_5_0)
    elseif(SHADER MATCHES "\\.frag\\.hlsl$")
        set(DXC_PROFILE ps_6_0)
        set(FXC_PROFILE ps_5_0)
    elseif(SHADER MATCHES "\\.comp\\.hlsl$")
        set(DXC_PROFILE cs_6_0)
        set(FXC_PROFILE cs_5_0)
    else()
        continue()
    endif()

    execute_process(
        COMMAND "${DXC_EXECUTABLE}" -T ${DXC_PROFILE} -E main
                -Fo "${OUTPUT_DIR}/validation.dxil" "${SHADER}"
        RESULT_VARIABLE DXC_RESULT
        OUTPUT_VARIABLE DXC_OUTPUT
        ERROR_VARIABLE DXC_ERROR)
    if(NOT DXC_RESULT EQUAL 0)
        message(FATAL_ERROR
            "DirectX 12 HLSL validation failed for ${SHADER}:\n${DXC_OUTPUT}${DXC_ERROR}")
    endif()
    math(EXPR DXC_COUNT "${DXC_COUNT} + 1")

    if(DEFINED FXC_EXECUTABLE AND EXISTS "${FXC_EXECUTABLE}" AND
       SHADER MATCHES "[/\\\\]opengl[/\\\\]")
        execute_process(
            COMMAND "${FXC_EXECUTABLE}" /nologo /T ${FXC_PROFILE} /E main
                    /Fo "${OUTPUT_DIR}/validation.dxbc" "${SHADER}"
            RESULT_VARIABLE FXC_RESULT
            OUTPUT_VARIABLE FXC_OUTPUT
            ERROR_VARIABLE FXC_ERROR)
        if(NOT FXC_RESULT EQUAL 0)
            message(FATAL_ERROR
                "DirectX 11 HLSL validation failed for ${SHADER}:\n${FXC_OUTPUT}${FXC_ERROR}")
        endif()
        math(EXPR FXC_COUNT "${FXC_COUNT} + 1")
    endif()
endforeach()

message(STATUS
    "HLSL validation passed: ${DXC_COUNT} DirectX 12 shader(s), ${FXC_COUNT} DirectX 11 shader(s)")
