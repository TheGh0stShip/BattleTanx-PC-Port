#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/pickup_effects.hpp"
#include "battletanx/pickup_runtime.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

// Power-up rules that sit between the world-side pickup lifecycle
// (pickup_runtime) and the unit/owner effect switch (pickup_effects):
// contact eligibility, targetability under cloak, shield absorption, ammo
// consumption with the unlimited-ammo option, the retail weight table, the
// random-spot distribution and the per-contact effect request with its deltas.
namespace battletanx::powerup {

// ---- Kind census (counts only) ----
// Type-10 bank items across the 22 world streams (bank group 0 in 21 streams;
// stream 27 has no bank). Index = item parameter; 22 is the random-spot marker.
inline constexpr std::array<std::uint16_t,23> world_placed_kind_count{
    473U,10U,119U,19U,88U,270U,0U,43U,24U,17U,13U,0U,9U,25U,83U,33U,0U,0U,0U,0U,0U,0U,464U};
// Sum over streams of header +0x284 spawnCounts[kind] (random distribution).
inline constexpr std::array<std::uint16_t,22> world_distributed_kind_count{
    24U,8U,47U,47U,40U,46U,0U,31U,35U,34U,25U,0U,34U,0U,0U,25U,0U,0U,0U,0U,0U,0U};

enum class KindSource : std::uint8_t {
    world_item,        // placed or distributed by a world stream
    wreck_drop,        // destroy_unit: kind 11, 210 ticks
    reroll_only,       // reachable only through pick_effect_kind
    spawn_grant_only,  // reachable only through func_8009A22C's random table
    unused,            // no retail producer found
};
static_assert(std::is_same_v<std::underlying_type_t<KindSource>,std::uint8_t>);
[[nodiscard]] KindSource pickup_kind_source(std::uint8_t kind) noexcept;

// ---- Unit control mode (+0x350) ----
// ai_set_unit_mode (func_800E26DC): 1 = computer-driven leader, 2 = engaged
// follower or non-selectable unit; 0 = not under computer control.
inline constexpr std::uint8_t unit_mode_player=0U;
inline constexpr std::uint8_t unit_mode_computer_leader=1U;
inline constexpr std::uint8_t unit_mode_follower=2U;
// TEMPORARY POLICY mapping for hosts that only know UnitControl.
[[nodiscard]] std::uint8_t retail_unit_mode(UnitControl control,bool squad_follower) noexcept;

// func_80091608 / func_800916B8 contact test plus the extra mode check in
// update_tank_drive before apply_pickup.
struct ContactQuery {
    bool pickup_consumed{};
    bool contact_is_unit{true};          // grid contact kind 0
    std::uint8_t collector_class{};      // owner record +0x1AE
    std::int32_t pickup_owner_class{level_pickup_owner_class};
    std::uint8_t unit_mode{};            // unit +0x350
};
[[nodiscard]] bool pickup_contact_eligible(const ContactQuery& query) noexcept;

// ---- Targetability (func_8009FB8C) ----
inline constexpr std::int32_t mine_untargetable_ticks=90;  // now-0x5A >= +0x4B0
struct TargetabilityInput {
    std::int32_t tick{};
    std::int32_t value10{};                                  // +0x10
    bool carrying_pickup{};                                  // +0x98 non-null
    std::int32_t last_mine_tick{retail::never_fired_tick};   // +0x4B0
    std::int32_t cloak_end_tick{};                           // +0x0C
    std::int32_t last_fire_tick{retail::never_fired_tick};   // +0x4B4
};
[[nodiscard]] bool unit_targetable(const TargetabilityInput& input) noexcept;
[[nodiscard]] TargetabilityInput targetability_from(const TimedPowerupState& timed,std::int32_t tick,
                                                    bool carrying_pickup,std::int32_t last_mine_tick) noexcept;

// ---- Shield (apply_unit_damage, func_800983E0) ----
struct ShieldAbsorb {
    std::int32_t remaining_damage{};
    std::int32_t absorbed{};
    bool depleted{};                     // strength reached 0 (the marker itself stays linked)
};
ShieldAbsorb absorb_shield_damage(TimedPowerupState& timed,UnitRecord& unit,std::int32_t amount) noexcept;

// ---- Ammo consumption (fire_unit_weapon.c:486) ----
struct AmmoRules {
    bool cloak_pickups{};                // D_80134E68
    bool global_infinite{};              // D_80135760
    bool unlimited_ammo{};               // D_80134E69 (options screen)
};
[[nodiscard]] bool weapon_consumes_ammo(std::uint8_t weapon_kind,std::uint8_t unit_mode,
                                        bool unit_flag_40,const AmmoRules& rules) noexcept;

// ---- Weight table (D_802D5BA0[23], u8 total D_802D5BB7) ----
// The retail total is u8 and wraps. A zero total divides by zero in retail;
// here the pickup keeps its kind and no random value is drawn (POLICY).
// A walk past index 22 (possible only after wrap) also keeps the kind.
struct WeightRoll {
    std::optional<std::uint8_t> kind;
    bool rng_drawn{};
};
[[nodiscard]] WeightRoll roll_weighted_kind(const PickupWeights& weights,RetailRandom& rng) noexcept;
void reset_weights(PickupWeights& weights) noexcept;  // reset_weight_table (func_80090EE0)

// ---- Random-spot distribution (func_80088670 tail) ----
enum class DistributionStatus : std::uint8_t {
    placed,
    campaign_mode,     // modes 3..6 skip the distribution
    not_enough_spots,  // spotCount < total
    negative_count,    // malformed header; retail would loop zero times
    no_spots,          // total > 0 with zero spots; checked before not_enough_spots
};
static_assert(std::is_same_v<std::underlying_type_t<DistributionStatus>,std::uint8_t>);
struct DistributionResult {
    DistributionStatus status{DistributionStatus::placed};
    std::int64_t total{};
    std::vector<PickupPlacement> placed;     // kind already passed through pick_effect_kind
};
// Retail RNG order: per kind ascending, per copy: spot rolls (re-rolled while
// taken), then the pick_effect_kind roll. Only the first 100 spots count.
[[nodiscard]] DistributionResult distribute_pickups_interleaved(
    std::span<const PickupPlacement> spots,
    const std::array<std::int32_t,pickup_spawn_count_kinds>& counts,
    std::int32_t game_mode,bool reroll_option,RetailRandom& rng);

// ---- Wreck drop (destroy_unit.c:245) ----
inline constexpr std::uint8_t wreck_drop_kind=11U;
inline constexpr std::int32_t wreck_drop_ticks=0xD2;
// spawn_effect_kind_03 with duration 210: empty when the creation gate rejects
// it. Timed pickups never enter the weight table.
[[nodiscard]] std::optional<PickupRecord> make_wreck_drop(EntityId id,float x,float z,std::int32_t tick,
                                                         std::uint8_t victim_class,const PickupOptions& options) noexcept;

// ---- Respawn / removal ----
enum class PickupLifecycle : std::uint8_t { respawns, removed_on_collect, timed };
static_assert(std::is_same_v<std::underlying_type_t<PickupLifecycle>,std::uint8_t>);
[[nodiscard]] PickupLifecycle pickup_lifecycle(const PickupRecord& pickup,const PickupOptions& options) noexcept;

// ---- Effect request and deltas ----
struct EffectRequest {
    EntityId pickup{};
    EntityId collector{};
    std::uint8_t kind{};
    bool announce{true};                 // false for the replay/command path (update_unit_from_pose)
};

struct UnitDelta {
    std::int32_t health{};
    std::array<std::int32_t,retail::ammo_slots> ammo{};
    std::optional<std::uint8_t> secondary_selected;
    std::int32_t shield{};
    bool shield_created{};
    std::uint16_t flags_set{};
    std::int32_t cloak_end_tick_delta{};
    std::int32_t cloak_flash_set{-1};    // -1 = unchanged
};
struct OwnerDelta {
    std::int32_t score{};
    std::uint8_t lives{};
    std::int16_t respawn_bank0{};
    std::int32_t timed_event{-1};        // func_800A51E8(owner, kind+11, 3.0)
};
struct EffectOutcome {
    EffectRequest request{};
    PickupEffectResult result{};
    UnitDelta unit{};
    OwnerDelta owner{};
    bool play_sound{};                   // sound 0x2B at the unit
};

EffectOutcome apply_effect_request(const EffectRequest& request,UnitRecord& unit,OwnerRecord& owner,
                                   TimedPowerupState& timed,const PickupEffectOptions& options) noexcept;

// ---- Full contact pass ----
struct Collector {
    EntityId id{};
    UnitRecord* unit{};
    OwnerRecord* owner{};
    TimedPowerupState* timed{};
    std::uint8_t owner_class{};
    std::uint8_t unit_mode{};
};
// Collector-major order (each unit's drive update walks its own contacts),
// pickups in span order within a collector. Each pickup is taken once.
std::vector<EffectOutcome> collect_with_effects(std::span<PickupRecord> pickups,std::span<const Collector> collectors,
                                                const PickupEffectOptions& options,
                                                GameplayEventQueue& events,SoundEventQueue& sounds);

} // namespace battletanx::powerup
