#include "battletanx/render_math.hpp"

#include <cmath>
#include <stdexcept>

namespace battletanx {
namespace {

CameraVector subtract(CameraVector first,CameraVector second) {
    return {first.x-second.x,first.y-second.y,first.z-second.z};
}

float dot(CameraVector first,CameraVector second) {
    return first.x*second.x+first.y*second.y+first.z*second.z;
}

CameraVector cross(CameraVector first,CameraVector second) {
    return {first.y*second.z-first.z*second.y,
            first.z*second.x-first.x*second.z,
            first.x*second.y-first.y*second.x};
}

CameraVector normalized(CameraVector value) {
    const float length=std::sqrt(dot(value,value));
    if (!(length>0.000001F)) throw std::runtime_error("render vector has zero length");
    return {value.x/length,value.y/length,value.z/length};
}

} // namespace

Matrix4 identity_matrix() noexcept {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

Matrix4 multiply_matrix(const Matrix4& left,const Matrix4& right) noexcept {
    Matrix4 output{};
    for (std::size_t row=0U; row<4U; ++row) {
        for (std::size_t column=0U; column<4U; ++column) {
            for (std::size_t inner=0U; inner<4U; ++inner) {
                output[row*4U+column]+=left[row*4U+inner]*right[inner*4U+column];
            }
        }
    }
    return output;
}

Matrix4 yaw_translation_matrix(std::uint16_t angle,float x,float y,float z,
                               const AngleTable& angles) noexcept {
    const float sine=angles.sine(angle);
    const float cosine=angles.cosine(angle);
    return {cosine,0,sine,x, 0,1,0,y, -sine,0,cosine,z, 0,0,0,1};
}

Matrix4 look_at_matrix(const CameraVector& eye,const CameraVector& target,
                       const CameraVector& up) {
    const CameraVector forward=normalized(subtract(target,eye));
    const CameraVector side=normalized(cross(forward,up));
    const CameraVector actual_up=cross(side,forward);
    return {side.x,side.y,side.z,-dot(side,eye),
            actual_up.x,actual_up.y,actual_up.z,-dot(actual_up,eye),
            -forward.x,-forward.y,-forward.z,dot(forward,eye),
            0,0,0,1};
}

Matrix4 perspective_matrix(float vertical_fov_degrees,float aspect_ratio,
                           float near_plane,float far_plane) {
    if (!(vertical_fov_degrees>0.0F && vertical_fov_degrees<180.0F) ||
        !(aspect_ratio>0.0F) || !(near_plane>0.0F && far_plane>near_plane)) {
        throw std::runtime_error("render perspective parameters are invalid");
    }
    constexpr float pi=3.14159265358979323846F;
    const float cotangent=1.0F/std::tan(vertical_fov_degrees*pi/360.0F);
    return {cotangent/aspect_ratio,0,0,0,
            0,cotangent,0,0,
            0,0,(near_plane+far_plane)/(near_plane-far_plane),
            (2.0F*near_plane*far_plane)/(near_plane-far_plane),
            0,0,-1,0};
}

} // namespace battletanx
