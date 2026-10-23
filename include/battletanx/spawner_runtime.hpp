#pragma once

#include "battletanx/campaign_spawn.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/runtime_constants.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

// Runtime for kind-0x1C spawners (spawn_available_owner_unit func_8010D59C)
// and the two squad respawn paths (respawn_squad_unit func_800A5348,
// respawn_squad_member func_800A5784), layered over level_spawns.hpp
// (SpawnerDefinition/SpawnerState) and campaign_spawn.hpp (TeamSpawnPoint,
// SpawnRespawnPoint). Every spawn is emitted as an ordered value request; no
// callback creates units.
namespace battletanx::spawner_runtime {

using retail::UnitKind;

inline constexpr std::size_t owner_count=4U;
inline constexpr std::size_t member_slots=5U;            // owner->units[5]
inline constexpr std::size_t area_query_capacity=16U;    // func_80107A14 capacity
inline constexpr std::int32_t marker_reuse_ticks=360;    // func_800A5784
inline constexpr std::uint8_t gated_parameter=0x45U;     // spawner +0x30
inline constexpr std::uint8_t gated_bits_value=1U;       // spawner +0x31
inline constexpr std::uint32_t spawn_roll_mask=0xFU;     // 1 in 16
inline constexpr std::int32_t fast_interval_step=30;     // D_80134E64 != 0
inline constexpr std::int32_t slow_interval_step=45;
inline constexpr float spawner_area_scale=1.0F;          // D_800755B0 (ROM 0x55B0)
inline constexpr float respawn_area_scale=1.0F;          // literal in 800A5348/800A5784
inline constexpr std::int32_t wreck_kind=0x1A;           // notified with message 4
inline constexpr std::int32_t linked_effect_kind=5;
inline constexpr std::int32_t wreck_message=4;
inline constexpr std::int32_t destroyed_event=0x22;      // func_800A51E8(owner 0, 0x22, 3.0)
inline constexpr float destroyed_event_seconds=3.0F;
inline constexpr std::int32_t forced_flag_game_kind=9;   // func_8009FA70

// func_8009FA70 UnitSpawn +0x1C flags.
inline constexpr std::uint16_t order_flag_option=0x0100U;
inline constexpr std::uint16_t order_flag_force=0x1000U;
inline constexpr std::uint16_t order_flag_campaign_player=0x8000U;

[[nodiscard]] constexpr bool campaign_kind(std::int32_t game_kind) noexcept {
    return game_kind>=3 && game_kind<=6;
}

// spawner +0x10 mode -> area shape / unit kind (mode 3 draws one random bit).
[[nodiscard]] std::optional<UnitKind> unit_kind_for_mode(std::uint8_t mode) noexcept;
[[nodiscard]] constexpr std::int32_t interval_step(bool fast_interval) noexcept {
    return fast_interval ? fast_interval_step : slow_interval_step;
}

// ---- Area box (build_area_box_by_shape func_80096E80 -> init_area_box func_80109268) ----

struct AreaBox {
    std::int16_t x{}, z{};                     // float position truncated to s32, stored s16
    std::int16_t min_a{}, max_a{}, min_b{}, max_b{};
    std::uint16_t radius{};                    // integer_sqrt(a*a+b*b)
    std::uint16_t angle{};                     // box +0x10
};

// Nullopt for shapes above 2 (retail leaves the box unwritten) or a
// non-finite/out-of-range coordinate.
[[nodiscard]] std::optional<AreaBox> build_area_box(float x,float z,std::uint16_t angle,
                                                    std::uint32_t shape,float scale) noexcept;

// One func_80107A14 result. reserved_handle mirrors result pointers below 4,
// which every caller skips.
struct AreaHit {
    bool reserved_handle{};
    std::int32_t kind{};
    std::uint32_t object_token{};              // caller-defined identity
    bool is_linked_effect{};                   // equals spawner +0x28
};

struct AreaQueryResult {
    bool found{};                              // func_80107A14 return byte
    std::vector<AreaHit> hits;                 // only the first 16 are read
};

using AreaQuery=std::function<AreaQueryResult(const AreaBox&)>;

enum class AreaRule : std::uint8_t {
    spawner,     // kind 5 passes only if it is the linked effect; sender = spawner
    respawn,     // kind 5 blocks like any other kind; sender = null
};
static_assert(sizeof(AreaRule)==1U);

struct AreaVerdict {
    bool clear{true};
    std::vector<std::uint32_t> wreck_notifications;   // tokens sent message 4, in order
};

// Scans hits in order. The spawner rule returns at the first blocker, so
// wrecks after it are not notified; the respawn rule notifies every wreck.
// has_link: the spawner has a linked effect (+0x28 non-null); without one
// every kind-5 hit blocks.
[[nodiscard]] AreaVerdict evaluate_area(const AreaQueryResult& result,AreaRule rule,
                                        bool has_link=false);

// ---- Ordered spawn requests ----

enum class OrderSource : std::uint8_t { spawner, squad_unit_respawn, squad_member_respawn };
static_assert(sizeof(OrderSource)==1U);

struct SpawnOrder {
    std::uint32_t sequence{};
    OrderSource source{};
    std::uint8_t owner{};
    std::uint8_t member_slot{};
    std::uint16_t unit_id{};
    UnitKind kind{UnitKind::tank};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t parameter{};                  // UnitSpawn +0x1E
    std::uint16_t flags{};                     // UnitSpawn +0x1C
    std::int32_t tick{};
};

// spawn_unit_at_placement flag rule.
[[nodiscard]] std::uint16_t order_flags(std::int32_t game_kind,std::uint8_t owner,
                                        bool option,bool force) noexcept;

// Rollback (TEMPORARY POLICY, host only): the producers apply the retail side
// effects when the order is queued, because retail spawn_unit is synchronous.
// A consumer that drops an order must clear owner.unit_active[member_slot]. It
// cannot restore the spawner budget, last_tick, squad reserve/member counts or
// MemberMarker::last_used from the order alone, so a dropped order is treated as
// a spawn that died at once. Consumers should not drop orders.
class SpawnOrderQueue {
public:
    explicit SpawnOrderQueue(std::size_t capacity=32U) : capacity_(capacity) {}
    [[nodiscard]] bool full() const noexcept { return orders_.size()>=capacity_; }
    bool push(SpawnOrder order);
    [[nodiscard]] const std::vector<SpawnOrder>& orders() const noexcept { return orders_; }
    std::vector<SpawnOrder> drain();
    // Writes the sequence counter and pending orders. The capacity is host
    // configuration and is not written.
    void serialize(StateWriter& writer) const;

private:
    std::size_t capacity_;
    std::uint32_t next_sequence_{};
    std::vector<SpawnOrder> orders_;
};

// ---- Owner records seen by the spawner and respawn paths ----

struct OwnerSpawnRecord {
    std::array<std::uint16_t,member_slots> unit_ids{};   // owner +0x00
    std::array<bool,member_slots> unit_active{};         // get_unit(id)->value01
    std::int32_t budget{};                               // owner +0x208 (non-zero passes; >0 decrements)
    bool gate_open{};                                    // owner +0x23 bit 0
    void serialize(StateWriter& writer) const;
};
using OwnerSpawnRecords=std::array<OwnerSpawnRecord,owner_count>;

// ---- Spawner set (kind 0x1C) ----

enum class SpawnerCreateError : std::uint8_t {
    pool_full,              // allocate_effect failed: no record, count unchanged (TEMPORARY POLICY capacity)
    unexpected_link,        // unlinked definition given a linked serial (host input error)
    linked_outside_campaign,// placed type 20 only links in kinds 3..6 (load_level_data.c:475)
};
static_assert(sizeof(SpawnerCreateError)==1U);

enum class SpawnerTickStatus : std::uint8_t {
    finished,               // linked effect replaced (serial changed) or already finished
    finished_destroyed,     // linked effect destroyed: event 0x22 for 3.0 on owner 0
    gated,
    interval_pending,
    roll_failed,
    no_budget,
    area_blocked,
    no_free_member,         // all 5 units alive: budget and last tick unchanged
    order_queue_full,       // TEMPORARY POLICY: state unchanged after the retail draws
    spawned,
};
static_assert(sizeof(SpawnerTickStatus)==1U);

struct SpawnerTickResult {
    SpawnerTickStatus status{};
    std::vector<std::uint32_t> wreck_notifications;
    bool destroyed_event{};
};

struct SpawnerSlot {
    SpawnerState state;
    bool active{};
    // TEMPORARY POLICY "linked effect allocation failed": a linked definition
    // created without a kind-5 serial. Retail (load_level_data.c:470-476) passes
    // the null spawn_effect_kind_5 result straight to spawn_effect_kind_1c, so
    // the spawner has +0x28 == NULL, runs unlinked (every kind-5 hit blocks) and
    // still counts in spawn_counts. state.definition.linked is false here.
    bool link_failed{};
};

class SpawnerSet {
public:
    explicit SpawnerSet(std::size_t capacity=64U) : capacity_(capacity) {}

    // spawn_effect_kind_1c: increments spawn_counts[owner] on success. A linked
    // definition without linked_serial is created unlinked (see link_failed).
    [[nodiscard]] std::optional<SpawnerCreateError> create(const SpawnerDefinition& definition,
        std::int32_t game_kind,std::optional<std::int32_t> linked_serial);

    // Updates every active spawner in creation order (effect list order:
    // STRONG INFERENCE). linked_lookup maps a spawner index to its linked view.
    std::vector<SpawnerTickResult> tick(std::int32_t game_kind,std::int32_t now,
        OwnerSpawnRecords& owners,RetailRandom& rng,const AreaQuery& query,
        const std::function<std::optional<LinkedEffectView>(std::size_t)>& linked_lookup,
        SpawnOrderQueue& queue);

    [[nodiscard]] const std::vector<SpawnerSlot>& slots() const noexcept { return slots_; }
    [[nodiscard]] const std::array<std::uint8_t,owner_count>& spawn_counts() const noexcept {
        return spawn_counts_;
    }
    // Capacity is host configuration and is not written.
    void serialize(StateWriter& writer) const;

private:
    std::size_t capacity_;
    std::vector<SpawnerSlot> slots_;
    std::array<std::uint8_t,owner_count> spawn_counts_{};   // session +0x560
};

// One func_8010D59C call. Draw order: roll (&0xF), then the mode-3 bit, both
// after the gate and interval checks. A mode above 3 (host-built definition
// only; unpack_spawner masks to 0..3) uses the tank shape like the retail
// switch fallthrough. A non-finite position cannot build a box and reports
// area_blocked on every tick.
SpawnerTickResult tick_spawner(SpawnerState& spawner,const std::optional<LinkedEffectView>& linked,
                               std::int32_t game_kind,std::int32_t tick,OwnerSpawnRecords& owners,
                               RetailRandom& rng,const AreaQuery& query,SpawnOrderQueue& queue);

// ---- Squad respawn (func_800A5348 / func_800A5784) ----

struct LineupCycle {
    std::array<std::uint8_t,member_slots> kinds{};   // func_800A68C0(controller)->teams
    std::uint8_t count{1U};
    std::uint8_t next_index{};                       // squad +0x309
};

// All TEMPORARY POLICY: retail reads teams[index] unchecked as s32 values.
enum class LineupError : std::uint8_t {
    empty,                  // count 0: retail divides by zero
    index_out_of_range,     // count > 5 or next_index >= count
    only_goliath,           // count 1 and kind 2: retail loops forever
    invalid_kind,           // lineup value > 2
};
static_assert(sizeof(LineupError)==1U);

// Advances next_index exactly as the retail do/while: a goliath drawn from
// the last entry is skipped when the index wraps to 0.
[[nodiscard]] std::optional<UnitKind> next_lineup_kind(LineupCycle& lineup,LineupError* error) noexcept;

// One entry of session +0x370 group[owner]. respawn_squad_unit reads .point
// (entries 0..n-1); respawn_squad_member reads entries 1..n-1 and writes
// last_used. Both paths take the same span (RespawnContext::markers).
struct MemberMarker {
    SpawnRespawnPoint point;
    std::int32_t last_used{};                        // marker +0x14
    void serialize(StateWriter& writer) const;
};
void serialize_member_markers(StateWriter& writer,std::span<const MemberMarker> markers);

// Builds one owner's session +0x370 group from
// CampaignSpawnPlan::respawn_points[owner].points: entry i == points[i], with
// last_used=0 (the MemberMarker default; CONFIRMED: retail zeroes +0x14 at level load, load_level_data.c:294). Build
// once per owner at wave load and keep the vector alive while RespawnContext
// spans it; rebuilding resets every cooldown.
[[nodiscard]] std::vector<MemberMarker> build_member_markers(std::span<const SpawnRespawnPoint> points);

struct SquadSpawnRecord {
    std::uint8_t owner{};                            // squad +0x16 (marker group / team row)
    LineupCycle lineup;
    std::int16_t reserve{};                          // +0x3A
    std::array<std::int16_t,2> member_respawns{};    // +0x3E tank, +0x40 bike
    bool human_controlled{};                         // +0x1A6 == 0 (STRONG INFERENCE)
    bool respawn_pending{};                          // +0x24
    void serialize(StateWriter& writer) const;
};

struct RespawnContext {
    std::int32_t game_kind{};
    std::int32_t tick{};
    bool reserve_lock_player{};                      // D_80135772: owner 0 keeps its reserve
    // session +0x370 group[owner]: shared by both respawn paths. Build it from
    // CampaignSpawnPlan::respawn_points[owner] so index i is the same marker
    // for both (the caller owns the storage and keeps it alive).
    std::span<MemberMarker> markers;
    std::optional<std::array<TeamSpawnPoint,team_spawns_per_row>> team_row; // row D_801260A0[owner]
};

enum class RespawnStatus : std::uint8_t {
    spawned,
    no_clear_point,          // non-campaign: none of the 5 rotated points clear
    no_free_member,
    no_markers,              // campaign with an empty group: retail dereferences null
    no_team_row,
    lineup_error,
    order_queue_full,        // TEMPORARY POLICY
    invalid_point,           // TEMPORARY POLICY: campaign marker with a non-finite/out-of-range position
};
static_assert(sizeof(RespawnStatus)==1U);

struct RespawnResult {
    RespawnStatus status{};
    std::optional<LineupError> lineup_error;
    std::vector<std::uint32_t> wreck_notifications;
    std::optional<std::size_t> point_index;          // marker index or rotated column
};

// respawn_squad_unit. Order: lineup cycle, then (non-campaign) one draw for
// the random%5 starting column, then area checks. Campaign markers are tried in
// order; when none is clear the last marker is used anyway (retail fallthrough).
// A campaign marker that cannot build an area box returns invalid_point.
RespawnResult respawn_squad_unit(SquadSpawnRecord& squad,OwnerSpawnRecord& owner,
                                 const RespawnContext& context,RetailRandom& rng,
                                 const AreaQuery& query,SpawnOrderQueue& queue);

// respawn_squad_member. context.markers 1..n-1; a marker is skipped while
// last_used > tick-360. Shape is bike when the tank count (+0x3E) is 0. No draws.
// A marker without a valid box is skipped like a blocked one.
// Note: the chosen marker's last_used is written through context.markers (a
// mutable span) even though context is passed by const reference.
RespawnResult respawn_squad_member(SquadSpawnRecord& squad,OwnerSpawnRecord& owner,
                                   const RespawnContext& context,const AreaQuery& query,
                                   SpawnOrderQueue& queue);

// update_squad_state (func_800A3DE0) respawn gating.
struct SquadRespawnClock {
    bool active{};                                   // +0x18
    // +0x22 live unit count. Host-maintained: no writer was found in
    // spawn_unit.c (UNRESOLVED where retail updates it). update_squad_state
    // computes canRespawn after respawn_squad_member, so a caller that applies a
    // member order synchronously must refresh alive before unit_respawn_due.
    std::uint8_t alive{};
    std::uint8_t unit_cap{};                         // +0x38
    bool unlimited{};                                // +0x308
    std::int32_t due_tick{};                         // +0x300 (0 = not armed)
    std::int32_t delay{};                            // +0x304
    float delay_scale{};                             // +0x318
    void serialize(StateWriter& writer) const;
};

// Called first: whether respawn_squad_member runs this tick (kinds 3..6 only).
[[nodiscard]] bool member_respawn_due(const SquadRespawnClock& clock,const SquadSpawnRecord& squad,
                                      std::int32_t game_kind) noexcept;

// Then: arms/extends the timer and reports whether respawn_squad_unit runs.
// Call finish_unit_respawn with its outcome.
[[nodiscard]] bool unit_respawn_due(SquadRespawnClock& clock,const SquadSpawnRecord& squad,
                                    std::int32_t game_kind,std::int32_t tick) noexcept;
void finish_unit_respawn(SquadRespawnClock& clock,const RespawnResult& result) noexcept;

void serialize_spawner_state(StateWriter& writer,const SpawnerState& state);

} // namespace battletanx::spawner_runtime
