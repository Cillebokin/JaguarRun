#include "SparrowCore/Optimizer/OptimizationTerminator.h"

namespace sparrow
{
Result<void> OptimizationTerminatorConfig::Validate() const
{
    if (maxDuration.count() < 0)
    {
        return Result<void>::FromError(
            {jaguar::ErrorCode::InvalidInput,
             "optimization terminator duration must not be negative"});
    }
    return Result<void>::FromValue();
}

OptimizationTerminator::OptimizationTerminator(
    OptimizationTerminatorConfig config)
    : m_Config(config),
      m_StartTime(std::chrono::steady_clock::now()),
      m_PhaseStartTime(m_StartTime)
{
}

void OptimizationTerminator::Start() noexcept
{
    m_StartTime = std::chrono::steady_clock::now();
    m_PhaseStartTime = m_StartTime;
    m_PhaseDuration = std::chrono::milliseconds{0};
    m_StopRequested.store(false, std::memory_order_relaxed);
}

void OptimizationTerminator::StartPhase(
    std::chrono::milliseconds phaseDuration) noexcept
{
    m_PhaseStartTime = std::chrono::steady_clock::now();
    m_PhaseDuration = phaseDuration.count() < 0
        ? std::chrono::milliseconds{0}
        : phaseDuration;
}

void OptimizationTerminator::RequestStop() noexcept
{
    m_StopRequested.store(true, std::memory_order_relaxed);
}

bool OptimizationTerminator::ShouldTerminate(std::size_t iteration) const noexcept
{
    if (m_StopRequested.load(std::memory_order_relaxed))
    {
        return true;
    }
    if (m_Config.maxIterations > 0U && iteration >= m_Config.maxIterations)
    {
        return true;
    }
    if (m_Config.maxDuration.count() > 0 && Elapsed() >= m_Config.maxDuration)
    {
        return true;
    }
    if (m_PhaseDuration.count() > 0)
    {
        const auto phaseElapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_PhaseStartTime);
        return phaseElapsed.count() >= m_PhaseDuration.count();
    }
    return false;
}

std::chrono::milliseconds OptimizationTerminator::Elapsed() const noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_StartTime);
    return elapsed.count() < 0 ? std::chrono::milliseconds{0} : elapsed;
}
}
