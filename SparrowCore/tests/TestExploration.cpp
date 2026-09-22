#include "SparrowCore/Optimizer/Compression.h"
#include "SparrowCore/Optimizer/Exploration.h"
#include "SparrowCore/Optimizer/SparrowLbfBuilder.h"
#include "SparrowCore/Optimizer/SparrowOptimizer.h"

#include "JaguarCore/Entities/Item.h"

#include <chrono>
#include <ostream>
#include <utility>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "EXPLORATION TEST FAILURE: " << message << '\n';
    return false;
}

class RecordingListener final : public sparrow::OptimizationListener
{
public:
    void OnProgress(const sparrow::OptimizationProgress&) override
    {
        ++progressCount;
    }

    void OnImprovement(const sparrow::OptimizationProgress&) override
    {
        ++improvementCount;
    }

    std::size_t progressCount = 0U;
    std::size_t improvementCount = 0U;
};

jaguar::Item MakeItem(jaguar::ItemIndex index)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    return jaguar::Item::Create(
               index,
               static_cast<jaguar::ItemId>(3000U + index),
               {polygon.Value(),
                jaguar::Transform2d::Identity(),
                jaguar::ShapeModifyMode::Inflate,
                {}},
               surrogateConfig)
        .Value();
}
}

bool RunExplorationTests(std::ostream& output)
{
    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U)},
        {2U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(), output, "exploration instance failed"))
    {
        return false;
    }
    sparrow::SparrowConstructorConfig constructorConfig;
    constructorConfig.seed = 42U;
    constructorConfig.nContainerSamples = 64U;
    const auto constructed = sparrow::ConstructSparrowInitialSolution(
        instance.Value(), constructorConfig);
    if (!Expect(constructed.HasValue(), output, "exploration initial solution failed"))
    {
        return false;
    }

    sparrow::ExplorationConfig explorationConfig;
    explorationConfig.maxIterations = 2U;
    explorationConfig.maxConsecutiveFailures = 2U;
    explorationConfig.shrinkStep = 0.01;
    explorationConfig.separator.nContainerSamples = 32U;
    explorationConfig.separator.nFocussedSamples = 16U;
    RecordingListener listener;
    sparrow::OptimizationTerminator explorationTerminator({
        16U,
        std::chrono::milliseconds{2000}});
    const auto explored = sparrow::Explore(
        instance.Value(),
        constructed.Value().solution,
        explorationConfig,
        42U,
        explorationTerminator,
        &listener);
    if (!Expect(explored.HasValue() && explored.Value().metrics.feasible,
                output,
                "exploration must preserve a feasible best solution"))
    {
        return false;
    }

    sparrow::CompressionConfig compressionConfig;
    compressionConfig.maxIterations = 2U;
    compressionConfig.maxConsecutiveFailures = 2U;
    compressionConfig.shrinkMin = 0.001;
    compressionConfig.shrinkMax = 0.01;
    compressionConfig.separator.nContainerSamples = 32U;
    compressionConfig.separator.nFocussedSamples = 16U;
    sparrow::OptimizationTerminator compressionTerminator({
        16U,
        std::chrono::milliseconds{2000}});
    const auto compressed = sparrow::Compress(
        instance.Value(),
        explored.Value().solution,
        compressionConfig,
        42U,
        compressionTerminator,
        &listener);
    if (!Expect(compressed.HasValue() && compressed.Value().metrics.feasible
                    && listener.progressCount > 0U,
                  output,
                  "compression and listener must complete with valid metrics"))
    {
        return false;
    }

    sparrow::SparrowConfig optimizerConfig;
    optimizerConfig.seed = 42U;
    optimizerConfig.constructor.nContainerSamples = 32U;
    optimizerConfig.exploration.maxIterations = 1U;
    optimizerConfig.exploration.maxConsecutiveFailures = 1U;
    optimizerConfig.exploration.separator.nContainerSamples = 16U;
    optimizerConfig.exploration.separator.nFocussedSamples = 8U;
    optimizerConfig.compression.maxIterations = 1U;
    optimizerConfig.compression.maxConsecutiveFailures = 1U;
    optimizerConfig.compression.separator.nContainerSamples = 16U;
    optimizerConfig.compression.separator.nFocussedSamples = 8U;
    optimizerConfig.terminator.maxIterations = 32U;
    optimizerConfig.terminator.maxDuration = std::chrono::milliseconds{2000};
    const auto optimized = sparrow::Optimize(
        instance.Value(), optimizerConfig, &listener);
    return Expect(optimized.HasValue() && optimized.Value().metrics.feasible,
                  output,
                  "top-level Sparrow optimizer must return a feasible solution");
}
