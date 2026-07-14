#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::concurrency
{

enum class TaskPriority : uint8_t
{
    Background,
    Low,
    Normal,
    High,
    Critical,
    Count
};

enum class TaskStatus : uint8_t
{
    Queued,
    Running,
    Completed,
    Cancelled,
    Failed
};

class TaskCancelled final : public std::runtime_error
{
  public:
    TaskCancelled() : std::runtime_error("Task was cancelled")
    {
    }
};

class CancellationToken;

namespace detail
{
struct CancellationState
{
    std::atomic<bool> Cancelled{false};
};
struct TaskState;

template <typename Function, bool WithToken> struct TaskInvokeResult;
template <typename Function> struct TaskInvokeResult<Function, true>
{
    using Type = std::invoke_result_t<Function, const CancellationToken &>;
};
template <typename Function> struct TaskInvokeResult<Function, false>
{
    using Type = std::invoke_result_t<Function>;
};
} // namespace detail

class CancellationToken
{
  public:
    CancellationToken() = default;
    [[nodiscard]] bool IsCancellationRequested() const noexcept;
    void ThrowIfCancellationRequested() const;
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(m_primary) || static_cast<bool>(m_secondary);
    }

  private:
    friend class CancellationSource;
    friend class TaskSystem;
    explicit CancellationToken(std::shared_ptr<detail::CancellationState> primary,
                               std::shared_ptr<detail::CancellationState> secondary = {})
        : m_primary(std::move(primary)), m_secondary(std::move(secondary))
    {
    }

    std::shared_ptr<detail::CancellationState> m_primary;
    std::shared_ptr<detail::CancellationState> m_secondary;
};

class CancellationSource
{
  public:
    CancellationSource() : m_state(std::make_shared<detail::CancellationState>())
    {
    }

    [[nodiscard]] CancellationToken Token() const
    {
        return CancellationToken(m_state);
    }
    void Cancel() const noexcept
    {
        m_state->Cancelled.store(true, std::memory_order_release);
    }
    [[nodiscard]] bool IsCancellationRequested() const noexcept
    {
        return m_state->Cancelled.load(std::memory_order_acquire);
    }

  private:
    friend class TaskSystem;
    std::shared_ptr<detail::CancellationState> m_state;
};

class TaskHandle
{
  public:
    TaskHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return static_cast<bool>(m_state);
    }
    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] TaskStatus Status() const noexcept;
    void Cancel() const noexcept;
    void Wait() const;
    void RethrowIfFailed() const;

  private:
    friend class TaskSystem;
    explicit TaskHandle(std::shared_ptr<detail::TaskState> state) : m_state(std::move(state))
    {
    }
    std::shared_ptr<detail::TaskState> m_state;
};

template <typename T> class AsyncResult
{
  public:
    AsyncResult() = default;
    AsyncResult(TaskHandle handle, std::shared_future<T> future)
        : m_handle(std::move(handle)), m_future(std::move(future))
    {
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_handle.IsValid() && m_future.valid();
    }
    [[nodiscard]] bool IsReady() const noexcept
    {
        return m_handle.IsReady();
    }
    [[nodiscard]] TaskStatus Status() const noexcept
    {
        return m_handle.Status();
    }
    void Cancel() const noexcept
    {
        m_handle.Cancel();
    }
    void Wait() const
    {
        m_handle.Wait();
    }
    decltype(auto) Get() const
    {
        if constexpr (std::is_void_v<T>)
            m_future.get();
        else
            return m_future.get();
    }
    [[nodiscard]] const TaskHandle &Handle() const noexcept
    {
        return m_handle;
    }
    [[nodiscard]] std::shared_future<T> SharedFuture() const
    {
        return m_future;
    }

  private:
    TaskHandle m_handle;
    std::shared_future<T> m_future;
};

struct TaskSystemStatistics
{
    uint32_t WorkerCount = 0;
    uint64_t Submitted = 0;
    uint64_t Completed = 0;
    uint64_t Cancelled = 0;
    uint64_t Failed = 0;
    uint64_t Queued = 0;
    uint64_t Active = 0;
};

class TaskSystem
{
  public:
    using Task = std::function<void(const CancellationToken &)>;

    explicit TaskSystem(uint32_t workerCount = 0);
    ~TaskSystem();
    TaskSystem(const TaskSystem &) = delete;
    TaskSystem &operator=(const TaskSystem &) = delete;

    static TaskSystem &Global();

    TaskHandle Submit(Task task, TaskPriority priority = TaskPriority::Normal,
                      CancellationToken externalCancellation = {});

    template <typename Function>
    auto SubmitFuture(Function &&function, TaskPriority priority = TaskPriority::Normal,
                      CancellationToken externalCancellation = {})
    {
        using FunctionType = std::decay_t<Function>;
        using ResultType = typename detail::TaskInvokeResult<
            FunctionType, std::is_invocable_v<FunctionType, const CancellationToken &>>::Type;

        auto promise = std::make_shared<std::promise<ResultType>>();
        std::shared_future<ResultType> future = promise->get_future().share();
        TaskHandle handle = Submit(
            [callback = FunctionType(std::forward<Function>(function)), promise](const CancellationToken &token) mutable {
                try
                {
                    token.ThrowIfCancellationRequested();
                    if constexpr (std::is_void_v<ResultType>)
                    {
                        if constexpr (std::is_invocable_v<FunctionType, const CancellationToken &>)
                            callback(token);
                        else
                            callback();
                        token.ThrowIfCancellationRequested();
                        promise->set_value();
                    }
                    else
                    {
                        ResultType value = [&]() -> ResultType {
                            if constexpr (std::is_invocable_v<FunctionType, const CancellationToken &>)
                                return callback(token);
                            else
                                return callback();
                        }();
                        token.ThrowIfCancellationRequested();
                        promise->set_value(std::move(value));
                    }
                }
                catch (...)
                {
                    promise->set_exception(std::current_exception());
                    throw;
                }
            },
            priority, std::move(externalCancellation));
        return AsyncResult<ResultType>(std::move(handle), std::move(future));
    }

    template <typename Function>
    void ParallelFor(size_t count, size_t minimumGrain, Function &&function,
                     TaskPriority priority = TaskPriority::High)
    {
        if (count == 0)
            return;
        minimumGrain = std::max<size_t>(minimumGrain, 1);
        const size_t desiredTasks = std::max<size_t>(1, WorkerCount() + 1);
        const size_t grain = std::max(minimumGrain, (count + desiredTasks - 1) / desiredTasks);
        std::vector<TaskHandle> handles;
        handles.reserve((count + grain - 1) / grain);
        auto callback = std::make_shared<std::decay_t<Function>>(std::forward<Function>(function));
        for (size_t begin = 0; begin < count; begin += grain)
        {
            const size_t end = std::min(count, begin + grain);
            handles.push_back(Submit([callback, begin, end](const CancellationToken &token) {
                for (size_t index = begin; index < end; ++index)
                {
                    token.ThrowIfCancellationRequested();
                    (*callback)(index);
                }
            }, priority));
        }
        for (const TaskHandle &handle : handles)
        {
            Wait(handle);
            handle.RethrowIfFailed();
        }
    }

    void Wait(const TaskHandle &handle);
    void WaitIdle();
    [[nodiscard]] uint32_t WorkerCount() const noexcept;
    [[nodiscard]] TaskSystemStatistics Statistics() const noexcept;

  private:
    struct QueuedTask;
    void WorkerMain(uint32_t workerIndex);
    bool TryExecuteOne();
    bool PopTask(QueuedTask &task);
    void Execute(QueuedTask &task);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::vector<std::thread> m_workers;
    std::vector<std::deque<QueuedTask>> m_queues;
    bool m_accepting = true;
    bool m_stopping = false;
    uint64_t m_sequence = 0;
    std::atomic<uint64_t> m_submitted{0};
    std::atomic<uint64_t> m_completed{0};
    std::atomic<uint64_t> m_cancelled{0};
    std::atomic<uint64_t> m_failed{0};
    std::atomic<uint64_t> m_queued{0};
    std::atomic<uint64_t> m_active{0};
};

} // namespace engine::concurrency
