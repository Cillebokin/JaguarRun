#pragma once

#include "SparrowCore/Optimizer/Exploration.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace sparrow::detail
{
float ExplorationMinimumStripWidthForTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept;

struct ExplorationTraceEvent
{
    enum class Type
    {
        PoolInsert,
        PoolSample,
        DisruptionMove,
    };

    enum class DisruptionMoveKind
    {
        Swap,
        ContainedRelocation,
    };

    Type type = Type::PoolInsert;
    std::size_t failureId = 0U;
    std::size_t insertionIndex = 0U;
    std::vector<std::size_t> poolOrder;
    float rawSample = 0.0F;
    float mappedSample = 0.0F;
    std::size_t selectedIndex = 0U;
    std::size_t consumedU64Words = 0U;
    std::uint32_t firstLayerIndex = 0U;
    std::array<std::uint64_t, 4U> followingU64{};
    DisruptionMoveKind disruptionMoveKind = DisruptionMoveKind::Swap;
    std::size_t itemIndex = 0U;
    std::size_t relatedItemIndex = 0U;
    std::array<float, 3U> fromPose{};
    std::array<float, 3U> toPose{};
};

using ExplorationTraceCallback =
    std::function<void(const ExplorationTraceEvent&)>;

Result<ExplorationResult> ExploreWithTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& initialSolution,
    const ExplorationConfig& config,
    std::uint64_t seed,
    OptimizationTerminator& terminator,
    OptimizationListener* listener,
    const ExplorationTraceCallback& callback);
}
