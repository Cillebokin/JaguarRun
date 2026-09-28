#include "JaguarCore/Problems/Bpp.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace jaguar
{
namespace
{
constexpr std::size_t kLayoutKeyPartBits = sizeof(std::size_t) * 4U;
constexpr std::size_t kLayoutKeyPartMask =
    std::numeric_limits<std::size_t>::max() >> kLayoutKeyPartBits;

std::vector<const BPLayoutSnapshot*> layoutsInSlotOrder(
    const std::vector<BPLayoutSnapshot>& layouts)
{
    std::vector<const BPLayoutSnapshot*> ordered;
    ordered.reserve(layouts.size());
    for (const BPLayoutSnapshot& layout : layouts)
        ordered.push_back(&layout);
    std::sort(ordered.begin(), ordered.end(),
              [](const BPLayoutSnapshot* left,
                 const BPLayoutSnapshot* right) {
                  const std::size_t leftSlot =
                      left->layoutIndex & kLayoutKeyPartMask;
                  const std::size_t rightSlot =
                      right->layoutIndex & kLayoutKeyPartMask;
                  if (leftSlot != rightSlot)
                      return leftSlot < rightSlot;
                  return left->layoutIndex < right->layoutIndex;
              });
    return ordered;
}
}

JAGUARCORE_API double BPSolution::Density() const
{
    const auto orderedLayouts = layoutsInSlotOrder(layouts);
    float totalContainerArea = 0.0f;
    float totalItemArea = 0.0f;
    for (const BPLayoutSnapshot* snapshot : orderedLayouts)
    {
        totalContainerArea += static_cast<float>(
            snapshot->layoutSnapshot.container.Area());
        totalItemArea += snapshot->layoutSnapshot.PlacedItemArea();
    }
    return static_cast<double>(totalItemArea / totalContainerArea);
}

JAGUARCORE_API std::uint64_t BPSolution::Cost(const BPInstance& instance) const
{
    std::uint64_t result = 0;
    for (const BPLayoutSnapshot& snapshot : layouts)
    {
        if (snapshot.binIndex < instance.BinCount())
            result += instance.GetBin(snapshot.binIndex).cost;
    }
    return result;
}
}
