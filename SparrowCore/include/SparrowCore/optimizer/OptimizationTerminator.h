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
    // Zero means that only the phase timeout or an explicit stop request
    // limits the optimization, matching sparrow::BasicTerminator.
    std::size_t maxIterations = 0U;
    std::chrono::milliseconds maxDuration{0};

    Result<void> Validate() const;
};

class SPARROWCORE_API OptimizationTerminator
{
public:
    explicit OptimizationTerminator(OptimizationTerminatorConfig config = {});

    void Start() noexcept;
    void StartPhase(std::chrono::milliseconds phaseDuration) noexcept;
    void RequestStop() noexcept;
    bool ShouldTerminate(std::size_t iteration) const noexcept;
    std::chrono::milliseconds Elapsed() const noexcept;

private:
    OptimizationTerminatorConfig m_Config;
    std::chrono::steady_clock::time_point m_StartTime;
    std::chrono::steady_clock::time_point m_PhaseStartTime;
    std::chrono::milliseconds m_PhaseDuration{0};
    std::atomic<bool> m_StopRequested{false};
};
}
