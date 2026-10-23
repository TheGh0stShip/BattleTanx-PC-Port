#include "battletanx/deployables_runtime.hpp"

#include <cmath>

namespace battletanx::deployables {
namespace {

void push_event(DeployablesContext& context,DeployableEventKind kind,EntityId subject,std::int16_t tag,
                float x,float z,std::uint8_t variant=0U,std::uint16_t sound=0U) {
    DeployableEvent event;
    event.kind=kind;
    event.tick=context.tick;
    event.subject=subject;
    event.tag=tag;
    event.variant=variant;
    event.sound=sound;
    event.x=x;
    event.z=z;
    context.events.push(event);
}

std::uint16_t reversed_heading(std::uint16_t heading) noexcept {
    return static_cast<std::uint16_t>(heading+0x8000U);
}

// fire_unit_weapon.c:430/:451: (u32)(heading * 182.04445f + 32768.0f) & 0xFFFF.
// TEMPORARY POLICY: a negative sum (heading below about -180 degrees) is
// converted through a signed 64-bit intermediate (float->u32 of a negative
// value is undefined in C++); retail cvt behaviour there is UNRESOLVED.
std::uint16_t reversed_heading(const DeployerUnit& unit) noexcept {
    if (!unit.heading_degrees) return reversed_heading(unit.heading);
    const float h=*unit.heading_degrees*182.04445F+32768.0F;
    if (!(h>-9.0e18F && h<9.0e18F)) return 0x8000U;   // NaN / out of range: TEMPORARY POLICY
    const auto wide=static_cast<std::int64_t>(h);
    return static_cast<std::uint16_t>(static_cast<std::uint64_t>(wide)&0xFFFFU);
}

float rear_offset(std::uint8_t unit_kind) noexcept {
    switch (unit_kind) {
    case 1U: return buddy_rear_offset_bike;
    case 0U:
    case 2U: return buddy_rear_offset_tank;
    default: return 0.0F;   // sZeroOffset: other kinds spawn at the hull origin
    }
}

// TEMPORARY POLICY: func_80107A14 overlap of a query box against an existing
// mine footprint (create_effect_motion -10..10) as an axis-aligned box test.
bool mine_blocks(const MinePool& mines,float x,float z,float half_extent) {
    const float reach=mine_place_half_extent+half_extent;
    bool blocked=false;
    mines.for_each([&](EntityId,const MineRecord& mine) {
        if (blocked || mine.detonate_tick!=0) return;   // detonating mines freed their motion
        if (std::fabs(mine.x-x)<=reach && std::fabs(mine.z-z)<=reach) blocked=true;
    });
    return blocked;
}

void set_awake(DeployablesContext& context,EntityId id,MineRecord& mine,bool in_update) {
    if (mine.awake || mine.wake_pending) return;
    if (in_update) mine.wake_pending=true;
    else mine.awake=true;
    push_event(context,DeployableEventKind::mine_woken,id,mine.tag,mine.x,mine.z);
}

std::optional<EntityId> start_grid(DeployablesContext& context,const DeployerUnit& unit,bool buddies) {
    const auto id=context.state.grids.allocate();
    if (!id) return std::nullopt;   // spawn_effect_kind_30 is void: failure is silent
    GridRecord& grid=*context.state.grids.get(*id);
    const float fx=unit.hull_forward_x;
    const float fz=unit.hull_forward_z;
    grid.origin_x=unit.x;
    grid.origin_z=unit.z;
    grid.step_x=-fx*grid_spacing;
    grid.step_z=-fz*grid_spacing;
    grid.side_z=fx*grid_spacing;
    grid.side_x=-fz*grid_spacing;
    grid.tag=unit.id;
    grid.heading=buddies ? reversed_heading(unit) : std::uint16_t{0U};
    grid.buddies=buddies;
    push_event(context,DeployableEventKind::grid_started,*id,grid.tag,grid.origin_x,grid.origin_z);
    return id;
}

bool request_squad(DeployablesContext& context,const SquadSpawnRequest& request,std::int16_t tag) {
    const bool spawned=context.world.spawn_squad_unit && context.world.spawn_squad_unit(request);
    push_event(context,spawned ? DeployableEventKind::buddy_requested : DeployableEventKind::buddy_failed,
               no_entity,tag,request.x,request.z,request.squad_kind);
    return spawned;
}

// step_grid_barrage (func_8010DF90). Returns true when the grid finished.
bool step_grid(DeployablesContext& context,EntityId id,GridRecord& grid) {
    const float column=static_cast<float>(grid.column)-2.0F;
    const float row=static_cast<float>(grid.row+1U);
    // Two random_float_range(60) calls in source order: x first, then z.
    const float x=grid.origin_x+grid.step_x*row+grid.side_x*column+context.random.next_float(grid_jitter_range)-grid_jitter_bias;
    const float z=grid.origin_z+grid.step_z*row+grid.side_z*column+context.random.next_float(grid_jitter_range)-grid_jitter_bias;
    if (grid.buddies) {
        if ((grid.column+grid.row)%grid_buddy_period==0) {
            SquadSpawnRequest request;
            request.team=context.world.unit_team ? context.world.unit_team(grid.tag) : std::uint8_t{0U};
            request.owner_unit=grid.tag;
            request.squad_kind=grid_buddy_squad_kind;
            request.heading=grid.heading;
            request.x=x;
            request.z=z;
            request.check_area=false;
            request.origin_x=x;
            request.origin_z=z;
            static_cast<void>(request_squad(context,request,grid.tag));
        }
    } else {
        static_cast<void>(place_mine(context,x,z,grid.tag));
    }
    if (++grid.column==grid_size) {
        grid.column=0U;
        if (++grid.row==grid_size) {
            push_event(context,DeployableEventKind::grid_finished,id,grid.tag,grid.origin_x,grid.origin_z);
            return true;
        }
    }
    return false;
}

// delayed_blast_effect_update (func_8010DC0C). Returns true when removed.
bool update_mine(DeployablesContext& context,EntityId id,MineRecord& mine) {
    if (mine.detonate_tick==0) {
        push_event(context,DeployableEventKind::mine_detonating,id,mine.tag,mine.x,mine.z,
                   mine_detonate_variant,sound_mine_detonate);
        mine.detonate_tick=context.tick;
        return false;
    }
    if (!(mine.detonate_tick+mine_detonate_ticks<context.tick)) return false;
    const auto flag=static_cast<std::uint8_t>(static_cast<std::uint16_t>(mine.tag)&0xFFU);
    push_event(context,DeployableEventKind::mine_splash,id,mine.tag,mine.x,mine.z);
    // ORDERING POLICY: units through the adapter first, then mines in slot order.
    if (context.world.splash_units)
        static_cast<void>(context.world.splash_units(mine.x,mine.z,mine_splash_radius,mine_splash_amount,flag,id));
    const float radius_sq=mine_splash_radius*mine_splash_radius;
    const float cx=mine.x;
    const float cz=mine.z;
    context.state.mines.for_each([&](EntityId other,MineRecord& target) {
        if (other==id || target.detonate_tick!=0) return;
        const float dx=target.x-cx;
        const float dz=target.z-cz;
        if (dx*dx+dz*dz<=radius_sq) set_awake(context,other,target,true);   // message 7
    });
    return true;
}

} // namespace

void DeployablesState::serialize(StateWriter& writer) const {
    writer.u32(static_cast<std::uint32_t>(mine_capacity));
    for (std::size_t slot=0U; slot<mine_capacity; ++slot) {
        writer.u16(mines.slot_generation(slot));   // next allocate reuses it
        writer.boolean(mines.slot_occupied(slot));
        if (!mines.slot_occupied(slot)) continue;
        const EntityId id=mines.slot_id(slot);
        const MineRecord& m=*mines.get(id);
        writer.id(id);
        writer.f32(m.x);
        writer.f32(m.z);
        writer.s16(m.tag);
        writer.s32(m.placed_tick);
        writer.s32(m.detonate_tick);
        writer.boolean(m.awake);
        writer.boolean(m.wake_pending);
    }
    writer.u32(static_cast<std::uint32_t>(grid_capacity));
    for (std::size_t slot=0U; slot<grid_capacity; ++slot) {
        writer.u16(grids.slot_generation(slot));
        writer.boolean(grids.slot_occupied(slot));
        if (!grids.slot_occupied(slot)) continue;
        const EntityId id=grids.slot_id(slot);
        const GridRecord& g=*grids.get(id);
        writer.id(id);
        writer.f32(g.origin_x);
        writer.f32(g.origin_z);
        writer.f32(g.step_x);
        writer.f32(g.step_z);
        writer.f32(g.side_x);
        writer.f32(g.side_z);
        writer.u8(g.column);
        writer.u8(g.row);
        writer.s16(g.tag);
        writer.u16(g.heading);
        writer.boolean(g.buddies);
    }
}

void DeployablesState::clear() noexcept {
    mines=MinePool{};
    grids=GridPool{};
}

bool DeployablesState::mine_blocks_area(float x,float z,float half_extent) const {
    return mine_blocks(mines,x,z,half_extent);
}

bool mine_armed(const MineRecord& mine,std::int32_t tick) noexcept {
    return mine.placed_tick+mine_arm_ticks<tick;
}

std::optional<EntityId> place_mine(DeployablesContext& context,float x,float z,std::int16_t tag) {
    if (tag!=no_tag) {
        if (context.state.mine_blocks_area(x,z,mine_place_half_extent)) return std::nullopt;
        if (context.world.placement_blocked &&
            context.world.placement_blocked(x,z,mine_place_half_extent,mine_place_radius)) return std::nullopt;
    }
    const auto id=context.state.mines.allocate();
    if (!id) return std::nullopt;
    MineRecord& mine=*context.state.mines.get(*id);
    mine.x=x;
    mine.z=z;
    mine.tag=tag;
    mine.placed_tick=context.tick;
    mine.detonate_tick=0;
    push_event(context,DeployableEventKind::mine_placed,*id,tag,x,z,0U,sound_mine_placed);
    return id;
}

DeployResult fire_mine_weapon(DeployablesContext& context,const DeployerUnit& unit) {
    DeployResult result;
    if (unit.powered) {
        result.status=DeployStatus::placed;
        result.consume_whole_stock=true;
        if (const auto id=start_grid(context,unit,false)) result.spawned=*id;
        return result;
    }
    // Distinguish the two -1 causes for the caller; retail treats both alike.
    const bool pool_full=context.state.mines.full();
    if (const auto id=place_mine(context,unit.x,unit.z,unit.id)) {
        result.status=DeployStatus::placed;
        result.spawned=*id;
    } else {
        result.status=pool_full ? DeployStatus::pool_full : DeployStatus::blocked;
    }
    return result;
}

DeployResult fire_gun_buddy_weapon(DeployablesContext& context,const DeployerUnit& unit) {
    DeployResult result;
    if (unit.powered) {
        result.status=DeployStatus::placed;
        result.consume_whole_stock=true;
        if (const auto id=start_grid(context,unit,true)) result.spawned=*id;
        return result;
    }
    // func_800A7D10(hullMtx, {0, 0, offset}): local +z is the hull forward vector.
    const float offset=rear_offset(unit.unit_kind);
    SquadSpawnRequest request;
    request.team=unit.team;
    request.owner_unit=unit.id;
    request.squad_kind=buddy_squad_kind;
    request.heading=reversed_heading(unit);
    request.x=unit.x+unit.hull_forward_x*offset;
    request.z=unit.z+unit.hull_forward_z*offset;
    request.check_area=true;
    request.half_extent=squad_size_scale[buddy_squad_kind]*squad_half_extent_unit;
    request.origin_x=unit.x;
    request.origin_z=unit.z;
    result.status=request_squad(context,request,unit.id) ? DeployStatus::placed : DeployStatus::blocked;
    return result;
}

ContactResult contact_mine(DeployablesContext& context,EntityId id,ContactSender sender) {
    ContactResult result;
    MineRecord* mine=context.state.mines.get(id);
    if (mine==nullptr || mine->detonate_tick!=0) return result;
    switch (sender) {
    case ContactSender::mine:
        set_awake(context,id,*mine,false);
        break;
    case ContactSender::other:
        result.report=ContactReport::ordinary;
        break;
    case ContactSender::unit:
        if (mine_armed(*mine,context.tick)) {
            set_awake(context,id,*mine,false);
            result.report=ContactReport::damage;
            result.amount=mine_contact_amount;
            result.attacker=mine->tag;
            result.wear=static_cast<float>(mine_contact_amount)*mine_contact_wear_scale;
        } else {
            result.report=ContactReport::ordinary;
        }
        break;
    }
    return result;
}

bool wake_mine(DeployablesContext& context,EntityId id) {
    MineRecord* mine=context.state.mines.get(id);
    if (mine==nullptr) return false;
    set_awake(context,id,*mine,false);
    return true;
}

std::optional<std::pair<float,float>> mine_position(const DeployablesState& state,EntityId id) {
    const MineRecord* mine=state.mines.get(id);
    if (mine==nullptr) return std::nullopt;
    return std::pair<float,float>{mine->x,mine->z};
}

void update_deployables(DeployablesContext& context) {
    // Grids first. Mines a grid places this pass are idle and need no update.
    for (std::size_t slot=0U; slot<grid_capacity; ++slot) {
        const EntityId id=context.state.grids.slot_id(slot);
        GridRecord* grid=context.state.grids.get(id);
        if (grid==nullptr) continue;
        if (step_grid(context,id,*grid)) context.state.grids.release(id);
    }
    for (std::size_t slot=0U; slot<mine_capacity; ++slot) {
        const EntityId id=context.state.mines.slot_id(slot);
        MineRecord* mine=context.state.mines.get(id);
        if (mine==nullptr || !mine->awake) continue;
        if (update_mine(context,id,*mine)) {
            push_event(context,DeployableEventKind::mine_removed,id,mine->tag,mine->x,mine->z);
            context.state.mines.release(id);
        }
    }
    // Mines woken by a splash during this pass start updating next pass.
    context.state.mines.for_each([](EntityId,MineRecord& mine) {
        if (mine.wake_pending) { mine.wake_pending=false; mine.awake=true; }
    });
}

} // namespace battletanx::deployables
