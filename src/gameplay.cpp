#include "battletanx/gameplay.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace battletanx {
namespace {
constexpr double kStationaryThreshold=0.5;
constexpr std::uint32_t button_a=0x8000U;

float wrap_degrees(float value) {
    while (value>360.0F) value-=360.0F;
    while (value<0.0F) value+=360.0F;
    return value;
}

std::uint16_t degrees_to_angle(float degrees) {
    return static_cast<std::uint16_t>(degrees*(65536.0F/360.0F));
}

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

PlayerTank spawn_player_tank(const NavigationLevel& navigation) {
    for (std::size_t index=1U; index<navigation.triangles.size(); ++index) {
        if (navigation.kind_enabled[navigation.triangles[index].kind]!=0U) continue;
        const auto position=navigation_triangle_centroid(
            navigation,static_cast<std::uint16_t>(index));
        return PlayerTank{position.x,position.z,0.0F,0.0F,0.0F,0.0F,
                          static_cast<std::uint16_t>(index)};
    }
    throw std::runtime_error("level has no open player spawn triangle");
}

void update_player_tank(PlayerTank& tank,const PlayerInput& input,
                        const NavigationLevel& navigation,const AngleTable& angles) {
    constexpr float frame_rate_scale=0.15F;
    constexpr float turn_acceleration=0.18F;
    constexpr float drive_acceleration=0.055F;
    constexpr float drag=0.88F;
    const float steering=std::clamp(static_cast<float>(input.stick_x),-80.0F,80.0F);
    const float throttle=std::clamp(static_cast<float>(input.stick_y),-80.0F,80.0F);
    const float turn=steering*turn_acceleration*frame_rate_scale;
    tank.heading_degrees=wrap_degrees(tank.heading_degrees-turn);
    tank.turret_degrees=wrap_degrees(tank.turret_degrees-turn);

    const auto angle=degrees_to_angle(tank.heading_degrees);
    tank.velocity_x=(tank.velocity_x+angles.sine(angle)*throttle*drive_acceleration)*drag;
    tank.velocity_z=(tank.velocity_z+angles.cosine(angle)*throttle*drive_acceleration)*drag;
    const NavigationVertex from{tank.x,tank.z};
    const NavigationVertex candidate{tank.x+tank.velocity_x,tank.z+tank.velocity_z};
    const auto trace=trace_navigation_segment(
        navigation,tank.navigation_triangle,from,candidate);
    if (trace.stop==NavigationTraceStop::target) {
        tank.x=candidate.x;
        tank.z=candidate.z;
        tank.navigation_triangle=trace.triangle;
    } else {
        tank.velocity_x*=-0.3F;
        tank.velocity_z*=-0.3F;
    }
}

bool try_fire_player_projectile(PlayerProjectile& projectile,const PlayerTank& tank,
                                const PlayerInput& input,const AngleTable& angles) {
    if (projectile.active || (input.pressed&button_a)==0U) return false;
    constexpr float muzzle_offset=28.0F;
    constexpr float projectile_speed=18.0F;
    const auto angle=degrees_to_angle(tank.turret_degrees);
    const float direction_x=angles.sine(angle);
    const float direction_z=angles.cosine(angle);
    projectile={tank.x+direction_x*muzzle_offset,tank.z+direction_z*muzzle_offset,
                direction_x*projectile_speed,direction_z*projectile_speed,
                tank.navigation_triangle,120U,true};
    return true;
}

void update_player_projectile(PlayerProjectile& projectile,
                              const NavigationLevel& navigation) {
    if (!projectile.active) return;
    const NavigationVertex from{projectile.x,projectile.z};
    const NavigationVertex candidate{
        projectile.x+projectile.velocity_x,projectile.z+projectile.velocity_z};
    const auto trace=trace_navigation_segment(
        navigation,projectile.navigation_triangle,from,candidate);
    if (trace.stop!=NavigationTraceStop::target || projectile.frames_remaining<=1U) {
        projectile.active=false;
        projectile.frames_remaining=0U;
        return;
    }
    projectile.x=candidate.x;
    projectile.z=candidate.z;
    projectile.navigation_triangle=trace.triangle;
    --projectile.frames_remaining;
}

} // namespace battletanx

