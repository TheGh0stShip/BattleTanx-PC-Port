#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/level.hpp"

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

struct PlayerTank {
    float x{};
    float z{};
    float velocity_x{};
    float velocity_z{};
    float heading_degrees{};
    float turret_degrees{};
    std::uint16_t navigation_triangle{};
    std::int32_t primary_last_tick{-1000};
    std::int16_t primary_ammo{70};
};

struct PlayerProjectile {
    float x{};
    float z{};
    float velocity_x{};
    float velocity_z{};
    std::uint16_t navigation_triangle{};
    std::uint16_t frames_remaining{};
    bool active{};
};

void advance_planar_motion(PlanarMotion& motion, float elapsed_seconds);
PlayerInput translate_player_input(const ControllerPad& current,
                                   const ControllerPad& previous);
PlayerTank spawn_player_tank(const WorldLevel& world,const NavigationLevel& navigation);
void update_player_tank(PlayerTank& tank,const PlayerInput& input,
                        const NavigationLevel& navigation,const AngleTable& angles);
bool try_fire_player_projectile(PlayerProjectile& projectile,PlayerTank& tank,
                                const PlayerInput& input,const NavigationLevel& navigation,
                                const AngleTable& angles,std::int32_t game_tick);
void update_player_projectile(PlayerProjectile& projectile,
                              const NavigationLevel& navigation);

static_assert(sizeof(PlanarMotion)==0x30);

} // namespace battletanx

