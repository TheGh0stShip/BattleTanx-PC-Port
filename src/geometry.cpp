#include "battletanx/geometry.hpp"

namespace battletanx {

std::array<PlanarPoint,4> rotated_rect_corners(
    const RotatedRect& rect,const AngleTable& angles) {
    const float sine=-angles.sine(rect.angle);
    const float cosine=angles.cosine(rect.angle);
    const auto point=[&](std::int16_t u,std::int16_t v) {
        return PlanarPoint{
            sine*static_cast<float>(u)+cosine*static_cast<float>(v)+rect.origin_x,
            cosine*static_cast<float>(u)-sine*static_cast<float>(v)+rect.origin_z,
        };
    };
    const std::array corners{
        point(rect.min_u,rect.min_v),
        point(rect.min_u,rect.max_v),
        point(rect.max_u,rect.max_v),
        point(rect.max_u,rect.min_v),
    };
    std::size_t first=0U;
    for (std::size_t index=1U; index<corners.size(); ++index) {
        if (corners[index].z<corners[first].z) first=index;
    }
    std::array<PlanarPoint,4> ordered{};
    for (std::size_t index=0U; index<ordered.size(); ++index) {
        ordered[index]=corners[(index+first)&3U];
    }
    return ordered;
}

} // namespace battletanx

