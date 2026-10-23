#include "battletanx/geometry.hpp"

namespace battletanx {

void rotated_rect_corners(const RotatedRect& rect,const AngleTable& angles,
                          std::array<PlanarPoint,4>& output) {
    const float sine=-angles.sine(rect.angle);
    const float cosine=angles.cosine(rect.angle);
    std::array<PlanarPoint,4> corners{};
    corners[0].z=cosine*rect.min_u-sine*rect.min_v+rect.origin_z;
    corners[0].x=sine*rect.min_u+cosine*rect.min_v+rect.origin_x;
    corners[1].z=cosine*rect.min_u-sine*rect.max_v+rect.origin_z;
    corners[1].x=sine*rect.min_u+cosine*rect.max_v+rect.origin_x;
    corners[2].z=cosine*rect.max_u-sine*rect.max_v+rect.origin_z;
    corners[2].x=sine*rect.max_u+cosine*rect.max_v+rect.origin_x;
    corners[3].z=cosine*rect.max_u-sine*rect.min_v+rect.origin_z;
    corners[3].x=sine*rect.max_u+cosine*rect.min_v+rect.origin_x;
    std::size_t first=0U;
    for (std::size_t index=1U; index<corners.size(); ++index) {
        if (corners[index].z<corners[first].z) first=index;
    }
    for (std::size_t index=0U; index<output.size(); ++index) {
        output[index]=corners[(index+first)&3U];
    }
}

} // namespace battletanx

