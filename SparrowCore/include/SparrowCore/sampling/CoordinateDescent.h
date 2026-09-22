#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
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
    std::size_t maxEvaluations = 1000U;
};

SPARROWCORE_API Result<std::pair<jaguar::Transform2d, SampleEval>>
RefineCoordinateDescent(
    std::pair<jaguar::Transform2d, SampleEval> initial,
    SampleEvaluator& evaluator,
    CoordinateDescentConfig config,
    SparrowRng& rng);
}
