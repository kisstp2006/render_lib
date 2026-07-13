#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::profiling {

struct GpuProfilerConfig
{
    bool Enabled = true;
    uint32_t RetainedFrames = 240;
};

struct GpuPassTiming
{
    // Slash-separated names form a lightweight hierarchy in reports and UI,
    // for example "Shadows/Directional" and "Shadows/Local lights".
    std::string Name;
    float Milliseconds = 0.0f;
};

struct GpuPipelineStatistics
{
    uint64_t DrawCalls = 0;
    uint64_t ComputeDispatches = 0;
    uint64_t InputAssemblyVertices = 0;
    uint64_t InputAssemblyPrimitives = 0;
    uint64_t VertexShaderInvocations = 0;
    uint64_t FragmentShaderInvocations = 0;
    uint64_t ComputeShaderInvocations = 0;
};

struct GpuMemoryStatistics
{
    // Driver-reported process/heap usage and budget when available. The
    // engine-owned value is exact for Vulkan allocations and may be zero on
    // APIs that do not expose allocation ownership.
    uint64_t UsageBytes = 0;
    uint64_t BudgetBytes = 0;
    uint64_t AvailableBytes = 0;
    uint64_t EngineOwnedBytes = 0;
    uint64_t PeakUsageBytes = 0;
};

struct GpuFrameProfile
{
    uint64_t FrameIndex = 0;
    std::string BackendName;
    std::string AdapterName;
    bool TimingAvailable = false;
    bool PipelineStatisticsAvailable = false;
    bool MemoryBudgetAvailable = false;
    float FrameMilliseconds = 0.0f;
    std::vector<GpuPassTiming> Passes;
    GpuPipelineStatistics Pipeline;
    GpuMemoryStatistics Memory;
};

struct GpuPassSummary
{
    std::string Name;
    float LatestMilliseconds = 0.0f;
    float AverageMilliseconds = 0.0f;
    float MinimumMilliseconds = 0.0f;
    float MaximumMilliseconds = 0.0f;
    uint32_t SampleCount = 0;
};

struct GpuProfileSnapshot
{
    bool Enabled = false;
    bool TimingAvailable = false;
    bool PipelineStatisticsAvailable = false;
    bool MemoryBudgetAvailable = false;
    uint64_t CompletedFrame = 0;
    std::string BackendName;
    std::string AdapterName;
    float FrameMilliseconds = 0.0f;
    GpuPipelineStatistics Pipeline;
    GpuMemoryStatistics Memory;
    std::vector<GpuPassSummary> Passes;
    std::vector<GpuFrameProfile> Frames;
};

// API-neutral, asynchronous GPU profile history. Native backends submit
// completed query results; consumers never wait for the GPU.
class GpuProfiler
{
public:
    static GpuProfiler& Get();

    void Configure(const GpuProfilerConfig& config);
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void Reset();

    void SubmitFrame(GpuFrameProfile frame);
    GpuProfileSnapshot Snapshot() const;
    bool WriteJsonReport(const std::string& path) const;

private:
    GpuProfiler();
    ~GpuProfiler();
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace engine::profiling
