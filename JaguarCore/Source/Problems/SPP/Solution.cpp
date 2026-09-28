#include "JaguarCore/Problems/Spp.h"

namespace jaguar
{
JAGUARCORE_API double SPSolution::Density() const
{
    const float containerArea =
        static_cast<float>(layoutSnapshot.container.Area());

    const float area = layoutSnapshot.PlacedItemArea();
    return static_cast<double>(area / containerArea);
}

JAGUARCORE_API double SPSolution::StripWidth() const noexcept
{
    return strip.width;
}
}
