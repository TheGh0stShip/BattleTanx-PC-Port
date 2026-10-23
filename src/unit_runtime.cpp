#include "battletanx/unit_runtime.hpp"

#include <cmath>

namespace battletanx {

float human_owner_scale(std::uint32_t level) noexcept {
    switch (level) {
    case 0U: return retail::human_scale_level0;
    case 2U: return retail::human_scale_level2;
    default: return retail::human_scale_default;
    }
}

std::uint16_t degrees_to_retail_angle(float degrees) noexcept {
    const float scaled=degrees*182.04445F;
    if (!std::isfinite(scaled) || scaled>=2147483648.0F || scaled<-2147483648.0F) return 0U;
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled)));
}

UnitRecord make_unit(retail::UnitKind kind,std::uint8_t owner,std::uint8_t team,
                     float x,float z,std::uint16_t heading,std::int32_t tick,
                     UnitControl control) {
    const auto index=static_cast<std::size_t>(kind);
    UnitRecord unit;
    unit.kind=kind;
    unit.owner=owner;
    unit.team=team;
    unit.alive=true;
    unit.control=control;
    unit.x=x;
    unit.z=z;
    unit.heading_degrees=angle_to_degrees(heading);
    unit.turret_degrees=unit.heading_degrees;
    unit.max_health=retail::unit_max_health[index];
    unit.health=unit.max_health;
    unit.footprint=retail::unit_footprint[index];
    unit.spawn_tick=tick;
    if (kind==retail::UnitKind::tank) {
        unit.primary_weapon.kind=retail::tank_primary_weapon;
        unit.ammo[retail::tank_primary_weapon]=retail::tank_primary_ammo;
    }
    return unit;
}

UnitRecord unit_from_player_tank(const PlayerTank& tank,std::uint8_t owner,std::uint8_t team,
                                 std::int32_t tick) {
    UnitRecord unit=make_unit(retail::UnitKind::tank,owner,team,tank.x,tank.z,0U,tick,
                              UnitControl::player);
    unit.primary=true;
    sync_unit_from_player_tank(unit,tank);
    return unit;
}

void sync_unit_from_player_tank(UnitRecord& unit,const PlayerTank& tank) noexcept {
    unit.x=tank.x;
    unit.z=tank.z;
    unit.velocity_x=tank.velocity_x;
    unit.velocity_z=tank.velocity_z;
    unit.track_left=tank.track_left;
    unit.track_right=tank.track_right;
    unit.track_phase_left=tank.track_phase_left;
    unit.track_phase_right=tank.track_phase_right;
    unit.heading_degrees=tank.heading_degrees;
    unit.turret_degrees=tank.turret_degrees;
    unit.navigation_triangle=tank.navigation_triangle;
    unit.primary_weapon.last_fire_tick=tank.primary_last_tick;
    unit.ammo[retail::tank_primary_weapon]=tank.primary_ammo;
}

void sync_player_tank_from_unit(PlayerTank& tank,const UnitRecord& unit) noexcept {
    tank.x=unit.x;
    tank.z=unit.z;
    tank.velocity_x=unit.velocity_x;
    tank.velocity_z=unit.velocity_z;
    tank.track_left=unit.track_left;
    tank.track_right=unit.track_right;
    tank.track_phase_left=unit.track_phase_left;
    tank.track_phase_right=unit.track_phase_right;
    tank.heading_degrees=unit.heading_degrees;
    tank.turret_degrees=unit.turret_degrees;
    tank.navigation_triangle=unit.navigation_triangle;
    tank.primary_last_tick=unit.primary_weapon.last_fire_tick;
    tank.primary_ammo=unit.ammo[retail::tank_primary_weapon];
}

} // namespace battletanx
