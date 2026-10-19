#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace sparrow
{
std::chrono::steady_clock::time_point CompressionClockNow() noexcept;
}

namespace sparrow::testing
{
struct CompressionClockSequenceState
{
    std::vector<std::chrono::nanoseconds> samples;
    std::size_t nextSample = 0U;
    bool exhausted = false;
};

class ScopedCompressionClockSequence final
{
public:
    explicit ScopedCompressionClockSequence(
        std::vector<std::chrono::nanoseconds> samples);
    ~ScopedCompressionClockSequence();

    ScopedCompressionClockSequence(const ScopedCompressionClockSequence&) = delete;
    ScopedCompressionClockSequence& operator=(
        const ScopedCompressionClockSequence&) = delete;
    ScopedCompressionClockSequence(ScopedCompressionClockSequence&&) = delete;
    ScopedCompressionClockSequence& operator=(ScopedCompressionClockSequence&&) = delete;

    std::size_t SamplesConsumed() const noexcept;
    bool Exhausted() const noexcept;

private:
    static thread_local CompressionClockSequenceState* s_Current;

    CompressionClockSequenceState m_State;
    CompressionClockSequenceState* m_Previous = nullptr;

    friend std::chrono::steady_clock::time_point
    sparrow::CompressionClockNow() noexcept;
};
}
