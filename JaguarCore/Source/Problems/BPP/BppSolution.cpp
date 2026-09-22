#include "JaguarCore/Problems/Bpp.h"

namespace jaguar
{
JAGUARCORE_API double BPSolution::Density() const
{
    double totalContainerArea = 0.0;
    double totalItemArea = 0.0;
    for (const BPLayoutSnapshot& snapshot : layouts)
    {
        totalContainerArea += snapshot.layoutSnapshot.container.Area();
        for (const LayoutPlacementSnapshot& placement :
             snapshot.layoutSnapshot.placements)
        {
            totalItemArea += placement.placedItem.Area();
        }
    }
    return totalContainerArea > 0.0 ? totalItemArea / totalContainerArea : 0.0;
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
