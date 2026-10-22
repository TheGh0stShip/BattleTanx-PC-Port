#include "battletanx/gameplay.hpp"

namespace battletanx {
namespace {
constexpr double kStationaryThreshold=0.5;

std::uint32_t merge_direction_buttons(std::uint16_t buttons) {
    const auto value=static_cast<std::uint32_t>(buttons);
    return value | ((value&0x0F00U)>>8U) | ((value&0x000FU)<<8U);
}
} // namespace

void advance_planar_motion(PlanarMotion& motion,float elapsed_seconds) {
    motion.velocity_x+=motion.acceleration_scale*motion.acceleration_x*elapsed_seconds;
    motion.velocity_z+=motion.acceleration_scale*motion.acceleration_z*elapsed_seconds;

    const float velocity_x=motion.velocity_x;
    const float absolute_x=velocity_x<0.0F ? -velocity_x : velocity_x;
    motion.velocity_x=absolute_x<kStationaryThreshold ? 0.0F : velocity_x;
    const float velocity_z=motion.velocity_z;
    const float absolute_z=velocity_z<0.0F ? -velocity_z : velocity_z;
    motion.velocity_z=absolute_z<kStationaryThreshold ? 0.0F : velocity_z;

    motion.turn_rate+=motion.turn_scale*motion.turn_acceleration*elapsed_seconds;
    motion.x+=motion.velocity_x*elapsed_seconds;
    motion.z+=motion.velocity_z*elapsed_seconds;
    motion.heading_degrees+=motion.turn_rate*elapsed_seconds;
    if (motion.heading_degrees>360.0F) {
        do motion.heading_degrees-=360.0F;
        while (motion.heading_degrees>360.0F);
    }
    while (motion.heading_degrees<0.0F) motion.heading_degrees+=360.0F;
}

PlayerInput translate_player_input(const ControllerPad& current,
                                   const ControllerPad& previous) {
    const std::uint32_t held=merge_direction_buttons(current.buttons);
    const std::uint32_t prior=merge_direction_buttons(previous.buttons);
    return PlayerInput{
        current.stick_x,
        current.stick_y,
        held,
        held&~prior,
        prior&~held,
    };
}

} // namespace battletanx

