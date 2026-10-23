#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/gameplay.hpp"

#include <array>

namespace battletanx {

struct CameraVector {
    float x{};
    float y{};
    float z{};
};

struct CameraPose {
    CameraVector eye;
    CameraVector target;
    CameraVector up{0.0F,1.0F,0.0F};
    float vertical_fov_degrees{37.0F};
    float aspect_ratio{4.0F/3.0F};
    float near_plane{16.0F};
    float far_plane{5000.0F};
};

CameraPose build_flat_tank_camera(const PlayerTank& tank,const AngleTable& angles);
CameraPose build_tank_camera(const PlayerTank& tank,const AngleTable& angles,
                             const std::array<float,3>& ground_normal);

} // namespace battletanx
