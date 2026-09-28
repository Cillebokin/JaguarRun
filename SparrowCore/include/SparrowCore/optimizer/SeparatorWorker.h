#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Quantify/CollisionTracker.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <optional>

namespace sparrow
{
struct SeparatorConfig
{
    // Zero disables separator rounds, matching Sparrow's strike cap.
    std::size_t strikeLimit = 3U;
    std::size_t noImprovementLimit = 200U;
    std::size_t nContainerSamples = 50U;
    std::size_t nFocussedSamples = 25U;
    std::size_t nCoordinateDescents = 3U;
    std::size_t nWorkers = 3U;

    Result<void> Validate() const;
};

struct SeparatorWorkerResult
{
    std::optional<jaguar::Transform2d> transform;
    SampleEval evaluation;
    std::size_t evaluationCount = 0U;
    std::size_t surrogateRejectCount = 0U;
    std::size_t exactCollisionCount = 0U;
};

SPARROWCORE_API Result<SeparatorWorkerResult> FindBestSeparatorMove(
    const jaguar::Layout& layout,
    const CollisionTracker& tracker,
    jaguar::PlacedItemHandle movingHandle,
    const SeparatorConfig& config,
    SparrowRng& rng);
}
