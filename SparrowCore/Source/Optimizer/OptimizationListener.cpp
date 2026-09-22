#include "SparrowCore/Optimizer/OptimizationListener.h"

namespace sparrow
{
OptimizationListener::~OptimizationListener() = default;

void OptimizationListener::OnProgress(const OptimizationProgress&)
{
}

void OptimizationListener::OnImprovement(const OptimizationProgress&)
{
}
}
