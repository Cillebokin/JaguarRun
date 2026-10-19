#pragma once

#include "SparrowCore/Optimizer/SeparatorWorker.h"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace sparrow
{
enum class SeparatorSearchTracePhase
{
    ReferencePlacement,
    FocusedSample,
    ContainerSample,
    PreRefine,
    FinalRefine,
};

struct SeparatorSearchTraceEvent
{
    enum class Type
    {
        Evaluation,
        Report,
        Result,
    };

    Type type = Type::Evaluation;
    SeparatorSearchTracePhase phase =
        SeparatorSearchTracePhase::ReferencePlacement;
    jaguar::Transform2d transform;
    SampleEval evaluation;
    bool accepted = false;
    std::vector<std::pair<jaguar::Transform2d, SampleEval>> retained;
    std::size_t evaluationCount = 0U;
    bool present = false;
};

using SeparatorSearchTraceCallback =
    std::function<void(const SeparatorSearchTraceEvent&)>;

Result<SeparatorWorkerResult> FindBestSeparatorMoveWithTrace(
    const jaguar::Layout& layout,
    const CollisionTracker& tracker,
    jaguar::PlacedItemHandle movingHandle,
    const SeparatorConfig& config,
    SparrowRng& rng,
    const SeparatorSearchTraceCallback& callback);
}
