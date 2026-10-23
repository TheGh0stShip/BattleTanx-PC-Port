#pragma once

#include "battletanx/deterministic_rng.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/entity_pool.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_kinds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

// Placed and deployed weapon entities: proximity mines (weapon 12, effect
// kind 29), the powered 5x5 deploy grid (weapons 12 and 13 with unit flag
// 0x2000, effect kind 30) and gun-buddy squad-unit requests (weapon 13,
// effect kind 27). Squad units themselves belong to the unit/squad runtime;
// this module only builds and forwards their spawn requests.
namespace battletanx::deployables {

// ---- Retail constants (see evidence.md for sources) -------------------------
inline constexpr std::uint8_t mine_effect_kind=29U;          // 0x1D
inline constexpr std::uint8_t grid_effect_kind=30U;          // 0x1E
inline constexpr std::uint8_t squad_effect_kind=27U;         // 0x1B
inline constexpr std::int32_t mine_arm_ticks=60;             // handler: startTick + 0x3C < ticks
inline constexpr std::int32_t mine_detonate_ticks=8;         // update: startTick + 8 < ticks
inline constexpr float mine_splash_radius=108.0F;
inline constexpr std::int32_t mine_splash_amount=20;
inline constexpr std::int32_t mine_contact_amount=30;        // report 8 value 0x1E
inline constexpr float mine_contact_wear_scale=0.2F;         // tank +damage -= value * 0.2
inline constexpr float mine_place_half_extent=10.0F;         // area box -10..10
inline constexpr std::int16_t mine_place_radius=14;
inline constexpr std::uint8_t mine_detonate_variant=0x0DU;   // kind-26 variant
inline constexpr std::uint16_t sound_mine_placed=0x19U;
inline constexpr std::uint16_t sound_mine_detonate=5U;
inline constexpr std::int16_t no_tag=-1;                     // tag -1: area check skipped
inline constexpr std::uint8_t grid_size=5U;
inline constexpr float grid_spacing=100.0F;
inline constexpr float grid_jitter_range=60.0F;
inline constexpr float grid_jitter_bias=30.0F;
inline constexpr std::uint8_t grid_buddy_period=3U;          // (column + row) % 3 == 0: 8 of 25 cells
inline constexpr std::uint8_t buddy_squad_kind=0U;           // weapon 13 unpowered
inline constexpr std::uint8_t grid_buddy_squad_kind=1U;      // weapon 13 powered grid
inline constexpr float buddy_rear_offset_bike=-82.0F;        // unit kind 1
inline constexpr float buddy_rear_offset_tank=-145.0F;       // unit kinds 0 and 2
inline constexpr float squad_half_extent_unit=45.0F;         // D_80126038[kind] * 45
// D_80126038 (ROM 0xB6038, f32[]): squad footprint scale for kinds 0..3.
// Single source of truth: unit_kinds.hpp turret_scale (same table).
inline constexpr const auto& squad_size_scale=turret_scale;

// TEMPORARY POLICY: retail mines and grids share the 1024-slot effect pool
// (D_802C3A30). The native runtime gives them dedicated fixed pools; a full
// pool fails the spawn exactly as a NULL allocate_effect does.
inline constexpr std::size_t mine_capacity=128U;
inline constexpr std::size_t grid_capacity=8U;

// ---- Records ----------------------------------------------------------------
struct MineRecord {
    float x{}, z{};
    std::int16_t tag{no_tag};       // +0x24: placing unit id (low byte = splash flag)
    std::int32_t placed_tick{};     // +0x18: arming reference
    std::int32_t detonate_tick{};   // +0x1C: 0 = detonation not started
    bool awake{};                   // moved to the updating list (release_effect)
    bool wake_pending{};            // woken during this update pass
};
using MinePool=EntityPool<MineRecord,mine_capacity>;

struct GridRecord {
    float origin_x{}, origin_z{};
    float step_x{}, step_z{};       // rows: behind the hull, 100 units apart
    float side_x{}, side_z{};       // columns: hull-perpendicular, 100 units apart
    std::uint8_t column{}, row{};
    std::int16_t tag{no_tag};       // +0x2C: deploying unit id
    std::uint16_t heading{};        // +0x30: buddy heading
    bool buddies{};                 // +0x32: false = mines, true = squad units
};
using GridPool=EntityPool<GridRecord,grid_capacity>;

struct DeployablesState {
    MinePool mines;
    GridPool grids;
    void serialize(StateWriter& writer) const;
    void clear() noexcept;
    // func_80107A14 as seen by other placers (gun-buddy area check): true when
    // a non-detonating mine footprint (-10..10) overlaps the box x/z +-half.
    // TEMPORARY POLICY: axis-aligned box overlap, |d| <= 10 + half on both axes.
    // Integrators fold it into placed::UpdateContext::area_clear (see api.md).
    [[nodiscard]] bool mine_blocks_area(float x,float z,float half_extent) const;
};

// ---- Events -----------------------------------------------------------------
enum class DeployableEventKind : std::uint8_t {
    mine_placed,        // sound 0x19 (fire path and mine grid)
    mine_woken,         // release_effect: contact, message 3 or message 7
    mine_detonating,    // first update: kind-26 variant 0xD + sound 5
    mine_splash,        // message 7 broadcast, radius 108, value 20
    mine_removed,       // status 2
    grid_started,
    grid_finished,
    buddy_requested,
    buddy_failed,
};
static_assert(sizeof(DeployableEventKind)==1U);

struct DeployableEvent {
    DeployableEventKind kind{};
    std::int32_t tick{};
    EntityId subject{};
    std::int16_t tag{no_tag};
    std::uint8_t variant{};
    std::uint16_t sound{};
    float x{}, z{};
};
using DeployableEventQueue=EventQueue<DeployableEvent>;

// ---- External hooks (adapter; see api.md for the expected owners) ----------
struct SquadSpawnRequest {
    std::uint8_t team{};            // spawn_squad_unit_at arg0
    std::int16_t owner_unit{};      // arg1 (+0x10)
    std::uint8_t squad_kind{};      // arg2
    std::uint16_t heading{};        // arg3 (+0x18)
    float x{}, z{};                 // arg4
    bool check_area{};              // arg6 != 0
    float half_extent{};            // D_80126038[kind] * 45 when check_area
    // arg5, the second point: unit x/z for the unpowered buddy
    // (fire_unit_weapon.c:452 passes v = unit position), equal to x/z for the
    // grid (step_grid_barrage.c:48 passes &pos twice). UNRESOLVED: whether
    // func_800A1408 reads it; carried so the adapter can.
    float origin_x{}, origin_z{};
};

struct DeployablesWorld {
    // func_80107A14 filtered as in spawn_effect_kind1d_if_area_clear: true
    // when a static shape or a non-unit object other than a native mine
    // overlaps the box. Mines in this module are checked internally. Empty:
    // nothing external blocks.
    std::function<bool(float x,float z,float half_extent,std::int16_t radius)> placement_blocked;
    // Message 7 to units in range (expected: weapon_runtime apply_splash_damage
    // with amount 20, attacker = flag). Returns units messaged. Empty: skipped.
    std::function<std::size_t(float x,float z,float radius,std::int32_t amount,
                              std::uint8_t flag,EntityId source)> splash_units;
    // spawn_squad_unit_at (func_800A1408). False: the spawn failed (-1).
    std::function<bool(const SquadSpawnRequest&)> spawn_squad_unit;
    // get_entity_record(id)->+0x1AE for grid buddies. Empty: team 0.
    std::function<std::uint8_t(std::int16_t unit)> unit_team;
};

struct DeployablesContext {
    DeployablesState& state;
    DeployableEventQueue& events;
    RetailRandom& random;
    const DeployablesWorld& world;
    std::int32_t tick{};            // D_801B4AA8
};

// ---- Fire paths (fire_unit_weapon cases 12 and 13) -------------------------
struct DeployerUnit {
    std::int16_t id{};              // unit->id (mine tag / squad owner)
    std::uint8_t unit_kind{};       // 0 tank, 1 bike, 2 heavy
    std::uint8_t team{};            // owner record team (player->kind)
    bool powered{};                 // flags & 0x2000
    float x{}, z{};
    float hull_forward_x{}, hull_forward_z{};   // unit->hullFwdX/Z (unit vector)
    std::uint16_t heading{};        // u16(heading * 182.04445); fallback when heading_degrees is empty
    // unit->heading (f32 degrees). When set, the reversed buddy heading is
    // computed as retail does: (u32)(heading * 182.04445f + 32768.0f) & 0xFFFF
    // (fire_unit_weapon.c:430, :451), rounding after the +32768 add.
    std::optional<float> heading_degrees{};
};

enum class DeployStatus : std::uint8_t {
    placed,         // fire bookkeeping proceeds
    blocked,        // area check failed: retail returns before bookkeeping
    pool_full,      // allocation failed: retail returns before bookkeeping
};
static_assert(sizeof(DeployStatus)==1U);

struct DeployResult {
    DeployStatus status{DeployStatus::blocked};
    EntityId spawned{};             // mine or grid id; none for a single buddy
    bool consume_whole_stock{};     // powered: ammo[id] forced to 1 before the decrement
    bool return_before_bookkeeping() const noexcept { return status!=DeployStatus::placed; }
};

// Weapon 12. Unpowered: one mine at the unit centre (area-checked), sound
// 0x19. Powered: a 5x5 mine grid behind the hull.
DeployResult fire_mine_weapon(DeployablesContext& context,const DeployerUnit& unit);
// Weapon 13. Unpowered: one kind-0 squad unit behind the hull, facing back.
// Powered: a 5x5 grid that requests a kind-1 squad unit on the 8 of 25
// cells where (column + row) % 3 == 0 (step_grid_barrage.c:47).
DeployResult fire_gun_buddy_weapon(DeployablesContext& context,const DeployerUnit& unit);

// spawn_effect_kind1d_if_area_clear (func_8010DAA0).
std::optional<EntityId> place_mine(DeployablesContext& context,float x,float z,std::int16_t tag);

// ---- Mine message handler (timed_effect_handle_message, func_8010DDD4) -----
enum class ContactSender : std::uint8_t { unit, mine, other };   // contact object +0x08 kind 0 / 0x1D / else
enum class ContactReport : std::uint8_t {
    unchanged,      // report left as the caller's default
    ordinary,       // report kind 1
    damage,         // report kind 8: apply_unit_damage(value, attacker, 0), +damage -= value*0.2
};
static_assert(sizeof(ContactSender)==1U && sizeof(ContactReport)==1U);

struct ContactResult {
    ContactReport report{ContactReport::unchanged};
    std::int32_t amount{};          // 30 for damage
    std::int16_t attacker{no_tag};  // mine tag
    float wear{};                   // amount * 0.2 subtracted from the tank +damage field
};

// Message 0: a unit/object footprint touches the mine.
ContactResult contact_mine(DeployablesContext& context,EntityId mine,ContactSender sender);
// Message 3 / 7: unconditional wake (no arming check).
bool wake_mine(DeployablesContext& context,EntityId mine);
// Message 2: position report.
[[nodiscard]] std::optional<std::pair<float,float>> mine_position(const DeployablesState& state,EntityId mine);
[[nodiscard]] bool mine_armed(const MineRecord& mine,std::int32_t tick) noexcept;

// Updates grids, then awake mines, in slot order (see evidence.md, ordering policy).
void update_deployables(DeployablesContext& context);

} // namespace battletanx::deployables
