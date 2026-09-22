#pragma once

#include "SparrowCore/Export.h"

#include <array>
#include <cstdint>

namespace sparrow
{
class SPARROWCORE_API SparrowRng
{
public:
    explicit SparrowRng(std::uint64_t seed) noexcept;

    std::uint64_t NextU64() noexcept;
    std::uint32_t NextU32() noexcept;
    float Uniform01F32() noexcept;
    float Normal01F32();

    std::array<std::uint64_t, 4> State() const noexcept;

private:
    std::array<std::uint64_t, 4> m_State{};
    bool m_HasNormalSpare = false;
    float m_NormalSpare = 0.0F;
};
}
