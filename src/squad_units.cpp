#include "battletanx/squad_units.hpp"

#include "battletanx/unit_kinds.hpp"

namespace battletanx::squad {

std::optional<placed::TurretSpawn> to_turret_spawn(const deployables::SquadSpawnRequest& request) noexcept {
    if (request.squad_kind>=turret_types) return std::nullopt;
    if (request.owner_unit<0 || request.owner_unit>0xFF) return std::nullopt;
    return placed::TurretSpawn{
        .team=request.team,
        .owner=static_cast<std::uint8_t>(request.owner_unit),
        .type=request.squad_kind,
        .heading=request.heading,
        .x=request.x,
        .z=request.z,
        .check_area=request.check_area,
        .scaled=false,          // arg7 is 0 at both weapon-13 call sites
    };
}

SpawnOutcome spawn_squad_unit(placed::World& world,const placed::UpdateContext& context,
                              const deployables::SquadSpawnRequest& request) {
    SpawnOutcome outcome;
    if (request.squad_kind>=turret_types) { outcome.status=SpawnStatus::invalid_kind; return outcome; }
    const std::optional<placed::TurretSpawn> spawn=to_turret_spawn(request);
    if (!spawn) { outcome.status=SpawnStatus::invalid_owner; return outcome; }
    if (context.angles==nullptr || context.rng==nullptr) { outcome.status=SpawnStatus::no_context; return outcome; }
    // Distinguish the two retail -1 paths before delegating (same order as retail:
    // area query first, then allocation).
    const float half=turret_scale[request.squad_kind]*turret_half_extent;
    if (spawn->check_area && context.area_clear && !context.area_clear(spawn->x,spawn->z,half)) {
        outcome.status=SpawnStatus::blocked;
        return outcome;
    }
    if (world.size()>=placed::world_capacity) { outcome.status=SpawnStatus::pool_full; return outcome; }
    const std::optional<EntityId> id=world.spawn_turret(*spawn,context);
    if (!id) { outcome.status=SpawnStatus::pool_full; return outcome; }
    outcome.status=SpawnStatus::spawned;
    outcome.id=*id;
    return outcome;
}

void SquadStats::serialize(StateWriter& writer) const {
    writer.u32(requested);
    writer.u32(spawned);
    writer.u32(blocked);
    writer.u32(failed);
}

SpawnOutcome SquadAdapter::spawn(const deployables::SquadSpawnRequest& request) {
    ++stats_.requested;
    const SpawnOutcome outcome=spawn_squad_unit(*world_,*context_,request);
    if (outcome.ok()) ++stats_.spawned;
    else if (outcome.status==SpawnStatus::blocked) ++stats_.blocked;
    else ++stats_.failed;
    return outcome;
}

std::function<bool(const deployables::SquadSpawnRequest&)> SquadAdapter::hook() {
    return [this](const deployables::SquadSpawnRequest& request) { return spawn(request).ok(); };
}

} // namespace battletanx::squad
