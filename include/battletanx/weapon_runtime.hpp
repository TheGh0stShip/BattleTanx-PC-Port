#pragma once

#include "battletanx/deployables_runtime.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/projectile_runtime.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/weapon_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace battletanx {

// Compatibility names retained for the host/session and renderer boundary.
// The implementation is the single projectile pool owned by projectile_runtime.
using WeaponProjectile=ShellRecord;
using WeaponProjectilePool=ShellPool;

// Per-unit weapon side state. Projectiles live in the single ShellPool
// (projectile_runtime.hpp); this module owns no pool.
struct WeaponRuntimeState {
    std::array<std::uint8_t,unit_capacity> hull_side{};           // Weapon +0x08 toggle per unit slot
    // unit +0x4B0 (weapon 14); spawn_unit writes -1000 (func_80097038).
    std::array<std::int32_t,unit_capacity> last_mine_tick=[] {
        std::array<std::int32_t,unit_capacity> t{};
        t.fill(retail::never_fired_tick);
        return t;
    }();
    std::array<std::int32_t,unit_capacity> shield_end_tick{};     // unit +0x0C
    std::array<std::int32_t,unit_capacity> flash_timer{};         // unit +0x08
    std::array<float,unit_capacity> recoil{};                     // unit +0x3D0
    void serialize(StateWriter& writer) const;
};

// entity_is_targetable (func_8009FB8C) +0x4B0 term: false for 0x5A ticks
// after weapon 14. Other terms (computer-controlled flag, +0x98, shield window) are the caller's.
[[nodiscard]] bool decoy_allows_targeting(const WeaponRuntimeState& state,std::size_t unit_slot,
                                          std::int32_t tick) noexcept;

struct WeaponContext {
    ProjectileContext& base;
    WeaponRuntimeState& state;
    EffectEventQueue& effects;
    RetailRandom& random;
    bool multiplayer_rules{};   // D_80134E68
    // Hit handler override (shields, reflectors). Empty: handle_projectile_hit.
    std::function<ObjectResponse(EntityId victim,const ProjectileContact&)> object_response{};
    // func_80096784 for guided missiles. Empty: no steering, no release/burst.
    std::function<MissileControl(std::uint8_t owner)> missile_control{};
    // Weapons 12 and 13 (mines / unit deploy). Null: those ids are unsupported.
    deployables::DeployablesContext* deployables{};
};

struct FireOptions {
    bool force{};       // fire_unit_weapon arg3
    bool boosted{true}; // owner +0x30C for the missile speed
    bool remote{};      // func_800E3460(unit)!=0: networked remote unit
};

enum class WeaponFireStatus : std::uint8_t {
    not_ready, fired, spawn_failed, empty,
    unsupported,   // a collaborator is missing; nothing changed
    blocked,       // weapon 12/13 returned before bookkeeping (area/pool); nothing consumed
};
static_assert(std::is_same_v<std::underlying_type_t<WeaponFireStatus>,std::uint8_t>);

// fire_unit_weapon (func_800987D8), every id 0..16.
WeaponFireStatus fire_weapon(WeaponContext& context,EntityId unit_id,WeaponState& weapon,
                             const FireRequest& request,const FireOptions& options={});

// Message 7 broadcast (see projectile_runtime.hpp apply_splash_damage).
std::size_t apply_splash_damage(WeaponContext& context,float x,float z,float radius,
                                std::int32_t amount,std::uint8_t attacker,EntityId source);

// update_shells with this context's hooks bound (the one updater).
void update_weapon_projectiles(WeaponContext& context);

} // namespace battletanx
