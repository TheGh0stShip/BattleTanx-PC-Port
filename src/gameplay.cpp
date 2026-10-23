#include "battletanx/gameplay.hpp"
#include "battletanx/big_endian.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace battletanx {
namespace {
constexpr double kStationaryThreshold=0.5;
constexpr std::uint32_t axis_unbound=0x00100000U;
constexpr std::uint32_t axis_stick_x=0x00200000U;
constexpr std::uint32_t axis_stick_y=0x00400000U;

float wrap_degrees(float value) {
    while (value>360.0F) value-=360.0F;
    while (value<0.0F) value+=360.0F;
    return value;
}

std::uint16_t degrees_to_angle(float degrees) {
    return static_cast<std::uint16_t>(degrees*(65536.0F/360.0F));
}

float read_bound_axis(const PlayerInput& input,std::uint32_t positive,
                      std::uint32_t negative) {
    const auto analog=[](std::int8_t value) {
        return std::abs(static_cast<float>(value))>3.0F
            ? static_cast<float>(value) : 0.0F;
    };
    if (positive==axis_stick_x) return analog(input.stick_x);
    if (positive==axis_stick_y) return analog(input.stick_y);
    if (positive==axis_unbound) return 0.0F;
    float value=(input.held&positive)!=0U ? 80.0F : 0.0F;
    if ((input.held&negative)!=0U) value-=80.0F;
    return value;
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

InputBindings load_default_input_bindings(const Rom& rom) {
    constexpr std::size_t offset=0xB5D50U;
    constexpr std::size_t bytes=17U*4U;
    BigEndianView view(rom.bytes().subspan(offset,bytes));
    InputBindings result;
    for (std::size_t index=0U; index<result.words.size(); ++index) {
        result.words[index]=view.u32(index*4U);
    }
    return result;
}

DriveInput translate_drive_input(const PlayerInput& input,const InputBindings& bindings) {
    const auto& words=bindings.words;
    return DriveInput{
        read_bound_axis(input,words[8],words[9]),
        read_bound_axis(input,words[10],words[11]),
        -read_bound_axis(input,words[6],words[7]),
        (input.held&words[0])!=0U,
        (input.pressed&words[0])!=0U,
        (input.held&words[1])!=0U,
        (input.pressed&words[1])!=0U,
        (input.held&words[16])!=0U,
    };
}

PlayerTank spawn_player_tank(const WorldLevel& world,const NavigationLevel& navigation) {
    for (const auto& item : world.items) {
        if (item.type!=25 || (item.parameter>>4U)!=0U) continue;
        const NavigationVertex position{item.x,item.z};
        for (std::size_t index=1U; index<navigation.triangles.size(); ++index) {
            if (navigation.kind_enabled[navigation.triangles[index].kind]!=0U ||
                !navigation_point_inside_triangle(
                    navigation,position,static_cast<std::uint16_t>(index))) continue;
            const float heading=static_cast<float>(item.angle)*(360.0F/65536.0F);
            PlayerTank tank;
            tank.x=position.x;
            tank.z=position.z;
            tank.heading_degrees=heading;
            tank.turret_degrees=heading;
            tank.navigation_triangle=static_cast<std::uint16_t>(index);
            return tank;
        }
        throw std::runtime_error("player spawn marker is outside open navigation data");
    }
    throw std::runtime_error("level has no group-zero player spawn marker");
}

void update_player_tank(PlayerTank& tank,const DriveInput& input,
                        const NavigationLevel& navigation,const AngleTable& angles) {
    constexpr float frame_rate_scale=0.15F;
    constexpr float turn_acceleration=0.18F;
    constexpr float acceleration_scale=1.0F/100.0F;
    constexpr float grip=300.0F;
    constexpr float drag=250.0F;
    float steering=input.rotation;
    const float throttle=std::clamp(input.throttle,-200.0F,200.0F);
    if (tank.velocity_x<5.0F && tank.velocity_z<5.0F) steering*=1.5F;
    const float turn=steering*turn_acceleration*frame_rate_scale;
    tank.heading_degrees=wrap_degrees(tank.heading_degrees-turn);
    tank.turret_degrees=wrap_degrees(tank.turret_degrees-turn);

    const auto angle=degrees_to_angle(tank.heading_degrees);
    PlanarMotion motion{};
    motion.acceleration_scale=acceleration_scale;
    motion.x=tank.x;
    motion.z=tank.z;
    motion.velocity_x=tank.velocity_x;
    motion.velocity_z=tank.velocity_z;
    motion.heading_degrees=tank.heading_degrees;
    motion.acceleration_x=grip*throttle*angles.sine(angle)-tank.velocity_x*drag;
    motion.acceleration_z=grip*throttle*angles.cosine(angle)-tank.velocity_z*drag;
    advance_planar_motion(motion,frame_rate_scale);
    motion.velocity_x=std::clamp(motion.velocity_x,-512.0F,512.0F);
    motion.velocity_z=std::clamp(motion.velocity_z,-512.0F,512.0F);
    const NavigationVertex from{tank.x,tank.z};
    const NavigationVertex candidate{motion.x,motion.z};
    const auto trace=trace_navigation_segment(
        navigation,tank.navigation_triangle,from,candidate);
    if (trace.stop==NavigationTraceStop::target) {
        tank.x=candidate.x;
        tank.z=candidate.z;
        tank.velocity_x=motion.velocity_x;
        tank.velocity_z=motion.velocity_z;
        tank.navigation_triangle=trace.triangle;
    } else {
        tank.velocity_x=motion.velocity_x*-0.3F;
        tank.velocity_z=motion.velocity_z*-0.3F;
    }
}

bool try_fire_player_projectile(PlayerProjectile& projectile,PlayerTank& tank,
                                const DriveInput& input,const NavigationLevel& navigation,
                                const AngleTable& angles,std::int32_t game_tick) {
    constexpr std::int32_t cooldown=30;
    if (projectile.active || !input.fire || tank.primary_ammo<=0 ||
        tank.primary_last_tick+cooldown>=game_tick) return false;
    constexpr float muzzle_offset=45.0F;
    constexpr float projectile_speed=110.0F;
    const auto angle=degrees_to_angle(tank.turret_degrees);
    const float direction_x=angles.sine(angle);
    const float direction_z=angles.cosine(angle);
    const NavigationVertex origin{tank.x,tank.z};
    const NavigationVertex muzzle{
        tank.x+direction_x*muzzle_offset,tank.z+direction_z*muzzle_offset};
    const auto trace=trace_navigation_segment(
        navigation,tank.navigation_triangle,origin,muzzle);
    if (trace.stop!=NavigationTraceStop::target) return false;
    projectile={muzzle.x,muzzle.z,
                direction_x*projectile_speed,direction_z*projectile_speed,
                trace.triangle,112U,true};
    tank.primary_last_tick=game_tick;
    --tank.primary_ammo;
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

