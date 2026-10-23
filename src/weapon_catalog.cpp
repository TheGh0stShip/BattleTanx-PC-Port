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
        d.collision_mask=4U;
        d.ricochet_capable=true;
        d.ignores_owner=true;
        t[4]=d;
    }
    for (std::uint8_t id:{std::uint8_t{5U},std::uint8_t{10U}}) {
        // func_800F0FD8 (effect 12, size 0x2C); behaviour of the effect is
        // not recovered.
        WeaponDefinition d=make(id,id==5U ? "burst_a" : "burst_b",WeaponAction::effect12_burst,Evidence::unresolved);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=bike_muzzle;
        d.effect_kind=12U;
        d.damage=id==5U ? std::uint8_t{0x12U} : std::uint8_t{8U};
        d.heavy_damage=d.damage;
        d.projectiles=1U;
        d.fire_sound=9U;
        d.collision_mask=4U;
        t[id]=d;
    }
    {
        WeaponDefinition d=make(6U,"effect14",WeaponAction::effect14,Evidence::unresolved);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=turret_muzzle;
        d.effect_kind=14U;
        d.projectiles=1U;
        d.fire_sound=0x2DU;   // local (non-positional) only
        t[6]=d;
    }
    {
        WeaponDefinition d=make(7U,"effect15",WeaponAction::effect15,Evidence::unresolved);
        d.frame=MuzzleFrame::turret;
        d.muzzle=turret_muzzle;
        d.bike_muzzle=turret_muzzle;
        d.effect_kind=15U;
        d.projectiles=1U;
        t[7]=d;
    }
    t[8]=make(8U,"unused_8",WeaponAction::none,Evidence::confirmed);
    t[9]=make(9U,"unused_9",WeaponAction::none,Evidence::confirmed);
    {
        // case 11 -> func_8008DF2C (effect 22, size 0x48): speed 40 (30 when
        // the owner is not boosted), damage 0x28, trail 0x10, life 400.
        WeaponDefinition d=make(11U,"missile",WeaponAction::missile,Evidence::strong_inference);
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
        d.collision_mask=4U;
        d.ignores_owner=true;
        t[11]=d;
    }
    {
        // case 12 -> func_8010DAA0 (effect 29, size 0x28) at the unit;
        // delayed_blast_effect_update sends message 7 to radius 108.
        WeaponDefinition d=make(12U,"placed_blast",WeaponAction::placed_blast,Evidence::strong_inference);
        d.frame=MuzzleFrame::unit_origin;
        d.effect_kind=29U;
        d.projectiles=1U;
        d.fire_sound=0x19U;
        d.damage=static_cast<std::uint8_t>(splash_amount);
        d.heavy_damage=d.damage;
        d.range=splash_radius;
        d.splash=true;
        t[12]=d;
    }
    {
        WeaponDefinition d=make(13U,"rear_deploy",WeaponAction::rear_deploy,Evidence::unresolved);
        d.frame=MuzzleFrame::hull;
        d.muzzle=MuzzleOffset{0.0F,0.0F,-145.0F};
        d.bike_muzzle=MuzzleOffset{0.0F,0.0F,-82.0F};
        d.effect_kind=27U;
        d.projectiles=1U;
        t[13]=d;
    }
    {
        // case 14: kind-26 variant 0x11 at (+-90, 50, 0) and (0, 50, +-90).
        WeaponDefinition d=make(14U,"ring_burst",WeaponAction::ring_burst,Evidence::confirmed);
        d.frame=MuzzleFrame::unit_origin;
        d.effect_kind=26U;
        d.projectiles=4U;
        d.range=90.0F;
        d.trail=0x11U;
        t[14]=d;
    }
    t[15]=make(15U,"shield",WeaponAction::shield,Evidence::confirmed);
    t[16]=make(16U,"repair",WeaponAction::repair,Evidence::confirmed);
    return t;
}

constexpr std::array<WeaponDefinition,weapon_id_count> definitions=build();

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
