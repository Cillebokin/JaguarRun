#pragma once

#include "SparrowCore/Export.h"

#include <cstddef>

namespace sparrow
{
enum class OptimizationPhase
{
    OPTIMIZATION_PHASE_CONSTRUCTION,
    OPTIMIZATION_PHASE_EXPLORATION,
    OPTIMIZATION_PHASE_COMPRESSION,
    OPTIMIZATION_PHASE_COMPLETE,
};

struct OptimizationProgress
{
    OptimizationPhase phase =
        OptimizationPhase::OPTIMIZATION_PHASE_CONSTRUCTION;
    std::size_t iteration = 0U;
    double stripWidth = 0.0;
    double density = 0.0;
    float collisionLoss = 0.0F;
    bool isFeasible = false;
};

class SPARROWCORE_API OptimizationListener
{
public:
    virtual ~OptimizationListener();

    virtual void OnProgress(const OptimizationProgress& progress);
    virtual void OnImprovement(const OptimizationProgress& progress);
};
}
