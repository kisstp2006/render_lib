#pragma once

#include <cstdint>

struct TestRuntimePluginService
{
    uint32_t LoadCount = 0;
    uint32_t StartCount = 0;
    uint32_t ActivateCount = 0;
    uint32_t DeactivateCount = 0;
    uint32_t UpdateCount = 0;
    uint32_t ApplicationEventCount = 0;
    float AccumulatedSeconds = 0.0f;
};

inline constexpr const char* kTestRuntimeServiceName = "tests.runtime-counter";
inline constexpr const char* kTestRuntimeComponentName = "tests.CounterComponent";
inline constexpr const char* kTestRuntimePluginName = "TestRuntimePlugin";
inline constexpr const char* kTestDependentPluginName = "TestDependentPlugin";
inline constexpr const char* kTestDependentServiceName = "tests.dependent";
