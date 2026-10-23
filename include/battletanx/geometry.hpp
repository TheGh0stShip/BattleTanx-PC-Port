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

struct OrientedRect {
    float origin_x{};
    float origin_z{};
    float min_x{};
    float max_x{};
    float min_z{};
    float max_z{};
    std::uint16_t angle{};
};

void rotated_rect_corners(const RotatedRect& rect,const AngleTable& angles,
                          std::array<PlanarPoint,4>& output);
bool oriented_rects_overlap(const OrientedRect& first,const OrientedRect& second,
                            const AngleTable& angles);
bool segment_intersects_oriented_rect(PlanarPoint begin,PlanarPoint end,
                                      const OrientedRect& rect,const AngleTable& angles);

} // namespace battletanx

