#include "engine/profiling/MemoryProfiler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <malloc.h>
#include <Psapi.h>
#endif

namespace engine::profiling
{
namespace
{

constexpr size_t kMaximumTrackedAllocations = 262'144;
constexpr size_t kMaximumTags = 256;
constexpr size_t kMaximumTagLength = 63;
constexpr size_t kMaximumRetainedFrames = 600;
constexpr std::array<uint64_t, kMemorySizeBucketCount - 1> kBucketMaximums = {
    64, 256, 1024, 4096, 16 * 1024, 64 * 1024, 256 * 1024};
constexpr std::array<const char*, kMemorySizeBucketCount> kBucketLabels = {
    "0-64 B", "65-256 B", "257 B-1 KB", "1-4 KB", "4-16 KB", "16-64 KB",
    "64-256 KB", ">256 KB"};

thread_local const char* g_currentMemoryTag = "Unspecified";
thread_local uint32_t g_trackingPauseDepth = 0;

class TrackingPause
{
  public:
    TrackingPause() { ++g_trackingPauseDepth; }
    ~TrackingPause() { --g_trackingPauseDepth; }
};

size_t NormalizeAlignment(size_t alignment)
{
    alignment = std::max(alignment, sizeof(void*));
    if ((alignment & (alignment - 1)) == 0)
        return alignment;
    size_t rounded = 1;
    while (rounded < alignment && rounded <= std::numeric_limits<size_t>::max() / 2)
        rounded <<= 1;
    return rounded;
}

void* PlatformAllocate(size_t size, size_t alignment) noexcept
{
    size = std::max<size_t>(size, 1);
    alignment = NormalizeAlignment(alignment);
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* result = nullptr;
    return posix_memalign(&result, alignment, size) == 0 ? result : nullptr;
#endif
}

void PlatformFree(void* memory) noexcept
{
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

uint64_t CurrentThreadId()
{
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

size_t BucketIndex(uint64_t size)
{
    for (size_t index = 0; index < kBucketMaximums.size(); ++index)
        if (size <= kBucketMaximums[index])
            return index;
    return kMemorySizeBucketCount - 1;
}

size_t PointerHash(const void* pointer)
{
    uint64_t value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer) >> 4u);
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    return static_cast<size_t>(value % kMaximumTrackedAllocations);
}

std::string EscapeJson(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (char character : value)
    {
        switch (character)
        {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += character; break;
        }
    }
    return result;
}

void PopulateProcessMemory(MemoryProfileSnapshot& snapshot) noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
    {
        snapshot.ProcessMemoryAvailable = true;
        snapshot.ProcessResidentBytes = static_cast<uint64_t>(counters.WorkingSetSize);
        snapshot.ProcessPeakResidentBytes = static_cast<uint64_t>(counters.PeakWorkingSetSize);
        snapshot.ProcessPrivateBytes = static_cast<uint64_t>(counters.PrivateUsage);
    }
#else
    (void)snapshot;
#endif
}

} // namespace

struct MemoryProfiler::Impl
{
    enum class RecordState : uint8_t
    {
        Empty,
        Occupied,
        Tombstone
    };

    struct AllocationRecord
    {
        void* Pointer = nullptr;
        uint64_t Size = 0;
        uint64_t AllocationId = 0;
        uint64_t ThreadId = 0;
        uint16_t TagIndex = 0;
        uint8_t Bucket = 0;
        RecordState State = RecordState::Empty;
    };

    struct TagRecord
    {
        char Name[kMaximumTagLength + 1]{};
        uint64_t CurrentBytes = 0;
        uint64_t PeakBytes = 0;
        uint64_t LiveAllocations = 0;
        uint64_t TotalAllocations = 0;
        uint64_t TotalFrees = 0;
        uint64_t TotalAllocatedBytes = 0;
    };

    struct BucketRecord
    {
        uint64_t CurrentBytes = 0;
        uint64_t LiveAllocations = 0;
        uint64_t TotalAllocations = 0;
    };

    std::atomic<bool> Enabled{false};
    std::atomic<bool> LeakReportOnShutdown{false};
    std::atomic<uint64_t> LiveAllocationsAtomic{0};
    mutable std::mutex Mutex;
    AllocationRecord* Allocations = nullptr;
    TagRecord Tags[kMaximumTags]{};
    BucketRecord Buckets[kMemorySizeBucketCount]{};
    MemoryFrameStats Frames[kMaximumRetainedFrames]{};
    uint32_t TagCount = 1;
    uint32_t RetainedFrames = 240;
    uint32_t FrameCount = 0;
    uint32_t FrameWriteIndex = 0;
    uint64_t CurrentFrame = 0;
    uint64_t CurrentBytes = 0;
    uint64_t PeakBytes = 0;
    uint64_t TotalAllocations = 0;
    uint64_t TotalFrees = 0;
    uint64_t TotalAllocatedBytes = 0;
    uint64_t DroppedAllocations = 0;
    uint64_t NextAllocationId = 1;
    uint64_t FrameAllocatedBytes = 0;
    uint64_t FrameFreedBytes = 0;
    uint64_t FrameAllocations = 0;
    uint64_t FrameFrees = 0;
    uint64_t FramePeakBytes = 0;

    Impl()
    {
        std::memcpy(Tags[0].Name, "Unspecified", sizeof("Unspecified"));
    }

    bool EnsureAllocationStorage()
    {
        if (Allocations)
            return true;
        Allocations = static_cast<AllocationRecord*>(
            std::calloc(kMaximumTrackedAllocations, sizeof(AllocationRecord)));
        return Allocations != nullptr;
    }

    uint16_t FindOrCreateTag(const char* name)
    {
        if (!name || name[0] == '\0')
            return 0;
        for (uint32_t index = 0; index < TagCount; ++index)
            if (std::strncmp(Tags[index].Name, name, kMaximumTagLength) == 0)
                return static_cast<uint16_t>(index);
        if (TagCount >= kMaximumTags)
            return 0;
        const uint32_t index = TagCount++;
        const size_t length = std::min(std::strlen(name), kMaximumTagLength);
        std::memcpy(Tags[index].Name, name, length);
        Tags[index].Name[length] = '\0';
        return static_cast<uint16_t>(index);
    }

    AllocationRecord* Insert(void* pointer)
    {
        const size_t first = PointerHash(pointer);
        for (size_t probe = 0; probe < kMaximumTrackedAllocations; ++probe)
        {
            AllocationRecord& record = Allocations[(first + probe) % kMaximumTrackedAllocations];
            // Allocate() can never receive an address that is simultaneously
            // live, so there is no duplicate key to find later in the probe
            // chain. Reuse the first tombstone immediately. Scanning onward to
            // an Empty slot made the fixed table progressively slower as a
            // runtime session accumulated allocation churn, eventually doing
            // hundreds of thousands of probes per new/delete operation.
            if (record.State != RecordState::Occupied)
                return &record;
        }
        return nullptr;
    }

    AllocationRecord* Find(void* pointer)
    {
        const size_t first = PointerHash(pointer);
        for (size_t probe = 0; probe < kMaximumTrackedAllocations; ++probe)
        {
            AllocationRecord& record = Allocations[(first + probe) % kMaximumTrackedAllocations];
            if (record.State == RecordState::Empty)
                return nullptr;
            if (record.State == RecordState::Occupied && record.Pointer == pointer)
                return &record;
        }
        return nullptr;
    }
};

MemoryProfiler& MemoryProfiler::Get()
{
    static MemoryProfiler* profiler = []
    {
        void* storage = std::malloc(sizeof(MemoryProfiler));
        if (!storage)
            std::abort();
        auto* result = ::new (storage) MemoryProfiler();
        std::atexit([] { MemoryProfiler::Get().PrintShutdownLeakReport(); });
        return result;
    }();
    return *profiler;
}

MemoryProfiler::MemoryProfiler()
{
    void* storage = std::malloc(sizeof(Impl));
    if (!storage)
        std::abort();
    m_impl = ::new (storage) Impl();
}

void MemoryProfiler::Configure(const MemoryProfilerConfig& config)
{
    {
        std::scoped_lock lock(m_impl->Mutex);
        m_impl->RetainedFrames = std::clamp(config.RetainedFrames, 1u,
                                            static_cast<uint32_t>(kMaximumRetainedFrames));
    }
    m_impl->LeakReportOnShutdown.store(config.LeakReportOnShutdown,
                                       std::memory_order_relaxed);
    SetEnabled(config.Enabled);
}

void MemoryProfiler::SetEnabled(bool enabled)
{
    if (enabled)
    {
        std::scoped_lock lock(m_impl->Mutex);
        if (!m_impl->EnsureAllocationStorage())
        {
            m_impl->Enabled.store(false, std::memory_order_release);
            return;
        }
    }
    m_impl->Enabled.store(enabled, std::memory_order_release);
}

bool MemoryProfiler::IsEnabled() const
{
    return m_impl->Enabled.load(std::memory_order_relaxed);
}

void MemoryProfiler::SetLeakReportOnShutdown(bool enabled)
{
    m_impl->LeakReportOnShutdown.store(enabled, std::memory_order_relaxed);
}

void MemoryProfiler::Reset()
{
    TrackingPause pause;
    std::scoped_lock lock(m_impl->Mutex);
    if (m_impl->Allocations)
        std::memset(m_impl->Allocations, 0,
                    kMaximumTrackedAllocations * sizeof(Impl::AllocationRecord));
    for (auto& tag : m_impl->Tags)
        tag = {};
    for (auto& bucket : m_impl->Buckets)
        bucket = {};
    for (auto& frame : m_impl->Frames)
        frame = {};
    m_impl->TagCount = 1;
    std::memcpy(m_impl->Tags[0].Name, "Unspecified", sizeof("Unspecified"));
    m_impl->FrameCount = 0;
    m_impl->FrameWriteIndex = 0;
    m_impl->CurrentFrame = 0;
    m_impl->CurrentBytes = 0;
    m_impl->PeakBytes = 0;
    m_impl->TotalAllocations = 0;
    m_impl->TotalFrees = 0;
    m_impl->TotalAllocatedBytes = 0;
    m_impl->DroppedAllocations = 0;
    m_impl->NextAllocationId = 1;
    m_impl->FrameAllocatedBytes = 0;
    m_impl->FrameFreedBytes = 0;
    m_impl->FrameAllocations = 0;
    m_impl->FrameFrees = 0;
    m_impl->FramePeakBytes = 0;
    m_impl->LiveAllocationsAtomic.store(0, std::memory_order_relaxed);
}

void MemoryProfiler::BeginFrame()
{
    if (!IsEnabled())
        return;
    std::scoped_lock lock(m_impl->Mutex);
    m_impl->FrameAllocatedBytes = 0;
    m_impl->FrameFreedBytes = 0;
    m_impl->FrameAllocations = 0;
    m_impl->FrameFrees = 0;
    m_impl->FramePeakBytes = m_impl->CurrentBytes;
}

void MemoryProfiler::EndFrame()
{
    if (!IsEnabled())
        return;
    std::scoped_lock lock(m_impl->Mutex);
    MemoryFrameStats& frame = m_impl->Frames[m_impl->FrameWriteIndex];
    frame.FrameIndex = m_impl->CurrentFrame++;
    frame.CurrentBytes = m_impl->CurrentBytes;
    frame.PeakBytes = m_impl->FramePeakBytes;
    frame.LiveAllocations = m_impl->LiveAllocationsAtomic.load(std::memory_order_relaxed);
    frame.AllocatedBytes = m_impl->FrameAllocatedBytes;
    frame.FreedBytes = m_impl->FrameFreedBytes;
    frame.AllocationCount = m_impl->FrameAllocations;
    frame.FreeCount = m_impl->FrameFrees;
    m_impl->FrameWriteIndex = (m_impl->FrameWriteIndex + 1) % m_impl->RetainedFrames;
    m_impl->FrameCount = std::min(m_impl->FrameCount + 1, m_impl->RetainedFrames);
}

void* MemoryProfiler::Allocate(size_t size, size_t alignment, const char* tag) noexcept
{
    void* memory = PlatformAllocate(size, alignment);
    if (!memory || g_trackingPauseDepth != 0 || !IsEnabled())
        return memory;

    std::scoped_lock lock(m_impl->Mutex);
    Impl::AllocationRecord* record = m_impl->Insert(memory);
    if (!record)
    {
        ++m_impl->DroppedAllocations;
        return memory;
    }

    const uint64_t trackedSize = std::max<size_t>(size, 1);
    const uint16_t tagIndex = m_impl->FindOrCreateTag(tag ? tag : CurrentTag());
    const size_t bucketIndex = BucketIndex(trackedSize);
    record->Pointer = memory;
    record->Size = trackedSize;
    record->AllocationId = m_impl->NextAllocationId++;
    record->ThreadId = CurrentThreadId();
    record->TagIndex = tagIndex;
    record->Bucket = static_cast<uint8_t>(bucketIndex);
    record->State = Impl::RecordState::Occupied;

    ++m_impl->TotalAllocations;
    m_impl->TotalAllocatedBytes += trackedSize;
    m_impl->CurrentBytes += trackedSize;
    m_impl->PeakBytes = std::max(m_impl->PeakBytes, m_impl->CurrentBytes);
    m_impl->FramePeakBytes = std::max(m_impl->FramePeakBytes, m_impl->CurrentBytes);
    ++m_impl->FrameAllocations;
    m_impl->FrameAllocatedBytes += trackedSize;
    const uint64_t live = m_impl->LiveAllocationsAtomic.fetch_add(1, std::memory_order_relaxed) + 1;
    (void)live;

    Impl::TagRecord& tagRecord = m_impl->Tags[tagIndex];
    tagRecord.CurrentBytes += trackedSize;
    tagRecord.PeakBytes = std::max(tagRecord.PeakBytes, tagRecord.CurrentBytes);
    ++tagRecord.LiveAllocations;
    ++tagRecord.TotalAllocations;
    tagRecord.TotalAllocatedBytes += trackedSize;

    Impl::BucketRecord& bucket = m_impl->Buckets[bucketIndex];
    bucket.CurrentBytes += trackedSize;
    ++bucket.LiveAllocations;
    ++bucket.TotalAllocations;
    return memory;
}

void MemoryProfiler::Free(void* memory) noexcept
{
    if (!memory)
        return;

    if (g_trackingPauseDepth == 0 &&
        (IsEnabled() || m_impl->LiveAllocationsAtomic.load(std::memory_order_relaxed) != 0))
    {
        std::scoped_lock lock(m_impl->Mutex);
        if (Impl::AllocationRecord* record = m_impl->Find(memory))
        {
            const uint64_t size = record->Size;
            Impl::TagRecord& tag = m_impl->Tags[record->TagIndex];
            Impl::BucketRecord& bucket = m_impl->Buckets[record->Bucket];
            m_impl->CurrentBytes -= std::min(m_impl->CurrentBytes, size);
            ++m_impl->TotalFrees;
            ++m_impl->FrameFrees;
            m_impl->FrameFreedBytes += size;
            tag.CurrentBytes -= std::min(tag.CurrentBytes, size);
            tag.LiveAllocations -= std::min<uint64_t>(tag.LiveAllocations, 1);
            ++tag.TotalFrees;
            bucket.CurrentBytes -= std::min(bucket.CurrentBytes, size);
            bucket.LiveAllocations -= std::min<uint64_t>(bucket.LiveAllocations, 1);
            m_impl->LiveAllocationsAtomic.fetch_sub(1, std::memory_order_relaxed);
            record->Pointer = nullptr;
            record->State = Impl::RecordState::Tombstone;
        }
    }
    PlatformFree(memory);
}

MemoryProfileSnapshot MemoryProfiler::Snapshot(bool includeLeaks) const
{
    TrackingPause pause;
    MemoryProfileSnapshot result;
    // The always-on runtime card uses this OS counter even when expensive
    // allocation-level tracking is disabled. This keeps useful CPU-memory
    // visibility at negligible overhead.
    PopulateProcessMemory(result);
    std::scoped_lock lock(m_impl->Mutex);
    result.Enabled = IsEnabled();
    result.CurrentBytes = m_impl->CurrentBytes;
    result.PeakBytes = m_impl->PeakBytes;
    result.LiveAllocations = m_impl->LiveAllocationsAtomic.load(std::memory_order_relaxed);
    result.TotalAllocations = m_impl->TotalAllocations;
    result.TotalFrees = m_impl->TotalFrees;
    result.TotalAllocatedBytes = m_impl->TotalAllocatedBytes;
    result.DroppedAllocations = m_impl->DroppedAllocations;
    result.TrackingCapacity = kMaximumTrackedAllocations;

    result.Tags.reserve(m_impl->TagCount);
    for (uint32_t index = 0; index < m_impl->TagCount; ++index)
    {
        const Impl::TagRecord& tag = m_impl->Tags[index];
        result.Tags.push_back({tag.Name, tag.CurrentBytes, tag.PeakBytes, tag.LiveAllocations,
                               tag.TotalAllocations, tag.TotalFrees, tag.TotalAllocatedBytes});
    }
    std::sort(result.Tags.begin(), result.Tags.end(),
              [](const MemoryTagStats& left, const MemoryTagStats& right)
              { return left.CurrentBytes > right.CurrentBytes; });

    for (size_t index = 0; index < kMemorySizeBucketCount; ++index)
    {
        const Impl::BucketRecord& bucket = m_impl->Buckets[index];
        result.SizeBuckets[index] = {kBucketLabels[index], bucket.CurrentBytes,
                                     bucket.LiveAllocations, bucket.TotalAllocations};
    }

    result.Frames.reserve(m_impl->FrameCount);
    const uint32_t first = (m_impl->FrameWriteIndex + m_impl->RetainedFrames -
                            m_impl->FrameCount) % m_impl->RetainedFrames;
    for (uint32_t offset = 0; offset < m_impl->FrameCount; ++offset)
        result.Frames.push_back(m_impl->Frames[(first + offset) % m_impl->RetainedFrames]);

    if (includeLeaks && m_impl->Allocations)
    {
        result.Leaks.reserve(static_cast<size_t>(result.LiveAllocations));
        for (size_t allocationIndex = 0; allocationIndex < kMaximumTrackedAllocations;
             ++allocationIndex)
        {
            const Impl::AllocationRecord& allocation = m_impl->Allocations[allocationIndex];
            if (allocation.State != Impl::RecordState::Occupied)
                continue;
            result.Leaks.push_back({allocation.AllocationId,
                                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(allocation.Pointer)),
                                    allocation.Size, allocation.ThreadId,
                                    m_impl->Tags[allocation.TagIndex].Name});
        }
        std::sort(result.Leaks.begin(), result.Leaks.end(),
                  [](const MemoryLeakInfo& left, const MemoryLeakInfo& right)
                  { return left.Size > right.Size; });
    }
    return result;
}

bool MemoryProfiler::WriteJsonReport(const std::string& path, bool includeLeaks) const
{
    TrackingPause pause;
    const MemoryProfileSnapshot snapshot = Snapshot(includeLeaks);
    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return false;
    output << "{\n  \"enabled\": " << (snapshot.Enabled ? "true" : "false")
           << ",\n  \"currentBytes\": " << snapshot.CurrentBytes
           << ",\n  \"peakBytes\": " << snapshot.PeakBytes
           << ",\n  \"liveAllocations\": " << snapshot.LiveAllocations
           << ",\n  \"totalAllocations\": " << snapshot.TotalAllocations
           << ",\n  \"totalFrees\": " << snapshot.TotalFrees
           << ",\n  \"totalAllocatedBytes\": " << snapshot.TotalAllocatedBytes
           << ",\n  \"droppedAllocations\": " << snapshot.DroppedAllocations
           << ",\n  \"trackingCapacity\": " << snapshot.TrackingCapacity
           << ",\n  \"tags\": [\n";
    for (size_t index = 0; index < snapshot.Tags.size(); ++index)
    {
        const MemoryTagStats& tag = snapshot.Tags[index];
        output << "    {\"name\": \"" << EscapeJson(tag.Name) << "\", \"currentBytes\": "
               << tag.CurrentBytes << ", \"peakBytes\": " << tag.PeakBytes
               << ", \"liveAllocations\": " << tag.LiveAllocations
               << ", \"totalAllocations\": " << tag.TotalAllocations
               << ", \"totalFrees\": " << tag.TotalFrees
               << ", \"totalAllocatedBytes\": " << tag.TotalAllocatedBytes << "}"
               << (index + 1 == snapshot.Tags.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"sizeBuckets\": [\n";
    for (size_t index = 0; index < snapshot.SizeBuckets.size(); ++index)
    {
        const MemorySizeBucketStats& bucket = snapshot.SizeBuckets[index];
        output << "    {\"label\": \"" << EscapeJson(bucket.Label)
               << "\", \"currentBytes\": " << bucket.CurrentBytes
               << ", \"liveAllocations\": " << bucket.LiveAllocations
               << ", \"totalAllocations\": " << bucket.TotalAllocations << "}"
               << (index + 1 == snapshot.SizeBuckets.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"frames\": [\n";
    for (size_t index = 0; index < snapshot.Frames.size(); ++index)
    {
        const MemoryFrameStats& frame = snapshot.Frames[index];
        output << "    {\"frame\": " << frame.FrameIndex
               << ", \"currentBytes\": " << frame.CurrentBytes
               << ", \"peakBytes\": " << frame.PeakBytes
               << ", \"liveAllocations\": " << frame.LiveAllocations
               << ", \"allocatedBytes\": " << frame.AllocatedBytes
               << ", \"freedBytes\": " << frame.FreedBytes
               << ", \"allocationCount\": " << frame.AllocationCount
               << ", \"freeCount\": " << frame.FreeCount << "}"
               << (index + 1 == snapshot.Frames.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"leaks\": [\n";
    for (size_t index = 0; index < snapshot.Leaks.size(); ++index)
    {
        const MemoryLeakInfo& leak = snapshot.Leaks[index];
        output << "    {\"id\": " << leak.AllocationId << ", \"address\": \"0x"
               << std::hex << leak.Address << std::dec << "\", \"size\": " << leak.Size
               << ", \"thread\": " << leak.ThreadId << ", \"tag\": \""
               << EscapeJson(leak.Tag) << "\"}"
               << (index + 1 == snapshot.Leaks.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return output.good();
}

uint64_t MemoryProfiler::LiveAllocationCount() const
{
    return m_impl->LiveAllocationsAtomic.load(std::memory_order_relaxed);
}

const char* MemoryProfiler::CurrentTag() noexcept
{
    return g_currentMemoryTag;
}

void MemoryProfiler::SetCurrentTag(const char* tag) noexcept
{
    g_currentMemoryTag = tag && tag[0] != '\0' ? tag : "Unspecified";
}

void MemoryProfiler::PrintShutdownLeakReport() const noexcept
{
    if (!m_impl->LeakReportOnShutdown.load(std::memory_order_relaxed))
        return;
    TrackingPause pause;
    std::scoped_lock lock(m_impl->Mutex);
    const uint64_t live = m_impl->LiveAllocationsAtomic.load(std::memory_order_relaxed);
    if (live == 0)
    {
        std::fprintf(stderr, "[memory] shutdown leak report: no tracked leaks\n");
        return;
    }
    std::fprintf(stderr, "[memory] shutdown leak report: %llu allocation(s), %llu byte(s) still live\n",
                 static_cast<unsigned long long>(live),
                 static_cast<unsigned long long>(m_impl->CurrentBytes));
    size_t printed = 0;
    if (!m_impl->Allocations)
        return;
    for (size_t allocationIndex = 0; allocationIndex < kMaximumTrackedAllocations;
         ++allocationIndex)
    {
        const Impl::AllocationRecord& allocation = m_impl->Allocations[allocationIndex];
        if (allocation.State != Impl::RecordState::Occupied)
            continue;
        std::fprintf(stderr, "[memory] leak #%llu: %llu bytes, tag=%s, address=%p\n",
                     static_cast<unsigned long long>(allocation.AllocationId),
                     static_cast<unsigned long long>(allocation.Size),
                     m_impl->Tags[allocation.TagIndex].Name, allocation.Pointer);
        if (++printed == 32)
        {
            if (live > printed)
                std::fprintf(stderr, "[memory] ... %llu additional leak(s) omitted\n",
                             static_cast<unsigned long long>(live - printed));
            break;
        }
    }
}

MemoryTagScope::MemoryTagScope(const char* tag) noexcept
    : m_previous(MemoryProfiler::CurrentTag())
{
    MemoryProfiler::SetCurrentTag(tag);
}

MemoryTagScope::~MemoryTagScope()
{
    MemoryProfiler::SetCurrentTag(m_previous);
}

} // namespace engine::profiling

namespace
{

#if defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
constexpr std::size_t kDefaultNewAlignment =
    static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__);
#else
constexpr std::size_t kDefaultNewAlignment = alignof(std::max_align_t);
#endif

void* AllocateCppMemory(std::size_t size, std::size_t alignment)
{
    for (;;)
    {
        if (void* memory = engine::profiling::MemoryProfiler::Get().Allocate(
                size, alignment, engine::profiling::MemoryProfiler::CurrentTag()))
            return memory;
        std::new_handler handler = std::get_new_handler();
        if (!handler)
            throw std::bad_alloc();
        handler();
    }
}

} // namespace

void* operator new(std::size_t size)
{
    return AllocateCppMemory(size, kDefaultNewAlignment);
}

void* operator new[](std::size_t size)
{
    return AllocateCppMemory(size, kDefaultNewAlignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try { return ::operator new(size); } catch (...) { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}

void operator delete(void* memory) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void operator delete[](void* memory) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return AllocateCppMemory(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return AllocateCppMemory(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept
{
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}

void operator delete(void* memory, std::align_val_t) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    engine::profiling::MemoryProfiler::Get().Free(memory);
}
