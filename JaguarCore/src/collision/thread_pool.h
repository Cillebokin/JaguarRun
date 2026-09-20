#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace jaguar
{
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void parallelFor(std::size_t count,
                     std::size_t grainSize,
                     const std::function<void(std::size_t, std::size_t)>& function);

private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
