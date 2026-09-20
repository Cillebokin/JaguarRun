#pragma once

#include "SparrowCore/export.h"
#include "SparrowCore/optimizer/sample_eval.h"
#include "SparrowCore/random/sparrow_rng.h"
#include "SparrowCore/result_alias.h"

#include "JaguarCore/geometry/transformation.h"

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
refineCoordinateDescent(
    std::pair<jaguar::Transform2d, SampleEval> initial,
    SampleEvaluator& evaluator,
    CoordinateDescentConfig config,
    SparrowRng& rng);
}
