#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <limits>
#include <utility>

namespace sparrow
{
struct CoordinateDescentConfig
{
    float translationStepInit = 0.0F;
    float translationStepLimit = 0.0F;
    float rotationStepInit = 0.0F;
    float rotationStepLimit = 0.0F;
    bool rotationWiggle = false;
    std::size_t maxEvaluations = std::numeric_limits<std::size_t>::max();
};

SPARROWCORE_API Result<std::pair<jaguar::Transform2d, SampleEval>>
RefineCoordinateDescent(
    std::pair<jaguar::Transform2d, SampleEval> initial,
    SampleEvaluator& evaluator,
    CoordinateDescentConfig config,
    SparrowRng& rng);
}
