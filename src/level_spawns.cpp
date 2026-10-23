#include "battletanx/level_spawns.hpp"

#include <algorithm>

namespace battletanx {
namespace {

constexpr std::uint8_t flag_campaign_only=0x10U;
constexpr std::uint8_t flag_skirmish_only=0x40U;
constexpr std::uint8_t flag_capacity_gated=0x02U;

bool mode_filtered(std::uint8_t flags,bool campaign) noexcept {
    return ((flags&flag_campaign_only)!=0U && !campaign) ||
           ((flags&flag_skirmish_only)!=0U && campaign);
}

bool known_placed_type(std::int32_t type) noexcept {
    switch (type) {
    case 0: case 1: case 3: case 5: case 6: case 7: case 8: case 9:
    case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
    case 19: case 26: case 27: case 30: case 31:
        return true;
    default:
        return false;
    }
}

void skip(LevelSpawnPlan& plan,std::uint32_t index,std::int32_t type,SkipReason reason) {
    plan.skipped.push_back(SkippedItem{index,type,reason});
}

void interpret_bank_item(LevelSpawnPlan& plan,const WorldItem& item,std::uint32_t index,
                         const SpawnContext& context,bool campaign) {
    switch (item.type) {
    case 25: {
        const std::size_t group=item.parameter>>4U;
        if (group>=marker_groups) {
            skip(plan,index,item.type,SkipReason::marker_group_out_of_range);
            return;
        }
        auto& slots=plan.markers[group];
        if (slots.size()>=markers_per_group) {
            skip(plan,index,item.type,SkipReason::marker_group_full);
            return;
        }
        SpawnMarker marker;
        marker.placement_index=index;
        marker.x=item.x;
        marker.z=item.z;
        marker.heading=item.angle;
        marker.low=static_cast<std::uint8_t>(item.parameter&0xFU);
        marker.param=static_cast<std::uint8_t>(item.id&0xFFU);
        marker.high=static_cast<std::uint8_t>(
            (static_cast<std::int16_t>(item.id)>>12)&3);
        slots.push_back(marker);
        return;
    }
    case 10: {
        const bool campaign_flag=(item.flags&flag_campaign_only)!=0U;
        if (campaign!=campaign_flag) {
            skip(plan,index,item.type,SkipReason::pickup_mode_filtered);
            return;
        }
        PickupPlacement pickup{index,item.x,item.z,item.parameter};
        if (item.parameter==random_pickup_spot_parameter) {
            if (plan.random_spots.size()<max_random_pickup_spots) {
                plan.random_spots.push_back(pickup);
            } else {
                skip(plan,index,item.type,SkipReason::random_spot_overflow);
            }
            return;
        }
        plan.pickups.push_back(pickup);
        return;
    }
    case 22: {
        if ((item.id&1U)!=0U && !context.optional_placements_enabled) {
            skip(plan,index,item.type,SkipReason::optional_entry_gated);
            return;
        }
        const std::uint8_t player=context.player_slot_map[(item.id>>1U)&3U];
        if (static_cast<std::int32_t>(player)>=context.active_player_count) {
            skip(plan,index,item.type,SkipReason::player_slot_absent);
            return;
        }
        UnitSpawnDefinition unit;
        unit.placement_index=index;
        unit.x=item.x;
        unit.z=item.z;
        unit.heading=item.angle;
        unit.owner=player;
        unit.player=true;
        unit.source=SpawnSource::bank_unit_placement;
        unit.team=player<context.teams.size() ? context.teams[player] : std::uint8_t{0U};
        unit.side=static_cast<std::uint8_t>((item.id>>3U)&3U);
        unit.optional_entry=(item.id&1U)!=0U;
        unit.respawn_flag=!campaign && ((item.id>>8U)&1U)!=0U;
        plan.units.push_back(unit);
        return;
    }
    case 21: {
        if (context.view_count!=1) {
            skip(plan,index,item.type,SkipReason::view_count_gated);
            return;
        }
        SpawnerDefinition spawner=unpack_spawner(item.id,item.parameter,
                                                 context.fast_spawn_interval);
        spawner.placement_index=index;
        spawner.x=item.x;
        spawner.z=item.z;
        spawner.heading=item.angle;
        spawner.source=SpawnSource::bank_spawner;
        plan.spawners.push_back(spawner);
        return;
    }
    case 23:
    case 24:
        skip(plan,index,item.type,context.view_count==1 ? SkipReason::untraced_type
                                                        : SkipReason::view_count_gated);
        return;
    default:
        skip(plan,index,item.type,SkipReason::retail_ignored_type);
        return;
    }
}

void interpret_placed_item(LevelSpawnPlan& plan,const WorldItem& item,std::uint32_t index,
                           const SpawnContext& context,bool campaign) {
    if ((item.flags&flag_capacity_gated)!=0U && context.flag2_gate &&
        !context.flag2_gate(static_cast<std::uint8_t>((item.flags&0xCU)>>2U))) {
        skip(plan,index,item.type,SkipReason::capacity_gated);
        return;
    }
    const bool type20=item.type==20;
    if (type20 || item.type==21) {
        if (type20 && !campaign) {
            skip(plan,index,item.type,SkipReason::linked_spawner_mode);
            return;
        }
        if (!type20 && context.view_count!=1) {
            skip(plan,index,item.type,SkipReason::view_count_gated);
            return;
        }
        SpawnerDefinition spawner=unpack_spawner(item.id,item.parameter,
                                                 context.fast_spawn_interval);
        spawner.placement_index=index;
        spawner.x=item.x;
        spawner.z=item.z;
        spawner.heading=item.angle;
        spawner.linked=type20;
        spawner.source=type20 ? SpawnSource::placed_spawner_linked
                              : SpawnSource::placed_spawner_unlinked;
        plan.spawners.push_back(spawner);
        return;
    }
    skip(plan,index,item.type,known_placed_type(item.type) ? SkipReason::placed_object_other
                                                           : SkipReason::unknown_placed_type);
}

} // namespace

SpawnerDefinition unpack_spawner(std::uint16_t id,std::uint8_t parameter,
                                 bool fast_interval) noexcept {
    SpawnerDefinition spawner;
    spawner.id=id;
    spawner.owner=static_cast<std::uint8_t>(id&3U);
    spawner.mode=static_cast<std::uint8_t>((id>>2U)&3U);
    spawner.shape=spawner.mode==0U ? std::uint8_t{0U}
                : spawner.mode==1U ? std::uint8_t{1U} : std::uint8_t{2U};
    spawner.count_field=static_cast<std::uint8_t>((id>>4U)&0xFFU);
    const std::int32_t step=fast_interval ? 30 : 45;
    spawner.interval_ticks=static_cast<std::int16_t>(step*spawner.count_field);
    spawner.parameter=parameter;
    spawner.gate_bits=static_cast<std::uint8_t>((id>>12U)&3U);
    return spawner;
}

std::size_t LevelSpawnPlan::skipped_count(SkipReason reason) const noexcept {
    return static_cast<std::size_t>(std::count_if(skipped.begin(),skipped.end(),
        [reason](const SkippedItem& item) { return item.reason==reason; }));
}

LevelSpawnPlan interpret_level_spawns(const WorldLevel& level,const SpawnContext& context) {
    LevelSpawnPlan plan;
    const bool campaign=campaign_mode(context.game_mode);
    plan.random_distribution_allowed=!campaign;
    for (std::size_t group_index=0U; group_index<level.groups.size(); ++group_index) {
        const WorldGroup& group=level.groups[group_index];
        const bool bank=context.bank_group.has_value() && *context.bank_group==group_index;
        for (std::size_t n=0U; n<group.item_count; ++n) {
            const std::size_t item_index=static_cast<std::size_t>(group.first_item)+n;
            if (item_index>=level.items.size()) break;
            const WorldItem& item=level.items[item_index];
            const auto index=static_cast<std::uint32_t>(item_index);
            if (mode_filtered(item.flags,campaign)) {
                skip(plan,index,item.type,SkipReason::mode_flag_filtered);
                continue;
            }
            if (bank) interpret_bank_item(plan,item,index,context,campaign);
            else interpret_placed_item(plan,item,index,context,campaign);
        }
    }
    return plan;
}

std::uint8_t pick_effect_kind(std::uint8_t kind,bool reroll_option,RetailRandom& rng) noexcept {
    if (!reroll_option) return kind;
    switch (kind) {
    case 5: case 6: case 7: case 9: case 10: case 13: case 14: case 16:
        break;
    default:
        return kind;
    }
    static constexpr std::array<std::uint8_t,11> table{0U,0U,0U,3U,3U,2U,2U,4U,8U,12U,15U};
    return table[rng.next_u32()%11U];
}

std::vector<PickupPlacement> distribute_random_pickups(
    const LevelSpawnPlan& plan,
    const std::array<std::int32_t,pickup_spawn_count_kinds>& spawn_counts,
    RetailRandom& rng) {
    std::vector<PickupPlacement> placed;
    if (!plan.random_distribution_allowed) return placed;
    std::int32_t total=0;
    for (const std::int32_t count : spawn_counts) total+=count;
    const auto spot_count=static_cast<std::uint32_t>(plan.random_spots.size());
    if (static_cast<std::int64_t>(spot_count)<total || spot_count==0U) return placed;
    std::vector<bool> used(spot_count,false);
    for (std::size_t kind=0U; kind<spawn_counts.size(); ++kind) {
        for (std::int32_t j=0; j<spawn_counts[kind]; ++j) {
            std::uint32_t spot=0U;
            do {
                spot=rng.next_u32()%spot_count;
            } while (used[spot]);
            used[spot]=true;
            PickupPlacement pickup=plan.random_spots[spot];
            pickup.kind=static_cast<std::uint8_t>(kind);
            placed.push_back(pickup);
        }
    }
    return placed;
}

SpawnerState make_spawner_state(const SpawnerDefinition& definition,
                                std::int32_t linked_serial) noexcept {
    SpawnerState state;
    state.definition=definition;
    state.linked_serial=linked_serial;
    return state;
}

SpawnerUpdateResult update_spawner(SpawnerState& spawner,
                                   const std::optional<LinkedEffectView>& linked,
                                   SpawnerOwnerView& owner,std::int32_t tick,
                                   RetailRandom& rng,const SpawnAreaClear& area_clear) {
    SpawnerUpdateResult result;
    const SpawnerDefinition& def=spawner.definition;
    if (spawner.finished) {
        result.status=SpawnerUpdateStatus::finished;
        return result;
    }
    if (def.linked) {
        if (!linked.has_value() || linked->serial!=spawner.linked_serial) {
            spawner.finished=true;
            result.status=SpawnerUpdateStatus::finished;
            return result;
        }
        if (linked->destroyed) {
            spawner.finished=true;
            result.status=SpawnerUpdateStatus::finished_destroyed;
            return result;
        }
    }
    if ((def.parameter==0x45U || def.gate_bits==1U) && !owner.gate_open) {
        result.status=SpawnerUpdateStatus::gated;
        return result;
    }
    if (spawner.last_tick+def.interval_ticks>=tick) {
        result.status=SpawnerUpdateStatus::interval_pending;
        return result;
    }
    if ((rng.next_u32()&0xFU)!=0U) {
        result.status=SpawnerUpdateStatus::roll_failed;
        return result;
    }
    if (owner.budget==0) {
        result.status=SpawnerUpdateStatus::no_budget;
        return result;
    }
    std::uint8_t shape=def.shape;
    if (def.mode==3U) {
        (void)rng.next_u32();  // retail draws a bit; both outcomes give shape 2
        shape=2U;
    }
    const SpawnAreaQuery query{def.x,def.z,static_cast<std::uint16_t>(def.heading+0x200U),shape};
    if (area_clear && !area_clear(query)) {
        result.status=SpawnerUpdateStatus::area_blocked;
        return result;
    }
    for (std::size_t slot=0U; slot<owner.squad_slot_alive.size(); ++slot) {
        if (owner.squad_slot_alive[slot]) continue;
        result.request=SpawnRequest{def.owner,static_cast<std::uint8_t>(slot),def.x,def.z,
                                    def.heading,shape,def.parameter,tick};
        spawner.last_tick=tick;
        if (owner.budget>0) --owner.budget;
        result.status=SpawnerUpdateStatus::spawned;
        return result;
    }
    result.status=SpawnerUpdateStatus::no_free_slot;
    return result;
}

} // namespace battletanx
