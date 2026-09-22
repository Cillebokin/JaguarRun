#include "ThreadPool.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace jaguar
{
struct ThreadPool::State
{
    struct Task
    {
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    explicit State(std::size_t workerCount)
    {
        const std::size_t count = std::max<std::size_t>(1, workerCount);
        workers.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            workers.emplace_back([this] { WorkerLoop(); });
    }

    ~State()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            isStopping = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

    void WorkerLoop()
    {
        while (true)
        {
            Task task;
            TaskFunction function;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return isStopping || !tasks.empty(); });
                if (isStopping && tasks.empty())
                    return;
                task = tasks.front();
                tasks.pop();
                function = currentFunction;
            }

            // JaguarCore uses Result-based error reporting and forbids
            // exception handling in the implementation. The scheduled
            // callback reports expected failures through Result.
            const Result<void> taskResult = function(task.begin, task.end);
            if (!taskResult.HasValue())
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!firstError.has_value())
                    firstError = taskResult.GetError();
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                if (--pending == 0)
                    completed.notify_one();
            }
        }
    }

    std::vector<std::thread> workers;
    std::queue<Task> tasks;
    TaskFunction currentFunction;
    std::optional<Error> firstError;
    std::size_t pending = 0;
    bool isStopping = false;
    std::mutex mutex;
    std::condition_variable condition;
    std::condition_variable completed;
};

ThreadPool::ThreadPool(std::size_t workerCount)
    : m_State(std::make_unique<State>(workerCount))
{
}

ThreadPool::~ThreadPool() = default;

Result<void> ThreadPool::ParallelFor(
    std::size_t count,
    std::size_t grainSize,
    const TaskFunction& function)
{
    if (count == 0)
        return Result<void>::FromValue();
    const std::size_t grain = std::max<std::size_t>(1, grainSize);
    const std::size_t taskCount = (count + grain - 1) / grain;
    {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        m_State->currentFunction = function;
        m_State->firstError.reset();
        m_State->pending = taskCount;
        for (std::size_t task = 0; task < taskCount; ++task)
        {
            const std::size_t begin = task * grain;
            m_State->tasks.push({begin, std::min(count, begin + grain)});
        }
    }
    m_State->condition.notify_all();

    std::unique_lock<std::mutex> lock(m_State->mutex);
    m_State->completed.wait(lock, [this] { return m_State->pending == 0; });
    const std::optional<Error> error = m_State->firstError;
    lock.unlock();
    if (error.has_value())
        return Result<void>::FromError(*error);
    return Result<void>::FromValue();
}
}
