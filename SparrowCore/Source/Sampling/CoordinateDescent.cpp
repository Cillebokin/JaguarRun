#include "SparrowCore/Sampling/CoordinateDescent.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{
using CoordinateResult = sparrow::Result<
    std::pair<jaguar::Transform2d, sparrow::SampleEval>>;

CoordinateResult InvalidCoordinateConfig(const char* message)
{
    return CoordinateResult::FromError({jaguar::ErrorCode::InvalidInput, message});
}

CoordinateResult CoordinateTimeLimit(const char* message)
{
    return CoordinateResult::FromError({jaguar::ErrorCode::TimeLimit, message});
}

enum class Axis
{
    AXIS_HORIZONTAL,
    AXIS_VERTICAL,
    AXIS_FORWARD_DIAGONAL,
    AXIS_BACKWARD_DIAGONAL,
    AXIS_WIGGLE,
};

std::uint64_t BoundedIndex(sparrow::SparrowRng& rng, std::uint64_t bound) noexcept
{
    const std::uint64_t threshold =
        (std::numeric_limits<std::uint64_t>::max() % bound + 1U) % bound;
    std::uint64_t value = 0;
    do
    {
        value = rng.NextU64();
    } while (value < threshold);
    return value % bound;
}

Axis ChooseAxis(sparrow::SparrowRng& rng, bool rotationWiggle) noexcept
{
    const std::uint64_t selected = BoundedIndex(rng, rotationWiggle ? 6U : 4U);
    switch (selected)
    {
    case 0U:
        return Axis::AXIS_HORIZONTAL;
    case 1U:
        return Axis::AXIS_VERTICAL;
    case 2U:
        return Axis::AXIS_FORWARD_DIAGONAL;
    case 3U:
        return Axis::AXIS_BACKWARD_DIAGONAL;
    case 4U:
    case 5U:
        return Axis::AXIS_WIGGLE;
    }
    return Axis::AXIS_HORIZONTAL;
}

bool IsValidCoordinateConfig(const sparrow::CoordinateDescentConfig& config) noexcept
{
    return std::isfinite(config.translationStepInit)
        && std::isfinite(config.translationStepLimit)
        && std::isfinite(config.rotationStepInit)
        && std::isfinite(config.rotationStepLimit)
        && config.translationStepInit >= 0.0F
        && config.translationStepLimit >= 0.0F
        && config.rotationStepInit >= 0.0F
        && config.rotationStepLimit >= 0.0F
        && config.translationStepInit >= config.translationStepLimit
        && config.rotationStepInit >= config.rotationStepLimit;
}

struct State
{
    jaguar::Transform2d position = jaguar::Transform2d::Identity();
    sparrow::SampleEval evaluation{};
    Axis axis = Axis::AXIS_HORIZONTAL;
    float translationXStep = 0.0F;
    float translationYStep = 0.0F;
    float rotationStep = 0.0F;
};

bool HasCandidates(const State& state,
                   const sparrow::CoordinateDescentConfig& config) noexcept
{
    return !(state.translationXStep < config.translationStepLimit
             && state.translationYStep < config.translationStepLimit
             && (!config.rotationWiggle
                 || state.rotationStep < config.rotationStepLimit));
}

std::array<jaguar::Transform2d, 2> BuildCandidates(const State& state) noexcept
{
    const float x = static_cast<float>(state.position.translation.x);
    const float y = static_cast<float>(state.position.translation.y);
    const float rotation = static_cast<float>(state.position.rotationRad);
    float firstX = x;
    float firstY = y;
    float firstRotation = rotation;
    float secondX = x;
    float secondY = y;
    float secondRotation = rotation;

    switch (state.axis)
    {
    case Axis::AXIS_HORIZONTAL:
        firstX += state.translationXStep;
        secondX -= state.translationXStep;
        break;
    case Axis::AXIS_VERTICAL:
        firstY += state.translationYStep;
        secondY -= state.translationYStep;
        break;
    case Axis::AXIS_FORWARD_DIAGONAL:
        firstX += state.translationXStep;
        firstY += state.translationYStep;
        secondX -= state.translationXStep;
        secondY -= state.translationYStep;
        break;
    case Axis::AXIS_BACKWARD_DIAGONAL:
        firstX -= state.translationXStep;
        firstY += state.translationYStep;
        secondX += state.translationXStep;
        secondY -= state.translationYStep;
        break;
    case Axis::AXIS_WIGGLE:
        firstRotation += state.rotationStep;
        secondRotation -= state.rotationStep;
        break;
    }

    return {jaguar::Transform2d::FromRotationTranslation(
                static_cast<double>(firstRotation),
                {static_cast<double>(firstX), static_cast<double>(firstY)}),
            jaguar::Transform2d::FromRotationTranslation(
                static_cast<double>(secondRotation),
                {static_cast<double>(secondX), static_cast<double>(secondY)})};
}

void UpdateSteps(State& state, float multiplier) noexcept
{
    switch (state.axis)
    {
    case Axis::AXIS_HORIZONTAL:
        state.translationXStep *= multiplier;
        break;
    case Axis::AXIS_VERTICAL:
        state.translationYStep *= multiplier;
        break;
    case Axis::AXIS_FORWARD_DIAGONAL:
    case Axis::AXIS_BACKWARD_DIAGONAL:
    {
        const float diagonalMultiplier = std::sqrt(multiplier);
        state.translationXStep *= diagonalMultiplier;
        state.translationYStep *= diagonalMultiplier;
        break;
    }
    case Axis::AXIS_WIGGLE:
        state.rotationStep *= multiplier;
        break;
    }
}
}

namespace sparrow
{
Result<std::pair<jaguar::Transform2d, SampleEval>> RefineCoordinateDescent(
    std::pair<jaguar::Transform2d, SampleEval> initial,
    SampleEvaluator& evaluator,
    CoordinateDescentConfig config,
    SparrowRng& rng)
{
    if (!IsValidCoordinateConfig(config))
    {
        return InvalidCoordinateConfig("coordinate descent configuration is invalid");
    }
    if (!std::isfinite(initial.first.rotationRad)
        || !std::isfinite(initial.first.translation.x)
        || !std::isfinite(initial.first.translation.y))
    {
        return InvalidCoordinateConfig("coordinate descent initial transform is invalid");
    }
    if (config.maxEvaluations == 0U)
    {
        return CoordinateTimeLimit("coordinate descent evaluation budget is zero");
    }

    State state;
    state.position = initial.first;
    state.evaluation = initial.second;
    state.axis = ChooseAxis(rng, config.rotationWiggle);
    state.translationXStep = config.translationStepInit;
    state.translationYStep = config.translationStepInit;
    state.rotationStep = config.rotationStepInit;

    const std::size_t initialEvaluationCount = evaluator.EvaluationCount();
    while (HasCandidates(state, config))
    {
        const std::size_t currentEvaluationCount = evaluator.EvaluationCount();
        if (currentEvaluationCount < initialEvaluationCount)
        {
            return InvalidCoordinateConfig("evaluator count moved backwards");
        }
        const std::size_t usedEvaluations =
            currentEvaluationCount - initialEvaluationCount;
        if (usedEvaluations >= config.maxEvaluations)
        {
            return CoordinateTimeLimit("coordinate descent evaluation budget exceeded");
        }

        const auto candidateTransforms = BuildCandidates(state);
        std::array<std::pair<jaguar::Transform2d, SampleEval>, 2> evaluated{};
        for (std::size_t index = 0; index < evaluated.size(); ++index)
        {
            const std::size_t beforeEvaluation = evaluator.EvaluationCount();
            if (beforeEvaluation < initialEvaluationCount
                || beforeEvaluation - initialEvaluationCount >= config.maxEvaluations)
            {
                return CoordinateTimeLimit("coordinate descent evaluation budget exceeded");
            }
            evaluated[index].first = candidateTransforms[index];
            evaluated[index].second = evaluator.Evaluate(
                candidateTransforms[index], std::optional<SampleEval>{state.evaluation});
        }

        const std::size_t bestIndex =
            IsBetterSample(evaluated[1].second, evaluated[0].second) ? 1U : 0U;
        const auto& best = evaluated[bestIndex];
        const bool better = IsBetterSample(best.second, state.evaluation);
        const bool worse = IsBetterSample(state.evaluation, best.second);
        if (!worse)
        {
            state.position = best.first;
            state.evaluation = best.second;
        }

        UpdateSteps(state, better ? 1.1F : 0.5F);
        if (!better)
        {
            state.axis = ChooseAxis(rng, config.rotationWiggle);
        }
    }

    return Result<std::pair<jaguar::Transform2d, SampleEval>>::FromValue(
        {state.position, state.evaluation});
}
}
