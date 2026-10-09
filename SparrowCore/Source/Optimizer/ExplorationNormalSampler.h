#pragma once

#include "SparrowCore/Random/SparrowRng.h"

namespace sparrow::detail
{
float RustNormalF32(SparrowRng& rng, float standardDeviation);
}
