#pragma once

#include "SparrowCore/Optimizer/Compression.h"

namespace sparrow::detail
{
float CompressionMinimumStripWidthForTrace(
    const jaguar::SPInstance& instance,
    const jaguar::SPSolution& solution) noexcept;
}
