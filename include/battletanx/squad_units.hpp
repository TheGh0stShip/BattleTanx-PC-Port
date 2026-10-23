#pragma once

// Squad units (gun buddies, weapon 13) are the same retail entity as the
// type-22 turrets: spawn_squad_unit_at (func_800A1408) allocates effect kind
// 27 (0x1B) and update_turret (func_800A16C0) runs it. placed_objects.hpp
// already ports that entity, so this module only adapts the
// deployables::DeployablesWorld::spawn_squad_unit hook onto placed::World.
//
// Call sites mapped (CONFIRMED, decomp src/code):
//   fire_unit_weapon.c:452   spawn(player->kind, unit->id, 0, heading+0x8000, pos, -, unit, 0, 0)
//                            -> check_area (arg6 = non-null unit pointer), not flagged
//   step_grid_barrage.c:48   spawn(record(target)->+0x1AE, target, 1, +0x30, pos, -, 0, 0, 0)
//                            -> no area check, not flagged
// Heading reversal, the rear offset and the grid cadence are computed by
// deployables_runtime (fire_gun_buddy_weapon / step_grid); the adapter takes
// the finished request.

#include "battletanx/deployables_runtime.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/placed_objects.hpp"
#include "battletanx/state_hash.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>

namespace battletanx::squad {

enum class SpawnStatus : std::uint8_t {
    spawned,
    blocked,            // area check failed (retail -1)
    pool_full,          // allocate_effect failed (retail -1)
    invalid_kind,       // squad kind outside D_80126038 (TEMPORARY POLICY: retail reads past the table)
    invalid_owner,      // owner outside 0..255 (TEMPORARY POLICY: native TurretRecord::owner is u8, retail s32)
    no_context,         // missing angle table or random source
};
static_assert(std::is_same_v<std::underlying_type_t<SpawnStatus>,std::uint8_t>);

struct SpawnOutcome {
    SpawnStatus status{SpawnStatus::no_context};
    EntityId id{};
    [[nodiscard]] bool ok() const noexcept { return status==SpawnStatus::spawned; }
};

// Request -> placed::TurretSpawn. nullopt for an invalid kind or owner.
[[nodiscard]] std::optional<placed::TurretSpawn> to_turret_spawn(const deployables::SquadSpawnRequest& request) noexcept;

// spawn_squad_unit_at for a deployables request. The area query is
// UpdateContext::area_clear (true = clear); empty means clear.
SpawnOutcome spawn_squad_unit(placed::World& world,const placed::UpdateContext& context,
                              const deployables::SquadSpawnRequest& request);

// Counters kept by the adapter for diagnostics and state hashing.
struct SquadStats {
    std::uint32_t requested{};
    std::uint32_t spawned{};
    std::uint32_t blocked{};
    std::uint32_t failed{};     // pool full / invalid / no context

    void serialize(StateWriter& writer) const;
};

// Owns no entities: units live in the placed::World pool. Both referenced
// objects must outlive the adapter and any hook made from it.
class SquadAdapter {
public:
    SquadAdapter(placed::World& world,const placed::UpdateContext& context) noexcept
        : world_(&world), context_(&context) {}
    // hook() captures this; a copy would leave the hook on the original.
    SquadAdapter(const SquadAdapter&)=delete;
    SquadAdapter& operator=(const SquadAdapter&)=delete;

    SpawnOutcome spawn(const deployables::SquadSpawnRequest& request);
    // Value for DeployablesWorld::spawn_squad_unit. Captures this adapter.
    // A false return (blocked area or full turret pool) is reported by
    // deployables as buddy_failed; only stats() tells the two apart.
    [[nodiscard]] std::function<bool(const deployables::SquadSpawnRequest&)> hook();

    [[nodiscard]] const SquadStats& stats() const noexcept { return stats_; }
    void serialize(StateWriter& writer) const { stats_.serialize(writer); }

private:
    placed::World* world_;
    const placed::UpdateContext* context_;
    SquadStats stats_{};
};

} // namespace battletanx::squad
