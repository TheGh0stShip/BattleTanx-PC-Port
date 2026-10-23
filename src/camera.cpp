#include "battletanx/camera.hpp"
#include "battletanx/geometry.hpp"
#include "battletanx/render_math.hpp"

#include <algorithm>
#include <cstdint>

namespace battletanx {
namespace {
CameraVector transform(const Matrix4& matrix,float local_x,float local_y,float local_z) {
    return CameraVector{
        matrix[0]*local_x+matrix[1]*local_y+matrix[2]*local_z+matrix[3],
        matrix[4]*local_x+matrix[5]*local_y+matrix[6]*local_z+matrix[7],
        matrix[8]*local_x+matrix[9]*local_y+matrix[10]*local_z+matrix[11],
    };
}

bool camera_blocking_type(std::int32_t type) {
    return type==1 || type==19 || type==26 || type==27 || type==30;
}

CameraPose build_camera(const PlayerTank& tank,const AngleTable& angles,
                        const std::array<float,3>& ground_normal,
                        const WorldLevel* world) {
    const auto heading=static_cast<std::uint16_t>(
        static_cast<std::int32_t>(tank.heading_degrees*182.04445F));
    const auto anchor=terrain_yaw_translation_matrix(
        heading,tank.x,tank.y,tank.z,ground_normal,angles);
    CameraPose pose;
    pose.eye=transform(anchor,0.0F,84.0F,-303.0F);
    if (world!=nullptr) {
        float nearest=1.0F;
        for (const auto& item:world->items) {
            if (!camera_blocking_type(item.type) || item.shape<0 ||
                static_cast<std::size_t>(item.shape)>=world->shapes.size()) continue;
            const auto& shape=world->shapes[static_cast<std::size_t>(item.shape)];
            const auto hit=segment_oriented_rect_hit_fraction(
                {tank.x,tank.z},{pose.eye.x,pose.eye.z},
                {item.x,item.z,static_cast<float>(shape.min_x),
                 static_cast<float>(shape.max_x),static_cast<float>(shape.min_z),
                 static_cast<float>(shape.max_z),item.angle},angles);
            // The anchor can legitimately begin inside a broad object footprint.
            // Retail's camera trace starts outside the owning unit, so do not let
            // a zero-distance containment collapse the eye onto the tank.
            if (hit && *hit>0.0001F) nearest=std::min(nearest,*hit);
        }
        pose.eye.x=tank.x+(pose.eye.x-tank.x)*nearest;
        pose.eye.y=tank.y+(pose.eye.y-tank.y)*nearest;
        pose.eye.z=tank.z+(pose.eye.z-tank.z)*nearest;
    }
    pose.eye.x=tank.x+(pose.eye.x-tank.x)*0.8F;
    pose.eye.z=tank.z+(pose.eye.z-tank.z)*0.8F;
    pose.target=transform(anchor,0.0F,-630.0F,4227.0F);
    pose.up={anchor[1],anchor[5],anchor[9]};
    return pose;
}
} // namespace

CameraPose build_flat_tank_camera(const PlayerTank& tank,const AngleTable& angles) {
    return build_tank_camera(tank,angles,{0.0F,1.0F,0.0F});
}

CameraPose build_tank_camera(const PlayerTank& tank,const AngleTable& angles,
                             const std::array<float,3>& ground_normal) {
    return build_camera(tank,angles,ground_normal,nullptr);
}

CameraPose build_tank_camera(const PlayerTank& tank,const AngleTable& angles,
                             const std::array<float,3>& ground_normal,
                             const WorldLevel& world) {
    return build_camera(tank,angles,ground_normal,&world);
}

} // namespace battletanx
