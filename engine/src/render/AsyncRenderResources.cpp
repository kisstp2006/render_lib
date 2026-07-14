#include "engine/render/AsyncRenderResources.h"

#include <algorithm>
#include <stdexcept>

namespace engine::render
{

namespace
{
uint64_t AlignUp(uint64_t value, uint64_t alignment)
{
    alignment = std::max<uint64_t>(alignment, 1);
    return ((value + alignment - 1) / alignment) * alignment;
}
} // namespace

FrameGpuArena::FrameGpuArena(uint64_t bytesPerFrame, uint32_t frameCount)
    : m_capacityPerFrame(bytesPerFrame), m_offsets(frameCount), m_peaks(frameCount)
{
    if (bytesPerFrame == 0 || frameCount == 0)
        throw std::invalid_argument("Frame GPU arena requires non-zero capacity and frame count");
}

ArenaAllocation FrameGpuArena::Allocate(uint32_t frameSlot, uint64_t size, uint64_t alignment)
{
    if (size == 0)
        return {};
    std::scoped_lock lock(m_mutex);
    if (frameSlot >= m_offsets.size())
        throw std::out_of_range("Frame GPU arena slot is out of range");
    const uint64_t offset = AlignUp(m_offsets[frameSlot], alignment);
    if (offset > m_capacityPerFrame || size > m_capacityPerFrame - offset)
        return {};
    m_offsets[frameSlot] = offset + size;
    m_peaks[frameSlot] = std::max(m_peaks[frameSlot], m_offsets[frameSlot]);
    return {frameSlot, static_cast<uint64_t>(frameSlot) * m_capacityPerFrame + offset, size};
}

void FrameGpuArena::Reset(uint32_t frameSlot)
{
    std::scoped_lock lock(m_mutex);
    if (frameSlot >= m_offsets.size())
        throw std::out_of_range("Frame GPU arena slot is out of range");
    m_offsets[frameSlot] = 0;
}

uint64_t FrameGpuArena::Used(uint32_t frameSlot) const
{
    std::scoped_lock lock(m_mutex);
    return frameSlot < m_offsets.size() ? m_offsets[frameSlot] : 0;
}

uint64_t FrameGpuArena::Peak(uint32_t frameSlot) const
{
    std::scoped_lock lock(m_mutex);
    return frameSlot < m_peaks.size() ? m_peaks[frameSlot] : 0;
}

StagingRingAllocator::StagingRingAllocator(uint64_t capacity) : m_capacity(capacity)
{
    if (capacity == 0)
        throw std::invalid_argument("Staging ring requires non-zero capacity");
}

void StagingRingAllocator::ReclaimLocked(uint64_t completedTimeline)
{
    std::erase_if(m_regions, [completedTimeline](const Region &region) {
        return region.RetireTimeline != UINT64_MAX && region.RetireTimeline <= completedTimeline;
    });
}

StagingAllocation StagingRingAllocator::Allocate(uint64_t size, uint64_t alignment, uint64_t completedTimeline)
{
    if (size == 0 || size > m_capacity)
        return {};
    std::scoped_lock lock(m_mutex);
    ReclaimLocked(completedTimeline);
    std::sort(m_regions.begin(), m_regions.end(), [](const Region &left, const Region &right) {
        return left.Allocation.Offset < right.Allocation.Offset;
    });
    uint64_t candidate = 0;
    for (const Region &region : m_regions)
    {
        candidate = AlignUp(candidate, alignment);
        if (candidate <= region.Allocation.Offset && size <= region.Allocation.Offset - candidate)
            break;
        candidate = region.Allocation.Offset + region.Allocation.Size;
    }
    candidate = AlignUp(candidate, alignment);
    if (candidate > m_capacity || size > m_capacity - candidate)
        return {};
    StagingAllocation allocation{m_nextSerial++, candidate, size};
    m_regions.push_back({allocation, UINT64_MAX});
    return allocation;
}

bool StagingRingAllocator::Retire(uint64_t serial, uint64_t timelineValue)
{
    std::scoped_lock lock(m_mutex);
    const auto found = std::find_if(m_regions.begin(), m_regions.end(),
                                    [serial](const Region &region) { return region.Allocation.Serial == serial; });
    if (found == m_regions.end())
        return false;
    found->RetireTimeline = timelineValue;
    return true;
}

bool StagingRingAllocator::Discard(uint64_t serial)
{
    std::scoped_lock lock(m_mutex);
    const auto found = std::find_if(m_regions.begin(), m_regions.end(),
                                    [serial](const Region &region) { return region.Allocation.Serial == serial; });
    if (found == m_regions.end() || found->RetireTimeline != UINT64_MAX)
        return false;
    m_regions.erase(found);
    return true;
}

void StagingRingAllocator::Reclaim(uint64_t completedTimeline)
{
    std::scoped_lock lock(m_mutex);
    ReclaimLocked(completedTimeline);
}

uint64_t StagingRingAllocator::Used() const
{
    std::scoped_lock lock(m_mutex);
    uint64_t used = 0;
    for (const Region &region : m_regions)
        used += region.Allocation.Size;
    return used;
}

void DeferredReleaseQueue::Enqueue(uint64_t safeAfterFrame, Release release)
{
    if (!release)
        return;
    std::scoped_lock lock(m_mutex);
    m_entries.push_back({safeAfterFrame, m_sequence++, std::move(release)});
}

size_t DeferredReleaseQueue::ReleaseCompleted(uint64_t completedFrame)
{
    std::vector<Entry> ready;
    {
        std::scoped_lock lock(m_mutex);
        for (auto iterator = m_entries.begin(); iterator != m_entries.end();)
        {
            if (iterator->SafeAfterFrame <= completedFrame)
            {
                ready.push_back(std::move(*iterator));
                iterator = m_entries.erase(iterator);
            }
            else
                ++iterator;
        }
    }
    std::sort(ready.begin(), ready.end(), [](const Entry &left, const Entry &right) {
        if (left.SafeAfterFrame != right.SafeAfterFrame)
            return left.SafeAfterFrame < right.SafeAfterFrame;
        return left.Sequence < right.Sequence;
    });
    for (Entry &entry : ready)
        entry.Callback();
    return ready.size();
}

size_t DeferredReleaseQueue::Flush()
{
    return ReleaseCompleted(UINT64_MAX);
}

size_t DeferredReleaseQueue::Pending() const
{
    std::scoped_lock lock(m_mutex);
    return m_entries.size();
}

} // namespace engine::render
