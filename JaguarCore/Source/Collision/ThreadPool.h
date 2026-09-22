#pragma once

#include "JaguarCore/Result.h"

#include <cstddef>
#include <functional>
#include <memory>

namespace jaguar
{
class ThreadPool
{
public:
    using TaskFunction = std::function<Result<void>(std::size_t, std::size_t)>;

    explicit ThreadPool(std::size_t workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    Result<void> ParallelFor(std::size_t count,
                             std::size_t grainSize,
                             const TaskFunction& function);

private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
