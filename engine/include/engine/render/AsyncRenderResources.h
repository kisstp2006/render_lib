#pragma once

#include "engine/concurrency/TaskSystem.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::render
{

struct ArenaAllocation
{
    uint32_t FrameSlot = 0;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    explicit operator bool() const noexcept
    {
        return Size != 0;
    }
};

// Offset allocator for persistently allocated, per-frame GPU buffers. A slot
// may only be reset after its fence has completed.
class FrameGpuArena
{
  public:
    FrameGpuArena(uint64_t bytesPerFrame, uint32_t frameCount);
    ArenaAllocation Allocate(uint32_t frameSlot, uint64_t size, uint64_t alignment);
    void Reset(uint32_t frameSlot);
    [[nodiscard]] uint64_t CapacityPerFrame() const noexcept
    {
        return m_capacityPerFrame;
    }
    [[nodiscard]] uint64_t Used(uint32_t frameSlot) const;
    [[nodiscard]] uint64_t Peak(uint32_t frameSlot) const;

  private:
    uint64_t m_capacityPerFrame = 0;
    mutable std::mutex m_mutex;
    std::vector<uint64_t> m_offsets;
    std::vector<uint64_t> m_peaks;
};

struct StagingAllocation
{
    uint64_t Serial = 0;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    explicit operator bool() const noexcept
    {
        return Size != 0;
    }
};

// Fence/timeline-aware suballocator used by persistent upload buffers.
class StagingRingAllocator
{
  public:
    explicit StagingRingAllocator(uint64_t capacity);
    StagingAllocation Allocate(uint64_t size, uint64_t alignment, uint64_t completedTimeline);
    bool Retire(uint64_t serial, uint64_t timelineValue);
    bool Discard(uint64_t serial);
    void Reclaim(uint64_t completedTimeline);
    [[nodiscard]] uint64_t Capacity() const noexcept
    {
        return m_capacity;
    }
    [[nodiscard]] uint64_t Used() const;

  private:
    struct Region
    {
        StagingAllocation Allocation;
        uint64_t RetireTimeline = UINT64_MAX;
    };

    void ReclaimLocked(uint64_t completedTimeline);
    uint64_t m_capacity = 0;
    uint64_t m_nextSerial = 1;
    mutable std::mutex m_mutex;
    std::vector<Region> m_regions;
};

class DeferredReleaseQueue
{
  public:
    using Release = std::function<void()>;
    void Enqueue(uint64_t safeAfterFrame, Release release);
    size_t ReleaseCompleted(uint64_t completedFrame);
    size_t Flush();
    [[nodiscard]] size_t Pending() const;

  private:
    struct Entry
    {
        uint64_t SafeAfterFrame = 0;
        uint64_t Sequence = 0;
        Release Callback;
    };
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
    uint64_t m_sequence = 0;
};

enum class AsyncPipelineState : uint8_t
{
    Building,
    Ready,
    Failed,
    Cancelled
};

template <typename Pipeline> struct AsyncPipelineSharedState
{
    std::atomic<AsyncPipelineState> State{AsyncPipelineState::Building};
    mutable std::mutex Mutex;
    std::shared_ptr<Pipeline> PipelineObject;
    std::shared_ptr<Pipeline> Fallback;
    std::string Error;
    concurrency::TaskHandle Task;
};

template <typename Pipeline> class AsyncPipelineHandle
{
  public:
    AsyncPipelineHandle() = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return static_cast<bool>(m_state);
    }
    [[nodiscard]] AsyncPipelineState State() const noexcept
    {
        return m_state ? m_state->State.load(std::memory_order_acquire) : AsyncPipelineState::Failed;
    }
    [[nodiscard]] bool UsesFallback() const noexcept
    {
        return State() != AsyncPipelineState::Ready;
    }
    [[nodiscard]] std::shared_ptr<Pipeline> Resolve() const
    {
        if (!m_state)
            return {};
        std::scoped_lock lock(m_state->Mutex);
        return m_state->PipelineObject ? m_state->PipelineObject : m_state->Fallback;
    }
    [[nodiscard]] std::string Error() const
    {
        if (!m_state)
            return "Invalid pipeline handle";
        std::scoped_lock lock(m_state->Mutex);
        return m_state->Error;
    }
    void Cancel() const noexcept
    {
        if (m_state)
            m_state->Task.Cancel();
    }
    void Wait() const
    {
        if (m_state)
            m_state->Task.Wait();
    }

  private:
    template <typename> friend class AsyncPipelineLibrary;
    explicit AsyncPipelineHandle(std::shared_ptr<AsyncPipelineSharedState<Pipeline>> state)
        : m_state(std::move(state))
    {
    }
    std::shared_ptr<AsyncPipelineSharedState<Pipeline>> m_state;
};

// Native pipeline creation is supplied by the backend. Resolve() always returns
// the conspicuous fallback object until the background build has completed.
template <typename Pipeline> class AsyncPipelineLibrary
{
  public:
    using Build = std::function<std::shared_ptr<Pipeline>(const concurrency::CancellationToken &)>;

    explicit AsyncPipelineLibrary(std::shared_ptr<Pipeline> fallback, concurrency::TaskSystem *tasks = nullptr)
        : m_fallback(std::move(fallback)), m_tasks(tasks ? tasks : &concurrency::TaskSystem::Global())
    {
        if (!m_fallback)
            throw std::invalid_argument("Async pipeline library requires a visible fallback pipeline");
    }

    AsyncPipelineHandle<Pipeline> Request(std::string key, Build build,
                                          concurrency::TaskPriority priority = concurrency::TaskPriority::Low)
    {
        if (key.empty() || !build)
            throw std::invalid_argument("Pipeline request requires a key and build callback");
        std::scoped_lock lock(m_mutex);
        if (const auto existing = m_entries.find(key); existing != m_entries.end())
            return AsyncPipelineHandle<Pipeline>(existing->second);
        auto state = std::make_shared<AsyncPipelineSharedState<Pipeline>>();
        state->Fallback = m_fallback;
        state->Task = m_tasks->Submit(
            [state, callback = std::move(build)](const concurrency::CancellationToken &token) {
                try
                {
                    token.ThrowIfCancellationRequested();
                    std::shared_ptr<Pipeline> pipeline = callback(token);
                    token.ThrowIfCancellationRequested();
                    if (!pipeline)
                        throw std::runtime_error("Pipeline build returned an empty object");
                    {
                        std::scoped_lock stateLock(state->Mutex);
                        state->PipelineObject = std::move(pipeline);
                    }
                    state->State.store(AsyncPipelineState::Ready, std::memory_order_release);
                }
                catch (const concurrency::TaskCancelled &)
                {
                    state->State.store(AsyncPipelineState::Cancelled, std::memory_order_release);
                    throw;
                }
                catch (const std::exception &exception)
                {
                    {
                        std::scoped_lock stateLock(state->Mutex);
                        state->Error = exception.what();
                    }
                    state->State.store(AsyncPipelineState::Failed, std::memory_order_release);
                    throw;
                }
            },
            priority);
        m_entries.emplace(std::move(key), state);
        return AsyncPipelineHandle<Pipeline>(std::move(state));
    }

    void Clear()
    {
        std::scoped_lock lock(m_mutex);
        for (auto &[key, state] : m_entries)
        {
            (void)key;
            state->Task.Cancel();
        }
        m_entries.clear();
    }

  private:
    std::shared_ptr<Pipeline> m_fallback;
    concurrency::TaskSystem *m_tasks = nullptr;
    std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<AsyncPipelineSharedState<Pipeline>>> m_entries;
};

} // namespace engine::render
