#include "battletanx/campaign_spawn.hpp"

#include "battletanx/big_endian.hpp"
#include "battletanx/unit_runtime.hpp"

#include <algorithm>

namespace battletanx {
namespace {

// D_80126010 (ROM 0xB6010, u16[4]): kind-0x1B object health by sub-kind.
constexpr std::array<std::uint16_t,4> owned_object_health{25U,50U,100U,100U};
constexpr std::uint32_t table_row_placement=0xFFFFFFFFU;

void fail(SpawnPlanError* error,SpawnPlanError value) noexcept {
    if (error!=nullptr) *error=value;
}

void audit(CampaignSpawnPlan& plan,std::size_t owner,std::size_t slot,std::uint32_t index,
           PlanAuditReason reason) {
    plan.audits.push_back(PlanAudit{static_cast<std::uint8_t>(owner),
                                    static_cast<std::uint8_t>(slot),index,reason});
}

std::optional<retail::UnitKind> unit_kind(std::uint32_t value) noexcept {
    if (value>2U) return std::nullopt;
    return static_cast<retail::UnitKind>(value);
}

std::uint16_t unit_id(std::size_t owner,std::size_t slot) noexcept {
    return static_cast<std::uint16_t>(owner*squad_slots+slot);
}

// Marker kind -> unit kind (initialize_player_record, kinds 3..6).
std::optional<retail::UnitKind> marker_unit_kind(CampaignSpawnPlan& plan,
                                                 const SpawnOwnerSetup& setup,
                                                 std::size_t owner,std::size_t slot,
                                                 const SpawnMarker& marker) {
    if (marker.low<marker_lineup_base) {
        const auto kind=unit_kind(marker.low);
        if (!kind) audit(plan,owner,slot,marker.placement_index,PlanAuditReason::marker_kind_invalid);
        return kind;
    }
    const std::size_t entry=marker.low-marker_lineup_base;
    if (entry>=setup.lineup.size()) {
        audit(plan,owner,slot,marker.placement_index,PlanAuditReason::marker_lineup_out_of_range);
        return std::nullopt;
    }
    if (entry>=setup.lineup_count) {
        audit(plan,owner,slot,marker.placement_index,PlanAuditReason::marker_lineup_beyond_count);
        return std::nullopt;
    }
    const auto kind=unit_kind(setup.lineup[entry]);
    if (!kind) audit(plan,owner,slot,marker.placement_index,PlanAuditReason::lineup_kind_invalid);
    return kind;
}

void place_campaign_owner(CampaignSpawnPlan& plan,const SpawnOwnerSetup& setup,
                          std::size_t owner,const std::vector<SpawnMarker>& markers) {
    PlannedOwner& planned=plan.owners[owner];
    std::size_t best=0U;
    if (!setup.computer) {
        for (std::size_t i=0U; i<markers.size(); ++i) {
            if (markers[i].low==marker_lineup_base) best=i;
        }
    }
    planned.reserve=setup.computer ? 5 : static_cast<std::int32_t>(markers.size());
    for (std::size_t i=0U; i<markers.size(); ++i) {
        const SpawnMarker& marker=markers[i];
        const auto kind=marker_unit_kind(plan,setup,owner,i,marker);
        if (!kind) continue;
        PlannedUnit unit;
        unit.owner=static_cast<std::uint8_t>(owner);
        unit.team=setup.team;
        unit.slot=static_cast<std::uint8_t>(i);
        unit.unit_id=unit_id(owner,i);
        unit.kind=*kind;
        unit.x=marker.x;
        unit.z=marker.z;
        unit.heading=marker.heading;
        unit.parameter=marker.param;
        unit.primary=i==best;
        unit.selectable=!setup.computer && i==best;
        unit.flags=setup.computer ? spawn_flag_computer : std::uint16_t{0U};
        unit.source=PlacementSource::campaign_marker;
        unit.placement_index=marker.placement_index;
        if (unit.primary) planned.active_unit=unit.unit_id;
        if (!setup.computer) {
            // A kind-2 unit consults func_80096FC8 first; that branch is not
            // modelled and falls through to the selectable rule.
            if (*kind==retail::UnitKind::goliath) {
                audit(plan,owner,i,marker.placement_index,PlanAuditReason::goliath_hold_rule_untraced);
            }
            if (!unit.selectable) unit.flags|=spawn_flag_held;
        }
        plan.units.push_back(unit);
    }
}

void place_table_owner(CampaignSpawnPlan& plan,const SpawnOwnerSetup& setup,
                       std::size_t owner,std::int32_t game_kind,bool tanks_only,
                       const TeamSpawnTable& table) {
    PlannedOwner& planned=plan.owners[owner];
    planned.reserve=setup.lives;
    const std::size_t row=plan.slot_order[owner]&3U;
    const std::size_t count=std::min<std::size_t>(setup.lineup_count,squad_slots);
    std::int32_t spawned=0;
    for (std::size_t i=0U; i<count; ++i) {
        std::size_t index=i;
        if (game_kind!=7) index=i==0U ? 0U : i==1U ? count-1U : i-1U;
        if (spawned>=setup.lives) {
            if (!setup.computer) ++planned.extra_tank_respawns;
            audit(plan,owner,i,table_row_placement,PlanAuditReason::unit_cap_reached);
            continue;
        }
        const std::uint8_t raw=tanks_only ? std::uint8_t{0U} : setup.lineup[index];
        const auto kind=unit_kind(raw);
        if (!kind) {
            audit(plan,owner,i,table_row_placement,PlanAuditReason::lineup_kind_invalid);
            continue;
        }
        const TeamSpawnPoint& point=table[row][index];
        PlannedUnit unit;
        unit.owner=static_cast<std::uint8_t>(owner);
        unit.team=setup.team;
        unit.slot=static_cast<std::uint8_t>(i);
        unit.unit_id=unit_id(owner,i);
        unit.kind=*kind;
        unit.x=point.x;
        unit.z=point.z;
        unit.heading=point.heading;
        unit.primary=i==0U;
        unit.selectable=!setup.computer && i==0U;
        unit.flags=(setup.computer || game_kind==7) ? spawn_flag_computer : std::uint16_t{0U};
        unit.source=PlacementSource::team_spawn_table;
        unit.placement_index=static_cast<std::uint32_t>(row*team_spawns_per_row+index);
        plan.units.push_back(unit);
        ++spawned;
    }
    planned.active_unit=unit_id(owner,0U);
}

std::optional<std::uint16_t> open_triangle_at(const NavigationLevel& navigation,float x,float z) {
    const NavigationVertex point{x,z};
    for (std::size_t index=1U; index<navigation.triangles.size(); ++index) {
        if (navigation.kind_enabled[navigation.triangles[index].kind]!=0U) continue;
        const auto triangle=static_cast<std::uint16_t>(index);
        if (navigation_point_inside_triangle(navigation,point,triangle)) return triangle;
    }
    return std::nullopt;
}

// Tail of func_80088670 with pick_effect_kind applied after each spot draw.
std::vector<PickupPlacement> distribute_with_reroll(const LevelSpawnPlan& level_plan,
                                                    const WorldLevel& level,RetailRandom& rng) {
    std::vector<PickupPlacement> placed;
    if (!level_plan.random_distribution_allowed) return placed;
    std::int64_t total=0;
    for (const std::int32_t count : level.pickup_spawn_counts) total+=count;
    const auto spot_count=static_cast<std::uint32_t>(level_plan.random_spots.size());
    if (static_cast<std::int64_t>(spot_count)<total || spot_count==0U) return placed;
    std::vector<bool> used(spot_count,false);
    for (std::size_t kind=0U; kind<level.pickup_spawn_counts.size(); ++kind) {
        for (std::int32_t j=0; j<level.pickup_spawn_counts[kind]; ++j) {
            std::uint32_t spot=0U;
            do {
                spot=rng.next_u32()%spot_count;
            } while (used[spot]);
            used[spot]=true;
            PickupPlacement pickup=level_plan.random_spots[spot];
            pickup.kind=pick_effect_kind(static_cast<std::uint8_t>(kind),true,rng);
            placed.push_back(pickup);
        }
    }
    return placed;
}

void write_point(StateWriter& writer,float x,float z,std::uint16_t heading) {
    writer.f32(x);
    writer.f32(z);
    writer.u16(heading);
}

} // namespace

std::optional<ControllerRoster> decode_controller_roster(
    std::span<const std::uint8_t> table,std::int32_t index) noexcept {
    if (index<0 || index>=static_cast<std::int32_t>(controller_roster_count) ||
        table.size()<controller_roster_count*controller_roster_size) return std::nullopt;
    const std::size_t start=static_cast<std::size_t>(index)*controller_roster_size;
    ControllerRoster roster;
    roster.count=table[start];
    if (roster.count==0U || roster.count>roster.kinds.size()) return std::nullopt;
    const auto word=[&](std::size_t offset) {
        return static_cast<std::uint32_t>(table[start+offset])<<24U |
            static_cast<std::uint32_t>(table[start+offset+1U])<<16U |
            static_cast<std::uint32_t>(table[start+offset+2U])<<8U |
            table[start+offset+3U];
    };
    for (std::size_t slot=0U; slot<roster.kinds.size(); ++slot) {
        const std::uint32_t kind=word(4U+slot*4U);
        if (kind>2U) return std::nullopt;
        roster.kinds[slot]=static_cast<std::uint8_t>(kind);
    }
    roster.initial_event=static_cast<std::int32_t>(word(0x40U));
    roster.secondary_event=static_cast<std::int32_t>(word(0x44U));
    return roster;
}

std::optional<TeamSpawnTable> parse_team_spawn_table(std::span<const std::uint8_t> world_stream) {
    constexpr std::size_t entry_size=12U;
    if (world_stream.size()<4U) return std::nullopt;
    const BigEndianView view(world_stream);
    const std::size_t header=view.u32(0U);
    const std::size_t base=header+team_spawn_table_offset;
    if (header!=0x24U || base+team_spawn_rows*team_spawns_per_row*entry_size>world_stream.size()) {
        return std::nullopt;
    }
    TeamSpawnTable table{};
    for (std::size_t row=0U; row<team_spawn_rows; ++row) {
        for (std::size_t column=0U; column<team_spawns_per_row; ++column) {
            const std::size_t offset=base+(row*team_spawns_per_row+column)*entry_size;
            table[row][column]=TeamSpawnPoint{view.f32(offset),view.f32(offset+4U),
                                              view.u16(offset+8U)};
        }
    }
    return table;
}

void assign_slot_order(std::int32_t game_kind,RetailRandom& rng,
                       std::array<std::uint8_t,spawn_owner_count>& order,
                       std::array<std::uint8_t,spawn_owner_count>& rank) noexcept {
    if (game_kind>=3 && game_kind!=9) {
        order={0U,1U,2U,3U};
        rank={0U,1U,2U,3U};
        return;
    }
    const auto draw=[&rng]() { return static_cast<std::uint8_t>(rng.next_u32()&3U); };
    order[0]=draw();
    do order[1]=draw(); while (order[1]==order[0]);
    do order[2]=draw(); while (order[2]==order[1] || order[2]==order[0]);
    do order[3]=draw(); while (order[3]==order[1] || order[3]==order[0] || order[3]==order[2]);
    for (std::uint8_t n=0U; n<spawn_owner_count; ++n) rank[order[n]]=n;
}

std::size_t CampaignSpawnPlan::audit_count(PlanAuditReason reason) const noexcept {
    return static_cast<std::size_t>(std::count_if(audits.begin(),audits.end(),
        [reason](const PlanAudit& entry) { return entry.reason==reason; }));
}

std::optional<CampaignSpawnPlan> build_campaign_spawn_plan(
    const CampaignSpawnInput& input,const WorldLevel& level,
    const NavigationLevel* navigation,RetailRandom& rng,SpawnPlanError* error) {
    if (input.game_kind==demo_game_kind) {
        fail(error,SpawnPlanError::demo_playback);
        return std::nullopt;
    }
    if (!input.owners[0].present) {
        fail(error,SpawnPlanError::player_owner_absent);
        return std::nullopt;
    }
    CampaignSpawnPlan plan;
    plan.campaign=campaign_mode(input.game_kind);
    if (!plan.campaign && !input.team_spawns) {
        fail(error,SpawnPlanError::no_team_spawn_table);
        return std::nullopt;
    }
    assign_slot_order(input.game_kind,rng,plan.slot_order,plan.slot_rank);

    SpawnContext context;
    context.game_mode=input.game_kind;
    context.view_count=input.view_count;
    context.bank_group=level.bank_group;
    context.fast_spawn_interval=input.fast_spawn_interval;
    context.optional_placements_enabled=input.optional_placements_enabled;
    context.active_player_count=input.active_player_count;
    context.player_slot_map=plan.slot_rank;
    for (std::size_t owner=0U; owner<spawn_owner_count; ++owner) {
        context.teams[owner]=input.owners[owner].team;
    }
    context.flag2_gate=input.flag2_gate;
    const LevelSpawnPlan level_plan=interpret_level_spawns(level,context);
    plan.skipped=level_plan.skipped;

    if (plan.campaign && level_plan.markers[0].empty()) {
        fail(error,SpawnPlanError::no_player_markers);
        return std::nullopt;
    }

    // Fixed pickups re-roll in item order during the bank loop.
    for (PickupPlacement pickup : level_plan.pickups) {
        pickup.kind=pick_effect_kind(pickup.kind,input.pickup_reroll,rng);
        plan.pickups.push_back(pickup);
    }
    const std::vector<PickupPlacement> distributed=input.pickup_reroll
        ? distribute_with_reroll(level_plan,level,rng)
        : distribute_random_pickups(level_plan,level.pickup_spawn_counts,rng);
    plan.pickups.insert(plan.pickups.end(),distributed.begin(),distributed.end());

    for (const UnitSpawnDefinition& placed : level_plan.units) {
        OwnedObjectPlacement object;
        object.placement_index=placed.placement_index;
        object.x=placed.x;
        object.z=placed.z;
        object.heading=placed.heading;
        object.owner=placed.owner;
        object.team=placed.team;
        object.sub_kind=placed.side;
        object.health=owned_object_health[placed.side&3U];
        object.flagged=placed.respawn_flag;
        plan.owned_objects.push_back(object);
    }
    for (const SpawnerDefinition& definition : level_plan.spawners) {
        PlannedSpawner spawner;
        spawner.definition=definition;
        spawner.unit_kind=static_cast<retail::UnitKind>(definition.shape);
        spawner.random_shape=definition.mode==3U;
        plan.spawners.push_back(spawner);
    }

    for (std::size_t owner=0U; owner<spawn_owner_count; ++owner) {
        const SpawnOwnerSetup& setup=input.owners[owner];
        const std::vector<SpawnMarker>& markers=level_plan.markers[owner];
        OwnerRespawnPoints& respawn=plan.respawn_points[owner];
        for (std::size_t i=1U; i<markers.size(); ++i) {
            respawn.member_points.push_back(SpawnRespawnPoint{markers[i].x,markers[i].z,
                markers[i].heading,markers[i].param,markers[i].placement_index});
        }
        if (!setup.present) {
            if (!markers.empty()) {
                audit(plan,owner,0U,markers.front().placement_index,
                      PlanAuditReason::owner_absent_markers_unused);
            }
            continue;
        }
        PlannedOwner& planned=plan.owners[owner];
        planned.present=true;
        planned.team=setup.team;
        if (!setup.computer) planned.human_scale=human_owner_scale(setup.difficulty);
        if (plan.campaign) {
            respawn.policy=RespawnPolicy::campaign_markers;
            for (const SpawnMarker& marker : markers) {
                respawn.points.push_back(SpawnRespawnPoint{marker.x,marker.z,marker.heading,
                                                      marker.param,marker.placement_index});
            }
            place_campaign_owner(plan,setup,owner,markers);
        } else {
            respawn.policy=RespawnPolicy::team_spawn_row_random_offset;
            const std::size_t row=plan.slot_order[owner]&3U;
            for (std::size_t column=0U; column<team_spawns_per_row; ++column) {
                const TeamSpawnPoint& point=(*input.team_spawns)[row][column];
                respawn.points.push_back(SpawnRespawnPoint{point.x,point.z,point.heading,0U,
                    static_cast<std::uint32_t>(row*team_spawns_per_row+column)});
            }
            place_table_owner(plan,setup,owner,input.game_kind,input.pickup_reroll,
                              *input.team_spawns);
        }
    }

    const std::uint16_t active=plan.owners[0].active_unit;
    const auto found=std::find_if(plan.units.begin(),plan.units.end(),
        [active](const PlannedUnit& unit) { return unit.unit_id==active; });
    if (found==plan.units.end()) {
        fail(error,SpawnPlanError::invalid_player_kind);
        return std::nullopt;
    }
    plan.player.owner=0U;
    plan.player.unit_id=found->unit_id;
    plan.player.kind=found->kind;
    plan.player.x=found->x;
    plan.player.z=found->z;
    plan.player.heading=found->heading;
    plan.player.placement_index=found->placement_index;
    plan.player.source=found->source;
    if (navigation!=nullptr) {
        plan.player.navigation_triangle=open_triangle_at(*navigation,found->x,found->z);
        if (!plan.player.navigation_triangle) {
            audit(plan,0U,found->slot,found->placement_index,
                  PlanAuditReason::player_start_off_navigation);
        }
    }
    return plan;
}

void CampaignSpawnPlan::serialize(StateWriter& writer) const {
    writer.boolean(campaign);
    for (std::size_t n=0U; n<spawn_owner_count; ++n) {
        writer.u8(slot_order[n]);
        writer.u8(slot_rank[n]);
    }
    writer.u16(player.unit_id);
    writer.u8(static_cast<std::uint8_t>(player.kind));
    write_point(writer,player.x,player.z,player.heading);
    writer.u32(player.placement_index);
    writer.u8(static_cast<std::uint8_t>(player.source));
    writer.boolean(player.navigation_triangle.has_value());
    writer.u16(player.navigation_triangle.value_or(std::uint16_t{0U}));
    for (const PlannedOwner& owner : owners) {
        writer.boolean(owner.present);
        writer.u8(owner.team);
        writer.u16(owner.active_unit);
        writer.s32(owner.reserve);
        writer.s16(owner.extra_tank_respawns);
        writer.boolean(owner.human_scale.has_value());
        writer.f32(owner.human_scale.value_or(0.0F));
    }
    writer.u32(static_cast<std::uint32_t>(units.size()));
    for (const PlannedUnit& unit : units) {
        writer.u8(unit.owner);
        writer.u8(unit.team);
        writer.u8(unit.slot);
        writer.u16(unit.unit_id);
        writer.u8(static_cast<std::uint8_t>(unit.kind));
        write_point(writer,unit.x,unit.z,unit.heading);
        writer.u8(unit.parameter);
        writer.boolean(unit.primary);
        writer.boolean(unit.selectable);
        writer.u16(unit.flags);
        writer.u8(static_cast<std::uint8_t>(unit.source));
        writer.u32(unit.placement_index);
    }
    writer.u32(static_cast<std::uint32_t>(owned_objects.size()));
    for (const OwnedObjectPlacement& object : owned_objects) {
        writer.u32(object.placement_index);
        write_point(writer,object.x,object.z,object.heading);
        writer.u8(object.owner);
        writer.u8(object.team);
        writer.u8(object.sub_kind);
        writer.u16(object.health);
        writer.boolean(object.flagged);
    }
    writer.u32(static_cast<std::uint32_t>(spawners.size()));
    for (const PlannedSpawner& spawner : spawners) {
        const SpawnerDefinition& d=spawner.definition;
        writer.u32(d.placement_index);
        write_point(writer,d.x,d.z,d.heading);
        writer.u16(d.id);
        writer.u8(d.parameter);
        writer.s16(d.interval_ticks);
        writer.u8(static_cast<std::uint8_t>(spawner.unit_kind));
        writer.boolean(spawner.random_shape);
        writer.boolean(d.linked);
    }
    for (const OwnerRespawnPoints& respawn : respawn_points) {
        writer.u8(static_cast<std::uint8_t>(respawn.policy));
        writer.u32(static_cast<std::uint32_t>(respawn.points.size()));
        for (const SpawnRespawnPoint& point : respawn.points) {
            write_point(writer,point.x,point.z,point.heading);
            writer.u8(point.parameter);
        }
        writer.u32(static_cast<std::uint32_t>(respawn.member_points.size()));
        for (const SpawnRespawnPoint& point : respawn.member_points) {
            write_point(writer,point.x,point.z,point.heading);
            writer.u8(point.parameter);
        }
    }
    writer.u32(static_cast<std::uint32_t>(pickups.size()));
    for (const PickupPlacement& pickup : pickups) {
        writer.u32(pickup.placement_index);
        writer.f32(pickup.x);
        writer.f32(pickup.z);
        writer.u8(pickup.kind);
    }
    writer.u32(static_cast<std::uint32_t>(skipped.size()));
    for (const SkippedItem& item : skipped) {
        writer.u32(item.placement_index);
        writer.s32(item.type);
        writer.u8(static_cast<std::uint8_t>(item.reason));
    }
    writer.u32(static_cast<std::uint32_t>(audits.size()));
    for (const PlanAudit& entry : audits) {
        writer.u8(entry.owner);
        writer.u8(entry.slot);
        writer.u32(entry.placement_index);
        writer.u8(static_cast<std::uint8_t>(entry.reason));
    }
}

} // namespace battletanx
