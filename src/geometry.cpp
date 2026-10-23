#include "battletanx/geometry.hpp"

namespace battletanx {

void rotated_rect_corners(const RotatedRect& rect,const AngleTable& angles,
                          std::array<PlanarPoint,4>& output) {
    const float sine=-angles.sine(rect.angle);
    const float cosine=angles.cosine(rect.angle);
    const float origin_x=static_cast<float>(rect.origin_x);
    const float origin_z=static_cast<float>(rect.origin_z);
    const float min_u=static_cast<float>(rect.min_u);
    const float max_u=static_cast<float>(rect.max_u);
    const float min_v=static_cast<float>(rect.min_v);
    const float max_v=static_cast<float>(rect.max_v);
    std::array<PlanarPoint,4> corners{};
    corners[0].z=cosine*min_u-sine*min_v+origin_z;
    corners[0].x=sine*min_u+cosine*min_v+origin_x;
    corners[1].z=cosine*min_u-sine*max_v+origin_z;
    corners[1].x=sine*min_u+cosine*max_v+origin_x;
    corners[2].z=cosine*max_u-sine*max_v+origin_z;
    corners[2].x=sine*max_u+cosine*max_v+origin_x;
    corners[3].z=cosine*max_u-sine*min_v+origin_z;
    corners[3].x=sine*max_u+cosine*min_v+origin_x;
    std::size_t first=0U;
    for (std::size_t index=1U; index<corners.size(); ++index) {
        if (corners[index].z<corners[first].z) first=index;
    }
    for (std::size_t index=0U; index<output.size(); ++index) {
        output[index]=corners[(index+first)&3U];
    }
}

} // namespace battletanx

