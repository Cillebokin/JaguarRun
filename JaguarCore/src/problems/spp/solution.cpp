#include "JaguarCore/problems/spp.h"

namespace jaguar
{
JAGUARCORE_API double SPSolution::density() const
{
    const double containerArea = layoutSnapshot.container.area();
    if (containerArea <= 0.0)
        return 0.0;

    double area = 0.0;
    for (const LayoutPlacementSnapshot& placement : layoutSnapshot.placements)
        area += placement.placedItem.area();
    return area / containerArea;
}

JAGUARCORE_API double SPSolution::stripWidth() const noexcept
{
    return strip.width;
}
}
