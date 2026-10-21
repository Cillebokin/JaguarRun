#pragma once

#include "SparrowCore/Optimizer/LbfSearch.h"
#include "SparrowCore/Optimizer/SampleEval.h"

#include <functional>
#include <cstddef>
#include <utility>
#include <vector>

namespace sparrow
{
enum class LbfSearchTracePhase
{
    ContainerSample,
    LocalSearch,
    PreRefine,
    FinalRefine,
};

struct LbfSearchTraceEvent
{
    enum class Type
    {
        Evaluation,
        Report,
        Result,
    };

    Type type = Type::Evaluation;
    LbfSearchTracePhase phase = LbfSearchTracePhase::ContainerSample;
    jaguar::Transform2d transform = jaguar::Transform2d::Identity();
    SampleEval evaluation{};
    bool accepted = false;
    std::vector<std::pair<jaguar::Transform2d, SampleEval>> retained;
    std::size_t evaluationCount = 0U;
};

using LbfSearchTraceCallback = std::function<void(const LbfSearchTraceEvent&)>;

SPARROWCORE_API Result<std::optional<SearchResult>> SearchLbfWithTrace(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfSearchTraceCallback& callback);

// Internal test seam for the private LBF uniform sampler. Not part of the
// SparrowCore public include surface.
Result<std::vector<jaguar::Transform2d>> SampleLbfUniformForTrace(
    const jaguar::Rect2d& bounds,
    const jaguar::Item& item,
    SparrowRng& rng,
    std::size_t sampleCount);
}
