#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/Optimizer/LbfLoss.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Collision/HazardFilter.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace sparrow
{
struct SearchResult
{
    jaguar::Transform2d transform = jaguar::Transform2d::Identity();
    LbfLoss loss{};
    std::size_t samples = 0;
    std::vector<LbfLoss> tighteningHistory;
};

SPARROWCORE_API Result<std::optional<SearchResult>> SearchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter);
}
