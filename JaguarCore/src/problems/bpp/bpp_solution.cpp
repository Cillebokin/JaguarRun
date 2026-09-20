#include "JaguarCore/problems/bpp.h"

namespace jaguar
{
JAGUARCORE_API double BPSolution::density() const
{
    double totalContainerArea = 0.0;
    double totalItemArea = 0.0;
    for (const BPLayoutSnapshot& snapshot : layouts)
    {
        totalContainerArea += snapshot.layoutSnapshot.container.area();
        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            totalItemArea += placement.placedItem.area();
        }
    }
    return totalContainerArea > 0.0 ? totalItemArea / totalContainerArea : 0.0;
}

JAGUARCORE_API std::uint64_t BPSolution::cost(const BPInstance& instance) const
{
    std::uint64_t result = 0;
    for (const BPLayoutSnapshot& snapshot : layouts)
    {
        if (snapshot.binIndex < instance.binCount())
            result += instance.bin(snapshot.binIndex).cost;
    }
    return result;
}
}
