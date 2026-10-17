#pragma once

#include <cstddef>
#include <vector>

namespace sparrow
{
class SparrowRng;

namespace detail
{
void ShuffleSliceRandom(std::vector<std::size_t>& values,
                        SparrowRng& rng) noexcept;
}
}
