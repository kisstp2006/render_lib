include_guard(GLOBAL)

function(engine_apply_target_compile_options target)
    # Explicit per-target requirement rather than relying on an ambient
    # CMAKE_CXX_STANDARD directory default, which would also force this
    # standard onto FetchContent'd third-party targets and (if this project
    # is embedded via add_subdirectory) would be the kind of directory-wide
    # setting a parent project should never have to worry about.
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

function(engine_apply_target_defaults target folder)
    set_target_properties(${target} PROPERTIES
        FOLDER "Engine/${folder}"
        POSITION_INDEPENDENT_CODE ON)
    engine_apply_target_compile_options(${target})
endfunction()

function(engine_add_module target)
    set(options)
    set(one_value_args FOLDER ALIAS)
    set(multi_value_args SOURCES PUBLIC_DEPS PRIVATE_DEPS PRIVATE_INCLUDE_DIRS
                         PUBLIC_DEFINITIONS PRIVATE_DEFINITIONS)
    cmake_parse_arguments(MODULE "${options}" "${one_value_args}"
                          "${multi_value_args}" ${ARGN})
    if(NOT MODULE_SOURCES)
        message(FATAL_ERROR "engine_add_module(${target}) requires SOURCES")
    endif()

    add_library(${target} STATIC ${MODULE_SOURCES})
    target_link_libraries(${target}
        PUBLIC engine_build_config ${MODULE_PUBLIC_DEPS}
        PRIVATE ${MODULE_PRIVATE_DEPS})
    if(MODULE_PRIVATE_INCLUDE_DIRS)
        target_include_directories(${target} PRIVATE ${MODULE_PRIVATE_INCLUDE_DIRS})
    endif()
    if(MODULE_PUBLIC_DEFINITIONS)
        target_compile_definitions(${target} PUBLIC ${MODULE_PUBLIC_DEFINITIONS})
    endif()
    if(MODULE_PRIVATE_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${MODULE_PRIVATE_DEFINITIONS})
    endif()

    engine_apply_target_defaults(${target} "${MODULE_FOLDER}")
    source_group(TREE ${ENGINE_ROOT} PREFIX "Engine" FILES ${MODULE_SOURCES})
    if(MODULE_ALIAS)
        add_library(engine::${MODULE_ALIAS} ALIAS ${target})
    endif()
endfunction()
