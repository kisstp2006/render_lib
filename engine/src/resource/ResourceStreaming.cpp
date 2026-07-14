#include "engine/resource/ResourceStreaming.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace engine::resources
{

StreamingState StreamingTicket::Status() const noexcept
{
    return m_state ? m_state->Status.load(std::memory_order_acquire) : StreamingState::Evicted;
}

void StreamingTicket::Cancel() const noexcept
{
    if (m_state)
        m_state->Cancellation.Cancel();
}

std::shared_ptr<void> StreamingTicket::Resource() const
{
    if (!m_state)
        return {};
    std::scoped_lock lock(m_state->Mutex);
    return m_state->Resource;
}

std::string StreamingTicket::Error() const
{
    if (!m_state)
        return {};
    std::scoped_lock lock(m_state->Mutex);
    return m_state->Error;
}

ResourceStreamingScheduler::ResourceStreamingScheduler(StreamingBudget budget, concurrency::TaskSystem *tasks)
    : m_tasks(tasks ? tasks : &concurrency::TaskSystem::Global()), m_budget(budget)
{
    m_budget.MaxConcurrentLoads = std::max(m_budget.MaxConcurrentLoads, 1u);
}

StreamingTicket ResourceStreamingScheduler::Queue(StreamingRequest request)
{
    if (request.Key.empty() || !request.Load)
        throw std::invalid_argument("Streaming request requires a key and a load callback");
    std::scoped_lock lock(m_mutex);
    if (const auto existing = m_entries.find(request.Key); existing != m_entries.end())
        return StreamingTicket(existing->second.State);
    auto state = std::make_shared<detail::StreamingSharedState>();
    const std::string key = request.Key;
    m_entries.emplace(key, Entry{std::move(request), state, {}, m_sequence++});
    return StreamingTicket(std::move(state));
}

bool ResourceStreamingScheduler::SetPriority(const std::string &key, concurrency::TaskPriority priority)
{
    std::scoped_lock lock(m_mutex);
    const auto found = m_entries.find(key);
    if (found == m_entries.end() || found->second.State->Status.load() != StreamingState::Queued)
        return false;
    found->second.Request.Priority = priority;
    return true;
}

bool ResourceStreamingScheduler::Evict(const std::string &key)
{
    std::scoped_lock lock(m_mutex);
    const auto found = m_entries.find(key);
    if (found == m_entries.end())
        return false;
    Entry &entry = found->second;
    const StreamingState status = entry.State->Status.load(std::memory_order_acquire);
    if (status == StreamingState::Loading || status == StreamingState::Queued)
        entry.State->Cancellation.Cancel();
    if (status == StreamingState::Loading)
    {
        m_reservedCpu -= entry.Request.Cost.CpuBytes;
        m_reservedVram -= entry.Request.Cost.VramBytes;
    }
    if (status == StreamingState::Resident)
    {
        m_residentCpu -= entry.Request.Cost.CpuBytes;
        m_residentVram -= entry.Request.Cost.VramBytes;
    }
    {
        std::scoped_lock stateLock(entry.State->Mutex);
        entry.State->Resource.reset();
    }
    entry.State->Status.store(StreamingState::Evicted, std::memory_order_release);
    m_entries.erase(found);
    return true;
}

bool ResourceStreamingScheduler::FitsBudget(const Entry &entry, uint64_t ioScheduled) const
{
    const StreamingCost &cost = entry.Request.Cost;
    return cost.CpuBytes <= m_budget.MaxResidentCpuBytes - std::min(m_budget.MaxResidentCpuBytes,
               m_residentCpu + m_reservedCpu) &&
           cost.VramBytes <= m_budget.MaxResidentVramBytes - std::min(m_budget.MaxResidentVramBytes,
               m_residentVram + m_reservedVram) &&
           cost.IoBytes <= m_budget.MaxIoBytesPerTick - std::min(m_budget.MaxIoBytesPerTick, ioScheduled);
}

void ResourceStreamingScheduler::PollCompleted()
{
    for (auto &[key, entry] : m_entries)
    {
        (void)key;
        if (entry.State->Status.load(std::memory_order_acquire) != StreamingState::Loading ||
            !entry.Result.IsReady())
            continue;
        m_reservedCpu -= entry.Request.Cost.CpuBytes;
        m_reservedVram -= entry.Request.Cost.VramBytes;
        try
        {
            std::shared_ptr<void> resource = entry.Result.Get();
            if (!resource)
                throw std::runtime_error("Streaming callback returned an empty resource");
            {
                std::scoped_lock stateLock(entry.State->Mutex);
                entry.State->Resource = std::move(resource);
            }
            m_residentCpu += entry.Request.Cost.CpuBytes;
            m_residentVram += entry.Request.Cost.VramBytes;
            entry.State->Status.store(StreamingState::Resident, std::memory_order_release);
        }
        catch (const concurrency::TaskCancelled &)
        {
            entry.State->Status.store(StreamingState::Cancelled, std::memory_order_release);
        }
        catch (const std::exception &exception)
        {
            std::scoped_lock stateLock(entry.State->Mutex);
            entry.State->Error = exception.what();
            entry.State->Status.store(StreamingState::Failed, std::memory_order_release);
        }
    }
}

void ResourceStreamingScheduler::Tick()
{
    std::scoped_lock lock(m_mutex);
    PollCompleted();
    uint32_t active = 0;
    std::vector<Entry *> queued;
    for (auto &[key, entry] : m_entries)
    {
        (void)key;
        const StreamingState status = entry.State->Status.load(std::memory_order_acquire);
        if (status == StreamingState::Loading)
            ++active;
        else if (status == StreamingState::Queued)
        {
            if (entry.State->Cancellation.IsCancellationRequested())
                entry.State->Status.store(StreamingState::Cancelled, std::memory_order_release);
            else
                queued.push_back(&entry);
        }
    }
    std::sort(queued.begin(), queued.end(), [](const Entry *left, const Entry *right) {
        if (left->Request.Priority != right->Request.Priority)
            return left->Request.Priority > right->Request.Priority;
        return left->Sequence < right->Sequence;
    });

    uint64_t ioScheduled = 0;
    m_budgetBlocked = 0;
    for (Entry *entry : queued)
    {
        if (active >= m_budget.MaxConcurrentLoads || !FitsBudget(*entry, ioScheduled))
        {
            ++m_budgetBlocked;
            continue;
        }
        entry->State->Status.store(StreamingState::Loading, std::memory_order_release);
        m_reservedCpu += entry->Request.Cost.CpuBytes;
        m_reservedVram += entry->Request.Cost.VramBytes;
        ioScheduled += entry->Request.Cost.IoBytes;
        ++active;
        entry->Result = m_tasks->SubmitFuture(entry->Request.Load, entry->Request.Priority,
                                               entry->State->Cancellation.Token());
    }
    m_lastIoScheduled = ioScheduled;
}

void ResourceStreamingScheduler::CancelAll()
{
    std::scoped_lock lock(m_mutex);
    for (auto &[key, entry] : m_entries)
    {
        (void)key;
        entry.State->Cancellation.Cancel();
    }
}

void ResourceStreamingScheduler::WaitIdle()
{
    for (;;)
    {
        Tick();
        const StreamingStatistics statistics = Statistics();
        if (statistics.Loading == 0 && statistics.Queued == 0)
            return;
        std::this_thread::yield();
    }
}

void ResourceStreamingScheduler::SetBudget(StreamingBudget budget)
{
    budget.MaxConcurrentLoads = std::max(budget.MaxConcurrentLoads, 1u);
    std::scoped_lock lock(m_mutex);
    m_budget = budget;
}

StreamingBudget ResourceStreamingScheduler::Budget() const
{
    std::scoped_lock lock(m_mutex);
    return m_budget;
}

StreamingStatistics ResourceStreamingScheduler::Statistics() const
{
    std::scoped_lock lock(m_mutex);
    StreamingStatistics result;
    result.ResidentCpuBytes = m_residentCpu;
    result.ResidentVramBytes = m_residentVram;
    result.ReservedCpuBytes = m_reservedCpu;
    result.ReservedVramBytes = m_reservedVram;
    result.IoBytesScheduledLastTick = m_lastIoScheduled;
    result.BudgetBlocked = m_budgetBlocked;
    for (const auto &[key, entry] : m_entries)
    {
        (void)key;
        switch (entry.State->Status.load(std::memory_order_acquire))
        {
        case StreamingState::Queued: ++result.Queued; break;
        case StreamingState::Loading: ++result.Loading; break;
        case StreamingState::Resident: ++result.Resident; break;
        case StreamingState::Failed: ++result.Failed; break;
        case StreamingState::Cancelled: ++result.Cancelled; break;
        default: break;
        }
    }
    return result;
}

} // namespace engine::resources
