#include "battletanx/geometry.hpp"

#include <algorithm>
#include <limits>

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

namespace {

void oriented_corners(const OrientedRect& rect,const AngleTable& angles,
                      std::array<PlanarPoint,4>& corners) {
    const float sine=-angles.sine(rect.angle);
    const float cosine=angles.cosine(rect.angle);
    const std::array<PlanarPoint,4> local{{
        {rect.min_x,rect.min_z},{rect.max_x,rect.min_z},
        {rect.max_x,rect.max_z},{rect.min_x,rect.max_z}}};
    for (std::size_t index=0; index<local.size(); ++index) {
        corners[index].x=rect.origin_x+sine*local[index].z+cosine*local[index].x;
        corners[index].z=rect.origin_z+cosine*local[index].z-sine*local[index].x;
    }
}

bool separated_on_axis(const std::array<PlanarPoint,4>& first,
                       const std::array<PlanarPoint,4>& second,float axis_x,float axis_z) {
    float first_min=std::numeric_limits<float>::max();
    float first_max=std::numeric_limits<float>::lowest();
    float second_min=std::numeric_limits<float>::max();
    float second_max=std::numeric_limits<float>::lowest();
    for (const auto& point:first) {
        const float value=point.x*axis_x+point.z*axis_z;
        first_min=std::min(first_min,value);
        first_max=std::max(first_max,value);
    }
    for (const auto& point:second) {
        const float value=point.x*axis_x+point.z*axis_z;
        second_min=std::min(second_min,value);
        second_max=std::max(second_max,value);
    }
    return first_max<second_min || second_max<first_min;
}

} // namespace

bool oriented_rects_overlap(const OrientedRect& first,const OrientedRect& second,
                            const AngleTable& angles) {
    std::array<PlanarPoint,4> first_corners{};
    std::array<PlanarPoint,4> second_corners{};
    oriented_corners(first,angles,first_corners);
    oriented_corners(second,angles,second_corners);
    for (const auto* corners:{&first_corners,&second_corners}) {
        for (std::size_t edge=0; edge<2U; ++edge) {
            const auto& begin=(*corners)[edge];
            const auto& end=(*corners)[edge+1U];
            if (separated_on_axis(first_corners,second_corners,
                                  -(end.z-begin.z),end.x-begin.x)) return false;
        }
    }
    return true;
}

} // namespace battletanx

