#include "SparrowCore/Optimizer/OptimizationListener.h"

namespace sparrow
{
OptimizationListener::~OptimizationListener() = default;

void OptimizationListener::OnPhase(OptimizationPhase)
{
}

void OptimizationListener::OnProgress(const OptimizationProgress&)
{
}

void OptimizationListener::OnImprovement(const OptimizationProgress&)
{
}

void OptimizationListener::OnSolution(const OptimizationSolutionReport&)
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
