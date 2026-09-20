#include "SparrowCore/sampling/coordinate_descent.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{
using CoordinateResult = sparrow::Result<
    std::pair<jaguar::Transform2d, sparrow::SampleEval>>;

CoordinateResult invalidCoordinateConfig(const char* message)
{
    return CoordinateResult::fromError({jaguar::ErrorCode::InvalidInput, message});
}

CoordinateResult coordinateTimeLimit(const char* message)
{
    return CoordinateResult::fromError({jaguar::ErrorCode::TimeLimit, message});
}

enum class Axis
{
    Horizontal,
    Vertical,
    ForwardDiagonal,
    BackwardDiagonal,
    Wiggle,
};

std::uint64_t boundedIndex(sparrow::SparrowRng& rng, std::uint64_t bound) noexcept
{
    const std::uint64_t threshold =
        (std::numeric_limits<std::uint64_t>::max() % bound + 1U) % bound;
    std::uint64_t value = 0;
    do
    {
        value = rng.nextU64();
    } while (value < threshold);
    return value % bound;
}

Axis chooseAxis(sparrow::SparrowRng& rng, bool rotationWiggle) noexcept
{
    const std::uint64_t selected = boundedIndex(rng, rotationWiggle ? 6U : 4U);
    switch (selected)
    {
    case 0U:
        return Axis::Horizontal;
    case 1U:
        return Axis::Vertical;
    case 2U:
        return Axis::ForwardDiagonal;
    case 3U:
        return Axis::BackwardDiagonal;
    case 4U:
    case 5U:
        return Axis::Wiggle;
    }
    return Axis::Horizontal;
}

bool validCoordinateConfig(const sparrow::CoordinateDescentConfig& config) noexcept
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
    jaguar::Transform2d position = jaguar::Transform2d::identity();
    sparrow::SampleEval evaluation{};
    Axis axis = Axis::Horizontal;
    float translationXStep = 0.0F;
    float translationYStep = 0.0F;
    float rotationStep = 0.0F;
};

bool hasCandidates(const State& state,
                   const sparrow::CoordinateDescentConfig& config) noexcept
{
    return !(state.translationXStep < config.translationStepLimit
             && state.translationYStep < config.translationStepLimit
             && (!config.rotationWiggle
                 || state.rotationStep < config.rotationStepLimit));
}

std::array<jaguar::Transform2d, 2> candidates(const State& state) noexcept
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
    case Axis::Horizontal:
        firstX += state.translationXStep;
        secondX -= state.translationXStep;
        break;
    case Axis::Vertical:
        firstY += state.translationYStep;
        secondY -= state.translationYStep;
        break;
    case Axis::ForwardDiagonal:
        firstX += state.translationXStep;
        firstY += state.translationYStep;
        secondX -= state.translationXStep;
        secondY -= state.translationYStep;
        break;
    case Axis::BackwardDiagonal:
        firstX -= state.translationXStep;
        firstY += state.translationYStep;
        secondX += state.translationXStep;
        secondY -= state.translationYStep;
        break;
    case Axis::Wiggle:
        firstRotation += state.rotationStep;
        secondRotation -= state.rotationStep;
        break;
    }

    return {jaguar::Transform2d::fromRotationTranslation(
                static_cast<double>(firstRotation),
                {static_cast<double>(firstX), static_cast<double>(firstY)}),
            jaguar::Transform2d::fromRotationTranslation(
                static_cast<double>(secondRotation),
                {static_cast<double>(secondX), static_cast<double>(secondY)})};
}

void updateSteps(State& state, float multiplier) noexcept
{
    switch (state.axis)
    {
    case Axis::Horizontal:
        state.translationXStep *= multiplier;
        break;
    case Axis::Vertical:
        state.translationYStep *= multiplier;
        break;
    case Axis::ForwardDiagonal:
    case Axis::BackwardDiagonal:
    {
        const float diagonalMultiplier = std::sqrt(multiplier);
        state.translationXStep *= diagonalMultiplier;
        state.translationYStep *= diagonalMultiplier;
        break;
    }
    case Axis::Wiggle:
        state.rotationStep *= multiplier;
        break;
    }
}
}

namespace sparrow
{
Result<std::pair<jaguar::Transform2d, SampleEval>> refineCoordinateDescent(
    std::pair<jaguar::Transform2d, SampleEval> initial,
    SampleEvaluator& evaluator,
    CoordinateDescentConfig config,
    SparrowRng& rng)
{
    if (!validCoordinateConfig(config))
    {
        return invalidCoordinateConfig("coordinate descent configuration is invalid");
    }
    if (!std::isfinite(initial.first.rotationRad)
        || !std::isfinite(initial.first.translation.x)
        || !std::isfinite(initial.first.translation.y))
    {
        return invalidCoordinateConfig("coordinate descent initial transform is invalid");
    }
    if (config.maxEvaluations == 0U)
    {
        return coordinateTimeLimit("coordinate descent evaluation budget is zero");
    }

    State state;
    state.position = initial.first;
    state.evaluation = initial.second;
    state.axis = chooseAxis(rng, config.rotationWiggle);
    state.translationXStep = config.translationStepInit;
    state.translationYStep = config.translationStepInit;
    state.rotationStep = config.rotationStepInit;

    const std::size_t initialEvaluationCount = evaluator.evaluationCount();
    while (hasCandidates(state, config))
    {
        const std::size_t currentEvaluationCount = evaluator.evaluationCount();
        if (currentEvaluationCount < initialEvaluationCount)
        {
            return invalidCoordinateConfig("evaluator count moved backwards");
        }
        const std::size_t usedEvaluations =
            currentEvaluationCount - initialEvaluationCount;
        if (usedEvaluations >= config.maxEvaluations)
        {
            return coordinateTimeLimit("coordinate descent evaluation budget exceeded");
        }

        const auto candidateTransforms = candidates(state);
        std::array<std::pair<jaguar::Transform2d, SampleEval>, 2> evaluated{};
        for (std::size_t index = 0; index < evaluated.size(); ++index)
        {
            const std::size_t beforeEvaluation = evaluator.evaluationCount();
            if (beforeEvaluation < initialEvaluationCount
                || beforeEvaluation - initialEvaluationCount >= config.maxEvaluations)
            {
                return coordinateTimeLimit("coordinate descent evaluation budget exceeded");
            }
            evaluated[index].first = candidateTransforms[index];
            evaluated[index].second = evaluator.evaluate(
                candidateTransforms[index], std::optional<SampleEval>{state.evaluation});
        }

        const std::size_t bestIndex =
            betterSample(evaluated[1].second, evaluated[0].second) ? 1U : 0U;
        const auto& best = evaluated[bestIndex];
        const bool better = betterSample(best.second, state.evaluation);
        const bool worse = betterSample(state.evaluation, best.second);
        if (!worse)
        {
            state.position = best.first;
            state.evaluation = best.second;
        }

        updateSteps(state, better ? 1.1F : 0.5F);
        if (!better)
        {
            state.axis = chooseAxis(rng, config.rotationWiggle);
        }
    }

    return Result<std::pair<jaguar::Transform2d, SampleEval>>::fromValue(
        {state.position, state.evaluation});
}
}
