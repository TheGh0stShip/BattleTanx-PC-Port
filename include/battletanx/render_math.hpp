#pragma once

#include "battletanx/camera.hpp"

#include <array>
#include <cstdint>

namespace battletanx {

using Matrix4=std::array<float,16>;

Matrix4 identity_matrix() noexcept;
Matrix4 multiply_matrix(const Matrix4& left,const Matrix4& right) noexcept;
Matrix4 yaw_translation_matrix(std::uint16_t angle,float x,float y,float z,
                               const AngleTable& angles) noexcept;
Matrix4 terrain_yaw_translation_matrix(
    std::uint16_t angle,float x,float y,float z,const std::array<float,3>& normal,
    const AngleTable& angles) noexcept;
Matrix4 look_at_matrix(const CameraVector& eye,const CameraVector& target,
                       const CameraVector& up);
Matrix4 perspective_matrix(float vertical_fov_degrees,float aspect_ratio,
                           float near_plane,float far_plane);

} // namespace battletanx
