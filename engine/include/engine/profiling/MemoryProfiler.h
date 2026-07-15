#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::profiling
{

inline constexpr size_t kMemorySizeBucketCount = 8;

struct MemoryProfilerConfig
{
    bool Enabled = false;
    bool LeakReportOnShutdown = false;
    uint32_t RetainedFrames = 240;
};

struct MemoryTagStats
{
    std::string Name;
    uint64_t CurrentBytes = 0;
    uint64_t PeakBytes = 0;
    uint64_t LiveAllocations = 0;
    uint64_t TotalAllocations = 0;
    uint64_t TotalFrees = 0;
    uint64_t TotalAllocatedBytes = 0;
};

struct MemorySizeBucketStats
{
    std::string Label;
    uint64_t CurrentBytes = 0;
    uint64_t LiveAllocations = 0;
    uint64_t TotalAllocations = 0;
};

struct MemoryFrameStats
{
    uint64_t FrameIndex = 0;
    uint64_t CurrentBytes = 0;
    uint64_t PeakBytes = 0;
    uint64_t LiveAllocations = 0;
    uint64_t AllocatedBytes = 0;
    uint64_t FreedBytes = 0;
    uint64_t AllocationCount = 0;
    uint64_t FreeCount = 0;
};

struct MemoryLeakInfo
{
    uint64_t AllocationId = 0;
    uint64_t Address = 0;
    uint64_t Size = 0;
    uint64_t ThreadId = 0;
    std::string Tag;
};

struct MemoryProfileSnapshot
{
    bool Enabled = false;
    bool ProcessMemoryAvailable = false;
    uint64_t ProcessResidentBytes = 0;
    uint64_t ProcessPeakResidentBytes = 0;
    uint64_t ProcessPrivateBytes = 0;
    uint64_t CurrentBytes = 0;
    uint64_t PeakBytes = 0;
    uint64_t LiveAllocations = 0;
    uint64_t TotalAllocations = 0;
    uint64_t TotalFrees = 0;
    uint64_t TotalAllocatedBytes = 0;
    uint64_t DroppedAllocations = 0;
    uint64_t TrackingCapacity = 0;
    std::vector<MemoryTagStats> Tags;
    std::array<MemorySizeBucketStats, kMemorySizeBucketCount> SizeBuckets;
    std::vector<MemoryFrameStats> Frames;
    std::vector<MemoryLeakInfo> Leaks;
};

class MemoryProfiler
{
  public:
    static MemoryProfiler& Get();

    void Configure(const MemoryProfilerConfig& config);
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetLeakReportOnShutdown(bool enabled);
    void Reset();

    void BeginFrame();
    void EndFrame();

    MemoryProfileSnapshot Snapshot(bool includeLeaks = false) const;
    bool WriteJsonReport(const std::string& path, bool includeLeaks = true) const;
    uint64_t LiveAllocationCount() const;

    // Allocations returned here must be released through Free(). Alignment is
    // normalized to a valid platform alignment. These functions back global
    // C++ new/delete and the runtime plugin host allocator.
    void* Allocate(size_t size, size_t alignment, const char* tag = nullptr) noexcept;
    void Free(void* memory) noexcept;

    static const char* CurrentTag() noexcept;

  private:
    friend class MemoryTagScope;
    struct Impl;

    MemoryProfiler();
    static void SetCurrentTag(const char* tag) noexcept;
    void PrintShutdownLeakReport() const noexcept;

    Impl* m_impl = nullptr;
};

class MemoryTagScope
{
  public:
    explicit MemoryTagScope(const char* tag) noexcept;
    ~MemoryTagScope();

    MemoryTagScope(const MemoryTagScope&) = delete;
    MemoryTagScope& operator=(const MemoryTagScope&) = delete;

  private:
    const char* m_previous = nullptr;
};

} // namespace engine::profiling

#define ENGINE_MEMORY_PROFILE_JOIN_IMPL(a, b) a##b
#define ENGINE_MEMORY_PROFILE_JOIN(a, b) ENGINE_MEMORY_PROFILE_JOIN_IMPL(a, b)
#define ENGINE_MEMORY_TAG_SCOPE(tag)                                                                \
    ::engine::profiling::MemoryTagScope ENGINE_MEMORY_PROFILE_JOIN(memoryTagScope_, __LINE__)(tag)
