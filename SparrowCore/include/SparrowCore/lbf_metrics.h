#pragma once

#include <cstddef>
#include <cstdint>

namespace sparrow
{
struct LbfMetrics
{
    std::uint64_t seed = 0;
    std::size_t sampleCount = 0;
    std::size_t placedItemCount = 0;
    std::size_t remainingDemand = 0;
    double stripWidth = 0.0;
    double density = 0.0;
    bool feasible = false;
};
}
