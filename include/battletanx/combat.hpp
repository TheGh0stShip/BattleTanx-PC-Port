#pragma once

#include "battletanx/runtime_events.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstdint>

namespace battletanx {

// Global switches consulted by the retail damage path, made explicit.
struct CombatConfig {
    bool damage_disabled{};          // D_80135762
    bool computer_only_damage{};     // D_80135763: only state-2 (computer) units take damage
    bool unlimited_ammo{};           // D_80134E69 (game record byte 0x1B)
    bool free_fire{};                // D_80135760
    std::uint32_t game_mode{5U};     // D_8012578C game type (5 = first campaign wave)
};

struct CombatContext {
    UnitPool& units;
    std::array<OwnerRecord,owner_capacity>& owners;
    GameplayEventQueue& events;
    SoundEventQueue& sounds;
    const CombatConfig& config;
    std::int32_t tick;
};

enum class DamageResult : std::uint8_t { ignored, survived, destroyed };

// apply_unit_damage (func_800983E0). `attacker` is the attacking owner index
// (values >= 4 are non-owner sources). `direct` is the hit's local flag.
DamageResult apply_unit_damage(CombatContext& context,EntityId victim,std::int32_t amount,
                               std::uint8_t attacker,bool direct,EntityId source=no_entity);

// destroy_unit (func_80097DD0). Returns false if the unit was not alive.
bool destroy_unit(CombatContext& context,EntityId victim,std::uint8_t killer,bool crushed=false);

// Retail scaled damage before shields: integer truncation at both steps,
// minimum 1. Exposed for tests.
[[nodiscard]] std::int32_t scaled_damage(std::int32_t amount,float victim_damage_scale,
                                         const float* attacker_armor_scale) noexcept;

// Shell/bullet hit response codes returned by the unit hit handler.
enum class HitResponse : std::uint8_t {
    impact=0U,        // normal impact
    pass_through=1U,  // default response, the projectile keeps going
    impact_decal=2U,
    crushed=5U,
    impact_silent=6U, // the hit destroyed the unit
    ricochet=7U,
};

// unit_handle_hit_message (func_8009EEC0) cases 2/22/34 for a unit victim.
HitResponse handle_projectile_hit(CombatContext& context,EntityId victim,std::uint8_t amount,
                                  std::uint8_t attacker,bool direct,EntityId projectile);

} // namespace battletanx
