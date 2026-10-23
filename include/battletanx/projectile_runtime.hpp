#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/combat.hpp"

#include <functional>
#include <optional>

namespace battletanx {

// Retail shells live in the shared 1024-slot effect pool (D_802C3A30). The
// native runtime gives them a dedicated pool; exhaustion fails the spawn the
// same way a NULL effect allocation does.
inline constexpr std::size_t shell_capacity=128U;

// Effect kind 34 record (spawn_effect_kind_22 / update_bullet_effect).
struct ShellRecord {
    float x{}, y{}, z{};
    float velocity_x{}, velocity_z{};     // +0x1C / +0x18
    float speed{retail::shell_speed};     // +0x20
    float travelled{};                    // +0x24
    float range{retail::shell_unbounded_range}; // +0x28
    std::uint16_t heading{};              // +0x30
    std::int16_t life{retail::shell_life_ticks}; // +0x32
    std::uint8_t damage{retail::shell_damage};   // +0x37
    std::uint8_t attacker{no_owner};      // +0x38 owner index of the firing unit
    bool local{};                         // +0x36 flag
    EntityId ignore{};                    // +0x2C: the firing unit is not hit
};

using ShellPool=EntityPool<ShellRecord,shell_capacity>;

struct SegmentHit {
    float distance{};   // along the segment from its start, world units
    float x{}, z{};
};

// Static-world queries supplied by the integrating layer (the existing
// shell/world collision). Both may be empty for headless tests.
struct WorldCollision {
    // find_segment_hit with mask 4 (static shapes).
    std::function<std::optional<SegmentHit>(float x0,float z0,float x1,float z1)> segment;
    // func_80085D84: false when a point has left the world.
    std::function<bool(float x,float z)> inside;
};

// Native collision adapter for a decoded retail level. Static object bounds
// use the same blocking types as tank motion; navigation boundaries close the
// playable world. The returned callbacks borrow all three arguments.
[[nodiscard]] WorldCollision level_world_collision(const WorldLevel& world,
                                                    const NavigationLevel& navigation,
                                                    const AngleTable& angles);

struct ProjectileContext {
    CombatContext& combat;
    ShellPool& shells;
    const WorldCollision& world;
    const AngleTable& angles;
    float frame_scale{1.0F};   // D_801257B0; 1.0 per 1/30 s tick
};

struct FireRequest {
    bool trigger_held{};
    bool trigger_pressed{};
};

enum class FireResult : std::uint8_t { not_ready, fired, spawn_failed, empty };

// fire_unit_weapon (func_800987D8) for the cannon weapons 1 and 3.
FireResult fire_unit_weapon(ProjectileContext& context,EntityId unit_id,WeaponState& weapon,
                            const FireRequest& request);

// Nearest unit footprint crossed by the segment, ignoring `ignore` and dead
// units. Ties resolve to the lowest unit slot.
struct UnitSegmentHit {
    EntityId unit{};
    float distance{};
};
[[nodiscard]] std::optional<UnitSegmentHit> find_unit_segment_hit(
    const UnitPool& units,const AngleTable& angles,float x0,float z0,float x1,float z1,
    EntityId ignore);

// Segment against a footprint centred on (cx, cz) and rotated so that local
// +z points along (sine, cosine). Returns the entry distance from the start.
[[nodiscard]] std::optional<float> segment_footprint_distance(
    float x0,float z0,float x1,float z1,float cx,float cz,float sine,float cosine,
    const retail::Footprint& footprint) noexcept;

// update_bullet_effect (func_8008E480) for every live shell, in slot order.
void update_shells(ProjectileContext& context);

} // namespace battletanx
