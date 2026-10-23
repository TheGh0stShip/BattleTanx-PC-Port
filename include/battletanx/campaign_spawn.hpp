#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/level.hpp"
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

namespace battletanx {

// Level-start placement for every owner (setup_players func_800A5FF0 ->
// initialize_player_record func_800A31C0), layered over interpret_level_spawns.
//
// Recovered placement rules:
//  * game kinds 3..6: owner N's squad comes from type-25 marker group N
//    (session +0x370, filled by spawn_bank_record_entries). Owner 0 is the
//    human player; its active unit is the last marker whose kind field is 10
//    (lineup slot 0), or marker 0 when none is. Computer owners start on marker 0.
//  * other kinds: owner N uses the world-header team spawn table
//    (header +0x18C, [4][5] x/z/angle) row D_801260A0[N].
//  * type-22 bank items are not units: they create entity kind 0x1B objects
//    (func_800A1408) whose sub-kind is (id>>3)&3.

inline constexpr std::size_t spawn_owner_count=4U;
inline constexpr std::size_t squad_slots=5U;
inline constexpr std::size_t team_spawn_rows=4U;
inline constexpr std::size_t team_spawns_per_row=5U;
inline constexpr std::size_t team_spawn_table_offset=0x18CU;  // world header +0x18C
inline constexpr std::uint8_t owned_object_entity_kind=0x1BU;
inline constexpr std::uint8_t marker_lineup_base=10U;         // marker kind >= 10: lineup[kind-10]
inline constexpr std::int32_t demo_game_kind=8;
inline constexpr std::size_t controller_roster_count=14U;
inline constexpr std::size_t controller_roster_size=72U;
inline constexpr std::size_t controller_roster_rom_offset=0xB62D8U; // D_801262D8

// Retail controller/team record returned by func_800A68C0. Campaign save
// offset 0x10 and each wave's +0x0C fields select one of these records; they
// are roster indices, not unit-kind values.
struct ControllerRoster {
    std::uint8_t count{};
    std::array<std::uint8_t,squad_slots> kinds{};
    std::int32_t initial_event{};       // +0x40, dispatched at unit spawn
    std::int32_t secondary_event{};     // +0x44
};

[[nodiscard]] std::optional<ControllerRoster> decode_controller_roster(
    std::span<const std::uint8_t> table,std::int32_t index) noexcept;
// poll_player_bundle_selection: the retail front end wraps selections 0..9 and
// skips locked bundle 1 until its unlock flag is set.
[[nodiscard]] std::int32_t cycle_player_bundle(
    std::int32_t current,std::int32_t direction,bool bundle_one_unlocked=false) noexcept;

struct TeamSpawnPoint {
    float x{}, z{};
    std::uint16_t heading{};
};
using TeamSpawnTable=std::array<std::array<TeamSpawnPoint,team_spawns_per_row>,team_spawn_rows>;

// Reads header +0x18C of a decompressed world stream. Nullopt if the stream
// is too short or its offset header is invalid.
[[nodiscard]] std::optional<TeamSpawnTable> parse_team_spawn_table(
    std::span<const std::uint8_t> world_stream);

struct SpawnOwnerSetup {
    bool present{};
    bool computer{};                              // setup +0x11 ("local" in the decomp)
    std::uint8_t team{};                          // game setup team[j], +0x1D4
    std::uint8_t difficulty{1U};                  // D_801262C0[j] -> owner +0x30C
    std::int32_t lives{5};                        // setup +0x18 (non-campaign unit cap)
    std::array<std::uint8_t,squad_slots> lineup{};// func_800A68C0(controller)->kinds
    std::uint8_t lineup_count{1U};                // func_800A68C0(controller)->count
};

struct CampaignSpawnInput {
    std::int32_t game_kind{3};                    // wave kind; 3..6 campaign
    std::int32_t view_count{1};                   // func_8007AAA0
    std::int32_t active_player_count{1};          // func_8007AAB0
    std::array<SpawnOwnerSetup,spawn_owner_count> owners{};
    bool fast_spawn_interval{};                   // D_80134E64
    bool optional_placements_enabled{};           // func_8007C728() && D_80134E67
    bool pickup_reroll{};                         // D_80134E68 (also forces tank lineups outside 3..6)
    // Team spawn rows for kinds outside 3..6 (parse_team_spawn_table).
    std::optional<TeamSpawnTable> team_spawns;
    // Placed flags&2 gate, forwarded to SpawnContext::flag2_gate.
    std::function<bool(std::uint8_t team_slot)> flag2_gate;
};

enum class SpawnPlanError : std::uint8_t {
    demo_playback,           // kind 8: placement comes from the recording
    player_owner_absent,     // owner 0 not present
    no_player_markers,       // kinds 3..6: marker group 0 empty
    no_team_spawn_table,     // other kinds: table missing
    invalid_player_kind,     // owner 0's active marker has no valid unit kind
};

enum class PlacementSource : std::uint8_t { campaign_marker, team_spawn_table };

enum class PlanAuditReason : std::uint8_t {
    marker_kind_invalid,          // direct kind 3..9 is not a unit kind
    marker_lineup_out_of_range,   // kind-10 >= 5 (retail reads past kinds[5])
    marker_lineup_beyond_count,   // kind-10 >= lineup count (stale lineup entry)
    lineup_kind_invalid,          // lineup value > 2
    owner_absent_markers_unused,  // markers for an owner that is not present
    unit_cap_reached,             // non-campaign: spawned units reached lives
    player_start_off_navigation,  // no open triangle contains the start
    goliath_hold_rule_untraced,   // human kind-2 marker: func_80096FC8 untraced
};

struct PlanAudit {
    std::uint8_t owner{};
    std::uint8_t slot{};
    std::uint32_t placement_index{};  // world item, or 0xFFFFFFFF for table rows
    PlanAuditReason reason{};
};

inline constexpr std::uint16_t spawn_flag_computer=0x40U;   // UnitSpawn +0x1C
inline constexpr std::uint16_t spawn_flag_held=0x100U;

struct PlannedUnit {
    std::uint8_t owner{};
    std::uint8_t team{};
    std::uint8_t slot{};                // owner->units[slot] = owner*5+slot
    std::uint16_t unit_id{};
    retail::UnitKind kind{retail::UnitKind::tank};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t parameter{};           // UnitSpawn +0x1E (marker id&0xFF)
    bool primary{};                     // UnitSpawn +0x00
    bool selectable{};                  // UnitSpawn +0x16
    std::uint16_t flags{};              // UnitSpawn +0x1C
    PlacementSource source{};
    std::uint32_t placement_index{};    // world item or table row*5+column
};

struct PlannedOwner {
    bool present{};
    std::uint8_t team{};
    std::uint16_t active_unit{};        // owner +0x20
    std::int32_t reserve{};             // owner +0x38
    std::int16_t extra_tank_respawns{}; // owner +0x3A increment (human, non-campaign)
    // owner +0x310..+0x318: human_owner_scale(difficulty); computer owners take
    // the game-mode team entry (not modelled here: nullopt).
    std::optional<float> human_scale;
};

struct PlayerStart {
    std::uint8_t owner{};
    std::uint16_t unit_id{};
    retail::UnitKind kind{retail::UnitKind::tank};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint32_t placement_index{};
    PlacementSource source{};
    std::optional<std::uint16_t> navigation_triangle;
};

struct OwnedObjectPlacement {              // bank type 22 -> func_800A1408
    std::uint32_t placement_index{};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t owner{};
    std::uint8_t team{};
    std::uint8_t sub_kind{};             // (id>>3)&3, indexes D_80126038 / D_80126010
    std::uint16_t health{};              // D_80126010[sub_kind]
    bool flagged{};                      // object +0x0C bit 2
};

struct PlannedSpawner {
    SpawnerDefinition definition;
    retail::UnitKind unit_kind{};        // area shape passed as func_8009FA70 kind
    bool random_shape{};                 // mode 3 draws a bit at runtime (same result)
};

struct SpawnRespawnPoint {
    float x{}, z{};
    std::uint16_t heading{};
    std::uint8_t parameter{};            // marker +0x10 (unit +0x1E on respawn)
    std::uint32_t placement_index{};     // world item or table row*5+column
};

enum class RespawnPolicy : std::uint8_t {
    campaign_markers,                    // func_800A5348 first clear marker in order
    team_spawn_row_random_offset,        // func_800A5348 rotating from random%5
};

struct OwnerRespawnPoints {
    RespawnPolicy policy{};
    std::vector<SpawnRespawnPoint> points;          // respawn_squad_unit candidates
    // respawn_squad_member (func_800A5784) candidates in every kind: marker
    // group entries 1..4, each reusable 360 ticks after its last use.
    std::vector<SpawnRespawnPoint> member_points;
};

struct CampaignSpawnPlan {
    bool campaign{};
    std::array<std::uint8_t,spawn_owner_count> slot_order{0U,1U,2U,3U};  // D_801260A0
    std::array<std::uint8_t,spawn_owner_count> slot_rank{0U,1U,2U,3U};   // D_801260A4
    PlayerStart player;
    std::array<PlannedOwner,spawn_owner_count> owners{};
    std::vector<PlannedUnit> units;
    std::vector<OwnedObjectPlacement> owned_objects;
    std::vector<PlannedSpawner> spawners;
    std::array<OwnerRespawnPoints,spawn_owner_count> respawn_points{};
    std::vector<PickupPlacement> pickups;   // fixed and distributed, kind after re-roll
    std::vector<SkippedItem> skipped;       // world items rejected by interpret_level_spawns
    std::vector<PlanAudit> audits;

    [[nodiscard]] std::size_t audit_count(PlanAuditReason reason) const noexcept;
    void serialize(StateWriter& writer) const;
};

// shuffle_four_slot_order (func_800A30A8) for kinds < 3 and 9; identity otherwise
// (func_800A3070).
void assign_slot_order(std::int32_t game_kind,RetailRandom& rng,
                       std::array<std::uint8_t,spawn_owner_count>& order,
                       std::array<std::uint8_t,spawn_owner_count>& rank) noexcept;

// Random-number order: slot shuffle, fixed pickup re-rolls in item order, then
// the distributed pickups (spot draw, then re-roll, per pickup).
[[nodiscard]] std::optional<CampaignSpawnPlan> build_campaign_spawn_plan(
    const CampaignSpawnInput& input,const WorldLevel& level,
    const NavigationLevel* navigation,RetailRandom& rng,SpawnPlanError* error);

} // namespace battletanx
