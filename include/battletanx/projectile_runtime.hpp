#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/combat.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/entity_pool.hpp"
#include "battletanx/level.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/unit_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>

namespace battletanx {

// Retail projectiles live in the shared 1024-slot effect pool (D_802C3A30).
// The native runtime keeps every weapon-owned moving or timed record in ONE
// pool (ShellPool) advanced by ONE updater (update_shells). Exhaustion fails
// the spawn the same way a NULL allocate_effect (func_8008AD10) does.
inline constexpr std::size_t shell_capacity=128U;

// Effect-table indices (init_effect_kind_table, func_8008B9E8). Deployable
// kinds are listed for the deployables runtime, which owns their lifecycles.
namespace effect_kind {
inline constexpr std::uint8_t shell=2U;          // func_8008D660 / update func_8008CF40
inline constexpr std::uint8_t beam=12U;          // func_800F0FD8 (instant; visual record only)
inline constexpr std::uint8_t beam_fork=13U;     // func_800F15A0 / update func_800F1670
inline constexpr std::uint8_t nuke=14U;          // func_800F29F0 / update func_800F2AE4
inline constexpr std::uint8_t grenade=15U;       // func_800F3344 / update func_800F3490
inline constexpr std::uint8_t missile=22U;       // func_8008DF2C / update func_8008D768
inline constexpr std::uint8_t deployed_unit=27U; // func_800A1408 (deployables runtime)
inline constexpr std::uint8_t placed_blast=29U;  // func_8010DAA0 / update func_8010DC0C (deployables runtime)
inline constexpr std::uint8_t grid_barrage=30U;  // func_8010DED4 (deployables runtime)
inline constexpr std::uint8_t bullet=34U;        // func_8008E29C / update func_8008E480
inline constexpr std::uint8_t scripted=38U;      // spawn_effect_kind_26 (func_800ED8C0), visual
} // namespace effect_kind

// The single projectile record. Fields not used by a kind keep defaults.
// Names read by BattleSession/main (x..ignore) are unchanged.
struct ShellRecord {
    float x{}, y{}, z{};
    float velocity_x{}, velocity_z{};     // bullet +0x1C/+0x18; nuke/grenade vx/vz
    float speed{retail::shell_speed};     // +0x20
    float travelled{};                    // +0x24
    float range{retail::shell_unbounded_range}; // +0x28
    std::uint16_t heading{};              // +0x30
    std::int16_t life{retail::shell_life_ticks}; // +0x32; nuke/grenade timer; fork delay
    std::uint8_t damage{retail::shell_damage};   // +0x37
    std::uint8_t attacker{no_owner};      // +0x38 owner index of the firing unit
    bool local{};                         // +0x36 flag
    EntityId ignore{};                    // the firing unit is not hit
    std::uint8_t effect_kind{effect_kind::bullet};
    std::uint8_t weapon{1U};
    std::uint8_t bullet_kind{};           // 0 cannon, 1 gun, 2 shell
    std::uint8_t trail{};                 // kind-38 trail variant, 0 = none
    float velocity_y{};                   // nuke/grenade vy
    float accel_x{}, accel_z{};           // shell lateral acceleration
    std::uint16_t spin{};                 // nuke/grenade (visual)
    std::uint16_t turn{};                 // missile +0x1E (visual)
    bool active{true};                    // missile +0x24
    bool guided{};                        // missile +0x27
    bool wobble{};                        // missile +0x46
    std::int16_t bursts{};                // missile +0x44
    std::uint8_t phase{};                 // nuke: 0 flying, 1 script wait, 2 blast ring
    std::int32_t phase_tick{};            // tick the current phase began
    std::uint32_t blast_hits{};           // nuke ring: unit slots already notified
    std::uint8_t depth{};                 // beam fork depth
    std::uint8_t variant{};               // beam fork explosion variant
};
static_assert(unit_capacity<=32U,"blast_hits is a 32-bit unit slot mask");

using ShellPool=EntityPool<ShellRecord,shell_capacity>;

struct SegmentHit {
    float distance{};   // along the segment from its start, world units
    float x{}, z{};
    float normal_x{}, normal_z{}; // valid when has_normal
    bool has_normal{};
};

struct WorldCollision {
    // find_segment_hit with mask 4 (static shapes).
    std::function<std::optional<SegmentHit>(float x0,float z0,float x1,float z1)> segment;
    // func_80085D84: false when a point has left the world.
    std::function<bool(float x,float z)> inside;
};

// Native collision adapter for a decoded retail level. The callbacks borrow
// all three arguments. Normals are not produced (has_normal=false).
[[nodiscard]] WorldCollision level_world_collision(const WorldLevel& world,
                                                    const NavigationLevel& navigation,
                                                    const AngleTable& angles);

enum class EffectEventKind : std::uint8_t {
    muzzle_flash,       // func_800EED5C(unit, 0x1D)
    trail,              // spawn_effect_kind_26(end, trail)
    impact_explosion,   // spawn_effect_kind_26(hit, variant)
    impact_decal,       // func_80093D50
    ricochet_spawned,   // response 7: new bullet / sub-beam / redirected shell
    ricochet_dropped,   // response 7 with the pool exhausted
    projectile_removed, // status 2
    blast_armed,        // kind-38 variant 0xD
    splash,             // message 7 / message 3 broadcast
    ring_burst,         // weapon 14 kind-38 variant 0x11 (no damage)
    shield_raised,
    repaired,
    beam,               // kind-12 visual: origin, angle, length
    bounce,             // nuke/grenade ground or wall bounce
};
static_assert(std::is_same_v<std::underlying_type_t<EffectEventKind>,std::uint8_t>);

enum class RemovalReason : std::uint8_t { none, impact, expired, left_world, ricochet, detonated, finished };
static_assert(std::is_same_v<std::underlying_type_t<RemovalReason>,std::uint8_t>);

struct EffectEvent {
    EffectEventKind kind{};
    std::int32_t tick{};
    EntityId source{};
    EntityId other{};
    std::uint8_t variant{};
    std::uint8_t owner{no_owner};
    RemovalReason reason{};
    std::uint32_t gameplay_index{}; // gameplay events queued before this one
    float x{}, y{}, z{};
    std::uint16_t angle{};
    float length{};
    float normal_x{}, normal_z{};   // decal/impact: hit normal (0,0 when the hit has none)
};
using EffectEventQueue=EventQueue<EffectEvent>;

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
    std::uint8_t effect_kind{};
};

// func_80096784(owner): steering for a guided missile.
struct MissileControl {
    std::int8_t steer{};
    bool release{};   // breakMask test: guidance ends
    bool burst{};     // burstMask test: fire a beam from the missile
};

struct ExternalSegmentHit {
    std::uint16_t token{};
    float distance{};
};

// Optional collaborators; absent members select documented fallbacks.
struct ProjectileHooks {
    EffectEventQueue* effects{};
    RetailRandom* random{};
    std::function<ObjectResponse(EntityId victim,const ProjectileContact&)> object_response{};
    std::function<std::optional<ExternalSegmentHit>(float,float,float,float)> external_hit{};
    std::function<ObjectResponse(std::uint16_t,const ProjectileContact&)> external_response{};
    std::function<MissileControl(std::uint8_t owner)> missile_control{};
    // apply_proximity_hit_to_target.c: target +0x3D0 -= 6 per nuke-ring hit.
    // Empty: no recoil state (BattleSession).
    std::function<void(EntityId unit)> proximity_recoil{};
};

struct ProjectileContext {
    CombatContext& combat;
    ShellPool& shells;
    const WorldCollision& world;
    const AngleTable& angles;
    float frame_scale{1.0F};   // D_801257B0; 1.0 per 1/30 s tick
    const ProjectileHooks* hooks{};
};

struct FireRequest {
    bool trigger_held{};
    bool trigger_pressed{};
};

enum class FireResult : std::uint8_t { not_ready, fired, spawn_failed, empty };

// fire_unit_weapon (func_800987D8) for cannon weapons 1 and 3 (BattleSession).
FireResult fire_unit_weapon(ProjectileContext& context,EntityId unit_id,WeaponState& weapon,
                            const FireRequest& request);

struct UnitSegmentHit {
    EntityId unit{};
    float distance{};
};
[[nodiscard]] std::optional<UnitSegmentHit> find_unit_segment_hit(
    const UnitPool& units,const AngleTable& angles,float x0,float z0,float x1,float z1,
    EntityId ignore);

[[nodiscard]] std::optional<float> segment_footprint_distance(
    float x0,float z0,float x1,float z1,float cx,float cz,float sine,float cosine,
    const retail::Footprint& footprint) noexcept;

struct BulletSpawn {
    float x{}, y{}, z{};
    std::uint16_t heading{};
    float speed{retail::shell_speed};
    // Retail passes velocity = frame forward * speed (fire_unit_weapon.c:158-166,
    // spawn_effect_kind_22.c +0x18/+0x1C). When set, velocity_x/z are stored
    // verbatim; otherwise velocity = sine/cosine(heading) * speed (ricochets).
    bool has_velocity{};
    float velocity_x{}, velocity_z{};
    std::int16_t life{retail::shell_life_ticks};
    std::uint8_t damage{retail::shell_damage};
    std::uint8_t weapon{1U};
    std::uint8_t bullet_kind{};
    std::uint8_t trail{};
    std::uint8_t attacker{no_owner};
    bool local{};
    EntityId ignore{};
};
std::optional<EntityId> spawn_bullet(ProjectileContext& context,const BulletSpawn& spawn);

struct ShellSpawn {
    float x{}, y{}, z{};
    float velocity_x{}, velocity_z{};
    float accel_x{}, accel_z{};
    std::uint16_t heading{};
    std::uint8_t damage{0x0EU};
    std::uint8_t weapon{4U};
    std::uint8_t trail{0x10U};
    std::uint8_t attacker{no_owner};
    bool local{};
    EntityId ignore{};
};
std::optional<EntityId> spawn_shell(ProjectileContext& context,const ShellSpawn& spawn);

struct MissileSpawn {
    float x{}, y{}, z{};
    float speed{40.0F};
    std::uint16_t heading{};
    std::uint8_t damage{0x28U};
    std::uint8_t trail{0x10U};
    std::uint8_t attacker{no_owner};
    bool local{};
    bool guided{true};   // false for remote units (fire_unit_weapon.c:213-219)
    std::int16_t bursts{};
    EntityId ignore{};
};
// func_8008DF2C: guided, life 400 (8000 with bursts), speed / 2.5 (bursts > 0)
// or / 1.5 (bursts < 0).
std::optional<EntityId> spawn_missile(ProjectileContext& context,const MissileSpawn& spawn);

std::optional<EntityId> spawn_nuke(ProjectileContext& context,float x,float y,float z,
                                   std::uint16_t heading,std::uint8_t attacker,EntityId ignore);
std::optional<EntityId> spawn_grenade(ProjectileContext& context,RetailRandom& random,float x,float y,
                                      float z,std::uint16_t heading,std::uint8_t attacker,EntityId ignore);

struct BeamSpawn {
    float x{}, y{}, z{};
    std::uint16_t heading{};
    std::uint8_t attacker{no_owner};
    std::uint8_t variant{8U};
    std::uint8_t damage{0x26U};
    std::uint8_t depth{4U};
    bool local{};
    EntityId ignore{};
};
// func_800F0FD8: instant beam, length 4000. False when depth == 0.
bool fire_beam(ProjectileContext& context,const BeamSpawn& spawn);

// notify_objects_near_point (func_8009F958) message 7.
std::size_t apply_splash_damage(ProjectileContext& context,float x,float z,float radius,
                                std::int32_t amount,std::uint8_t attacker,EntityId source);

std::optional<EntityId> spawn_ricochet(ProjectileContext& context,const ShellRecord& from,
                                       float hit_x,float hit_z,std::uint16_t angle,EntityId hit);

// The single updater: every record in slot order; records spawned during the
// pass are first updated on the next call.
void update_shells(ProjectileContext& context);

void serialize_projectiles(StateWriter& writer,const ShellPool& pool);

// osGetTime>>16 units elapsed after `ticks` 30 Hz game ticks.
[[nodiscard]] std::int32_t script_clock_units(std::int32_t ticks) noexcept;

} // namespace battletanx
