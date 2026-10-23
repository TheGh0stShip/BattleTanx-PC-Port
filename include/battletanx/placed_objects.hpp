#pragma once

// World-level runtime for type-22 entities (effect kind 0x1B: turrets and
// bonus crates) and the related placed destructibles (effect kinds 5, 6, 9 and
// 18). Built above unit_kinds.hpp: the per-record rules (TurretRecord,
// update_turret, damage_turret, PlacedObjectRecord, damage_placed_object,
// destroy_placed_object) live there; this module adds pooled storage with
// generation-checked handles, message routing, events, renderer-neutral
// snapshots and deterministic serialization. Iteration is ascending slot order.

#include "battletanx/angle.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/entity_pool.hpp"
#include "battletanx/level.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_kinds.hpp"
#include "battletanx/unit_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace battletanx::placed {

enum class Kind : std::uint8_t {
    turret,      // effect kind 27 (0x1B), types 0..3
    staged,      // effect kind 5
    knockable,   // effect kind 6
    crushable,   // effect kind 9
    breakable,   // effect kind 18 (grid_effect_handle_message / destroy_placed_object)
};
static_assert(std::is_same_v<std::underlying_type_t<Kind>,std::uint8_t>);

// TEMPORARY POLICY: retail shares one effect pool between all effect kinds;
// the native manager owns a fixed pool of its own.
inline constexpr std::size_t world_capacity=128U;

// spawn_squad_unit_at (func_800A1408): D_8012607C = (D_8012607C+1) % 16.
inline constexpr std::uint8_t turret_serial_modulo=16U;
// create_effect_motion(..., 0xD6, 0) collision mode for turrets.
inline constexpr std::uint16_t turret_collision_mode=0xD6U;

// bonus_crate_handle_message destruction: spawn_effect_kind_26 variant 5 and
// a debris burst of effect D_80125FB0[type] (ROM 0xB5FB0, all entries 73),
// scale 1.5, size D_80126038[type], initial velocity D_80072CCC (0,15,0).
inline constexpr std::int32_t turret_explosion_variant=5;
inline constexpr std::int32_t turret_debris_effect=73;
inline constexpr float turret_debris_scale=1.5F;
inline constexpr float turret_debris_lift=15.0F;

// Falloff 50 beyond 4000 units, else trunc((1 - d/4000) * 50) + 50,
// D_80072D88..D_80072D90 (ROM 0x2D88). The decomp source of this formula is
// func_800A2A44 (update_distance_scaled_debris_emitter), the crate debris
// emitter that dispatch_object_event_a (kind 27) calls for event 3. That
// function subtracts the value from an emitter timer; it is not a turret
// damage handler. UNRESOLVED: applying it as turret message-3 damage here is
// not backed by a decomp turret path (see effects::step_distance_scaled_debris_emitter).
inline constexpr float turret_blast_radius=4000.0F;
inline constexpr float turret_blast_scale=50.0F;
inline constexpr std::int32_t turret_blast_base=50;
// TEMPORARY POLICY: turret messages 3 (blast) and 7 (direct damage) apply the
// falloff/damage above to turret health. When false, both return
// MessageResult::unsupported. The retail debris heading for this path is
// also UNRESOLVED, so the debris event carries no heading.
inline constexpr bool turret_blast_policy=true;

// Effect kind 18 record flags (DestroyedObject +0x1D).
inline constexpr std::uint8_t breakable_silent=0x01U;      // no explosion/notify
inline constexpr std::uint8_t breakable_crush_guard=0x04U; // flame (34) needs arg +0x18
inline constexpr std::uint8_t breakable_blasted=0x08U;     // set by message 3
inline constexpr std::uint8_t breakable_explosive=0x10U;   // variant 22 + splash
inline constexpr std::uint16_t breakable_crush_sound=0x23U; // func_80099CD8 on the crushing unit
inline constexpr std::int32_t breakable_rubble_variant=20;  // (flags & 0x0C) == 0x04
inline constexpr std::uint16_t breakable_rubble_sound=4U;

// TEMPORARY POLICY: bound on the pending event queue between clear_events()
// calls; further events are dropped and events_overflowed() is set.
inline constexpr std::size_t max_pending_events=512U;

// Effect kind 5 (frame_effect_handle_message, func_8009345C): message 3 sets
// this flag (+0x0C), which suppresses the segment/area notification on the
// first frame of a later collapse (update_effect_kind_5, asm 0x80092BD4).
inline constexpr std::uint8_t staged_blasted=0x10U;

// Effect kind 9 (slot_heading_effect_handle_message, func_80093A5C; contact
// path pulse_effect_handle_message, func_80093590). Flags at +0x0C.
inline constexpr std::uint8_t crushable_blast_flags=0x03U; // message 3/7: flags |= 3
inline constexpr std::uint8_t crushable_debris=0x02U;      // update spawns debris/effect then frees
inline constexpr std::uint8_t crushable_flattened=0x04U;   // guards messages 2/3/7
inline constexpr std::uint8_t crushable_oriented=0x08U;    // debris along +0x1E heading
// spawn_effect_kind_09 (func_80091D6C): +0x1C = 10 contact shakes.
inline constexpr std::uint16_t crushable_shake_ticks=10U;
// pulse handler: a reply is only issued when lastTime + 10 < D_801B4AA0.
inline constexpr std::int32_t crushable_contact_interval=10;
// pulse handler shake: unit +0x3D0 -= 6.0, unit +0x3D4 += 2 / -= 2 by parity.
inline constexpr float crushable_shake_drain=6.0F;
inline constexpr float crushable_shake_jitter=2.0F;
// clear_record_halfword_flags(motion, 0x10) when the shakes run out.
inline constexpr std::uint16_t crushable_motion_flag=0x10U;

// Reply code written to the contacting unit (update_tank_drive.c:618-641).
enum class ContactReply : std::uint8_t {
    none=0,      // shake absorbed the contact
    pass=1,      // no unit-side effect
    scrape=4,    // unit field +0x3D0 -= 1.2 when moving faster than 0.1
    crush=5,     // unit field +0x3D0 -= 6.0
};
static_assert(std::is_same_v<std::underlying_type_t<ContactReply>,std::uint8_t>);

// cosine_to_angle (func_800F3BE4): retail arccosine over the cosine half of
// the sine table (D_80139720 = D_80139620 + 0x40 entries), 0..0x8000.
[[nodiscard]] std::uint16_t retail_cosine_to_angle(float value,const AngleTable& angles) noexcept;
// Retail fling heading of an object pushed away from a source point
// (grid_effect_handle_message / slot_heading_effect_handle_message):
// acos(c/len) negated when the sine component is negative. nullopt for a
// zero or non-finite offset (retail leaves the heading unchanged).
[[nodiscard]] std::optional<std::uint16_t> fling_heading(float object_x,float object_z,float source_x,
                                                         float source_z,const AngleTable& angles) noexcept;

// STRONG INFERENCE: the runtime model record (12 bytes, list index at +2)
// and list record (4 bytes, count at +0) used by update_morphing_effect and
// update_effect_kind_5 match WorldShape and WorldModelRange. Returns the
// part count of the first list of the given model set, or nullopt when the
// index is out of range. The debris group (session +0x56C) is not parsed by
// level.hpp, so this cannot map an object's group+stage to its model.
[[nodiscard]] std::optional<std::uint8_t> model_part_count(const WorldLevel& level,std::int16_t shape) noexcept;

// Effect kind 9 state not carried by PlacedObjectRecord.
struct CrushableState {
    std::uint16_t shake{crushable_shake_ticks}; // +0x1C
    std::int32_t last_contact{};                // +0x18
    std::uint16_t heading{};                    // +0x1E
    bool released{};                            // release_effect pending

    void serialize(StateWriter& writer) const;
};

struct BreakableRecord {
    std::uint8_t flags{};
    std::uint8_t debris{};      // +0x1C: nonzero = fling parts along heading
    std::uint16_t heading{};    // +0x1E
    std::int32_t x{}, z{};      // x = +0x10, z = +0x0C (grid_effect_handle_message msg 2/3/7; integers)
    bool released{};            // release_effect pending; destroyed on next update

    void serialize(StateWriter& writer) const;
};

// Axis-aligned world bounds. Turrets use +-scale*45 unrotated (CONFIRMED);
// level placed objects pass bounds from rotated_bounds().
struct Bounds {
    float min_x{}, max_x{}, min_z{}, max_z{};
};
[[nodiscard]] Bounds turret_bounds(std::uint8_t type,float x,float z) noexcept;
// AABB of a rectangle with the given half extents rotated by heading.
[[nodiscard]] Bounds rotated_bounds(float x,float z,float half_x,float half_z,std::uint16_t heading,
                                    const AngleTable& angles) noexcept;
[[nodiscard]] bool bounds_contain(const Bounds& bounds,float x,float z) noexcept;

struct Entry {
    Kind kind{Kind::turret};
    bool active{true};          // TEMPORARY POLICY: inactive entries neither update nor take messages
    std::uint8_t serial{};
    Bounds bounds{};
    TurretRecord turret{};
    PlacedObjectRecord object{};
    BreakableRecord breakable{};
    CrushableState crushable{};

    void serialize(StateWriter& writer) const;
};

enum class EventKind : std::uint8_t {
    spawned,
    damaged,
    destroyed,          // turret flag 1 or placed object final stage
    score_awarded,
    return_fire,
    shot_fired,
    rebuild_started,
    rebuilt,
    removed,
    stage_lost,
    knocked_over,
    flattened,
    base_alert,
    crushed,
    explosion,          // value = effect kind 26 variant
    splash,             // notify_objects_near_point(108, 20)
    notify_area,
    debris,             // value = debris effect kind
    sound,              // value = sound id
    unit_shaken,        // owner = contacting unit; value = +0x3D4 jitter (+2/-2)
    contact_reply,      // owner = contacting unit; value = ContactReply
    collision_cleared,  // kind 9 motion flag 0x10 cleared
    owned_effects_removed, // remove_effects_by_owner (func_80093F3C)
};
static_assert(std::is_same_v<std::underlying_type_t<EventKind>,std::uint8_t>);

struct Event {
    EventKind kind{};
    EntityId entity{};
    std::uint8_t owner{0xFFU};
    std::int32_t value{};
    float x{}, z{};
    std::optional<TurretShot> shot;

    void serialize(StateWriter& writer) const;
};

// A hit from another effect (dispatcher message 0). source_kind is the
// sender's effect kind: 0 unit contact, 2/12/22/34/44 damaging effects,
// 5/18 placed contacts.
struct Hit {
    std::uint32_t source_kind{};
    std::int32_t damage{};
    std::uint8_t source_owner{0xFFU};
    std::uint16_t heading{};     // sender +0x0C
    bool shielded{};             // effect kind 5 slot mode 20 gate
    bool flame_forced{};         // sender arg +0x18 (kind 18 flame guard)
};

enum class MessageResult : std::uint8_t { ignored, handled, unsupported, inactive, stale };
static_assert(std::is_same_v<std::underlying_type_t<MessageResult>,std::uint8_t>);

struct Snapshot {
    EntityId id{};
    Kind kind{};
    bool active{};
    bool destroyed{};
    std::uint8_t subtype{};     // turret type, placed slot mode (low byte) or 0
    std::uint8_t team{};
    std::uint8_t stage{};       // model set currently drawn
    std::uint8_t stage_count{};
    std::uint8_t fade{};
    std::int16_t health{};
    std::uint16_t heading{};
    float x{}, z{};
    float scale{1.0F};
    Bounds bounds{};
};

struct UpdateContext {
    const AngleTable* angles{};
    std::span<const TurretTarget> targets;
    std::span<const TurretOwnerConfig> owner_configs;     // by turret owner index
    std::span<const std::uint8_t> owner_effect_counts;    // func_800A68AC by team
    std::int32_t tick{};
    float frame_delta{1.0F};
    std::int32_t search_range{turret_search_range};
    RetailRandom* rng{};
    std::function<bool(float x,float z,float half)> area_clear;
    std::function<bool(float x,float z,std::uint8_t target)> line_of_sight;
};

struct TurretSpawn {
    std::uint8_t team{};
    std::uint8_t owner{};
    std::uint8_t type{};
    std::uint16_t heading{};
    float x{}, z{};
    bool check_area{};
    bool scaled{};
};

class World {
public:
    using Pool=EntityPool<Entry,world_capacity>;

    // spawn_squad_unit_at: nullopt when the type is invalid, the area is
    // blocked or the pool is full.
    std::optional<EntityId> spawn_turret(const TurretSpawn& spawn,const UpdateContext& context);
    std::optional<EntityId> spawn_placed(const PlacedObjectRecord& record,const Bounds& bounds);
    std::optional<EntityId> spawn_breakable(const BreakableRecord& record,const Bounds& bounds);
    bool remove(EntityId id) noexcept;
    bool set_active(EntityId id,bool active) noexcept;

    // Dispatcher message 0.
    MessageResult hit(EntityId id,const Hit& contact,std::span<OwnerRecord> owners,std::int32_t tick,
                      std::int32_t game_mode);
    // Message 3: explosion at (x, z).
    MessageResult blast(EntityId id,float x,float z,std::span<OwnerRecord> owners,std::int32_t game_mode,
                        const AngleTable& angles);
    // Message 7: directed damage from (x, z).
    MessageResult direct_damage(EntityId id,std::int32_t damage,float x,float z,const AngleTable& angles);
    // TEMPORARY POLICY: compatibility overloads for existing callers; they
    // use reference_angle_table() for fling headings, which is not the
    // retail table. New callers pass the retail AngleTable.
    MessageResult blast(EntityId id,float x,float z,std::span<OwnerRecord> owners,std::int32_t game_mode);
    MessageResult direct_damage(EntityId id,std::int32_t damage,float x,float z);
    // Messages 6 and 37 answer 1 for turrets (func_800A27D0). Message 6 is
    // the attachment-offset query sent by draw_owner_flash (func_800F26B4);
    // the meaning of 37 is UNRESOLVED (no literal sender found).
    [[nodiscard]] bool answers_query(EntityId id,std::uint32_t message) const noexcept;

    void update(const UpdateContext& context);

    [[nodiscard]] const Entry* get(EntityId id) const noexcept { return pool_.get(id); }
    [[nodiscard]] std::size_t size() const noexcept { return pool_.size(); }
    [[nodiscard]] std::vector<Snapshot> snapshots() const;
    [[nodiscard]] std::vector<EntityId> at_point(float x,float z) const;
    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }
    [[nodiscard]] bool events_overflowed() const noexcept { return events_overflowed_; }
    void clear_events() noexcept { events_.clear(); events_overflowed_=false; }
    [[nodiscard]] std::uint8_t next_serial() const noexcept { return serial_; }

    void serialize(StateWriter& writer) const;

private:
    void push_event(const Event& event);
    void emit(EventKind kind,EntityId id,std::uint8_t owner,std::int32_t value,float x,float z);
    void finish_breakable(EntityId id,Entry& entry);
    MessageResult drop_stage(EntityId id,Entry& entry,bool blasted);
    MessageResult fling_crushable(EntityId id,Entry& entry,float x,float z,const AngleTable& angles);
    MessageResult crushable_contact(EntityId id,Entry& entry,const Hit& contact,std::int32_t tick);

    Pool pool_{};
    std::uint8_t serial_{};
    std::vector<Event> events_;
    bool events_overflowed_{};
};

} // namespace battletanx::placed
