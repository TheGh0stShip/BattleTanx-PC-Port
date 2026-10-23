#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battletanx {

// Weapon ids handled by fire_unit_weapon (func_800987D8), 0..16. Each id also
// indexes the unit ammo array (+0x480, s16[18]).
inline constexpr std::uint8_t weapon_id_count=17U;
inline constexpr std::uint8_t first_special_weapon=4U;   // ids >= 4 always consume ammo

// How firmly a recovered value is known.
enum class Evidence : std::uint8_t { confirmed, strong_inference, approximation, unresolved };

// What fire_unit_weapon does for an id.
enum class WeaponAction : std::uint8_t {
    none,              // no switch case (ids 0, 8, 9): only cooldown/ammo bookkeeping
    bullet,            // effect kind 34 via func_8008E29C (ids 1, 2, 3)
    spread_shell,      // effect kind 2 via func_8008D660 (id 4), three shells
    missile,           // effect kind 22 via func_8008DF2C (id 11)
    effect12_burst,    // effect kind 12 via func_800F0FD8 (ids 5, 10)
    effect14,          // effect kind 14 via func_800F29F0 (id 6)
    effect15,          // effect kind 15 via func_800F3344 (id 7)
    placed_blast,      // effect kind 29 via func_8010DAA0 (id 12)
    rear_deploy,       // effect kind 27 via func_800A1408 (id 13)
    ring_burst,        // four kind-26 variant 0x11 effects (id 14)
    shield,            // shieldEnd += 900 (id 15)
    repair,            // health += maxHealth * 0.3 (id 16)
};

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
    MuzzleOffset bike_muzzle{};       // unit kind 1 offset where the case differs
    std::uint8_t effect_kind{};       // effect table index (0 = none)
    float speed{};
    std::int16_t life{};              // ticks
    std::uint8_t damage{};            // value carried to the hit handler
    std::uint8_t heavy_damage{};      // unit kind 2 value where it differs
    float range{};                    // initial range (bullets re-probe the wall)
    std::uint8_t projectiles{};       // spawned per shot
    float recoil{};                   // added to unit +0x3D0
    std::uint16_t fire_sound{};       // 0 = none
    std::uint8_t trail{};             // kind-26 trail variant, 0 = none
    std::uint16_t collision_mask{};   // find_segment_hit mode
    bool splash{};
    bool ricochet_capable{};
    bool ignores_owner{};
    Evidence evidence{Evidence::unresolved};
};

[[nodiscard]] std::optional<WeaponDefinition> weapon_definition(std::uint8_t id) noexcept;

// D_80125EC0 (ROM 0xB5EC0, u8[20]) and D_80125ED4 (ROM 0xB5ED4, u8[17]).
inline constexpr std::array<std::uint8_t,weapon_id_count> weapon_cooldown_table{
    0U,30U,3U,40U,50U,54U,50U,10U,5U,0U,50U,50U,15U,60U,60U,120U,1U};
inline constexpr std::array<std::uint8_t,weapon_id_count> weapon_repeat_table{
    0U,1U,1U,1U,0U,0U,0U,0U,1U,0U,0U,0U,0U,0U,0U,0U,0U};

// Splash damage (message 7) from notify_objects_near_point (func_8009F958)
// as called by delayed_blast_effect_update (func_8010DC0C) and
// destroy_placed_object: radius 108, amount 20 (f32 -> s32), no falloff.
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
