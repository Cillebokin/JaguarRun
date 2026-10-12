#include "SparrowCore/Optimizer/OptimizationListener.h"

namespace sparrow
{
OptimizationListener::~OptimizationListener() = default;

void OptimizationListener::OnPhase(OptimizationPhase)
{
}

void OptimizationListener::OnSeparationProgress(const SeparationProgress&)
{
}

void OptimizationListener::OnSeparationResult(const SeparationResult&)
{
}

void OptimizationListener::OnCompressionProgress(double)
{
}
}
