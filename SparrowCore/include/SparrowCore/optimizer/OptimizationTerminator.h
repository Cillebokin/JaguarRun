#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/ResultAlias.h"

#include <atomic>
#include <chrono>
#include <cstddef>

namespace sparrow
{
struct OptimizationTerminatorConfig
{
    std::size_t maxIterations = 1000U;
    std::chrono::milliseconds maxDuration{0};

    Result<void> Validate() const;
};

class SPARROWCORE_API OptimizationTerminator
{
public:
    explicit OptimizationTerminator(OptimizationTerminatorConfig config = {});

    void Start() noexcept;
    void RequestStop() noexcept;
    bool ShouldTerminate(std::size_t iteration) const noexcept;
    std::chrono::milliseconds Elapsed() const noexcept;

private:
    OptimizationTerminatorConfig m_Config;
    std::chrono::steady_clock::time_point m_StartTime;
    std::atomic<bool> m_StopRequested{false};
};
}
