#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace battletanx {

// Pickup effect kind 3: spawn_effect_kind_03 (func_800911A0), update
// func_800913AC, contact handler func_800916B8, apply_pickup func_80099D90.

inline constexpr float pickup_half_extent=35.0F;           // grid body -35..35, mask 2
inline constexpr std::int32_t level_pickup_owner_class=-1;  // effect +0x2C
inline constexpr std::uint32_t pickup_respawn_range=3600U;
inline constexpr std::int32_t pickup_respawn_base=1800;
inline constexpr std::int32_t pickup_unsupported_flag=0x100; // event value bit
inline constexpr std::size_t pickup_weight_kinds=0x17U;      // D_802D5BA0 .. D_802D5BB6

struct PickupRecord {
    EntityId id{};                      // host handle reported in events
    std::uint8_t kind{};                // +0x18
    float x{}, z{};
    bool active{true};                  // grid body registered
    bool consumed{};                    // +0x20
    std::int32_t respawn_tick{};        // +0x24 (0 = none)
    std::int32_t expire_tick{};         // +0x28 (0 = permanent)
    std::int32_t owner_class{level_pickup_owner_class}; // +0x2C
    bool removed{};                     // effect ended (never respawns)
};

// D_802D5BA0 weights and the u8 total D_802D5BB7.
struct PickupWeights {
    std::array<std::uint8_t,pickup_weight_kinds> weights{};
    std::uint8_t total{};
    // spawn_effect_kind_03: permanent pickups of kind != 1 add one weight.
    void register_permanent(std::uint8_t kind) noexcept;
};

struct PickupOptions {
    std::int32_t game_mode{};           // 3..6: no respawn
    bool respawn_enabled{true};         // D_80134E66
    bool ammo_with_crate_disabled{};    // D_80134E69 (kind 11 ammo, kind 5 creation)
    bool extra_pickups_enabled{true};   // D_80134E65 (creation of kinds other than 0/5)
    bool replay_active{};               // func_800EC644: suppresses sound/event
};

// spawn_effect_kind_03 creation gate.
[[nodiscard]] bool pickup_kind_allowed(std::uint8_t kind,const PickupOptions& options) noexcept;

struct PickupCollector {
    EntityId id{};
    UnitRecord* unit{};
    std::uint8_t owner_class{};         // owner record +0x1AE (func_8009FBE4)
    std::uint8_t state_350{};           // unit +0x350; 2 = AI-driven, cannot collect
};

struct PickupCollectResult {
    std::size_t collected{};
    std::size_t unsupported{};
};

[[nodiscard]] bool pickup_overlaps(const PickupRecord& pickup,const UnitRecord& unit) noexcept;
[[nodiscard]] bool pickup_kind_supported(std::uint8_t kind) noexcept;

// Applies a confirmed kind to a unit. Returns false for kinds not modelled.
bool apply_pickup_effect(UnitRecord& unit,std::uint8_t kind,const PickupOptions& options) noexcept;

// Collects every overlapping, eligible pickup. Pickups are visited in span
// order, collectors in span order; each pickup is taken at most once.
PickupCollectResult collect_pickups(std::span<PickupRecord> pickups,
                                    std::span<PickupCollector> collectors,
                                    std::int32_t tick,const PickupOptions& options,
                                    GameplayEventQueue& events,SoundEventQueue& sounds);

// func_800913AC for every pickup. Weights may be null; the kind is then kept.
void update_pickups(std::span<PickupRecord> pickups,std::int32_t tick,
                    const PickupOptions& options,RetailRandom& rng,
                    const PickupWeights* weights);

} // namespace battletanx
