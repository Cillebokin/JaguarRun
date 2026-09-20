#pragma once

#include "SparrowCore/export.h"
#include "SparrowCore/lbf_config.h"
#include "SparrowCore/optimizer/lbf_loss.h"
#include "SparrowCore/random/sparrow_rng.h"
#include "SparrowCore/result_alias.h"

#include "JaguarCore/collision/hazard_filter.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/entities/layout.h"
#include "JaguarCore/geometry/transformation.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace sparrow
{
struct SearchResult
{
    jaguar::Transform2d transform = jaguar::Transform2d::identity();
    LbfLoss loss{};
    std::size_t samples = 0;
    std::vector<LbfLoss> tighteningHistory;
};

SPARROWCORE_API Result<std::optional<SearchResult>> searchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter);
}
