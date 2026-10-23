#pragma once

#include <array>
#include <cstdint>

namespace battletanx::retail {

// Unit kinds as stored in the unit record (+0x18).
enum class UnitKind : std::uint8_t { tank=0U, bike=1U, goliath=2U };

// spawn_unit (func_80097038): maximum health per unit kind.
inline constexpr std::array<std::int32_t,3> unit_max_health{100,30,200};

// spawn_unit / try_place_unit (func_8009FEE0): collision footprint per kind,
// unit-local x min, x max, z min, z max.
struct Footprint { std::int16_t min_x, max_x, min_z, max_z; };
inline constexpr std::array<Footprint,3> unit_footprint{{
    {-35,35,-73,81},{-26,26,-38,26},{-60,60,-75,81}}};

// spawn_unit: primary weapon and ammunition indexed by UnitKind.
inline constexpr std::array<std::uint8_t,3> unit_primary_weapon{1U,2U,3U};
inline constexpr std::array<std::int16_t,3> unit_primary_ammo{70,150,70};
// Names retained for tank-only interfaces.
inline constexpr std::uint8_t tank_primary_weapon=1U;
inline constexpr std::int16_t tank_primary_ammo=70;
inline constexpr std::int32_t never_fired_tick=-1000;
inline constexpr std::size_t ammo_slots=17U;

// D_80125EC0 (ROM 0xB5EC0, u8[16]): weapon cooldown in game ticks.
inline constexpr std::array<std::uint8_t,16> weapon_cooldown{
    0U,30U,3U,40U,50U,54U,50U,10U,5U,0U,50U,50U,15U,60U,60U,120U};
// D_80125ED4 (ROM 0xB5ED4, u8[18]): non-zero = fires while the trigger is held.
inline constexpr std::array<std::uint8_t,18> weapon_repeats{
    0U,1U,1U,1U,0U,0U,0U,0U,1U,0U,0U,0U,0U,0U,0U,0U,0U,0U};

// fire_unit_weapon (func_800987D8) case 1/3: muzzle offset in turret space
// (x, y, z) and shell speed; spawn_effect_kind_22 (func_8008E29C) fields.
inline constexpr float muzzle_forward=45.0F;
inline constexpr float muzzle_height=36.0F;
inline constexpr float shell_speed=110.0F;
inline constexpr std::int16_t shell_life_ticks=0x70;
inline constexpr std::uint8_t shell_damage=0x11U;
inline constexpr std::uint8_t goliath_shell_damage=0x28U;
inline constexpr float shell_range_probe=10000.0F;   // D_80071BB8 (f64)
inline constexpr float shell_unbounded_range=10000000.0F;

// Sound identifiers passed to func_800796F0 / func_80085C68 / func_80085B4C.
inline constexpr std::uint16_t sound_cannon=1U;            // fire_unit_weapon.c:172 (weapon 1)
inline constexpr std::uint16_t sound_cannon_alt=0x16U;     // fire_unit_weapon.c:172 (weapon 3)
inline constexpr std::uint16_t sound_empty_trigger=0x20U;  // fire_unit_weapon.c:520
inline constexpr std::uint16_t sound_hit_unit=0x0EU;       // update_bullet_effect.c impact
inline constexpr std::uint16_t sound_hit_other=3U;         // update_bullet_effect.c impact
inline constexpr std::uint16_t sound_unit_destroyed=0x0DU; // destroy_unit.c:164 (volume 0.75)
inline constexpr std::uint16_t sound_unit_crushed=0x1FU;   // destroy_unit.c:162
inline constexpr std::uint16_t sound_pickup=0x2BU;         // pickup collection
inline constexpr float positional_cannon_volume=1.2F;
inline constexpr float destroyed_volume=0.75F;

// apply_unit_damage (func_800983E0): D_8007222C = 100.0f.
inline constexpr float damage_scale_unit=100.0F;
// func_800A31C0: human owner scale by owner +0x30C (0, 2, other).
inline constexpr float human_scale_level0=80.0F;   // D_80072DA4
inline constexpr float human_scale_level2=120.0F;  // D_80072DA8
inline constexpr float human_scale_default=100.0F; // D_80072DAC

// Unit flag bits (+0x14) used by the recovered paths.
inline constexpr std::uint16_t flag_invulnerable=0x0001U;
inline constexpr std::uint16_t flag_instant_death=0x0002U;
inline constexpr std::uint16_t flag_crushed=0x0010U;
inline constexpr std::uint16_t flag_free_fire=0x0040U;
inline constexpr std::uint16_t flag_reserve_unit=0x0100U;
inline constexpr std::uint16_t flag_squad_member=0x0800U;
inline constexpr std::uint16_t flag_spawn_protected=0x1000U;
inline constexpr std::uint16_t flag_campaign_shielded=0x8000U;

// destroy_unit kill score per victim kind (modes other than 7).
inline constexpr std::array<std::int32_t,3> kill_score{1000,500,3000};
inline constexpr std::int32_t goliath_flagged_kill_score=5000;

// respawn_squad_member: a squad spawn point is reusable after 360 ticks.
inline constexpr std::int32_t spawn_point_reuse_ticks=360;
// entity_is_targetable: spawn grace before a unit can be targeted.
inline constexpr std::int32_t target_grace_ticks=90;

} // namespace battletanx::retail
