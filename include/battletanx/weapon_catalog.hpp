#pragma once

#include "battletanx/runtime_constants.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace battletanx {

// Weapon ids handled by fire_unit_weapon (func_800987D8), 0..16. Each id also
// indexes the unit ammo array (+0x480, s16[18]).
inline constexpr std::uint8_t weapon_id_count=17U;
inline constexpr std::uint8_t first_special_weapon=4U;   // ids >= 4 always consume ammo

// How firmly a recovered value is known.
enum class Evidence : std::uint8_t { confirmed, strong_inference, approximation, unresolved };
static_assert(std::is_same_v<std::underlying_type_t<Evidence>,std::uint8_t>);

// What fire_unit_weapon does for an id.
enum class WeaponAction : std::uint8_t {
    none,          // no switch case (ids 0, 8, 9): only cooldown/ammo bookkeeping
    bullet,        // effect 34 via func_8008E29C (ids 1, 2, 3)
    spread_shell,  // effect 2 via func_8008D660 (id 4), three shells
    beam,          // effect 12 via func_800F0FD8 (ids 5, 10), instant
    nuke,          // effect 14 via func_800F29F0 (id 6)
    grenade,       // effect 15 via func_800F3344 (id 7)
    missile,       // effect 22 via func_8008DF2C (id 11)
    mine_deploy,   // id 12: deployables runtime (func_8010DAA0 / func_8010DED4)
    unit_deploy,   // id 13: deployables runtime (func_800A1408 / func_8010DED4)
    decoy,         // id 14: four kind-38 variant 0x11 effects + untargetable window
    shield,        // id 15: shieldEnd += 900
    repair,        // id 16: health += maxHealth * 0.3
};
static_assert(std::is_same_v<std::underlying_type_t<WeaponAction>,std::uint8_t>);

enum class MuzzleFrame : std::uint8_t { none, turret, hull, unit_origin };

struct MuzzleOffset { float x{}, y{}, z{}; };

struct WeaponDefinition {
    std::uint8_t id{};
    std::string_view label;           // neutral descriptive label, not retail text
    WeaponAction action{WeaponAction::none};
    bool auto_fire{};                 // D_80125ED4[id] != 0: fires while held
    std::uint8_t cooldown{};          // D_80125EC0[id], game ticks
    std::uint8_t ammo_slot{};         // index into unit ammo (== id)
    MuzzleFrame frame{MuzzleFrame::none};
    MuzzleOffset muzzle{};            // tank/goliath offset
    MuzzleOffset bike_muzzle{};       // unit kind 1 offset
    std::uint8_t effect_kind{};       // effect table index (0 = none)
    float speed{};
    std::int16_t life{};              // ticks (or fuse ticks)
    std::uint8_t damage{};            // direct-hit value carried to the handler
    std::uint8_t heavy_damage{};      // unit kind 2 value where it differs
    float range{};                    // beam length / ring radius / initial range
    std::uint8_t projectiles{};       // spawned per shot
    float recoil{};                   // added to unit +0x3D0
    std::uint16_t fire_sound{};       // 0 = none
    bool fire_sound_local{};          // func_800796F0 (listener, non-positional)
    std::uint8_t trail{};             // kind-38 trail variant, 0 = none
    std::uint8_t explosion{};         // kind-38 variant at the end/detonation
    std::uint8_t depth{};             // beam depth (ricochet budget)
    std::uint8_t powered_depth{};     // beam depth with flag 0x2000
    float splash_radius{};            // message 7 / message 3 radius (0 = none)
    std::int32_t splash_amount{};     // message 7 amount; message 3: 50..100 falloff
    bool powered_variant{};           // flag 0x2000 changes the case
    bool powered_consumes_stock{};    // powered: ammo forced to 1 before the decrement
    bool deployable{};                // routed to the deployables runtime
    std::int32_t untargetable_ticks{};// entity_is_targetable window after firing
    std::uint16_t collision_mask{};   // find_segment_hit mode
    bool ricochet_capable{};
    bool ignores_owner{};
    Evidence evidence{Evidence::unresolved};
};

[[nodiscard]] std::optional<WeaponDefinition> weapon_definition(std::uint8_t id) noexcept;

// D_80125EC0 (ROM 0xB5EC0, u8[17]) and D_80125ED4 (ROM 0xB5ED4, u8[17]):
// single source in runtime_constants.hpp.
inline constexpr const std::array<std::uint8_t,weapon_id_count>& weapon_cooldown_table=retail::weapon_cooldown;
inline constexpr const std::array<std::uint8_t,weapon_id_count>& weapon_repeat_table=retail::weapon_repeats;

// Message 7 splash of the placed blast (delayed_blast_effect_update,
// func_8010DC0C): radius 108, amount 20. Owned by the deployables runtime;
// kept for the catalog entry.
inline constexpr float splash_radius=108.0F;
inline constexpr std::int32_t splash_amount=20;
inline constexpr std::int32_t delayed_blast_ticks=8;

// apply_pickup (func_80099D90) weapon grants.
struct WeaponGrant {
    std::uint8_t weapon{};
    std::int16_t amount{};
    bool multiplayer_only_effect{};   // kind 12 grants ammo only when D_80134E68 == 0
};
[[nodiscard]] std::optional<WeaponGrant> pickup_weapon_grant(std::uint8_t pickup_kind) noexcept;

// dispatch_owner_event_variant (func_8009A22C) event 22: random grant chosen
// from next_random_u32() & 0x1F. Returns the pickup kind applied.
[[nodiscard]] std::uint8_t random_grant_pickup_kind(std::uint32_t random) noexcept;

} // namespace battletanx
