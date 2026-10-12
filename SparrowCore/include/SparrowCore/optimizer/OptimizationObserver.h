#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/OptimizationListener.h"

namespace sparrow
{
// Optional C++-only progress callbacks. Passing an observer opts into events
// that are not part of Rust Sparrow's SolutionListener contract.
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

class SPARROWCORE_API OptimizationObserver
{
public:
    virtual ~OptimizationObserver() = default;

    virtual void OnProgress(const OptimizationProgress&) {}
    virtual void OnImprovement(const OptimizationProgress&) {}
};
}
