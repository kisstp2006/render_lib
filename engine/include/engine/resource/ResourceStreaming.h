#pragma once

#include "engine/concurrency/TaskSystem.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::resources
{

struct StreamingCost
{
    uint64_t CpuBytes = 0;
    uint64_t VramBytes = 0;
    uint64_t IoBytes = 0;
};

struct StreamingBudget
{
    uint64_t MaxResidentCpuBytes = std::numeric_limits<uint64_t>::max();
    uint64_t MaxResidentVramBytes = std::numeric_limits<uint64_t>::max();
    uint64_t MaxIoBytesPerTick = std::numeric_limits<uint64_t>::max();
    uint32_t MaxConcurrentLoads = 4;
};

enum class StreamingState : uint8_t
{
    Queued,
    Loading,
    Resident,
    Cancelled,
    Failed,
    Evicted
};

struct StreamingRequest
{
    std::string Key;
    StreamingCost Cost;
    concurrency::TaskPriority Priority = concurrency::TaskPriority::Normal;
    std::function<std::shared_ptr<void>(const concurrency::CancellationToken &)> Load;
};

namespace detail
{
struct StreamingSharedState
{
    std::atomic<StreamingState> Status{StreamingState::Queued};
    concurrency::CancellationSource Cancellation;
    mutable std::mutex Mutex;
    std::shared_ptr<void> Resource;
    std::string Error;
};
} // namespace detail

class StreamingTicket
{
  public:
    StreamingTicket() = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return static_cast<bool>(m_state);
    }
    [[nodiscard]] StreamingState Status() const noexcept;
    void Cancel() const noexcept;
    [[nodiscard]] std::shared_ptr<void> Resource() const;
    [[nodiscard]] std::string Error() const;

  private:
    friend class ResourceStreamingScheduler;
    explicit StreamingTicket(std::shared_ptr<detail::StreamingSharedState> state) : m_state(std::move(state))
    {
    }
    std::shared_ptr<detail::StreamingSharedState> m_state;
};

struct StreamingStatistics
{
    uint64_t ResidentCpuBytes = 0;
    uint64_t ResidentVramBytes = 0;
    uint64_t ReservedCpuBytes = 0;
    uint64_t ReservedVramBytes = 0;
    uint64_t IoBytesScheduledLastTick = 0;
    uint32_t Queued = 0;
    uint32_t Loading = 0;
    uint32_t Resident = 0;
    uint32_t Failed = 0;
    uint32_t Cancelled = 0;
    uint32_t BudgetBlocked = 0;
};

class ResourceStreamingScheduler
{
  public:
    explicit ResourceStreamingScheduler(StreamingBudget budget = {}, concurrency::TaskSystem *tasks = nullptr);

    StreamingTicket Queue(StreamingRequest request);
    bool SetPriority(const std::string &key, concurrency::TaskPriority priority);
    bool Evict(const std::string &key);
    void Tick();
    void CancelAll();
    void WaitIdle();

    void SetBudget(StreamingBudget budget);
    [[nodiscard]] StreamingBudget Budget() const;
    [[nodiscard]] StreamingStatistics Statistics() const;

  private:
    struct Entry
    {
        StreamingRequest Request;
        std::shared_ptr<detail::StreamingSharedState> State;
        concurrency::AsyncResult<std::shared_ptr<void>> Result;
        uint64_t Sequence = 0;
    };

    bool FitsBudget(const Entry &entry, uint64_t ioScheduled) const;
    void PollCompleted();

    concurrency::TaskSystem *m_tasks = nullptr;
    mutable std::mutex m_mutex;
    StreamingBudget m_budget;
    std::unordered_map<std::string, Entry> m_entries;
    uint64_t m_sequence = 0;
    uint64_t m_residentCpu = 0;
    uint64_t m_residentVram = 0;
    uint64_t m_reservedCpu = 0;
    uint64_t m_reservedVram = 0;
    uint64_t m_lastIoScheduled = 0;
    uint32_t m_budgetBlocked = 0;
};

} // namespace engine::resources
