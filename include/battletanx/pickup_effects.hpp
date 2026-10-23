#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/pickup_runtime.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace battletanx {

// Complete apply_pickup (func_80099D90) for every retail kind, plus the spawn
// weapon grant func_8009A22C and the bank kind re-roll (spawn_bank_record_entries).

inline constexpr std::uint8_t pickup_kind_count=20U;          // cases 0..19 in func_80099D90
inline constexpr std::uint8_t pickup_kind_random_spot=22U;    // bank parameter 22: spot marker
inline constexpr std::int32_t pickup_cloak_ticks=900;         // kind 12 with D_80134E68
inline constexpr std::int32_t pickup_cloak_flash=240;         // unit +0x08 on start
inline constexpr std::int32_t pickup_announce_progress=100;   // add_entity_progress(player,100)
inline constexpr std::int32_t pickup_score_bonus=900;         // kind 14: func_800A5AA4(player,900)
inline constexpr std::int32_t progress_threshold=50000;       // add_entity_progress_by_id
inline constexpr std::int32_t shield_large=100;               // kind 8: effect kind 23 value18
inline constexpr std::int32_t shield_small=25;                // kind 18
inline constexpr std::int32_t cloak_fire_reveal_ticks=45;     // func_8009FB8C: now-0x2D < +0x4B4

enum class PickupEffectStatus : std::uint8_t {
    applied,          // the retail case changed state
    no_change,        // retail case reached but nothing changed (e.g. ammo for weapon 0)
    no_retail_case,   // 6, 17, 20, 21, 22, >= 23: the retail switch has no case
};

enum class PickupEffectKind : std::uint8_t {
    none,
    health,           // 0 below max
    stored_health,    // 0 at full health: ammo[16] += 1
    health_and_ammo,  // 11
    primary_ammo,     // 5
    secondary_stock,  // 1,2,3,7,9,10,15,16,19 and 12 without cloak option
    map_reveal,       // 4: unit flags |= 0x80
    shield_created,   // 8/18 with no live shield
    shield_added,     // 8/18 onto the live shield
    cloak_started,    // 12 with D_80134E68
    cloak_extended,
    extra_life,       // 13
    score,            // 14
};

inline constexpr std::uint16_t flag_map_reveal=0x0080U;

// Per-unit timed state carried at unit +0x08/+0x0C/+0x10 and the shield link
// +0x4FC/+0x500. Shield strength itself lives in UnitRecord::shield_strength.
struct TimedPowerupState {
    std::int32_t cloak_end_tick{};  // +0x0C; 0 = inactive
    std::int32_t cloak_flash{};     // +0x08, decays by 0x20 per tick
    std::int32_t value10{};         // +0x10, cleared on expiry (blocks targeting when non-zero)
    bool shield_live{};             // +0x4FC non-null and serial matches
    std::int32_t last_fire_tick{retail::never_fired_tick}; // +0x4B4

    void serialize(StateWriter& writer) const;
};

struct PickupEffectOptions {
    PickupOptions base{};            // ammo_with_crate_disabled = D_80134E69, replay_active
    bool cloak_pickups{};            // D_80134E68 (also enables the bank re-roll)
    std::int32_t game_mode{};
    std::int32_t tick{};             // D_801B4AA8
    bool announce{true};             // third argument of func_80099D90
};

struct PickupEffectResult {
    PickupEffectStatus status{PickupEffectStatus::no_change};
    PickupEffectKind effect{PickupEffectKind::none};
    std::uint8_t kind{};
    std::int32_t health_delta{};
    std::uint8_t ammo_slot{0xFFU};
    std::int32_t ammo_delta{};
    bool selected_secondary{};       // secondary weapon was 0 and became ammo_slot
    std::int32_t shield_delta{};
    std::int32_t score_delta{};      // total progress added to the owner
    std::uint8_t lives_awarded{};    // func_800A5DD8 calls that took effect
    bool announced{};                // sound 0x2B and owner event kind+11 for 3.0
    std::int32_t owner_event{-1};    // kind+11 when announced
};

// Owner progress (+0x210) with the 50000 threshold rule. Returns the number of
// lives (func_800A5DD8 calls) awarded.
std::uint8_t add_owner_progress(OwnerRecord& owner,std::int32_t amount,std::int32_t game_mode) noexcept;
// func_800A5DD8: ignored for computer owners; respawn_bank[0] grows outside mode 7.
bool award_owner_life(OwnerRecord& owner,std::int32_t game_mode) noexcept;

PickupEffectResult apply_pickup_effect_complete(UnitRecord& unit,OwnerRecord& owner,std::uint8_t kind,
                                                const PickupEffectOptions& options,
                                                TimedPowerupState& timed) noexcept;

// Per-tick expiry from update_tank_drive / update_rail_unit_control.
void update_timed_powerups(TimedPowerupState& timed,std::int32_t tick) noexcept;
[[nodiscard]] bool cloak_hides_unit(const TimedPowerupState& timed,std::int32_t tick) noexcept;

// func_8009A22C. Event 22 rolls rng&0x1F; 23 grants nothing; 1 only when the
// unit is primary and the game tick is <= 0.
struct SpawnGrantResult {
    std::optional<std::uint8_t> granted_kind;
    PickupEffectResult effect{};
};
SpawnGrantResult grant_spawn_weapon(UnitRecord& unit,OwnerRecord& owner,std::uint32_t event,
                                    const PickupEffectOptions& options,TimedPowerupState& timed,
                                    RetailRandom& rng) noexcept;
// Random table of event 22 for a roll in 0..31.
[[nodiscard]] std::uint8_t spawn_grant_random_kind(std::uint32_t roll) noexcept;

// spawn_unit: which game modes call the grant. Campaign (3..6) grants only for
// owner index 0 with controller != 9. Modes 0,1,2,9 grant unless the owner's
// one-shot skip (+0x23 & 4) is consumed for a non-reserve unit.
[[nodiscard]] bool spawn_grant_applies(std::int32_t game_mode,std::uint16_t owner_index,
                                       std::int32_t controller,bool skip_flag,bool reserve_unit) noexcept;

// pick_effect_kind (spawn_bank_record_entries.c:101). Rolls only when enabled and
// the kind is one of 5,6,7,9,10,13,14,16.
[[nodiscard]] std::uint8_t reroll_bank_pickup_kind(std::uint8_t kind,bool enabled,RetailRandom& rng) noexcept;

} // namespace battletanx
