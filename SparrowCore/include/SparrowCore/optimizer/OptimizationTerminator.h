#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/ResultAlias.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>

namespace sparrow
{
struct OptimizationTerminatorConfig
{
    // Zero disables the global duration limit. Phase deadlines are independent;
    // StartPhase(0ms) sets the same phase-start deadline as Rust Duration::ZERO.
    std::size_t maxIterations = 0U;
    std::chrono::milliseconds maxDuration{0};

    Result<void> Validate() const;
};

class SPARROWCORE_API OptimizationTerminator
{
public:
    explicit OptimizationTerminator(OptimizationTerminatorConfig config = {});

    void Start() noexcept;
    // Starts a new phase deadline and clears a phase-scoped interrupt request.
    // A zero duration sets a deadline at phase start; a negative duration disables it.
    void StartPhase(std::chrono::milliseconds phaseDuration) noexcept;
    // Persistent cancellation, retained across phase transitions.
    void RequestStop() noexcept;
    // Interrupts only the active phase; StartPhase clears this request.
    void RequestPhaseStop() noexcept;
    bool ShouldTerminate(std::size_t iteration) const noexcept;
    std::chrono::milliseconds Elapsed() const noexcept;

private:
    OptimizationTerminatorConfig m_Config;
    std::chrono::steady_clock::time_point m_StartTime;
    std::optional<std::chrono::steady_clock::time_point> m_PhaseDeadline;
    std::atomic<bool> m_StopRequested{false};
    std::atomic<bool> m_PhaseStopRequested{false};
};
}
