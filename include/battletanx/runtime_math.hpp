#pragma once

#include "battletanx/angle.hpp"

#include <cstdint>

namespace battletanx {

// Deterministic 257-entry sine table computed from a fixed polynomial using
// only IEEE basic operations, for headless runs without the retail table.
// It is a host reference, not retail data.
[[nodiscard]] AngleTable reference_angle_table();

// Binary angle (65536 = 360 degrees) of the direction (dx, dz) with the
// retail convention forward = (sin a, cos a). Uses a fixed polynomial, so it
// is bit-identical across hosts. Returns 0 for a zero vector.
[[nodiscard]] std::uint16_t direction_angle(float dx,float dz) noexcept;

// Signed shortest difference target - current in binary-angle units.
[[nodiscard]] constexpr std::int32_t angle_delta(std::uint16_t current,std::uint16_t target) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(target-current));
}

} // namespace battletanx
