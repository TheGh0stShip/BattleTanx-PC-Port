#include "battletanx/weapon_catalog.hpp"

namespace battletanx {
namespace {

// Muzzle offsets from fire_unit_weapon (func_800987D8): turret (0, 36, 45),
// bike turret (0, 25, 19), hull side cannons (+-11.1, 25, 19).
constexpr MuzzleOffset turret_muzzle{0.0F,36.0F,45.0F};
constexpr MuzzleOffset bike_muzzle{0.0F,25.0F,19.0F};
constexpr MuzzleOffset hull_muzzle{11.1F,25.0F,19.0F};

constexpr WeaponDefinition make(std::uint8_t id,std::string_view label,WeaponAction action,Evidence evidence) {
    WeaponDefinition d{};
    d.id=id;
    d.label=label;
    d.action=action;
    d.auto_fire=weapon_repeat_table[id]!=0U;
    d.cooldown=weapon_cooldown_table[id];
    d.ammo_slot=id;
    d.evidence=evidence;
    return d;
}

constexpr std::array<WeaponDefinition,weapon_id_count> build() {
    std::array<WeaponDefinition,weapon_id_count> t{};
    t[0]=make(0U,"none",WeaponAction::none,Evidence::confirmed);
    for (std::uint8_t id:{std::uint8_t{1U},std::uint8_t{3U}}) {
        // case 1/3 -> func_8008E29C (effect 34): damage 0x11 (0x28 for unit
        // kind 2), bullet kind 0, trail 0xF, life 0x70, range re-probed.
        WeaponDefinition d=make(id,id==1U ? "cannon" : "cannon_alt",WeaponAction::bullet,Evidence::confirmed);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=turret_muzzle;
        d.effect_kind=34U;
        d.speed=110.0F;
        d.life=0x70;
        d.damage=0x11U;
        d.heavy_damage=0x28U;
        d.range=10000000.0F;
        d.projectiles=1U;
        d.recoil=-2.5F;
        d.fire_sound=id==1U ? std::uint16_t{1U} : std::uint16_t{0x16U};
        d.explosion=0x0BU;
        d.trail=0x0FU;
        d.collision_mask=0x400U;
        d.ricochet_capable=true;
        d.ignores_owner=true;
        t[id]=d;
    }
    {
        // case 2: alternating hull guns, damage 2, bullet kind 1, no trail.
        WeaponDefinition d=make(2U,"machine_gun",WeaponAction::bullet,Evidence::confirmed);
        d.frame=MuzzleFrame::hull;
        d.muzzle=hull_muzzle;
        d.bike_muzzle=hull_muzzle;
        d.effect_kind=34U;
        d.speed=110.0F;
        d.life=0x70;
        d.damage=2U;
        d.heavy_damage=2U;
        d.range=10000000.0F;
        d.projectiles=1U;
        d.recoil=-2.0F;
        d.fire_sound=0x22U;
        d.explosion=0x17U;
        d.collision_mask=0x400U;
        d.ricochet_capable=true;
        d.ignores_owner=true;
        t[2]=d;
    }
    {
        // case 4 -> func_8008D660 (effect 2, size 0x38): three shells, damage
        // 0xE, kind 0, trail 0x10, life 0x70; lateral accel spreads them.
        WeaponDefinition d=make(4U,"spread_shells",WeaponAction::spread_shell,Evidence::confirmed);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=bike_muzzle;
        d.effect_kind=2U;
        d.speed=50.0F;
        d.life=0x70;
        d.damage=0x0EU;
        d.heavy_damage=0x0EU;
        d.projectiles=3U;
        d.recoil=3.0F;
        d.fire_sound=0x0AU;
        d.trail=0x10U;
        d.explosion=0x09U;
        d.powered_variant=true;     // three random-spread shells
        d.collision_mask=4U;
        d.ricochet_capable=true;
        d.ignores_owner=true;
        t[4]=d;
    }
    for (std::uint8_t id:{std::uint8_t{5U},std::uint8_t{10U}}) {
        // case 5/10 -> spawn_beam_effect (func_800F0FD8): yaw = turretYaw *
        // 182.04445, explosion 0x12 (id 5) / 8 (id 10), value 0x26, depth 4
        // (0x16 powered: forks via func_800F15A0 when depth > 16), sound 9.
        // Powered sets ammo[5] = 1 for both ids (retail quirk).
        WeaponDefinition d=make(id,id==5U ? "beam" : "beam_alt",WeaponAction::beam,Evidence::confirmed);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=bike_muzzle;
        d.effect_kind=12U;
        d.damage=0x26U;
        d.heavy_damage=0x26U;
        d.range=4000.0F;
        d.projectiles=1U;
        d.fire_sound=9U;
        d.explosion=id==5U ? std::uint8_t{0x12U} : std::uint8_t{8U};
        d.depth=4U;
        d.powered_depth=0x16U;
        d.powered_variant=true;
        d.powered_consumes_stock=id==5U;
        d.collision_mask=4U;
        d.ricochet_capable=true;
        t[id]=d;
    }
    {
        // case 6 -> spawn_effect_kind_0e (func_800F29F0): velocity sin/cos*50,
        // vy 10, fuse 0xC8; update_bouncing_debris then script 17 ring:
        // message 3, radius 50 -> 8000 over 0xC00 clock units, 50..100 falloff.
        WeaponDefinition d=make(6U,"nuke",WeaponAction::nuke,Evidence::strong_inference);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=turret_muzzle;
        d.effect_kind=14U;
        d.speed=50.0F;
        d.life=0xC8;
        d.projectiles=1U;
        d.fire_sound=0x2DU;
        d.fire_sound_local=true;
        d.explosion=8U;
        d.splash_radius=8000.0F;
        d.splash_amount=100;
        d.collision_mask=0x10U;
        t[6]=d;
    }
    {
        // case 7 -> spawn_effect_kind_0f (func_800F3344): speed 30*(0.8+r0.4),
        // vy 10*(0.9+r0.2), fuse 0x50; update_bouncing_grenade: message 7
        // radius 100, amount 22, explosion 0x1A, sound 5.
        WeaponDefinition d=make(7U,"grenade",WeaponAction::grenade,Evidence::confirmed);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=turret_muzzle;
        d.effect_kind=15U;
        d.speed=30.0F;
        d.life=0x50;
        d.damage=22U;
        d.heavy_damage=22U;
        d.projectiles=1U;
        d.explosion=0x1AU;
        d.splash_radius=100.0F;
        d.splash_amount=22;
        d.collision_mask=0x10U;
        t[7]=d;
    }
    t[8]=make(8U,"unused_8",WeaponAction::none,Evidence::confirmed);
    t[9]=make(9U,"unused_9",WeaponAction::none,Evidence::confirmed);
    {
        // case 11 -> func_8008DF2C (effect 22, size 0x48): speed 40 (30 when
        // the owner is not boosted), damage 0x28, trail 0x10, life 400 (8000
        // with bursts), owner-steered (update_missile_effect).
        WeaponDefinition d=make(11U,"missile",WeaponAction::missile,Evidence::confirmed);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=bike_muzzle;
        d.effect_kind=22U;
        d.speed=40.0F;
        d.life=400;
        d.damage=0x28U;
        d.heavy_damage=0x28U;
        d.projectiles=1U;
        d.recoil=-2.5F;
        d.fire_sound=0x0AU;
        d.trail=0x10U;
        d.explosion=0x0BU;
        d.depth=5U;                 // burst beams (powered bursts = ammo[11])
        d.powered_variant=true;
        d.powered_consumes_stock=true;
        d.collision_mask=4U;
        d.ignores_owner=true;
        t[11]=d;
    }
    {
        // case 12: deployables runtime (fire_mine_weapon). Unpowered places
        // one effect-29 blast (sound 0x19); powered a 5x5 grid (ammo -> 1).
        WeaponDefinition d=make(12U,"mines",WeaponAction::mine_deploy,Evidence::confirmed);
        d.frame=MuzzleFrame::unit_origin;
        d.effect_kind=29U;
        d.projectiles=1U;
        d.fire_sound=0x19U;
        d.damage=static_cast<std::uint8_t>(splash_amount);
        d.heavy_damage=d.damage;
        d.splash_radius=splash_radius;
        d.splash_amount=splash_amount;
        d.powered_variant=true;
        d.powered_consumes_stock=true;
        d.deployable=true;
        t[12]=d;
    }
    {
        // case 13: deployables runtime (fire_gun_buddy_weapon). Unpowered one
        // squad unit at hull (0,0,-145) (-82 bike); powered a 5x5 grid.
        WeaponDefinition d=make(13U,"unit_deploy",WeaponAction::unit_deploy,Evidence::confirmed);
        d.frame=MuzzleFrame::hull;
        d.muzzle=MuzzleOffset{0.0F,0.0F,-145.0F};
        d.bike_muzzle=MuzzleOffset{0.0F,0.0F,-82.0F};
        d.effect_kind=27U;
        d.projectiles=1U;
        d.powered_variant=true;
        d.powered_consumes_stock=true;
        d.deployable=true;
        t[13]=d;
    }
    {
        // case 14: kind-38 variant 0x11 at (+-90, 50, 0) and (0, 50, +-90);
        // script 16 only spawns particles: NO damage (confirmed). The effect
        // is lastMineTime = now: entity_is_targetable (func_8009FB8C) is false
        // for 0x5A ticks.
        WeaponDefinition d=make(14U,"decoy",WeaponAction::decoy,Evidence::confirmed);
        d.frame=MuzzleFrame::unit_origin;
        d.effect_kind=38U;
        d.projectiles=4U;
        d.range=90.0F;
        d.trail=0x11U;
        d.explosion=0x11U;
        d.damage=0U;
        d.untargetable_ticks=0x5A;
        t[14]=d;
    }
    t[15]=make(15U,"shield",WeaponAction::shield,Evidence::confirmed);
    t[16]=make(16U,"repair",WeaponAction::repair,Evidence::confirmed);
    return t;
}

constexpr std::array<WeaponDefinition,weapon_id_count> definitions=build();
static_assert(weapon_cooldown_table[16]==1U && weapon_repeat_table.size()==weapon_id_count);

} // namespace

std::optional<WeaponDefinition> weapon_definition(std::uint8_t id) noexcept {
    if (id>=weapon_id_count) return std::nullopt;
    return definitions[id];
}

std::optional<WeaponGrant> pickup_weapon_grant(std::uint8_t pickup_kind) noexcept {
    // apply_pickup (func_80099D90): +0x488.. ammo counters.
    switch (pickup_kind) {
    case 2U: return WeaponGrant{4U,5,false};
    case 3U: return WeaponGrant{5U,5,false};
    case 1U: return WeaponGrant{6U,1,false};
    case 15U: return WeaponGrant{7U,5,false};
    case 7U: return WeaponGrant{11U,5,false};
    case 9U: return WeaponGrant{12U,5,false};
    case 10U: return WeaponGrant{13U,2,false};
    case 19U: return WeaponGrant{13U,1,false};
    case 16U: return WeaponGrant{14U,3,false};
    case 12U: return WeaponGrant{15U,1,true};
    default: return std::nullopt;
    }
}

std::uint8_t random_grant_pickup_kind(std::uint32_t random) noexcept {
    const std::uint32_t r=random&0x1FU;
    if (r==0U) return 1U;
    if (r<=6U) return 2U;
    if (r<=12U) return 3U;
    if (r<=14U) return 4U;
    if (r<=19U) return 7U;
    if (r<=21U) return 9U;
    if (r<=23U) return 15U;
    if (r<=25U) return 12U;
    if (r<=29U) return 18U;
    return 19U;
}

} // namespace battletanx
