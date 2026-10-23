#include "battletanx/camera.hpp"
#include "battletanx/render_math.hpp"

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
} // namespace

CameraPose build_flat_tank_camera(const PlayerTank& tank,const AngleTable& angles) {
    return build_tank_camera(tank,angles,{0.0F,1.0F,0.0F});
}

CameraPose build_tank_camera(const PlayerTank& tank,const AngleTable& angles,
                             const std::array<float,3>& ground_normal) {
    const auto heading=static_cast<std::uint16_t>(
        static_cast<std::int32_t>(tank.heading_degrees*182.04445F));
    const auto anchor=terrain_yaw_translation_matrix(
        heading,tank.x,tank.y,tank.z,ground_normal,angles);
    CameraPose pose;
    pose.eye=transform(anchor,0.0F,84.0F,-303.0F);
    pose.eye.x=tank.x+(pose.eye.x-tank.x)*0.8F;
    pose.eye.z=tank.z+(pose.eye.z-tank.z)*0.8F;
    pose.target=transform(anchor,0.0F,-630.0F,4227.0F);
    pose.up={anchor[1],anchor[5],anchor[9]};
    return pose;
}

} // namespace battletanx
