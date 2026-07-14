engine_add_module(engine_foundation
    FOLDER "Foundation"
    ALIAS foundation
    SOURCES
        ${ENGINE_ROOT}/include/engine/concurrency/TaskSystem.h
        ${ENGINE_ROOT}/include/engine/core/Log.h
        ${ENGINE_ROOT}/include/engine/profiling/CpuProfiler.h
        ${ENGINE_ROOT}/include/engine/profiling/GpuProfiler.h
        ${ENGINE_ROOT}/include/engine/profiling/MemoryProfiler.h
        ${ENGINE_ROOT}/src/concurrency/TaskSystem.cpp
        ${ENGINE_ROOT}/src/core/Log.cpp
        ${ENGINE_ROOT}/src/profiling/CpuProfiler.cpp
        ${ENGINE_ROOT}/src/profiling/GpuProfiler.cpp
        ${ENGINE_ROOT}/src/profiling/MemoryProfiler.cpp)
