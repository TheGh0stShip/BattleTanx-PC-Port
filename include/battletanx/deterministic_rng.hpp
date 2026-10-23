#pragma once

#include <cstdint>

namespace battletanx {

// The shared retail generator (D_80139610): seed = seed*0x10DCD + 1 modulo 2^32.
// next_random_u32 (func_800F38F4) returns the new seed with its 16-bit halves
// swapped; random_float_range (func_800F3880) returns seed/2^32 * range.
// The retail seed comes from osGetTime(); callers here must supply it.
class RetailRandom {
public:
    constexpr explicit RetailRandom(std::uint32_t seed=0U) noexcept : seed_(seed) {}

    constexpr std::uint32_t next_u32() noexcept {
        seed_=seed_*0x10DCDU+1U;
        return (seed_<<16U)|(seed_>>16U);
    }
    // Uses the advanced seed directly, as random_float_range does.
    constexpr float next_float(float range) noexcept {
        seed_=seed_*0x10DCDU+1U;
        return static_cast<float>(static_cast<double>(seed_)/4294967296.0)*range;
    }
    [[nodiscard]] constexpr std::uint32_t seed() const noexcept { return seed_; }
    constexpr void reseed(std::uint32_t seed) noexcept { seed_=seed; }

private:
    std::uint32_t seed_;
};

} // namespace battletanx
