#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/pickup_effects.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace battletanx {

// ---------------------------------------------------------------- unit kinds
// spawn_unit (func_80097038). Movement tables are indexed bike=0, tank=1,
// goliath=2 (note: not the unit kind order).
struct UnitMovementConstants {
    float grip{};             // +0x3B8 = D_80125F18 (ROM 0xB5F18)
    float mass{};             // D_80125F24 (ROM 0xB5F24); +0x378 = 1/mass
    float inverse_mass{};     // +0x378
    float inverse_inertia{};  // +0x37C = 1/(mass/12*6554)
    float value3BC{};         // +0x3BC = D_80125EFC (ROM 0xB5EFC); use unresolved
    float accel{};            // +0x3C0 = D_80125EF0 (ROM 0xB5EF0); also the rail turret rate
    float drag{};             // +0x3C4 = D_80125F08 (ROM 0xB5F08)
};

struct UnitKindDefinition {
    retail::UnitKind kind{};
    std::int32_t max_health{};
    retail::Footprint footprint{};
    std::uint16_t grid_mask{0x4D6U};       // func_80106D18 mode
    UnitMovementConstants movement{};
    std::uint8_t primary_weapon{};         // +0x468
    std::int16_t primary_ammo{};           // ammo[primary_weapon]
    std::int32_t kill_score{};             // destroy_unit, modes other than 7
    bool crushed_by_goliath{};             // unit_handle_hit_message case 0
    bool rail_capable{};                   // spawn_unit kind 2 + rail zone
};

enum class UnitModelTransform : std::uint8_t { body, turret };
enum class UnitModelAnimation : std::uint8_t { none, left_track, right_track };
struct UnitModelPart {
    std::uint16_t slot{};
    UnitModelTransform transform{};
    UnitModelAnimation animation{};
};
struct UnitModelSet {
    std::array<UnitModelPart,5> parts{};
    std::uint8_t part_count{};
    std::uint16_t shadow_slot{};
};

[[nodiscard]] std::optional<UnitKindDefinition> unit_kind_definition(std::uint8_t kind) noexcept;
[[nodiscard]] std::optional<UnitModelSet> unit_model_set(std::uint8_t kind,bool damaged) noexcept;

// spawn_unit rail zone (session +0xC8, 4 zones of 0x14). World coordinates; the
// retail record stores each point as {z-named, x-named} with x in the second word.
struct RailZone {
    bool active{};
    float start_x{}, start_z{}, end_x{}, end_z{};
};

inline constexpr std::uint16_t flag_rail_unit=0x0004U;
inline constexpr std::uint16_t flag_rail_spawn=0x0804U;   // spawn_unit: flags |= 0x804

struct RailState {
    float origin_x{}, origin_z{}, dir_x{}, dir_z{};
    float length{}, travel{}, velocity{};
    std::int32_t exit_tick{};   // +0x4EC, 0 = boost not held

    void serialize(StateWriter& writer) const;
};

struct UnitKindSpawnOptions {
    std::int32_t game_mode{};
    std::uint16_t flags{};             // UnitSpawn +0x1C
    bool local_owner{};                // owner +0x1A6
    bool goliath_health_bonus{};       // func_8007C6D8 for local goliaths
    bool reveal_cheat{};               // D_80135761: flags |= 0x80
    bool stocked_cheat{};              // D_80135765
    bool permanent_cloak_cheat{};      // D_8013576A (owner 0 primary)
    bool primary{};
    bool owner_rail_taken{};           // owner +0x23 & 2
    std::optional<RailZone> rail;      // zone D_801260A0[player]
    const AngleTable* angles{};        // required for rail heading
};

struct UnitKindSpawn {
    UnitRecord unit;
    TimedPowerupState timed;
    std::optional<RailState> rail;
    bool rail_claimed{};               // owner +0x23 |= 2
};

// Wraps make_unit and applies the per-kind and option-dependent spawn_unit
// defaults. Returns nullopt for unit kinds outside 0..2.
[[nodiscard]] std::optional<UnitKindSpawn> make_unit_of_kind(std::uint8_t kind,std::uint8_t owner,
        std::uint8_t team,float x,float z,std::uint16_t heading,std::int32_t tick,
        UnitControl control,const UnitKindSpawnOptions& options);

// update_rail_unit_control (func_8009BBC0), movement part.
struct RailInput {
    float move{};     // -100..100
    float aim{};      // -1..1 (multiplied by 100 in retail)
    bool boost{};
    bool ai_state_two{};
};
struct RailStepResult {
    bool bumped_end{};     // sound 0x17 at the rail end (speed > 15.9)
    bool exited{};         // flag 4 cleared after 60 ticks of boost
    bool exit_sound{};     // sound 0x15 started
};
RailStepResult update_rail_unit(UnitRecord& unit,RailState& rail,const RailInput& input,
                                std::int32_t game_mode,std::int32_t tick,float frame_delta,
                                bool exit_keeps_body_heading);

// ---------------------------------------------------------------- turrets
// Effect kind 27 (0x1B): spawn_squad_unit_at (func_800A1408), update_turret
// (func_800A16C0), damage bonus_crate_handle_message (func_800A27D0).
inline constexpr std::size_t turret_types=4U;
inline constexpr std::array<std::int16_t,turret_types> turret_shot_speed{70,48,48,48};    // D_80125FC0
inline constexpr std::array<std::int16_t,turret_types> turret_turn_rate{384,256,192,192}; // D_80125FC8
inline constexpr std::array<std::uint16_t,turret_types> turret_health{25U,50U,100U,100U}; // D_80126010
inline constexpr std::array<std::int32_t,turret_types> turret_projectile{1,0,0,3};        // D_80126028
inline constexpr std::array<float,turret_types> turret_scale{0.726F,1.0F,1.5F,1.5F};      // D_80126038
inline constexpr std::array<std::uint32_t,turret_types> turret_return_fire_end{5U,1U,1U,1U}; // D_80126048
inline constexpr std::array<std::int32_t,turret_types> turret_burst{1,0,0,0};             // D_80126058
inline constexpr std::array<float,5> turret_burst_cycle{0.25F,0.25F,2.1F,0.25F,2.1F};     // D_80126068
inline constexpr std::array<float,5> turret_owner_count_scale{1.5F,1.0F,0.75F,0.5F,1.0F}; // D_80126080
inline constexpr std::array<std::int32_t,turret_types> turret_destroy_score{100,500,1000,2500};
inline constexpr std::array<std::uint16_t,turret_types> turret_fire_sound{0x22U,1U,0x16U,9U};
inline constexpr float turret_muzzle_x=-40.0F, turret_muzzle_y=20.0F, turret_muzzle_z=50.0F; // D_80125FD0
inline constexpr float turret_half_extent=45.0F;
inline constexpr float turret_max_range_sq=25000000.0F;
inline constexpr float turret_search_initial=100000000.0F; // D_80072C78
inline constexpr float turret_search_scale=0.92F;          // D_80072C7C
inline constexpr std::int32_t turret_search_range=2000;    // D_80125858 default
inline constexpr std::uint8_t no_turret_target=0xFFU;

inline constexpr std::uint8_t turret_destroyed=0x01U;
inline constexpr std::uint8_t turret_muzzle_side=0x02U;
inline constexpr std::uint8_t turret_scaled=0x04U;     // spawn "flagged"
inline constexpr std::uint8_t turret_rebuilding=0x08U;
inline constexpr std::uint8_t turret_rebuild=0x10U;

struct TurretRecord {
    std::uint8_t flags{};          // +0x0C
    std::uint8_t team{};           // +0x0D
    std::uint8_t cycle{};          // +0x0E
    std::uint8_t owner{};          // +0x10
    std::uint8_t type{};           // +0x14 (0..3)
    std::uint16_t heading{};       // +0x18
    float x{}, z{};
    std::int32_t last_fire{};      // +0x28
    std::int16_t health{};         // +0x2C
    std::uint8_t target{no_turret_target}; // +0x2E
    std::uint8_t fade{0xFFU};      // +0x30
    std::uint8_t cooldown{};       // +0x31
    bool has_los{};                // +0x32
    std::int32_t aim_time{-1};     // +0x34
    std::uint16_t aim_heading{};   // +0x38
    bool removed{};

    void serialize(StateWriter& writer) const;
};

// Owner +0x318, +0x338, +0x340.
struct TurretOwnerConfig {
    float rebuild_time{100.0F};
    std::array<std::int16_t,turret_types> effect_kind{};
    std::array<std::int16_t,turret_types> fire_delay{};
};

struct TurretTarget {
    const UnitRecord* unit{};   // index in this span is the retail unit id
    bool targetable{};          // func_8009FB8C
    bool ai_state_two{};        // func_800E3460
};

struct TurretShot {
    float x{}, z{};             // muzzle
    std::uint16_t heading{};
    float velocity_x{}, velocity_z{};
    std::int16_t speed{};
    std::int32_t projectile{};  // 3 = beam (func_800F0FD8)
    std::int16_t effect_kind{};
    std::uint16_t sound{};
    std::uint8_t target{no_turret_target};
};

struct TurretContext {
    const AngleTable& angles;
    std::span<const TurretTarget> targets;
    const TurretOwnerConfig& owner;
    std::uint8_t owner_effect_count{};      // func_800A68AC(team), 0..4
    std::int32_t tick{};
    float frame_delta{1.0F};                // D_801257B0
    std::int32_t search_range{turret_search_range};
    RetailRandom& rng;
    std::function<bool(float x,float z,float half)> area_clear;          // func_80107A14 query
    std::function<bool(float x,float z,std::uint8_t target)> line_of_sight; // func_8010874C
};

struct TurretUpdateResult {
    std::optional<TurretShot> shot;
    bool removed{};
    bool started_rebuild{};
    bool rebuilt{};
    bool notified_target{};    // func_800E3C18
};

[[nodiscard]] std::optional<TurretRecord> spawn_turret(std::uint8_t team,std::uint8_t owner,std::uint8_t type,
        std::uint16_t heading,float x,float z,bool check_area,bool scaled,const TurretContext& context);
void find_turret_target(TurretRecord& turret,const TurretContext& context) noexcept;
TurretUpdateResult update_turret(TurretRecord& turret,TurretContext& context);

struct TurretDamageResult {
    bool handled{};
    bool destroyed{};
    std::int32_t score{};
    std::uint8_t lives_awarded{};
    bool return_fire{};
};
// Damage messages 2, 12, 22, 34, 44.
TurretDamageResult damage_turret(TurretRecord& turret,std::uint32_t message,std::int32_t damage,
                                 std::uint8_t attacker_owner,std::uint16_t hit_heading,
                                 std::span<OwnerRecord> owners,std::int32_t tick,std::int32_t game_mode);

// ---------------------------------------------------------------- placed objects
enum class PlacedObjectClass : std::uint8_t {
    staged,      // effect kind 5 (types 5, 11-14, 20, 31): multi-stage destructible
    knockable,   // effect kind 6 (type 3): knocked over by explosion message 3
    crushable,   // effect kind 9 (type 6): flattened by any damage message
};

enum class PlacedObjectEventKind : std::uint8_t {
    base_alert,       // owner event 1 for 3.0 (types 11-14)
    stage_lost,
    destroyed,        // last stage reached
    score_awarded,
    knocked_over,
    flattened,
};
struct PlacedObjectEvent {
    PlacedObjectEventKind kind{};
    std::uint8_t owner{0xFFU};
    std::int32_t value{};
};

inline constexpr std::array<std::uint8_t,4> slot_rank{0U,1U,2U,3U};   // D_801260A4
inline constexpr std::int32_t placed_base_score=1000;
inline constexpr std::int32_t placed_collapse_ticks=60;              // update_effect_kind_5 (0x3C)
inline constexpr float placed_splash_radius=108.0F;                  // destroy_placed_object
inline constexpr float placed_splash_value=20.0F;

struct PlacedObjectRecord {
    PlacedObjectClass object_class{PlacedObjectClass::staged};
    std::uint32_t slot_mode{};      // placed-object type (slot +4)
    std::uint8_t flags{};           // +0x0C
    std::uint8_t stage_count{};     // +0x0D
    std::uint8_t stage{};           // +0x0E
    std::int16_t health{};          // +0x12
    std::int32_t collapse_ticks{};  // update_effect_kind_5 frame
    bool knocked{};                 // kind 6 released
    float x{}, z{};

    void serialize(StateWriter& writer) const;
};

// spawn_effect_kind_5: model set count reduced by 1 (flags & 4) or 2 (flags & 8).
[[nodiscard]] PlacedObjectRecord make_placed_object(PlacedObjectClass object_class,std::uint32_t slot_mode,
                                                    std::uint8_t flags,std::uint8_t set_count,float x,float z);
[[nodiscard]] bool placed_object_destroyed(const PlacedObjectRecord& object) noexcept;

// zone_effect_handle_message (func_80093080) / pulse (func_80093590) /
// slot (func_800EFFC8) damage paths.
std::vector<PlacedObjectEvent> damage_placed_object(PlacedObjectRecord& object,std::uint32_t message,
        std::int32_t damage,std::uint8_t source_owner,bool shielded,std::span<OwnerRecord> owners,
        std::int32_t game_mode);
// Collapse animation; advances the stage when it finishes.
std::vector<PlacedObjectEvent> update_placed_object(PlacedObjectRecord& object);

// destroy_placed_object (func_8008FD6C), effect kind 18.
struct PlacedDestroyResult {
    std::int32_t explosion_variant{-1};  // effect kind 26 variant (22 or 5), -1 = silent
    bool splash{};                       // notify_objects_near_point(108, 20)
    bool notify_area{};
};
[[nodiscard]] PlacedDestroyResult destroy_placed_object(std::uint8_t flags) noexcept;

} // namespace battletanx
