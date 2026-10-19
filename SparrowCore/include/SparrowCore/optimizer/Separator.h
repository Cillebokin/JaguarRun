#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/OptimizationListener.h"
#include "SparrowCore/Optimizer/SeparatorWorker.h"

#include "JaguarCore/Problems/Spp.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace sparrow
{
namespace detail
{
struct E5Round26SeparatorTestAccess;
}

struct SeparatorResult
{
    jaguar::SPSolution solution;
    CollisionTrackerSnapshot trackerSnapshot;
    float elapsedSeconds = 0.0F;
    std::size_t surrogateRejectCount = 0U;
    std::size_t exactCollisionCount = 0U;
    std::size_t totalEvaluationCount = 0U;
    std::size_t totalMoveCount = 0U;
    std::size_t iterations = 0U;
};

class SPARROWCORE_API Separator
{
public:
    static Result<Separator> Create(jaguar::SPProblem problem,
                                    std::uint64_t seed);

    Result<SeparatorResult> Run(const SeparatorConfig& config,
                                OptimizationTerminator& terminator,
                                OptimizationListener* listener = nullptr,
                                OptimizationHistory* history = nullptr,
                                OptimizationPhase phase =
                                    OptimizationPhase::OPTIMIZATION_PHASE_EXPLORATION);
    Result<void> RebuildTracker();
    void ResetWorkerRngs(std::size_t workerCount);
    Result<void> ChangeStripWidth(
        double newWidth,
        std::optional<double> splitPosition = std::nullopt);
    Result<void> Rollback(const jaguar::SPSolution& solution);
    Result<jaguar::PlacedItemHandle> MoveItem(
        jaguar::PlacedItemHandle handle,
        const jaguar::Transform2d& transform);
    jaguar::SPSolution SaveSolution() const;
    SparrowRng& Random() noexcept;
    const CollisionTracker& Tracker() const noexcept;
    const jaguar::SPProblem& Problem() const noexcept;

private:
    friend struct detail::E5Round26SeparatorTestAccess;

    Separator(jaguar::SPProblem problem,
              CollisionTracker tracker,
              std::uint64_t seed);

    jaguar::SPProblem m_Problem;
    CollisionTracker m_Tracker;
    SparrowRng m_Rng;
    std::vector<SparrowRng> m_WorkerRngs;
};
}
