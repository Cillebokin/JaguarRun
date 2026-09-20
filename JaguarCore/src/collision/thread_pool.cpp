#include "thread_pool.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
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
            workers.emplace_back([this] { workerLoop(); });
    }

    ~State()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

    void workerLoop()
    {
        while (true)
        {
            Task task;
            std::function<void(std::size_t, std::size_t)> function;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return stopping || !tasks.empty(); });
                if (stopping && tasks.empty())
                    return;
                task = tasks.front();
                tasks.pop();
                function = currentFunction;
            }

            try
            {
                function(task.begin, task.end);
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!firstException)
                    firstException = std::current_exception();
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
    std::function<void(std::size_t, std::size_t)> currentFunction;
    std::exception_ptr firstException;
    std::size_t pending = 0;
    bool stopping = false;
    std::mutex mutex;
    std::condition_variable condition;
    std::condition_variable completed;
};

ThreadPool::ThreadPool(std::size_t workerCount)
    : m_State(std::make_unique<State>(workerCount))
{
}

ThreadPool::~ThreadPool() = default;

void ThreadPool::parallelFor(
    std::size_t count,
    std::size_t grainSize,
    const std::function<void(std::size_t, std::size_t)>& function)
{
    if (count == 0)
        return;
    const std::size_t grain = std::max<std::size_t>(1, grainSize);
    const std::size_t taskCount = (count + grain - 1) / grain;
    {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        m_State->currentFunction = function;
        m_State->firstException = nullptr;
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
    const std::exception_ptr exception = m_State->firstException;
    lock.unlock();
    if (exception)
        std::rethrow_exception(exception);
}
}
