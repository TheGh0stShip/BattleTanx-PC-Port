#include "battletanx/unit_kinds.hpp"

#include "battletanx/runtime_math.hpp"

#include <cmath>

namespace battletanx {
namespace {

// spawn_unit tables, index bike=0, tank=1, goliath=2.
inline constexpr std::array<float,3> table_ef0{0.22F,0.18F,0.16F};               // ROM 0xB5EF0
inline constexpr std::array<float,3> table_efc{200000.0F,200000.0F,200000.0F};   // ROM 0xB5EFC
inline constexpr std::array<float,3> table_f08{100.0F,250.0F,250.0F};            // ROM 0xB5F08
inline constexpr std::array<float,3> table_f18{230.0F,300.0F,230.0F};            // ROM 0xB5F18
inline constexpr std::array<float,3> table_f24{50.0F,100.0F,100.0F};             // ROM 0xB5F24

std::size_t movement_index(retail::UnitKind kind) noexcept {
    switch (kind) {
    case retail::UnitKind::bike: return 0U;
    case retail::UnitKind::tank: return 1U;
    case retail::UnitKind::goliath: return 2U;
    }
    return 1U;
}

std::uint16_t angle_distance(std::uint16_t a,std::uint16_t b) noexcept {
    const auto d=static_cast<std::uint16_t>(a-b);
    const auto e=static_cast<std::uint16_t>(b-a);
    return d<e ? d : e;
}

std::uint16_t turn_toward(std::uint16_t heading,std::uint16_t target,std::uint16_t step) noexcept {
    const auto fwd=static_cast<std::uint16_t>(target-heading);
    const auto back=static_cast<std::uint16_t>(heading-target);
    if (fwd<back) return static_cast<std::uint16_t>(heading+(fwd>=step ? step : fwd));
    return static_cast<std::uint16_t>(heading-(back>=step ? step : back));
}

bool turret_ready(const TurretRecord& turret,double wait,const TurretContext& context) noexcept {
    const double last=turret.last_fire;
    const double now=context.tick;
    if ((turret.flags&turret_scaled)!=0U) {
        const std::size_t i=context.owner_effect_count<turret_owner_count_scale.size() ?
                            context.owner_effect_count : turret_owner_count_scale.size()-1U;
        return last+wait*static_cast<double>(turret_owner_count_scale[i])<now;
    }
    return last+wait<now;
}

// set_yaw_transform + transform_point_3x4. Rotation sense matches
// pickup_overlaps (heading measured from +z toward +x).
void turret_muzzle(TurretRecord& turret,const TurretContext& context,float& mx,float& mz) {
    const float scale=turret_scale[turret.type];
    float ox=turret_muzzle_x*scale;
    const float oz=turret_muzzle_z*scale;
    if ((turret.flags&turret_muzzle_side)!=0U) ox=-ox;
    turret.flags^=turret_muzzle_side;
    const float s=context.angles.sine(turret.heading);
    const float c=context.angles.cosine(turret.heading);
    mx=turret.x+ox*c+oz*s;
    mz=turret.z-ox*s+oz*c;
}

TurretShot make_shot(const TurretRecord& turret,const TurretContext& context,float mx,float mz,
                     std::uint16_t aim) {
    TurretShot shot;
    shot.x=mx;
    shot.z=mz;
    shot.heading=aim;
    shot.speed=turret_shot_speed[turret.type];
    shot.velocity_x=context.angles.sine(aim)*static_cast<float>(shot.speed);
    shot.velocity_z=context.angles.cosine(aim)*static_cast<float>(shot.speed);
    shot.projectile=turret_projectile[turret.type];
    shot.effect_kind=context.owner.effect_kind[turret.type];
    shot.sound=turret_fire_sound[turret.type];
    shot.target=turret.target;
    return shot;
}

OwnerRecord* owner_in(std::span<OwnerRecord> owners,std::uint8_t index) noexcept {
    return index<owners.size() ? &owners[index] : nullptr;
}

} // namespace

std::optional<UnitKindDefinition> unit_kind_definition(std::uint8_t kind) noexcept {
    if (kind>2U) return std::nullopt;
    UnitKindDefinition d;
    d.kind=static_cast<retail::UnitKind>(kind);
    d.max_health=retail::unit_max_health[kind];
    d.footprint=retail::unit_footprint[kind];
    const std::size_t m=movement_index(d.kind);
    d.movement.grip=table_f18[m];
    d.movement.mass=table_f24[m];
    d.movement.inverse_mass=1.0F/table_f24[m];
    d.movement.inverse_inertia=1.0F/(table_f24[m]/12.0F*6554.0F);
    d.movement.value3BC=table_efc[m];
    d.movement.accel=table_ef0[m];
    d.movement.drag=table_f08[m];
    d.primary_weapon=retail::unit_primary_weapon[kind];
    d.primary_ammo=retail::unit_primary_ammo[kind];
    d.kill_score=retail::kill_score[kind];
    d.crushed_by_goliath=d.kind==retail::UnitKind::bike;
    d.rail_capable=d.kind==retail::UnitKind::goliath;
    return d;
}

std::optional<UnitModelSet> unit_model_set(std::uint8_t kind,bool damaged) noexcept {
    using Transform=UnitModelTransform;
    using Animation=UnitModelAnimation;
    UnitModelSet result;
    switch (static_cast<retail::UnitKind>(kind)) {
    case retail::UnitKind::tank:
        result.parts={{{0x0BU,Transform::body,Animation::left_track},
                       {0x0CU,Transform::body,Animation::right_track},
                       {static_cast<std::uint16_t>(damaged ? 0x13U : 0x0DU),Transform::turret,Animation::none},
                       {static_cast<std::uint16_t>(damaged ? 0x14U : 0x0EU),Transform::body,Animation::none}}};
        result.part_count=4U;
        result.shadow_slot=0x52U;
        return result;
    case retail::UnitKind::bike:
        result.parts={{{0x1AU,Transform::body,Animation::left_track},
                       {0x1BU,Transform::body,Animation::right_track},
                       {static_cast<std::uint16_t>(damaged ? 0x21U : 0x19U),Transform::body,Animation::none},
                       {static_cast<std::uint16_t>(damaged ? 0x22U : 0x1CU),Transform::body,Animation::none},
                       {static_cast<std::uint16_t>(damaged ? 0x1EU : 0x1DU),Transform::body,Animation::none}}};
        result.part_count=5U;
        result.shadow_slot=0x53U;
        return result;
    case retail::UnitKind::goliath:
        result.parts={{{static_cast<std::uint16_t>(damaged ? 0x2DU : 0x29U),Transform::turret,Animation::none},
                       {static_cast<std::uint16_t>(damaged ? 0x2BU : 0x27U),Transform::body,Animation::left_track},
                       {static_cast<std::uint16_t>(damaged ? 0x2CU : 0x28U),Transform::body,Animation::right_track},
                       {static_cast<std::uint16_t>(damaged ? 0x2EU : 0x2AU),Transform::body,Animation::none}}};
        result.part_count=4U;
        result.shadow_slot=0x54U;
        return result;
    }
    return std::nullopt;
}

void RailState::serialize(StateWriter& writer) const {
    writer.f32(origin_x); writer.f32(origin_z); writer.f32(dir_x); writer.f32(dir_z);
    writer.f32(length); writer.f32(travel); writer.f32(velocity); writer.s32(exit_tick);
}

std::optional<UnitKindSpawn> make_unit_of_kind(std::uint8_t kind,std::uint8_t owner,std::uint8_t team,
        float x,float z,std::uint16_t heading,std::int32_t tick,UnitControl control,
        const UnitKindSpawnOptions& options) {
    const std::optional<UnitKindDefinition> def=unit_kind_definition(kind);
    if (!def) return std::nullopt;
    UnitKindSpawn spawn;
    UnitRecord& unit=spawn.unit;
    unit=make_unit(def->kind,owner,team,x,z,heading,tick,control);
    unit.flags=options.flags;
    unit.primary=options.primary;
    // Rail placement (goliath only, not modes 2 and 9; the zone must be active).
    if (def->rail_capable && options.rail && options.rail->active && options.angles!=nullptr &&
        (!options.primary || options.local_owner || options.game_mode==7) &&
        !options.owner_rail_taken && options.game_mode!=9 && options.game_mode!=2) {
        const RailZone& zone=*options.rail;
        RailState rail;
        rail.origin_x=zone.start_x;
        rail.origin_z=zone.start_z;
        const float dx=zone.end_x-zone.start_x;
        const float dz=zone.end_z-zone.start_z;
        const float len=std::sqrt(dx*dx+dz*dz);
        rail.dir_x=dx/len;
        rail.dir_z=dz/len;
        rail.length=len;
        rail.travel=(unit.x-rail.origin_x)*rail.dir_x+(unit.z-rail.origin_z)*rail.dir_z;
        if (rail.travel<0.0F) rail.travel=0.0F;
        if (rail.travel>rail.length) rail.travel=rail.length;
        unit.x=rail.origin_x+rail.travel*rail.dir_x;
        unit.z=rail.origin_z+rail.travel*rail.dir_z;
        const std::uint16_t angle=direction_angle(rail.dir_x,rail.dir_z);
        unit.heading_degrees=angle_to_degrees(static_cast<std::uint16_t>(angle+0xC000U));
        unit.turret_degrees=unit.heading_degrees;
        unit.flags=static_cast<std::uint16_t>(unit.flags|flag_rail_spawn);
        spawn.rail=rail;
        spawn.rail_claimed=true;
    }
    unit.max_health=def->max_health;
    if (options.local_owner && options.goliath_health_bonus && def->kind==retail::UnitKind::goliath) {
        if ((unit.flags&flag_rail_unit)!=0U) unit.max_health*=2;
        else unit.max_health+=unit.max_health>>1;
    }
    unit.health=unit.max_health;
    unit.ammo.fill(0);
    unit.secondary_weapon={};
    if (options.stocked_cheat) {
        unit.secondary_weapon.kind=5U;
        unit.ammo[6]=1; unit.ammo[12]=60; unit.ammo[13]=3;
        unit.ammo[11]=unit.ammo[4]=unit.ammo[5]=50; unit.ammo[7]=99;
    }
    if (options.permanent_cloak_cheat && options.primary && owner==0U) spawn.timed.cloak_end_tick=1000000;
    unit.primary_weapon.kind=def->primary_weapon;
    unit.primary_weapon.last_fire_tick=retail::never_fired_tick;
    unit.ammo[def->primary_weapon]=def->primary_ammo;
    if ((unit.flags&retail::flag_spawn_protected)!=0U) {
        spawn.timed.cloak_flash=0;
        spawn.timed.cloak_end_tick=tick+60;
    }
    if (options.reveal_cheat) unit.flags=static_cast<std::uint16_t>(unit.flags|flag_map_reveal);
    return spawn;
}

RailStepResult update_rail_unit(UnitRecord& unit,RailState& rail,const RailInput& input,
                                std::int32_t game_mode,std::int32_t tick,float frame_delta,
                                bool exit_keeps_body_heading) {
    RailStepResult result;
    if (!unit.alive) return result;
    if (input.boost && !input.ai_state_two && game_mode!=7) {
        if (rail.exit_tick==0) rail.exit_tick=tick;
        else if (rail.exit_tick+60<tick) {
            if (!exit_keeps_body_heading) unit.heading_degrees=unit.turret_degrees;
            unit.flags=static_cast<std::uint16_t>(unit.flags&~flag_rail_unit);
            result.exited=true;
        } else if (rail.exit_tick+5<tick) result.exit_sound=true;
    } else rail.exit_tick=0;
    const float one=1.0F;
    float dt=static_cast<float>(static_cast<double>(frame_delta)*0.15);
    if (one<dt) dt=one;
    if (dt<0.1F) dt=0.1F;
    const auto def=unit_kind_definition(static_cast<std::uint8_t>(unit.kind));
    const float rate=def ? def->movement.accel : 0.0F;
    const float aim=-input.aim*100.0F;
    unit.turret_degrees-=aim*rate*dt*1.98F;
    const float scale=game_mode==7 ? 1.4F : one;
    const float limit=scale*26.5F;
    float accel=((-limit*input.move)/100.0F-rail.velocity)/(4.0F/scale);
    const float cap=scale*8.06F;
    if (cap<accel) accel=cap;
    else if (accel< -cap) accel=-cap;
    rail.velocity+=accel;
    if (limit<rail.velocity) rail.velocity=limit;
    if (rail.velocity< -limit) rail.velocity=-limit;
    rail.travel+=rail.velocity;
    if (rail.travel<0.0F) {
        if (static_cast<double>(rail.velocity)< -15.899999999999999) result.bumped_end=true;
        rail.travel=0.0F;
        rail.velocity=-rail.velocity/0.82F;
    }
    if (rail.travel>rail.length) {
        if (15.899999999999999<static_cast<double>(rail.velocity)) result.bumped_end=true;
        rail.travel=rail.length;
        rail.velocity=-rail.velocity/0.82F;
    }
    unit.x=rail.origin_x+rail.dir_x*rail.travel;
    unit.z=rail.origin_z+rail.dir_z*rail.travel;
    unit.y=0.0F;
    return result;
}

// ---------------------------------------------------------------- turrets

void TurretRecord::serialize(StateWriter& writer) const {
    writer.u8(flags); writer.u8(team); writer.u8(cycle); writer.u8(owner); writer.u8(type);
    writer.u16(heading); writer.f32(x); writer.f32(z); writer.s32(last_fire); writer.s16(health);
    writer.u8(target); writer.u8(fade); writer.u8(cooldown); writer.boolean(has_los);
    writer.s32(aim_time); writer.u16(aim_heading); writer.boolean(removed);
}

void find_turret_target(TurretRecord& turret,const TurretContext& context) noexcept {
    float best=turret_search_initial;
    std::uint8_t nearest=no_turret_target;
    for (std::size_t i=0; i<context.targets.size() && i<no_turret_target; ++i) {
        const TurretTarget& target=context.targets[i];
        if (target.unit==nullptr || target.unit->team==turret.team) continue;
        if (!target.unit->alive || !target.targetable) continue;
        const float dx=target.unit->x-turret.x;
        const float dz=target.unit->z-turret.z;
        const float distance=dx*dx+dz*dz;
        if (distance<best) { best=distance; nearest=static_cast<std::uint8_t>(i); }
    }
    const float range=static_cast<float>(context.search_range*context.search_range)*turret_search_scale;
    turret.target=static_cast<float>(static_cast<std::int32_t>(range))<best ? no_turret_target : nearest;
}

std::optional<TurretRecord> spawn_turret(std::uint8_t team,std::uint8_t owner,std::uint8_t type,
        std::uint16_t heading,float x,float z,bool check_area,bool scaled,const TurretContext& context) {
    if (type>=turret_types) return std::nullopt;
    const float half=turret_scale[type]*turret_half_extent;
    if (check_area && context.area_clear && !context.area_clear(x,z,half)) return std::nullopt;
    TurretRecord turret;
    turret.team=team;
    turret.owner=owner;
    turret.type=type;
    turret.heading=heading;
    turret.x=x;
    turret.z=z;
    turret.health=static_cast<std::int16_t>(turret_health[type]);
    turret.fade=0xFFU;
    find_turret_target(turret,context);
    if (scaled) turret.flags|=turret_scaled;
    turret.aim_time=-1;
    return turret;
}

TurretUpdateResult update_turret(TurretRecord& turret,TurretContext& context) {
    TurretUpdateResult result;
    if (turret.removed || turret.type>=turret_types) return result;
    const std::size_t type=turret.type;
    const float scale=turret_scale[type];
    if ((turret.flags&turret_rebuilding)!=0U) {
        if (context.tick>turret.last_fire &&
            (!context.area_clear || context.area_clear(turret.x,turret.z,scale*turret_half_extent))) {
            turret.last_fire=0;
            turret.flags&=0xF6U;
            turret.health=static_cast<std::int16_t>(turret_health[type]);
            turret.fade=0xFFU;
            find_turret_target(turret,context);
            result.rebuilt=true;
        }
        return result;
    }
    if (turret.fade!=0U) {
        turret.fade=turret.fade>=16U ? static_cast<std::uint8_t>(turret.fade-16U) : std::uint8_t{0U};
        return result;
    }
    if (turret.health<=0) {
        if ((turret.flags&(turret_scaled|turret_rebuild))!=0U) {
            turret.flags=static_cast<std::uint8_t>((turret.flags|turret_rebuilding)&0xEFU);
            const std::size_t i=context.owner_effect_count<turret_owner_count_scale.size() ?
                                context.owner_effect_count : turret_owner_count_scale.size()-1U;
            turret.last_fire=context.tick+static_cast<std::int32_t>(
                context.owner.rebuild_time/100.0F*1800.0F*turret_owner_count_scale[i]);
            result.started_rebuild=true;
            return result;
        }
        turret.removed=true;
        result.removed=true;
        return result;
    }
    if ((context.rng.next_u32()&0xFU)==0U) find_turret_target(turret,context);
    if (turret.cooldown!=0U) --turret.cooldown;
    const auto step=static_cast<std::uint16_t>(static_cast<std::uint32_t>(
        context.frame_delta*static_cast<float>(turret_turn_rate[type])));
    if (turret.target!=no_turret_target) {
        if (turret.target>=context.targets.size()) return result;
        const TurretTarget& target=context.targets[turret.target];
        if (target.unit==nullptr || !target.unit->alive) return result;
        const UnitRecord& unit=*target.unit;
        float dx=unit.x-turret.x;
        float dz=unit.z-turret.z;
        if (dx*dx+dz*dz>turret_max_range_sq) return result;
        const std::uint16_t want=direction_angle(dx,dz);
        turret.heading=turn_toward(turret.heading,want,step);
        const std::uint16_t dist=angle_distance(turret.heading,want);
        std::int32_t delay=context.owner.fire_delay[type];
        if (turret_burst[type]!=0) {
            delay=static_cast<std::int32_t>(static_cast<float>(delay)*turret_burst_cycle[turret.cycle%5U]);
        }
        if (dist>=0x1000U) return result;
        const float fdelay=static_cast<float>(delay);
        const float wait=target.ai_state_two ? fdelay*4.0F : fdelay;
        if (!turret_ready(turret,wait,context)) return result;
        float mx=0.0F,mz=0.0F;
        if (turret.cooldown==0U) {
            turret.cooldown=16U;
            turret_muzzle(turret,context,mx,mz);
            turret.has_los=!context.line_of_sight || context.line_of_sight(mx,mz,turret.target);
        } else {
            if (!turret.has_los) return result;
            turret_muzzle(turret,context,mx,mz);
        }
        if (!turret.has_los) return result;
        result.notified_target=true;
        std::uint16_t aim=turret.heading;
        if (dist<0x200U) {
            dx=unit.x-mx;
            dz=unit.z-mz;
            aim=direction_angle(dx,dz);
        }
        result.shot=make_shot(turret,context,mx,mz,aim);
        turret.last_fire=context.tick;
        if (turret_burst[type]!=0) {
            const std::uint8_t cycle=turret.cycle;
            turret.cycle=static_cast<std::uint8_t>(
                ((context.rng.next_u32()&7U)==0U ? cycle+2U : cycle+1U)%5U);
        }
        return result;
    }
    if (turret.aim_time!=-1 && context.tick-turret.aim_time<0x78) {
        turret.heading=turn_toward(turret.heading,turret.aim_heading,step);
        const std::uint16_t dist=angle_distance(turret.heading,turret.aim_heading);
        std::int32_t delay=context.owner.fire_delay[type];
        if (turret_burst[type]!=0) delay/=4;
        if (dist>=0x200U) return result;
        if (!turret_ready(turret,static_cast<double>(delay),context)) return result;
        float mx=0.0F,mz=0.0F;
        turret_muzzle(turret,context,mx,mz);
        result.shot=make_shot(turret,context,mx,mz,turret.heading);
        if (context.rng.next_u32()%turret_return_fire_end[type]==0U) turret.aim_time=-1;
        turret.last_fire=context.tick;
    }
    return result;
}

TurretDamageResult damage_turret(TurretRecord& turret,std::uint32_t message,std::int32_t damage,
                                 std::uint8_t attacker_owner,std::uint16_t hit_heading,
                                 std::span<OwnerRecord> owners,std::int32_t tick,std::int32_t game_mode) {
    TurretDamageResult result;
    if (message!=2U && message!=12U && message!=22U && message!=34U && message!=44U) return result;
    result.handled=true;
    turret.health=static_cast<std::int16_t>(static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(turret.health)-static_cast<std::uint16_t>(damage)));
    if (turret.health<=0 && (turret.flags&turret_destroyed)==0U) {
        turret.flags|=turret_destroyed;
        result.destroyed=true;
        OwnerRecord* player=owner_in(owners,attacker_owner);
        if (player!=nullptr && player->team!=turret.team && turret.type<turret_types) {
            result.score=turret_destroy_score[turret.type];
            result.lives_awarded=add_owner_progress(*player,result.score,game_mode);
        }
    } else if ((message==2U || message==12U || message==34U) &&
               attacker_owner!=turret.owner && turret.aim_time==-1) {
        turret.aim_time=tick;
        turret.aim_heading=static_cast<std::uint16_t>(hit_heading+0x8000U);
        result.return_fire=true;
    }
    return result;
}

// ---------------------------------------------------------------- placed objects

void PlacedObjectRecord::serialize(StateWriter& writer) const {
    writer.u8(static_cast<std::uint8_t>(object_class)); writer.u32(slot_mode); writer.u8(flags);
    writer.u8(stage_count); writer.u8(stage); writer.s16(health); writer.s32(collapse_ticks);
    writer.boolean(knocked); writer.f32(x); writer.f32(z); writer.boolean(updating);
}

PlacedObjectRecord make_placed_object(PlacedObjectClass object_class,std::uint32_t slot_mode,
                                      std::uint8_t flags,std::uint8_t set_count,float x,float z) {
    PlacedObjectRecord object;
    object.object_class=object_class;
    object.slot_mode=slot_mode;
    object.flags=flags;
    object.x=x;
    object.z=z;
    if (object_class==PlacedObjectClass::staged) {
        std::uint8_t count=set_count;
        if ((flags&8U)!=0U) count=static_cast<std::uint8_t>(count-2U);
        else if ((flags&4U)!=0U) count=static_cast<std::uint8_t>(count-1U);
        object.stage_count=count;
        object.health=(flags&0x20U)!=0U ? std::int16_t{1} : std::int16_t{0x11};
    } else {
        object.stage_count=set_count;
    }
    return object;
}

bool placed_object_destroyed(const PlacedObjectRecord& object) noexcept {
    switch (object.object_class) {
    case PlacedObjectClass::staged: return object.stage_count!=0U && object.stage+1U>=object.stage_count;
    case PlacedObjectClass::knockable: return object.knocked;
    case PlacedObjectClass::crushable: return (object.flags&4U)!=0U;
    }
    return false;
}

std::vector<PlacedObjectEvent> damage_placed_object(PlacedObjectRecord& object,std::uint32_t message,
        std::int32_t damage,std::uint8_t source_owner,bool shielded,std::span<OwnerRecord> owners,
        std::int32_t game_mode) {
    std::vector<PlacedObjectEvent> events;
    const bool damage_message=message==2U || message==12U || message==22U || message==34U;
    if (object.object_class==PlacedObjectClass::knockable) {
        if (message==3U && !object.knocked) {
            object.knocked=true;
            events.push_back({PlacedObjectEventKind::knocked_over,0xFFU,0});
        }
        return events;
    }
    if (object.object_class==PlacedObjectClass::crushable) {
        if (!damage_message && message!=44U) return events;
        const std::int32_t last=(object.flags&1U)!=0U ? object.stage_count-2 : object.stage_count-1;
        if (object.stage==last) return events;
        if ((object.flags&1U)!=0U) {
            if ((object.flags&4U)==0U) object.flags|=2U;
        } else object.flags|=4U;
        events.push_back({PlacedObjectEventKind::flattened,0xFFU,0});
        return events;
    }
    if (!damage_message) return events;
    if (object.slot_mode==0x14U && !shielded) damage=0;
    object.health=static_cast<std::int16_t>(object.health-damage);
    if (object.health<0) object.health=0;
    if ((object.flags&placed_flag_collapsing)!=0U) return events;
    const bool base=object.slot_mode>=11U && object.slot_mode<=14U;
    const std::uint8_t rank=base ? slot_rank[object.slot_mode-11U] : std::uint8_t{0xFFU};
    if (base) events.push_back({PlacedObjectEventKind::base_alert,rank,1});
    if (object.health!=0) return events;
    schedule_placed_update(object);
    object.flags|=placed_flag_collapsing;
    object.health=(object.flags&0x20U)!=0U ? std::int16_t{1} : std::int16_t{0x11};
    events.push_back({PlacedObjectEventKind::stage_lost,0xFFU,object.stage});
    if (base && static_cast<std::int32_t>(object.stage)==object.stage_count-2) {
        OwnerRecord* owner=owner_in(owners,rank);
        OwnerRecord* source=owner_in(owners,source_owner);
        if (owner!=nullptr && source!=nullptr && owner->team!=source->team) {
            add_owner_progress(*source,placed_base_score,game_mode);
            events.push_back({PlacedObjectEventKind::score_awarded,source_owner,placed_base_score});
        }
    }
    return events;
}

void schedule_placed_update(PlacedObjectRecord& object) noexcept {
    if (object.object_class!=PlacedObjectClass::staged || object.updating) return;
    object.updating=true;
    // release_effect does not reset effect +0x10; it is already 0 after any
    // completed collapse and a fresh effect starts at 0, so this is equivalent.
    object.collapse_ticks=0;
}

std::vector<PlacedObjectEvent> update_placed_object(PlacedObjectRecord& object) {
    std::vector<PlacedObjectEvent> events;
    if (object.object_class!=PlacedObjectClass::staged || !object.updating) return events;
    const std::int32_t last=static_cast<std::int32_t>(object.stage_count)-1;
    if ((object.flags&placed_flag_animated)==0U) {
        // asm 0x80092FB0: stage < stages-1 -> stage++; result 1. 0x80 is not cleared.
        if (static_cast<std::int32_t>(object.stage)<last) {
            ++object.stage;
            if (static_cast<std::int32_t>(object.stage)==last) {
                // TEMPORARY POLICY: retail emits no func_800E19B8 here.
                events.push_back({PlacedObjectEventKind::destroyed,0xFFU,object.stage});
            }
        }
        object.updating=false;
        return events;
    }
    if (++object.collapse_ticks<placed_collapse_ticks) return events;
    object.collapse_ticks=0;
    ++object.stage;
    const bool destroyed=static_cast<std::int32_t>(object.stage)==last;
    if (destroyed) {
        events.push_back({PlacedObjectEventKind::destroyed,0xFFU,object.stage});
        if ((object.flags&8U)!=0U) {   // flatten swap: result 2 without the flag clear
            object.updating=false;
            return events;
        }
    }
    if (object.health>0) {
        object.flags&=placed_collapse_clear_mask;
        object.updating=false;
    } else if (destroyed) {
        object.updating=false;         // result 2
    }
    // health <= 0 before the last stage: result 0, the collapse repeats.
    return events;
}

PlacedDestroyResult destroy_placed_object(std::uint8_t flags) noexcept {
    PlacedDestroyResult result;
    if ((flags&1U)!=0U) return result;
    result.notify_area=true;
    if ((flags&0x10U)!=0U) {
        if ((flags&8U)==0U) { result.explosion_variant=22; result.splash=true; }
    } else result.explosion_variant=5;
    return result;
}

} // namespace battletanx
