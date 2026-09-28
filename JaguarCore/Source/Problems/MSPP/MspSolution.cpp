#include "JaguarCore/Problems/MSpp.h"

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

std::vector<const MSPLayoutSnapshot*> layoutsInSlotOrder(
    const std::vector<MSPLayoutSnapshot>& layouts)
{
    std::vector<const MSPLayoutSnapshot*> ordered;
    ordered.reserve(layouts.size());
    for (const MSPLayoutSnapshot& layout : layouts)
        ordered.push_back(&layout);
    std::sort(ordered.begin(), ordered.end(),
              [](const MSPLayoutSnapshot* left,
                 const MSPLayoutSnapshot* right) {
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

JAGUARCORE_API double MSPSolution::Density() const
{
    const auto orderedLayouts = layoutsInSlotOrder(layouts);
    float containerArea = 0.0f;
    float itemArea = 0.0f;
    for (const MSPLayoutSnapshot* snapshot : orderedLayouts)
    {
        containerArea += static_cast<float>(snapshot->strip.fixedHeight) *
                         static_cast<float>(snapshot->strip.width);
        itemArea += snapshot->layoutSnapshot.PlacedItemArea();
    }
    return static_cast<double>(itemArea / containerArea);
}

JAGUARCORE_API double MSPSolution::TotalStripWidth() const
{
    float result = 0.0F;
    for (const MSPLayoutSnapshot* snapshot : layoutsInSlotOrder(layouts))
        result += static_cast<float>(snapshot->strip.width);
    return static_cast<double>(result);
}
}
