#include "JaguarCore/Problems/Spp.h"

namespace jaguar
{
JAGUARCORE_API double SPSolution::Density() const
{
    const double containerArea = layoutSnapshot.container.Area();
    if (containerArea <= 0.0)
        return 0.0;

    double area = 0.0;
    for (const LayoutPlacementSnapshot& placement : layoutSnapshot.placements)
        area += placement.placedItem.Area();
    return area / containerArea;
}

JAGUARCORE_API double SPSolution::StripWidth() const noexcept
{
    return strip.width;
}
}
