#pragma once

#include "battletanx/entity_pool.hpp"
#include "battletanx/gameplay.hpp"
#include "battletanx/runtime_constants.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace battletanx {

// Recovered retail capacities: 20 unit records of 0x508 bytes at D_802D8180
// (4 owners x 5 squad slots) and 4 owner records of 0x348 bytes at D_802DE620.
inline constexpr std::size_t unit_capacity=20U;
inline constexpr std::size_t owner_capacity=4U;
inline constexpr std::uint8_t no_owner=0xFFU;

enum class UnitControl : std::uint8_t { player, computer, inert };

struct WeaponState {
    std::uint8_t kind{};                                  // +0x468 / +0x474
    std::int32_t last_fire_tick{retail::never_fired_tick}; // +0x46C / +0x478
};

struct UnitRecord {
    retail::UnitKind kind{retail::UnitKind::tank};
    std::uint8_t owner{no_owner};        // owner record index, 0..3
    std::uint8_t team{};                 // +0x05, copied from the owner
    bool alive{};                        // +0x01
    bool primary{};                      // +0x03 (owner's primary unit)
    UnitControl control{UnitControl::inert};
    std::uint16_t flags{};               // +0x14, retail flag bits (retail::flag_*)
    float x{}, y{}, z{};                 // +0x24..+0x2C
    float heading_degrees{};             // +0x400
    float turret_degrees{};              // +0x3FC
    float velocity_x{}, velocity_z{};
    std::int8_t track_left{}, track_right{};
    std::uint8_t track_phase_left{}, track_phase_right{};
    std::int32_t health{};               // +0x1C
    std::int32_t max_health{};           // +0x20
    std::int32_t shield_strength{};      // attached shield +0x18; 0 = none
    retail::Footprint footprint{};
    WeaponState primary_weapon{};
    WeaponState secondary_weapon{};
    std::array<std::int16_t,retail::ammo_slots> ammo{}; // +0x480
    std::int32_t spawn_tick{};           // grace for target acquisition
    std::int32_t last_hit_tick{retail::never_fired_tick}; // +0x4B8
    float hit_shake{};                   // +0x3E0/+0x3E4 accumulator (amount/4)
    EntityId target{};                   // current target unit (host handle)
    std::uint16_t navigation_triangle{};
    std::uint8_t squad_slot{};           // index within the owner's 5 slots
    bool deferred_death{};               // +0x1C set when destroyed during result states
};

struct OwnerRecord {
    bool present{};
    bool computer{};                     // +0x1A6 (set for CPU slots)
    std::uint8_t team{};                 // +0x1AE
    std::uint8_t live_units{};           // +0x22
    std::uint8_t squad_cap{};            // +0x38 (setup "lives")
    std::array<std::int16_t,2> respawn_bank{}; // +0x3A, +0x3C
    bool respawn_enabled{};              // +0x308
    std::int32_t losses{};               // +0x30
    std::int32_t team_losses{};          // +0x2C (killed by own team)
    std::int32_t kills{};                // func_800A5B6C tally
    std::int32_t score{};                // func_800A5AA4 tally
    float armor_scale{retail::human_scale_default};  // +0x310
    float damage_scale{retail::human_scale_default}; // +0x314
    std::uint8_t result{};               // +0x10 (2 = won)
};

using UnitPool=EntityPool<UnitRecord,unit_capacity>;

// func_800A31C0: human owner scale chosen by owner +0x30C.
[[nodiscard]] float human_owner_scale(std::uint32_t level) noexcept;

// spawn_unit defaults for a unit of the given kind at a placement.
[[nodiscard]] UnitRecord make_unit(retail::UnitKind kind,std::uint8_t owner,std::uint8_t team,
                                   float x,float z,std::uint16_t heading,
                                   std::int32_t tick,UnitControl control);

// Adapters to the existing native player-tank state.
[[nodiscard]] UnitRecord unit_from_player_tank(const PlayerTank& tank,std::uint8_t owner,
                                               std::uint8_t team,std::int32_t tick);
void sync_unit_from_player_tank(UnitRecord& unit,const PlayerTank& tank) noexcept;
void sync_player_tank_from_unit(PlayerTank& tank,const UnitRecord& unit) noexcept;

[[nodiscard]] constexpr float angle_to_degrees(std::uint16_t angle) noexcept {
    return static_cast<float>(angle)*(360.0F/65536.0F);
}
// Retail conversion: (u16)(degrees * 182.04445f) through a signed word.
[[nodiscard]] std::uint16_t degrees_to_retail_angle(float degrees) noexcept;

} // namespace battletanx
