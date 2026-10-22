#pragma once

#include "battletanx/angle.hpp"

#include <array>
#include <cstdint>

namespace battletanx {

struct PlanarPoint {
    float x{};
    float z{};
};

struct RotatedRect {
    std::int16_t origin_z{};
    std::int16_t origin_x{};
    std::int16_t min_u{};
    std::int16_t max_u{};
    std::int16_t min_v{};
    std::int16_t max_v{};
    std::uint16_t angle{};
};

std::array<PlanarPoint,4> rotated_rect_corners(
    const RotatedRect& rect,const AngleTable& angles);

} // namespace battletanx

