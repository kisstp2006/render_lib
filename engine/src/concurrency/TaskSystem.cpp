#include "engine/concurrency/TaskSystem.h"

#include <algorithm>
#include <chrono>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace engine::concurrency
{

namespace detail
{
struct TaskState
{
    std::atomic<TaskStatus> Status{TaskStatus::Queued};
    CancellationSource Cancellation;
    CancellationToken Token;
    mutable std::mutex Mutex;
    std::condition_variable Completed;
    std::exception_ptr Exception;
};
} // namespace detail

namespace
{
thread_local TaskSystem *g_workerSystem = nullptr;

bool IsTerminal(TaskStatus status)
{
    return status == TaskStatus::Completed || status == TaskStatus::Cancelled || status == TaskStatus::Failed;
}

uint32_t DefaultWorkerCount()
{
    const uint32_t hardware = std::thread::hardware_concurrency();
    return std::clamp(hardware > 1 ? hardware - 1 : 1u, 1u, 32u);
}
} // namespace

struct TaskSystem::QueuedTask
{
    uint64_t Sequence = 0;
    Task Work;
    std::shared_ptr<detail::TaskState> State;
};

bool CancellationToken::IsCancellationRequested() const noexcept
{
    return (m_primary && m_primary->Cancelled.load(std::memory_order_acquire)) ||
           (m_secondary && m_secondary->Cancelled.load(std::memory_order_acquire));
}

void CancellationToken::ThrowIfCancellationRequested() const
{
    if (IsCancellationRequested())
        throw TaskCancelled();
}

bool TaskHandle::IsReady() const noexcept
{
    return !m_state || IsTerminal(m_state->Status.load(std::memory_order_acquire));
}

TaskStatus TaskHandle::Status() const noexcept
{
    return m_state ? m_state->Status.load(std::memory_order_acquire) : TaskStatus::Completed;
}

void TaskHandle::Cancel() const noexcept
{
    if (m_state)
        m_state->Cancellation.Cancel();
}

void TaskHandle::Wait() const
{
    if (!m_state || IsReady())
        return;
    std::unique_lock lock(m_state->Mutex);
    m_state->Completed.wait(lock, [this] { return IsReady(); });
}

void TaskHandle::RethrowIfFailed() const
{
    Wait();
    if (!m_state)
        return;
    std::exception_ptr exception;
    {
        std::scoped_lock lock(m_state->Mutex);
        exception = m_state->Exception;
    }
    if (exception)
        std::rethrow_exception(exception);
}

TaskSystem::TaskSystem(uint32_t workerCount)
    : m_queues(static_cast<size_t>(TaskPriority::Count))
{
    workerCount = workerCount == 0 ? DefaultWorkerCount() : std::max(workerCount, 1u);
    m_workers.reserve(workerCount);
    for (uint32_t index = 0; index < workerCount; ++index)
        m_workers.emplace_back([this, index] { WorkerMain(index); });
}

TaskSystem::~TaskSystem()
{
    {
        std::scoped_lock lock(m_mutex);
        m_accepting = false;
        m_stopping = true;
    }
    m_wake.notify_all();
    for (std::thread &worker : m_workers)
        if (worker.joinable())
            worker.join();
}

TaskSystem &TaskSystem::Global()
{
    static TaskSystem system;
    return system;
}

TaskHandle TaskSystem::Submit(Task task, TaskPriority priority, CancellationToken externalCancellation)
{
    if (!task)
        throw std::invalid_argument("Cannot submit an empty task");
    auto state = std::make_shared<detail::TaskState>();
    state->Token = CancellationToken(state->Cancellation.m_state, std::move(externalCancellation.m_primary));
    if (externalCancellation.m_secondary)
    {
        // Flattening two external states is intentionally conservative: if the external
        // token is already composed, retain its primary state and sample cancellation now.
        if (externalCancellation.IsCancellationRequested())
            state->Cancellation.Cancel();
    }
    {
        std::scoped_lock lock(m_mutex);
        if (!m_accepting)
            throw std::runtime_error("Task system is shutting down");
        const size_t queueIndex = std::min<size_t>(static_cast<size_t>(priority), m_queues.size() - 1);
        m_queues[queueIndex].push_back(QueuedTask{m_sequence++, std::move(task), state});
        m_submitted.fetch_add(1, std::memory_order_relaxed);
        m_queued.fetch_add(1, std::memory_order_relaxed);
    }
    m_wake.notify_one();
    return TaskHandle(std::move(state));
}

void TaskSystem::Wait(const TaskHandle &handle)
{
    // A waiting thread is useful CPU capacity. Let both workers and the render
    // thread drain ready work before sleeping on a task that is already
    // running elsewhere. Scene preparation submits short batches, so this
    // avoids a submit -> wake worker -> sleep main-thread round trip for the
    // common one/two-task case.
    while (!handle.IsReady())
    {
        if (!TryExecuteOne())
        {
            if (g_workerSystem == this)
                std::this_thread::yield();
            else
                handle.Wait();
        }
    }
}

void TaskSystem::WaitIdle()
{
    if (g_workerSystem == this)
    {
        while (m_queued.load(std::memory_order_acquire) != 0 ||
               m_active.load(std::memory_order_acquire) > 1)
        {
            if (!TryExecuteOne())
                std::this_thread::yield();
        }
        return;
    }
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] {
        return m_queued.load(std::memory_order_acquire) == 0 && m_active.load(std::memory_order_acquire) == 0;
    });
}

uint32_t TaskSystem::WorkerCount() const noexcept
{
    return static_cast<uint32_t>(m_workers.size());
}

TaskSystemStatistics TaskSystem::Statistics() const noexcept
{
    return {WorkerCount(), m_submitted.load(std::memory_order_relaxed), m_completed.load(std::memory_order_relaxed),
            m_cancelled.load(std::memory_order_relaxed), m_failed.load(std::memory_order_relaxed),
            m_queued.load(std::memory_order_relaxed), m_active.load(std::memory_order_relaxed)};
}

bool TaskSystem::PopTask(QueuedTask &task)
{
    std::scoped_lock lock(m_mutex);
    for (size_t index = m_queues.size(); index-- > 0;)
    {
        if (m_queues[index].empty())
            continue;
        task = std::move(m_queues[index].front());
        m_queues[index].pop_front();
        m_queued.fetch_sub(1, std::memory_order_relaxed);
        m_active.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool TaskSystem::TryExecuteOne()
{
    QueuedTask task;
    if (!PopTask(task))
        return false;
    Execute(task);
    return true;
}

void TaskSystem::Execute(QueuedTask &task)
{
    task.State->Status.store(TaskStatus::Running, std::memory_order_release);
    TaskStatus finalStatus = TaskStatus::Completed;
    std::exception_ptr failure;
    try
    {
        task.Work(task.State->Token);
        if (task.State->Token.IsCancellationRequested())
            finalStatus = TaskStatus::Cancelled;
    }
    catch (const TaskCancelled &)
    {
        finalStatus = TaskStatus::Cancelled;
    }
    catch (...)
    {
        finalStatus = TaskStatus::Failed;
        failure = std::current_exception();
    }

    // TaskHandle::Wait evaluates its predicate while holding State::Mutex.
    // Publish the terminal state under that same mutex; otherwise a notify can
    // land between the predicate check and the wait operation and be lost
    // forever. The render thread hit exactly this race in PrepareFrame while
    // every worker was already asleep on an empty queue.
    {
        std::scoped_lock stateLock(task.State->Mutex);
        task.State->Exception = failure;
        task.State->Status.store(finalStatus, std::memory_order_release);
    }
    if (finalStatus == TaskStatus::Completed)
        m_completed.fetch_add(1, std::memory_order_relaxed);
    else if (finalStatus == TaskStatus::Cancelled)
        m_cancelled.fetch_add(1, std::memory_order_relaxed);
    else
        m_failed.fetch_add(1, std::memory_order_relaxed);
    // WaitIdle observes queued/active while holding m_mutex. Publish the final
    // active transition under the same mutex so a waiter cannot evaluate the
    // predicate and go to sleep between this transition and the notification.
    // HISM submits several short parallel batches per frame and made this
    // pre-existing lost-wakeup window much easier to hit during shutdown.
    {
        std::scoped_lock lock(m_mutex);
        m_active.fetch_sub(1, std::memory_order_relaxed);
    }
    task.State->Completed.notify_all();
    if (m_queued.load(std::memory_order_acquire) == 0 && m_active.load(std::memory_order_acquire) == 0)
        m_idle.notify_all();
}

void TaskSystem::WorkerMain(uint32_t workerIndex)
{
#ifdef _WIN32
    const std::wstring name = L"Engine Job " + std::to_wstring(workerIndex);
    SetThreadDescription(GetCurrentThread(), name.c_str());
#else
    (void)workerIndex;
#endif
    g_workerSystem = this;
    for (;;)
    {
        if (TryExecuteOne())
            continue;
        std::unique_lock lock(m_mutex);
        m_wake.wait(lock, [this] {
            if (m_stopping)
                return true;
            return std::any_of(m_queues.begin(), m_queues.end(), [](const auto &queue) { return !queue.empty(); });
        });
        if (m_stopping && m_queued.load(std::memory_order_acquire) == 0)
            break;
    }
    g_workerSystem = nullptr;
}

} // namespace engine::concurrency
