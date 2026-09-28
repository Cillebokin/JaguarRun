#pragma once

#include "SparrowCore/Export.h"

#include "JaguarCore/Problems/Spp.h"

#include <cstddef>
#include <vector>

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

enum class OptimizationReportType
{
    OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE,
    OPTIMIZATION_REPORT_EXPLORATION_INFEASIBLE,
    OPTIMIZATION_REPORT_EXPLORATION_IMPROVING,
    OPTIMIZATION_REPORT_COMPRESSION_FEASIBLE,
    OPTIMIZATION_REPORT_FINAL,
};

struct OptimizationSolutionReport
{
    OptimizationReportType type =
        OptimizationReportType::OPTIMIZATION_REPORT_FINAL;
    OptimizationPhase phase =
        OptimizationPhase::OPTIMIZATION_PHASE_COMPLETE;
    std::size_t iteration = 0U;
    jaguar::SPSolution solution;
    double stripWidth = 0.0;
    double density = 0.0;
    float collisionLoss = 0.0F;
    bool isFeasible = false;
};

using OptimizationHistory = std::vector<OptimizationSolutionReport>;

class OptimizationListener;

struct SeparationProgress
{
    double stripWidth = 0.0;
    double density = 0.0;
    std::size_t iteration = 0U;
    float minimumLoss = 0.0F;
};

struct SeparationResult
{
    bool success = false;
    float elapsedSeconds = 0.0F;
    std::size_t totalEvaluations = 0U;
    std::size_t totalMoves = 0U;
    std::size_t iterations = 0U;
};

inline OptimizationSolutionReport MakeSolutionReport(
    OptimizationReportType type,
    OptimizationPhase phase,
    std::size_t iteration,
    const jaguar::SPSolution& solution,
    float collisionLoss,
    bool isFeasible)
{
    return {type,
            phase,
            iteration,
            solution,
            solution.StripWidth(),
            solution.Density(),
            collisionLoss,
            isFeasible};
}

inline void PublishSolutionReport(
    OptimizationHistory& history,
    OptimizationListener* listener,
    OptimizationSolutionReport report);

class SPARROWCORE_API OptimizationListener
{
public:
    virtual ~OptimizationListener();

    virtual void OnPhase(OptimizationPhase phase);
    virtual void OnProgress(const OptimizationProgress& progress);
    virtual void OnImprovement(const OptimizationProgress& progress);
    virtual void OnSolution(const OptimizationSolutionReport& report);
    virtual void OnSeparationProgress(const SeparationProgress& progress);
    virtual void OnSeparationResult(const SeparationResult& result);
    virtual void OnCompressionProgress(double shrinkStep);
};

inline void PublishSolutionReport(
    OptimizationHistory& history,
    OptimizationListener* listener,
    OptimizationSolutionReport report)
{
    history.push_back(report);
    if (listener != nullptr)
        listener->OnSolution(report);
}
}
