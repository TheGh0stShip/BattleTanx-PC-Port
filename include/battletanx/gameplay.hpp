#pragma once

#include <cstdint>

namespace battletanx {

struct PlanarMotion {
    float acceleration_scale{};
    float turn_scale{};
    float reserved{};
    float x{};
    float z{};
    float velocity_x{};
    float velocity_z{};
    float heading_degrees{};
    float turn_rate{};
    float acceleration_x{};
    float acceleration_z{};
    float turn_acceleration{};
};

struct ControllerPad {
    std::uint16_t buttons{};
    std::int8_t stick_x{};
    std::int8_t stick_y{};
};

struct PlayerInput {
    std::int8_t stick_x{};
    std::int8_t stick_y{};
    std::uint32_t held{};
    std::uint32_t pressed{};
    std::uint32_t released{};
};

void advance_planar_motion(PlanarMotion& motion, float elapsed_seconds);
PlayerInput translate_player_input(const ControllerPad& current,
                                   const ControllerPad& previous);

static_assert(sizeof(PlanarMotion)==0x30);

} // namespace battletanx

