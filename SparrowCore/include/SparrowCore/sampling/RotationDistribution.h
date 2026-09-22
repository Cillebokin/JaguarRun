#pragma once

#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Entities/Item.h"

#include <vector>

namespace sparrow
{
class SPARROWCORE_API RotationDistribution
{
public:
    static Result<RotationDistribution> FromItem(const jaguar::Item& item);

    float Sample(SparrowRng& rng) const noexcept;

private:
    enum class Kind
    {
        ROTATION_KIND_NONE,
        ROTATION_KIND_DISCRETE,
        ROTATION_KIND_CONTINUOUS,
    };

    RotationDistribution(Kind kind, std::vector<float> discreteRadians);

    Kind m_Kind = Kind::ROTATION_KIND_NONE;
    std::vector<float> m_DiscreteRadians;
};
}
