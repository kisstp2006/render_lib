#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::profiling {

struct CpuProfilerConfig
{
    bool Enabled = false;
    uint32_t RetainedFrames = 600;
    uint32_t LogIntervalFrames = 0;
    size_t MaximumEvents = 250'000;
};

struct CpuProfileEvent
{
    uint64_t Id = 0;
    uint64_t ParentId = 0;
    uint64_t FrameIndex = 0;
    uint64_t ThreadId = 0;
    uint32_t Depth = 0;
    double StartMicroseconds = 0.0;
    double DurationMicroseconds = 0.0;
    std::string Name;
    std::string Category;
};

struct CpuProfileZoneSummary
{
    std::string Name;
    std::string Category;
    double AverageMilliseconds = 0.0;
    double MinimumMilliseconds = 0.0;
    double MaximumMilliseconds = 0.0;
    double TotalMilliseconds = 0.0;
    uint64_t CallCount = 0;
};

struct CpuProfileSnapshot
{
    std::vector<CpuProfileEvent> Events;
    std::vector<CpuProfileZoneSummary> Summaries;
    std::unordered_map<uint64_t, std::string> ThreadNames;
    uint64_t FirstFrame = 0;
    uint64_t LastFrame = 0;
    uint64_t DroppedEvents = 0;
};

class CpuProfiler
{
public:
    static CpuProfiler& Get();

    void Configure(const CpuProfilerConfig& config);
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void Reset();

    void BeginFrame();
    void EndFrame();
    uint64_t CurrentFrame() const { return m_currentFrame.load(std::memory_order_relaxed); }

    void SetThreadName(std::string_view name);
    CpuProfileSnapshot Snapshot() const;
    bool WriteChromeTrace(const std::string& path) const;

private:
    friend class CpuProfileScope;

    struct ZoneToken
    {
        uint64_t Id = 0;
        uint64_t ParentId = 0;
        uint64_t FrameIndex = 0;
        uint64_t ThreadId = 0;
        uint32_t Depth = 0;
        double StartMicroseconds = 0.0;
        bool Active = false;
    };

    CpuProfiler();
    ZoneToken BeginZone();
    void EndZone(ZoneToken token, std::string name, std::string category);
    void LogFrameStatistics(uint64_t completedFrame);
    static uint64_t ThreadId();
    double NowMicroseconds() const;

    std::atomic<bool> m_enabled{false};
    std::atomic<uint64_t> m_currentFrame{0};
    std::atomic<uint64_t> m_nextEventId{1};
    CpuProfilerConfig m_config;
    mutable std::mutex m_mutex;
    std::vector<CpuProfileEvent> m_events;
    std::unordered_map<uint64_t, std::string> m_threadNames;
    uint64_t m_completedFrames = 0;
    uint64_t m_droppedEvents = 0;
    std::chrono::steady_clock::time_point m_origin;
};

class CpuProfileScope
{
public:
    explicit CpuProfileScope(std::string_view name,
                             std::string_view category = "CPU");
    ~CpuProfileScope();

    CpuProfileScope(const CpuProfileScope&) = delete;
    CpuProfileScope& operator=(const CpuProfileScope&) = delete;
    void End();

private:
    CpuProfiler* m_profiler = nullptr;
    CpuProfiler::ZoneToken m_token;
    std::string m_name;
    std::string m_category;
};

} // namespace engine::profiling

#define ENGINE_CPU_PROFILE_JOIN_IMPL(a, b) a##b
#define ENGINE_CPU_PROFILE_JOIN(a, b) ENGINE_CPU_PROFILE_JOIN_IMPL(a, b)
#define ENGINE_CPU_PROFILE_SCOPE(name) \
    ::engine::profiling::CpuProfileScope ENGINE_CPU_PROFILE_JOIN(cpuProfileScope_, __LINE__)(name)
#define ENGINE_CPU_PROFILE_SCOPE_CATEGORY(name, category) \
    ::engine::profiling::CpuProfileScope ENGINE_CPU_PROFILE_JOIN(cpuProfileScope_, __LINE__)(name, category)
#define ENGINE_CPU_PROFILE_FUNCTION() ENGINE_CPU_PROFILE_SCOPE(__func__)
