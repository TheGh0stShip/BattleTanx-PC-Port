#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/level.hpp"

#include <array>
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

struct InputBindings {
    std::array<std::uint32_t,17> words{};
};

struct DriveInput {
    float throttle{};
    float rotation{};
    float aim{};
    bool fire{};
    bool fire_pressed{};
    bool special{};
    bool special_pressed{};
    bool brake{};
};

struct PlayerTank {
    float x{};
    float y{};
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

using PlayerProjectilePool=std::array<PlayerProjectile,32>;

void advance_planar_motion(PlanarMotion& motion, float elapsed_seconds);
PlayerInput translate_player_input(const ControllerPad& current,
                                   const ControllerPad& previous);
InputBindings load_default_input_bindings(const Rom& rom);
DriveInput translate_drive_input(const PlayerInput& input,const InputBindings& bindings);
PlayerTank spawn_player_tank(const WorldLevel& world,const NavigationLevel& navigation);
void update_player_tank(PlayerTank& tank,const DriveInput& input,
                        const WorldLevel& world,const NavigationLevel& navigation,
                        const AngleTable& angles);
bool try_fire_player_projectile(PlayerProjectilePool& projectiles,PlayerTank& tank,
                                const DriveInput& input,const WorldLevel& world,
                                const NavigationLevel& navigation,const AngleTable& angles,
                                std::int32_t game_tick);
void update_player_projectiles(PlayerProjectilePool& projectiles,
                               const WorldLevel& world,const NavigationLevel& navigation,
                               const AngleTable& angles);

static_assert(sizeof(PlanarMotion)==0x30);

} // namespace battletanx

