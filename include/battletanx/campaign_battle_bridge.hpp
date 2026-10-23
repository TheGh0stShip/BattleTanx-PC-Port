#pragma once

// Composes the campaign world runtimes around a BattleSession using only the
// session's public API (battle_session.hpp). The session stays the shipped
// gameplay loop; this bridge feeds unit snapshots into the world runtimes,
// routes their outcomes back through apply_damage / spawn_unit / the five
// objective entry points, and consumes the session's gameplay events.
//
// Per-tick order (Bridge::tick). Every step is TEMPORARY POLICY unless marked;
// retail interleaves these updates inside one effect-list walk that the
// session does not expose (see docs in the research ledger, request R1).
//   A. pre-step (positions are the ones left by the previous session step)
//     A1. queen world: unit contacts (queens, then bases, then markers, units
//         in pool slot order), then drop-timer step; events routed to the
//         session objective entry points in queue order.
//     A2. placed objects: unit contacts (message 0, source kind 0), then the
//         bridge-owned turret bullets fired on earlier ticks move and resolve
//         unit hits (apply_damage), then turret update against the unit
//         snapshot; new bullets/beams and splash are routed to units.
//     A3. kind-0x1C spawners tick; spawn orders drained into spawn_unit.
//     A4. deployables: unit/mine contacts, then update_deployables (splash
//         routed to apply_damage; buddy requests become effect-27 turret
//         entities in the placed world through squad::SquadAdapter).
//         TEMPORARY POLICY: deployables share session.rng() with the session,
//         so their draws land before B's draws on every tick.
//   B. session.step(input)
//   C. post-step: new gameplay events in queue order
//     C1. unit_destroyed  -> queen carrier_destroyed (drop) + destruction effect
//     C2. projectile_impact -> impact effect; weapon_fired -> muzzle flash
//     C3. queen events raised by C1 routed (dropped is local only)
//     C4. effect runtime update and status-visual countdown.
//
// Contacts (queen, base, marker, mine) test the unit's rotated footprint
// (UnitRecord::footprint, runtime_constants unit_footprint) as its
// axis-aligned bounds against the object box, the same approximation as
// pickup_overlaps (TEMPORARY POLICY: the unit grid-body walk is unported).
//
// Owner index: retail passes unit->id (+0x04) to get_entity_record
// (fire_unit_weapon.c:450), so the deploy tag / squad owner is the owner
// index, not the unit pool slot (STRONG INFERENCE).

#include "battletanx/battle_session.hpp"
#include "battletanx/deployables_runtime.hpp"
#include "battletanx/effect_runtime.hpp"
#include "battletanx/placed_objects.hpp"
#include "battletanx/projectile_runtime.hpp"
#include "battletanx/queen_world.hpp"
#include "battletanx/spawner_runtime.hpp"
#include "battletanx/squad_units.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace battletanx::campaign_bridge {

// Turret fire (update_turret func_800A16C0, update_turret.c:339/:418/:336).
// Non-beam types call spawn_effect_kind_22 (func_8008E29C, effect 34 bullet):
//   speed D_80125FC0[type], kind byte +0x34 = D_80126028[type],
//   trail byte +0x35 = 0x10 when D_80126028[type]==0 else 0,
//   damage byte +0x37 = owner +0x338[type] (TurretOwnerConfig::effect_kind),
//   attacker byte +0x38 = turret owner, local flag +0x36 = 0 (indirect),
//   life 0x70, range probed to 10000 (D_80071BB8) against static shapes.
// Type 3 calls spawn_beam_effect (func_800F0FD8): variant 0x12, damage =
// owner +0x338[3], depth 5, instant, length 4000.
// BattleSession exposes no shell spawn (request R2), so the bridge owns these
// bullets and resolves their hits through apply_damage (TEMPORARY POLICY).
inline constexpr std::int16_t turret_bullet_life=0x70;           // spawn_effect_kind_22 +0x32
inline constexpr float turret_bullet_range_probe=10000.0F;      // D_80071BB8
inline constexpr std::uint8_t turret_bullet_trail=0x10U;        // +0x35 for kind 0
inline constexpr std::uint8_t turret_beam_variant=0x12U;        // update_turret.c:336
inline constexpr std::uint8_t turret_beam_depth=5U;
inline constexpr float turret_beam_length=4000.0F;              // spawn_beam_effect
inline constexpr std::int32_t turret_beam_projectile=3;         // D_80126028[type]==3
inline constexpr std::size_t turret_bullet_capacity=shell_capacity;   // TEMPORARY POLICY
// BattleSession::events_ capacity (battle_session.hpp:155); the queue's
// dropped() count is not exposed (request R14).
inline constexpr std::size_t session_event_capacity=1024U;
// update_tank_drive.c:618-627 contact replies (unit field +0x3D0).
inline constexpr float contact_crush_amount=6.0F;    // reply 5 and unit_shaken
inline constexpr float contact_scrape_amount=1.2F;   // reply 4, moving only
inline constexpr float contact_moving_speed=0.1F;    // placed_objects.hpp ContactReply::scrape
// placed_objects EventKind::splash = notify_objects_near_point(108, 20): the
// bridge uses battletanx::placed_splash_radius / placed_splash_value (unit_kinds.hpp).
inline constexpr std::uint8_t no_attacker=0xFFU;

enum class Phase : std::uint8_t {
    queen_contacts=0U,
    queen_step=1U,
    placed_contacts=2U,
    placed_update=3U,
    spawners=4U,
    deployables=5U,
    session_step=6U,
    session_events=7U,
    effects_update=8U,
    turret_bullets=9U,      // runs between placed_contacts and placed_update
};
static_assert(std::is_same_v<std::underlying_type_t<Phase>,std::uint8_t>);
inline constexpr std::size_t phase_count=10U;

// Spawner bookkeeping the session has no field for (owner +0x208 budget,
// owner +0x23 gate). Request R4.
struct SpawnerOwnerSetup {
    std::int32_t budget{};
    bool gate_open{};
};

struct BridgeConfig {
    std::int32_t game_kind{};                    // campaign kinds 3..6
    std::int32_t game_mode{};                    // placed::World::hit game_mode
    std::optional<queen_world::Config> queen;    // nullopt: no queen objectives
    std::array<SpawnerOwnerSetup,spawner_runtime::owner_count> spawner_owners{};
    // Owner +0x318/+0x338/+0x340, filled by the caller from the owner profile
    // (func_800A31C0 copies +0x338/+0x340 from profile +0x14/+0x18 tables).
    // Fill each entry from owner_profiles::make_turret_owner (owner_profiles.hpp;
    // per-wave skill profile gun tables). +0x340 is an s16[4] turret fire-delay
    // array indexed by turret type. effect_kind[type] is the damage byte.
    std::array<TurretOwnerConfig,owner_capacity> turret_owners{};
    // Static-shape collision for turret bullet range probes and beams. Empty
    // members: no static shapes (bullets fly until a unit or life ends).
    WorldCollision world{};
    // Spawner linked-effect view per spawner index. Empty: no linked effect.
    std::function<std::optional<LinkedEffectView>(std::size_t)> spawner_link;
    // Spawn-area query. Empty: live session units inside the box radius block
    // as kind-0 hits (TEMPORARY POLICY).
    spawner_runtime::AreaQuery area_query;
};

struct TickReport {
    std::array<std::uint8_t,phase_count> order{};   // phases in execution order
    std::uint8_t phases_run{};
    std::uint32_t queen_events_routed{};
    std::uint32_t queen_events_local{};
    std::uint32_t objective_calls_rejected{};       // ObjectiveEventStatus != applied
    std::uint32_t placed_hits{};
    std::uint32_t turret_shots_fired{};             // bullets spawned + beams fired
    std::uint32_t turret_shots_applied{};           // unit hits from bullets and beams
    std::uint32_t turret_shots_dropped{};           // bullet pool full
    std::uint32_t splash_hits{};
    std::uint32_t spawns_requested{};
    std::uint32_t spawns_failed{};                  // spawn_unit returned nullopt
    std::uint32_t mine_contacts{};
    std::uint32_t contact_replies{};                // placed contact replies applied to shadows
    std::uint32_t buddies_spawned{};
    std::uint32_t buddies_blocked{};
    std::uint32_t session_events_consumed{};
    std::uint32_t effect_spawns{};
    bool session_events_at_capacity{};              // later events may have been dropped (R14)
    bool placed_events_overflowed{};                // placed::World dropped events this tick
    std::uint32_t deployable_events_dropped{};      // cumulative drops; cleared by Bridge::clear_events
};

// A turret bullet the bridge owns (spawn_effect_kind_22 fields).
struct TurretBullet {
    float x{}, y{}, z{};
    float velocity_x{}, velocity_z{};   // +0x1C / +0x18
    float speed{};                      // +0x20
    float travelled{};                  // +0x24
    float range{};                      // +0x28
    std::uint16_t heading{};            // +0x30
    std::int16_t life{turret_bullet_life}; // +0x32
    std::uint8_t bullet_kind{};         // +0x34
    std::uint8_t trail{};               // +0x35
    std::uint8_t damage{};              // +0x37
    std::uint8_t attacker{no_attacker}; // +0x38
    EntityId turret{};                  // +0x2C (ignored by the range probe)

    void serialize(StateWriter& writer) const;
};

// Per-slot shadow of unit +0x3D0 / +0x3D4 (no UnitRecord field, request R7).
struct ContactShadow {
    EntityId unit{};
    float field_3d0{};
    float field_3d4{};
};

enum class CreateError : std::uint8_t { none=0U, queen_config=1U };
static_assert(sizeof(CreateError)==1U);

class Bridge {
public:
    Bridge(BridgeConfig config,const AngleTable& angles);
    // The squad adapter holds pointers into this object.
    Bridge(const Bridge&)=delete;
    Bridge& operator=(const Bridge&)=delete;
    Bridge(Bridge&&)=delete;
    Bridge& operator=(Bridge&&)=delete;

    [[nodiscard]] CreateError error() const noexcept { return error_; }

    // Level setup (forwarded to the owned runtimes).
    queen_world::PlaceResult place_queen_object(const queen_world::LevelObject& object);
    queen_world::MarkerPlaceResult place_marker(float x,float z,std::int16_t extent,std::uint8_t view_count);
    // campaign_spawn::objective_marker_entries output, forwarded unchanged.
    queen_world::MarkerPlaceResult place_bank_entry(const queen_world::MarkerBankEntry& entry,std::uint8_t view_count);
    std::size_t place_markers(std::span<const queen_world::MarkerBankEntry> entries,std::uint8_t view_count);
    void finish_queen_placement() noexcept;
    std::optional<EntityId> spawn_turret(const placed::TurretSpawn& spawn,BattleSession& session);
    std::optional<EntityId> spawn_placed(const PlacedObjectRecord& record,const placed::Bounds& bounds);
    std::optional<EntityId> spawn_breakable(const placed::BreakableRecord& record,const placed::Bounds& bounds);
    std::optional<spawner_runtime::SpawnerCreateError> add_spawner(const SpawnerDefinition& definition,
                                                                   std::optional<std::int32_t> linked_serial);

    // Weapon 12 / 13 fire path. BattleSession fires only shells, so the caller
    // invokes this when a unit selects a deployable weapon (request R3).
    deployables::DeployResult fire_deployable(BattleSession& session,EntityId unit,std::uint8_t weapon,
                                              bool powered);
    // Routes on_self_destruct; the carried-queen drop follows from the
    // unit_destroyed event the caller's destruction produces.
    ObjectiveEventStatus self_destruct(BattleSession& session,std::uint8_t owner);

    // One 30 Hz tick in the order documented above.
    TickReport tick(BattleSession& session,const SessionInput& input);

    // The bridge reads session events from a cursor; clear them only through
    // this call (BattleSession::clear_events has no notification, request R6).
    // PRECONDITION of tick(): call this once per tick after consuming the
    // report. BattleSession's event queue holds session_event_capacity entries
    // and silently drops the rest, so a caller that never clears stops the
    // bridge seeing unit_destroyed (carried-queen drops are then lost);
    // TickReport::session_events_at_capacity flags that state.
    void clear_events(BattleSession& session) noexcept;

    [[nodiscard]] const queen_world::QueenWorld* queens() const noexcept { return queen_ ? &*queen_ : nullptr; }
    [[nodiscard]] const placed::World& placed_world() const noexcept { return placed_; }
    [[nodiscard]] const spawner_runtime::SpawnerSet& spawners() const noexcept { return spawners_; }
    [[nodiscard]] const deployables::DeployablesState& deployable_state() const noexcept { return deployables_; }
    [[nodiscard]] const deployables::DeployableEventQueue& deployable_events() const noexcept { return deployable_events_; }
    [[nodiscard]] const effects::EffectRuntime& effect_runtime() const noexcept { return effects_; }
    [[nodiscard]] const std::vector<TurretBullet>& turret_bullets() const noexcept { return bullets_; }
    [[nodiscard]] const std::array<ContactShadow,unit_capacity>& contact_shadows() const noexcept { return contact_shadow_; }
    [[nodiscard]] const squad::SquadStats& squad_stats() const noexcept { return squad_.stats(); }
    [[nodiscard]] const std::vector<queen_world::QueenEvent>& routed_queen_events() const noexcept { return routed_; }

    void serialize(StateWriter& writer) const;

private:
    void run_queen_contacts(BattleSession& session,TickReport& report);
    void route_queen_events(BattleSession& session,TickReport& report);
    void run_placed(BattleSession& session,std::uint16_t clock,TickReport& report);
    void run_turret_bullets(BattleSession& session,TickReport& report);
    void apply_contact_replies(const UnitRecord& unit,std::size_t slot,std::size_t first,TickReport& report);
    void fire_turret_shot(BattleSession& session,const placed::Event& event,TickReport& report);
    void refresh_squad_context(BattleSession& session);
    void reset_squad_context() noexcept;
    void run_spawners(BattleSession& session,TickReport& report);
    void run_deployables(BattleSession& session,TickReport& report);
    void consume_session_events(BattleSession& session,std::uint16_t clock,std::uint16_t game_tick,
                                TickReport& report);
    placed::UpdateContext placed_context(BattleSession& session,
                                         std::array<TurretTarget,unit_capacity>& targets) const;
    deployables::DeployablesWorld deployables_world(BattleSession& session,TickReport* report);
    [[nodiscard]] bool overlaps(const UnitRecord& unit,float cx,float cz,float half) const noexcept;
    std::size_t splash_units(BattleSession& session,float x,float z,float radius,std::int32_t amount,
                             std::uint8_t attacker,TickReport& report);
    static void mark(TickReport& report,Phase phase) noexcept;

    BridgeConfig config_;
    AngleTable angles_;
    CreateError error_{CreateError::none};
    std::optional<queen_world::QueenWorld> queen_;
    placed::World placed_;
    spawner_runtime::SpawnerSet spawners_;
    spawner_runtime::SpawnOrderQueue orders_;
    spawner_runtime::OwnerSpawnRecords spawner_owners_{};
    // Member slot held by each unit across ticks (TEMPORARY POLICY, R4).
    std::array<std::array<EntityId,spawner_runtime::member_slots>,spawner_runtime::owner_count> member_ids_{};
    // Unit kinds sampled before session.step for destruction effects (#20);
    // overwritten every tick before use, so not serialized.
    std::array<EntityId,unit_capacity> pre_step_ids_{};
    std::array<std::uint8_t,unit_capacity> pre_step_kinds_{};
    deployables::DeployablesState deployables_;
    deployables::DeployableEventQueue deployable_events_{256U};
    effects::EffectRuntime effects_;
    std::vector<TurretBullet> bullets_;     // spawn order
    std::array<ContactShadow,unit_capacity> contact_shadow_{};
    // Gun-buddy spawn context (refreshed before every deployables call).
    std::array<TurretTarget,unit_capacity> squad_targets_{};
    placed::UpdateContext squad_context_{};
    squad::SquadAdapter squad_{placed_,squad_context_};
    std::vector<queen_world::QueenHandle> queen_handles_;   // placement order
    std::vector<queen_world::MarkerHandle> marker_handles_;
    std::vector<queen_world::QueenEvent> routed_;   // events routed during the last tick
    std::size_t session_event_cursor_{};
    std::uint32_t ticks_{};
};

} // namespace battletanx::campaign_bridge
