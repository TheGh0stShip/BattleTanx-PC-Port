#include "battletanx/camera.hpp"

#include <cstdint>

namespace battletanx {
namespace {
CameraVector transform_yaw(float local_x,float local_y,float local_z,
                           float origin_x,float origin_z,
                           float sine,float cosine) {
    return CameraVector{
        origin_x+local_x*cosine+local_z*sine,
        local_y,
        origin_z-local_x*sine+local_z*cosine,
    };
}
} // namespace

CameraPose build_flat_tank_camera(const PlayerTank& tank,const AngleTable& angles) {
    const auto heading=static_cast<std::uint16_t>(
        static_cast<std::int32_t>(tank.heading_degrees*182.04445F));
    const float sine=angles.sine(heading);
    const float cosine=angles.cosine(heading);
    CameraPose pose;
    pose.eye=transform_yaw(0.0F,84.0F,-303.0F,tank.x,tank.z,sine,cosine);
    pose.eye.x=tank.x+(pose.eye.x-tank.x)*0.8F;
    pose.eye.z=tank.z+(pose.eye.z-tank.z)*0.8F;
    pose.target=transform_yaw(0.0F,-630.0F,4227.0F,tank.x,tank.z,sine,cosine);
    return pose;
}

} // namespace battletanx
