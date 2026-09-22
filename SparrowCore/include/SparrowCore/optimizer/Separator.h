#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/SeparatorWorker.h"

#include "JaguarCore/Problems/Spp.h"

#include <cstdint>

namespace sparrow
{
struct SeparatorResult
{
    jaguar::SPSolution solution;
    CollisionTrackerSnapshot trackerSnapshot;
};

class SPARROWCORE_API Separator
{
public:
    static Result<Separator> Create(jaguar::SPProblem problem,
                                    std::uint64_t seed);

    Result<SeparatorResult> Run(const SeparatorConfig& config,
                                OptimizationTerminator& terminator);
    Result<void> RebuildTracker();
    jaguar::SPSolution SaveSolution() const;
    const CollisionTracker& Tracker() const noexcept;
    const jaguar::SPProblem& Problem() const noexcept;

private:
    Separator(jaguar::SPProblem problem,
              CollisionTracker tracker,
              std::uint64_t seed);

    jaguar::SPProblem m_Problem;
    CollisionTracker m_Tracker;
    SparrowRng m_Rng;
};
}
