#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/projectile_runtime.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/weapon_catalog.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace battletanx {

// Typed visual/audio side effects of the weapon paths. Sounds still go to the
// SoundEventQueue; these carry the effect spawns the renderer consumes.
enum class EffectEventKind : std::uint8_t {
    muzzle_flash,       // func_800EED5C(unit, 0x1D)
    trail,              // spawn_effect_kind_26(end, trail)
    impact_explosion,   // spawn_effect_kind_26(hit, 0xB / 9 / 0x17)
    impact_decal,       // func_80093D50 (response 2 or static hit)
    ricochet_spawned,   // response 7: new bullet / redirected shell
    ricochet_dropped,   // response 7 with the projectile pool exhausted
    projectile_removed, // status 2
    blast_armed,        // delayed blast first update: kind-26 variant 0xD
    splash,             // message 7 broadcast
    ring_burst,         // weapon 14 kind-26 variant 0x11
    shield_raised,
    repaired,
};

enum class RemovalReason : std::uint8_t { none, impact, expired, left_world, ricochet, detonated };

struct EffectEvent {
    EffectEventKind kind{};
    std::int32_t tick{};
    EntityId source{};          // projectile (or unit for unit-centred effects)
    EntityId other{};           // impacted unit / spawned ricochet
    std::uint8_t variant{};     // kind-26 variant or effect id
    std::uint8_t owner{no_owner};
    RemovalReason reason{};
    std::uint32_t gameplay_index{}; // gameplay events queued before this one
    float x{}, y{}, z{};
};
using EffectEventQueue=EventQueue<EffectEvent>;

// Projectile records for the definitions-driven weapons. Bullets (effect 34)
// share the layout fields of ShellRecord; shells (effect 2) add acceleration;
// missiles (effect 22) fly by heading; blasts (effect 29) are placed.
inline constexpr std::size_t weapon_projectile_capacity=96U;

struct WeaponProjectile {
    std::uint8_t effect_kind{};     // 34, 2, 22, 29
    std::uint8_t weapon{};
    std::uint8_t bullet_kind{};     // +0x34 bullet / +0x2E shell: 0 cannon, 1 gun, 2 other
    std::uint8_t trail{};
    float x{}, y{}, z{};
    float velocity_x{}, velocity_z{};
    float accel_x{}, accel_z{};     // shells only
    float speed{};
    float travelled{};
    float range{retail::shell_unbounded_range};
    std::uint16_t heading{};
    std::int16_t life{};
    std::uint8_t damage{};
    std::uint8_t attacker{no_owner};
    bool local{};
    bool active{true};              // missile +0x24
    bool triggered{};               // blast: proximity trigger seen
    std::int32_t start_tick{};      // blast +0x1C (0 = not yet started)
    EntityId ignore{};
};
using WeaponProjectilePool=EntityPool<WeaponProjectile,weapon_projectile_capacity>;

struct WeaponRuntimeState {
    WeaponProjectilePool projectiles;
    std::array<std::uint8_t,unit_capacity> hull_side{};           // Weapon +0x08 toggle per unit slot
    std::array<std::int32_t,unit_capacity> last_mine_tick{};      // unit +0x4B0
    std::array<std::int32_t,unit_capacity> shield_end_tick{};     // unit +0x0C
    std::array<std::int32_t,unit_capacity> flash_timer{};         // unit +0x08
    std::array<float,unit_capacity> recoil{};                     // unit +0x3D0
    void serialize(StateWriter& writer) const;
};

// Response of a hit handler: result code and, for 7, the outgoing angle.
struct ObjectResponse {
    HitResponse result{HitResponse::impact};
    std::uint16_t angle{};
};

struct ProjectileContact {
    EntityId projectile{};
    float x{}, y{}, z{};
    std::uint16_t angle{};
    std::uint8_t damage{};
    std::uint8_t attacker{};
    bool direct{};
};

// Lightweight shared services used by the definitions-driven projectile
// runtime. It deliberately excludes the legacy cannon-only shell pool.
struct WeaponBaseContext {
    CombatContext& combat;
    WorldCollision& world;
    const AngleTable& angles;
    float frame_scale{1.0F};
};

struct WeaponContext {
    WeaponBaseContext& base;
    WeaponRuntimeState& state;
    EffectEventQueue& effects;
    RetailRandom& random;
    bool multiplayer_rules{};   // D_80134E68
    // Optional hit handler override (shielded objects, reflectors). Empty:
    // the unit hit handler (handle_projectile_hit) is used.
    std::function<ObjectResponse(EntityId victim,const ProjectileContact&)> object_response{};
};

struct FireOptions {
    bool force{};       // fire_unit_weapon arg3
    bool boosted{true}; // owner +0x30C for the missile speed
};

enum class WeaponFireStatus : std::uint8_t {
    not_ready, fired, spawn_failed, empty,
    unsupported,   // the id's effect is not recovered; nothing changed
};

// fire_unit_weapon (func_800987D8) driven by weapon_definition().
WeaponFireStatus fire_weapon(WeaponContext& context,EntityId unit_id,WeaponState& weapon,
                             const FireRequest& request,const FireOptions& options={});

// Message 7 to every live unit within `radius` of (x, z): apply_unit_damage
// with direct=false, then hit_shake += amount/4. Returns units messaged.
std::size_t apply_splash_damage(WeaponContext& context,float x,float z,float radius,
                                std::int32_t amount,std::uint8_t attacker,EntityId source);

// update_bullet_effect response 7: a new bullet at `angle` with owner 0xFF,
// ignoring `hit`, life 0x70 and the range re-probed. Empty on exhaustion.
std::optional<EntityId> spawn_ricochet(WeaponContext& context,const WeaponProjectile& from,
                                       float hit_x,float hit_z,std::uint16_t angle,EntityId hit);

// Advances every record in slot order (bullets, shells, missiles, blasts).
void update_weapon_projectiles(WeaponContext& context);

} // namespace battletanx
