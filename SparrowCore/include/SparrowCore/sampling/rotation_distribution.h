#pragma once

#include "SparrowCore/random/sparrow_rng.h"
#include "SparrowCore/result_alias.h"

#include "JaguarCore/entities/item.h"

#include <vector>

namespace sparrow
{
class SPARROWCORE_API RotationDistribution
{
public:
    static Result<RotationDistribution> fromItem(const jaguar::Item& item);

    float sample(SparrowRng& rng) const noexcept;

private:
    enum class Kind
    {
        None,
        Discrete,
        Continuous,
    };

    RotationDistribution(Kind kind, std::vector<float> discreteRadians);

    Kind m_Kind = Kind::None;
    std::vector<float> m_DiscreteRadians;
};
}
