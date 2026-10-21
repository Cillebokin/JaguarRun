#pragma once

#include "SparrowCore/Optimizer/Compression.h"

namespace sparrow::detail
{
Result<float> CompressionMinimumStripWidthForTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution);
}
