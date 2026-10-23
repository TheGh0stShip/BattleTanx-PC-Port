#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/level.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace battletanx {

// Interpretation of world-stream items that place units, spawners, markers and
// pickups. Two retail consumers exist:
//  * the entity bank (spawn_bank_record_entries, func_80088670) reads the one
//    group named by world header +0x280;
//  * the placed-object loop (load_level_data.c, func_80088CF0) reads every
//    other group. Only its spawner types (20/21) are interpreted here.
//
// WorldLevel retains header +0x280 (bank group index) and +0x284
// (s32 spawnCounts[22]) so the native runtime can reproduce this split.

enum class SpawnSource : std::uint8_t {
    bank_unit_placement,      // bank type 22 (func_800A1408)
    bank_spawner,             // bank type 21 (unlinked kind-0x1C spawner)
    placed_spawner_linked,    // placed type 20 in modes 3..6 (linked to its kind-5 effect)
    placed_spawner_unlinked,  // placed type 21 (view count 1)
};

struct UnitSpawnDefinition {
    std::uint32_t placement_index{};  // index into WorldLevel::items
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t unit_kind{};         // not encoded in type 22 items; always 0 here
    std::uint8_t owner{};             // player slot after the D_801260A4 remap
    std::uint16_t interval_ticks{};   // 0: one-shot placement
    bool player{};                    // owner/player-slot placement (type 22)
    SpawnSource source{SpawnSource::bank_unit_placement};
    std::uint8_t team{};              // context.teams[owner]
    std::uint8_t side{};              // (id>>3)&3
    bool optional_entry{};            // id&1: needs the optional-placement gate
    bool respawn_flag{};              // modes 3..6 ? 0 : (id>>8)&1
};

struct SpawnerDefinition {
    std::uint32_t placement_index{};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint16_t id{};
    std::uint8_t owner{};             // id & 3
    std::uint8_t mode{};              // (id>>2) & 3
    std::uint8_t shape{};             // mode -> area shape 0,1,2,2
    std::uint8_t count_field{};       // (id>>4) & 0xFF
    std::int16_t interval_ticks{};    // step * count_field, step 30 or 45
    std::uint8_t parameter{};         // record +0x30 (0x45 is gated)
    std::uint8_t gate_bits{};         // (id>>12) & 3, record +0x31
    bool linked{};                    // created with a type-20 kind-5 effect
    SpawnSource source{SpawnSource::bank_spawner};
};

// spawn_effect_kind_1c (func_8010D480). fast_interval mirrors D_80134E64.
[[nodiscard]] SpawnerDefinition unpack_spawner(std::uint16_t id,std::uint8_t parameter,
                                               bool fast_interval) noexcept;

struct SpawnMarker {
    std::uint32_t placement_index{};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t low{};               // parameter & 0xF
    std::uint8_t param{};             // id & 0xFF
    std::uint8_t high{};              // ((s16)id >> 12) & 3
};

inline constexpr std::size_t marker_groups=4U;      // MarkerTable slots[4][5]
inline constexpr std::size_t markers_per_group=5U;

struct PickupPlacement {
    std::uint32_t placement_index{};
    float x{}, z{};
    std::uint8_t kind{};              // raw parameter; pick_effect_kind still applies
};

inline constexpr std::size_t max_random_pickup_spots=100U;
inline constexpr std::uint8_t random_pickup_spot_parameter=22U;
inline constexpr std::size_t pickup_spawn_count_kinds=22U;

enum class SkipReason : std::uint8_t {
    mode_flag_filtered,       // flags 0x10 outside 3..6 or 0x40 inside 3..6
    pickup_mode_filtered,     // type 10 without 0x10 in modes 3..6
    capacity_gated,           // placed flags&2 gate rejected
    view_count_gated,         // spawner/23/24 needs view count 1
    linked_spawner_mode,      // placed type 20 outside 3..6: no spawner
    optional_entry_gated,     // type 22 with id&1 and the gate closed
    player_slot_absent,       // type 22 player >= active player count
    marker_group_out_of_range,// parameter>>4 >= 4 (retail would overrun)
    marker_group_full,        // already 5 entries in the group
    random_spot_overflow,     // more than 100 spot candidates
    untraced_type,            // bank 23/24: recognised, behaviour untraced
    retail_ignored_type,      // bank type the retail switch has no case for
    placed_object_other,      // placed type handled by another system
    unknown_placed_type,      // placed type not in the recovered table
};

struct SkippedItem {
    std::uint32_t placement_index{};
    std::int32_t type{};
    SkipReason reason{};
};

struct SpawnContext {
    std::int32_t game_mode{};                      // gGameMode; 3..6 = campaign
    std::int32_t view_count{1};                    // func_8007AAA0
    std::optional<std::size_t> bank_group;         // header +0x280 (nullopt = -1)
    bool fast_spawn_interval{};                    // D_80134E64
    bool optional_placements_enabled{};            // func_8007C728() && D_80134E67
    std::int32_t active_player_count{1};           // func_8007AAB0
    std::array<std::uint8_t,4> player_slot_map{0U,1U,2U,3U}; // D_801260A4
    std::array<std::uint8_t,4> teams{0U,1U,2U,3U}; // game mode +0x1D4
    // Placed flags&2 gate: return false to remove the object. The argument is
    // (flags&0xC)>>2. Empty means "kept".
    std::function<bool(std::uint8_t team_slot)> flag2_gate;
};

[[nodiscard]] constexpr bool campaign_mode(std::int32_t mode) noexcept {
    return mode>=3 && mode<=6;
}

struct LevelSpawnPlan {
    std::array<std::vector<SpawnMarker>,marker_groups> markers;
    std::vector<UnitSpawnDefinition> units;
    std::vector<SpawnerDefinition> spawners;
    std::vector<PickupPlacement> pickups;
    std::vector<PickupPlacement> random_spots;     // type 10 parameter 22
    std::vector<SkippedItem> skipped;
    bool random_distribution_allowed{};            // false in modes 3..6

    [[nodiscard]] std::size_t skipped_count(SkipReason reason) const noexcept;
};

[[nodiscard]] LevelSpawnPlan interpret_level_spawns(const WorldLevel& level,
                                                    const SpawnContext& context);

// pick_effect_kind: re-roll for kinds 5,6,7,9,10,13,14,16 when D_80134E68.
[[nodiscard]] std::uint8_t pick_effect_kind(std::uint8_t kind,bool reroll_option,
                                            RetailRandom& rng) noexcept;

// Tail of func_80088670 outside 3..6: place spawnCounts[kind] pickups on random
// unused spots. Returns nothing when the plan forbids it or spots < total.
[[nodiscard]] std::vector<PickupPlacement> distribute_random_pickups(
    const LevelSpawnPlan& plan,
    const std::array<std::int32_t,pickup_spawn_count_kinds>& spawn_counts,
    RetailRandom& rng);

// ---- Runtime spawner (kind 0x1C update, func_8010D59C) ----

struct SpawnerState {
    SpawnerDefinition definition;
    std::int32_t last_tick{};                 // +0x18
    std::int32_t linked_serial{};             // +0x2C
    bool finished{};
};

[[nodiscard]] SpawnerState make_spawner_state(const SpawnerDefinition& definition,
                                              std::int32_t linked_serial=0) noexcept;

struct LinkedEffectView {
    std::int32_t serial{};                    // linked +0x04
    bool destroyed{};                         // linked +0x0E != 0
};

struct SpawnerOwnerView {
    std::int32_t budget{};                    // owner +0x208
    bool gate_open{};                         // owner +0x23 bit 0
    std::array<bool,5> squad_slot_alive{};    // get_unit(owner->units[i])->value01
};

struct SpawnAreaQuery {
    float x{}, z{};
    std::uint16_t angle{};                    // heading + 0x200
    std::uint8_t shape{};
};

struct SpawnRequest {
    std::uint8_t owner{};
    std::uint8_t squad_slot{};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t shape{};
    std::uint8_t parameter{};
    std::int32_t tick{};
};

enum class SpawnerUpdateStatus : std::uint8_t {
    finished,              // linked effect replaced (serial changed)
    finished_destroyed,    // linked effect destroyed: cooldown event 0x22, 3.0 on entity 0
    gated,                 // parameter 0x45 or gate bits 1 with owner bit clear
    interval_pending,
    roll_failed,
    no_budget,
    area_blocked,
    no_free_slot,
    spawned,
};

struct SpawnerUpdateResult {
    SpawnerUpdateStatus status{};
    std::optional<SpawnRequest> request;
};

inline constexpr std::int32_t spawner_destroyed_cooldown_event=0x22;
inline constexpr float spawner_destroyed_cooldown_seconds=3.0F;

// The area callback returns true when the area is clear. It must apply the
// retail grid rules itself (ignore kind 5 only when it is the linked effect,
// notify kind 0x1A wrecks with message 4, block on anything else).
using SpawnAreaClear=std::function<bool(const SpawnAreaQuery&)>;

SpawnerUpdateResult update_spawner(SpawnerState& spawner,
                                   const std::optional<LinkedEffectView>& linked,
                                   SpawnerOwnerView& owner,std::int32_t tick,
                                   RetailRandom& rng,const SpawnAreaClear& area_clear);

} // namespace battletanx
