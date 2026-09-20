#pragma once

#include "SparrowCore/export.h"

#include <array>
#include <cstdint>

namespace sparrow
{
class SPARROWCORE_API SparrowRng
{
public:
    explicit SparrowRng(std::uint64_t seed) noexcept;

    std::uint64_t nextU64() noexcept;
    std::uint32_t nextU32() noexcept;
    float uniform01F32() noexcept;
    float normal01F32();

    std::array<std::uint64_t, 4> state() const noexcept;

private:
    std::array<std::uint64_t, 4> m_State{};
    bool m_HasNormalSpare = false;
    float m_NormalSpare = 0.0F;
};
}
