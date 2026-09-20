#include "JaguarCore/problems/mspp.h"

namespace jaguar
{
JAGUARCORE_API double MSPSolution::density() const
{
    double containerArea = 0.0;
    double itemArea = 0.0;
    for (const MSPLayoutSnapshot& snapshot : layouts)
    {
        containerArea += snapshot.strip.fixedHeight * snapshot.strip.width;
        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            itemArea += placement.placedItem.area();
        }
    }
    return containerArea > 0.0 ? itemArea / containerArea : 0.0;
}

JAGUARCORE_API double MSPSolution::totalStripWidth() const noexcept
{
    double result = 0.0;
    for (const MSPLayoutSnapshot& snapshot : layouts)
        result += snapshot.strip.width;
    return result;
}
}
