#include "battletanx/placed_objects.hpp"

#include "battletanx/runtime_math.hpp"

#include <cmath>

namespace battletanx::placed {

namespace {

const TurretOwnerConfig& default_owner_config() {
    static const TurretOwnerConfig config{};
    return config;
}

const TurretOwnerConfig& owner_config(const UpdateContext& context,std::uint8_t owner) {
    return owner<context.owner_configs.size() ? context.owner_configs[owner] : default_owner_config();
}

std::uint8_t effect_count(const UpdateContext& context,std::uint8_t team) {
    return team<context.owner_effect_counts.size() ? context.owner_effect_counts[team] : std::uint8_t{0U};
}

void write_bounds(StateWriter& writer,const Bounds& bounds) {
    writer.f32(bounds.min_x); writer.f32(bounds.max_x); writer.f32(bounds.min_z); writer.f32(bounds.max_z);
}

bool turret_dead(const TurretRecord& turret) noexcept {
    return (turret.flags&turret_destroyed)!=0U || turret.health<=0;
}

const AngleTable& fallback_angles() {
    static const AngleTable table=reference_angle_table();
    return table;
}

// One mapping for every damage_placed_object result, shared by the message
// and blast paths so both report the same event kind.
EventKind event_kind_for(PlacedObjectEventKind kind) noexcept {
    switch (kind) {
    case PlacedObjectEventKind::base_alert: return EventKind::base_alert;
    case PlacedObjectEventKind::stage_lost: return EventKind::stage_lost;
    case PlacedObjectEventKind::destroyed: return EventKind::destroyed;
    case PlacedObjectEventKind::score_awarded: return EventKind::score_awarded;
    case PlacedObjectEventKind::knocked_over: return EventKind::knocked_over;
    case PlacedObjectEventKind::flattened: return EventKind::flattened;
    }
    return EventKind::damaged;
}

} // namespace

std::uint16_t retail_cosine_to_angle(float value,const AngleTable& angles) noexcept {
    // table(i) = D_80139720[i]; next(i) = D_80139724[i] = table(i+1).
    const auto table=[&angles](std::uint32_t index) { return angles.cosine(index<<8U); };
    std::uint32_t index=0U;
    if (value==-1.0F) {
        index=0x80U;
    } else {
        std::uint32_t step=0x40U;
        while (value<table(index+1U) && step!=0U) {
            const std::uint32_t probe=index^step;
            index=probe;
            if (table(probe)<value) index^=step;
            step>>=1U;
        }
    }
    const float numerator=table(index)-value;
    const float denominator=table(index)-table(index+1U);
    const std::uint16_t base=static_cast<std::uint16_t>(index<<8U);
    // TEMPORARY POLICY: retail divides unguarded; a flat table span or a
    // non-finite input yields the span start instead of an undefined cast.
    if (denominator==0.0F || !std::isfinite(numerator)) return base;
    const double fraction=static_cast<double>(numerator)*256.0/static_cast<double>(denominator);
    if (!(fraction>=0.0) || fraction>=65536.0) return base;
    return static_cast<std::uint16_t>((index<<8U)+static_cast<std::uint32_t>(fraction));
}

std::optional<std::uint16_t> fling_heading(float object_x,float object_z,float source_x,float source_z,
                                           const AngleTable& angles) noexcept {
    // CONFIRMED (grid_effect_handle_message.c message 3/7, slot_heading_effect):
    // cosine argument is the X offset, sign comes from the Z offset.
    const float ox=object_x-source_x;   // cosine axis
    const float oz=object_z-source_z;   // sign axis
    if (ox==0.0F && oz==0.0F) return std::nullopt;
    const float length=std::sqrt(oz*oz+ox*ox);
    if (!std::isfinite(length) || length==0.0F) return std::nullopt;
    const std::uint16_t angle=retail_cosine_to_angle(ox/length,angles);
    return oz<0.0F ? static_cast<std::uint16_t>(0U-angle) : angle;
}

std::optional<std::uint8_t> model_part_count(const WorldLevel& level,std::int16_t shape) noexcept {
    if (shape<0 || static_cast<std::size_t>(shape)>=level.shapes.size()) return std::nullopt;
    const WorldShape& set=level.shapes[static_cast<std::size_t>(shape)];
    if (set.lod_count==0U || set.first_range>=level.model_ranges.size()) return std::nullopt;
    return level.model_ranges[set.first_range].part_count;
}

void CrushableState::serialize(StateWriter& writer) const {
    writer.u16(shake); writer.s32(last_contact); writer.u16(heading); writer.boolean(released);
}

void Event::serialize(StateWriter& writer) const {
    writer.u8(static_cast<std::uint8_t>(kind)); writer.id(entity); writer.u8(owner); writer.s32(value);
    writer.f32(x); writer.f32(z); writer.boolean(shot.has_value());
    if (!shot) return;
    writer.f32(shot->x); writer.f32(shot->z); writer.u16(shot->heading);
    writer.f32(shot->velocity_x); writer.f32(shot->velocity_z); writer.s16(shot->speed);
    writer.s32(shot->projectile); writer.s16(shot->effect_kind); writer.u16(shot->sound); writer.u8(shot->target);
}

void BreakableRecord::serialize(StateWriter& writer) const {
    writer.u8(flags); writer.u8(debris); writer.u16(heading); writer.s32(x); writer.s32(z);
    writer.boolean(released);
}

void Entry::serialize(StateWriter& writer) const {
    writer.u8(static_cast<std::uint8_t>(kind)); writer.boolean(active); writer.u8(serial);
    write_bounds(writer,bounds);
    switch (kind) {
    case Kind::turret: turret.serialize(writer); break;
    case Kind::breakable: breakable.serialize(writer); break;
    case Kind::staged:
    case Kind::knockable: object.serialize(writer); break;
    case Kind::crushable: object.serialize(writer); crushable.serialize(writer); break;
    }
}

Bounds turret_bounds(std::uint8_t type,float x,float z) noexcept {
    const float half=type<turret_types ? turret_scale[type]*turret_half_extent : turret_half_extent;
    return Bounds{.min_x=x-half,.max_x=x+half,.min_z=z-half,.max_z=z+half};
}

Bounds rotated_bounds(float x,float z,float half_x,float half_z,std::uint16_t heading,
                      const AngleTable& angles) noexcept {
    const float s=std::fabs(angles.sine(heading));
    const float c=std::fabs(angles.cosine(heading));
    const float ex=half_x*c+half_z*s;
    const float ez=half_x*s+half_z*c;
    return Bounds{.min_x=x-ex,.max_x=x+ex,.min_z=z-ez,.max_z=z+ez};
}

bool bounds_contain(const Bounds& bounds,float x,float z) noexcept {
    return x>=bounds.min_x && x<=bounds.max_x && z>=bounds.min_z && z<=bounds.max_z;
}

void World::push_event(const Event& event) {
    if (events_.size()>=max_pending_events) { events_overflowed_=true; return; }
    events_.push_back(event);
}

void World::emit(EventKind kind,EntityId id,std::uint8_t owner,std::int32_t value,float x,float z) {
    push_event(Event{.kind=kind,.entity=id,.owner=owner,.value=value,.x=x,.z=z,.shot=std::nullopt});
}

std::optional<EntityId> World::spawn_turret(const TurretSpawn& spawn,const UpdateContext& context) {
    if (context.angles==nullptr || context.rng==nullptr || spawn.type>=turret_types || pool_.full()) {
        return std::nullopt;
    }
    TurretContext turret_context{.angles=*context.angles,.targets=context.targets,
        .owner=owner_config(context,spawn.owner),.owner_effect_count=effect_count(context,spawn.team),
        .tick=context.tick,.frame_delta=context.frame_delta,.search_range=context.search_range,
        .rng=*context.rng,.area_clear=context.area_clear,.line_of_sight=context.line_of_sight};
    // Retail runs the area query twice; one native query is equivalent.
    std::optional<TurretRecord> turret=battletanx::spawn_turret(spawn.team,spawn.owner,spawn.type,spawn.heading,
                                                                spawn.x,spawn.z,spawn.check_area,spawn.scaled,
                                                                turret_context);
    if (!turret) return std::nullopt;
    const std::optional<EntityId> id=pool_.allocate();
    if (!id) return std::nullopt;
    Entry& entry=*pool_.get(*id);
    entry.kind=Kind::turret;
    entry.serial=serial_;
    serial_=static_cast<std::uint8_t>((serial_+1U)%turret_serial_modulo);
    entry.turret=*turret;
    entry.bounds=turret_bounds(spawn.type,spawn.x,spawn.z);
    emit(EventKind::spawned,*id,spawn.owner,spawn.type,spawn.x,spawn.z);
    return id;
}

std::optional<EntityId> World::spawn_placed(const PlacedObjectRecord& record,const Bounds& bounds) {
    const std::optional<EntityId> id=pool_.allocate();
    if (!id) return std::nullopt;
    Entry& entry=*pool_.get(*id);
    switch (record.object_class) {
    case PlacedObjectClass::staged: entry.kind=Kind::staged; break;
    case PlacedObjectClass::knockable: entry.kind=Kind::knockable; break;
    case PlacedObjectClass::crushable: entry.kind=Kind::crushable; break;
    }
    entry.object=record;
    entry.crushable=CrushableState{};
    entry.bounds=bounds;
    emit(EventKind::spawned,*id,0xFFU,static_cast<std::int32_t>(record.slot_mode),record.x,record.z);
    return id;
}

std::optional<EntityId> World::spawn_breakable(const BreakableRecord& record,const Bounds& bounds) {
    const std::optional<EntityId> id=pool_.allocate();
    if (!id) return std::nullopt;
    Entry& entry=*pool_.get(*id);
    entry.kind=Kind::breakable;
    entry.breakable=record;
    entry.breakable.released=false;
    entry.bounds=bounds;
    emit(EventKind::spawned,*id,0xFFU,0,static_cast<float>(record.x),static_cast<float>(record.z));
    return id;
}

bool World::remove(EntityId id) noexcept { return pool_.release(id); }

bool World::set_active(EntityId id,bool active) noexcept {
    Entry* entry=pool_.get(id);
    if (entry==nullptr) return false;
    entry->active=active;
    return true;
}

MessageResult World::hit(EntityId id,const Hit& contact,std::span<OwnerRecord> owners,std::int32_t tick,
                         std::int32_t game_mode) {
    Entry* entry=pool_.get(id);
    if (entry==nullptr) return MessageResult::stale;
    if (!entry->active) return MessageResult::inactive;
    switch (entry->kind) {
    case Kind::turret: {
        TurretRecord& turret=entry->turret;
        const TurretDamageResult result=damage_turret(turret,contact.source_kind,contact.damage,contact.source_owner,
                                                      contact.heading,owners,tick,game_mode);
        if (!result.handled) return MessageResult::ignored;
        emit(EventKind::damaged,id,contact.source_owner,contact.damage,turret.x,turret.z);
        if (result.destroyed) {
            emit(EventKind::destroyed,id,contact.source_owner,turret.type,turret.x,turret.z);
            emit(EventKind::explosion,id,0xFFU,turret_explosion_variant,turret.x,turret.z);
            emit(EventKind::debris,id,turret.owner,turret_debris_effect,turret.x,turret.z);
            if (result.score!=0) emit(EventKind::score_awarded,id,contact.source_owner,result.score,turret.x,turret.z);
        }
        if (result.return_fire) emit(EventKind::return_fire,id,contact.source_owner,turret.aim_heading,turret.x,turret.z);
        return MessageResult::handled;
    }
    case Kind::staged:
    case Kind::knockable:
    case Kind::crushable: {
        if (entry->kind==Kind::crushable && contact.source_kind==0U) return crushable_contact(id,*entry,contact,tick);
        PlacedObjectRecord& object=entry->object;
        const std::vector<PlacedObjectEvent> placed=damage_placed_object(object,contact.source_kind,contact.damage,
                contact.source_owner,contact.shielded,owners,game_mode);
        for (const PlacedObjectEvent& e : placed) {
            emit(event_kind_for(e.kind),id,e.owner,e.value,object.x,object.z);
        }
        // pulse_effect_handle_message releases kind 9 after setting flag 2 or
        // 4; flag 2 makes the next update spawn debris and free the effect.
        if (entry->kind==Kind::crushable && !placed.empty()) entry->crushable.released=true;
        return placed.empty() ? MessageResult::ignored : MessageResult::handled;
    }
    case Kind::breakable: {
        BreakableRecord& b=entry->breakable;
        if (b.released) return MessageResult::ignored;
        const float x=static_cast<float>(b.x),z=static_cast<float>(b.z);
        switch (contact.source_kind) {
        case 34U:
            if ((b.flags&breakable_crush_guard)!=0U && !contact.flame_forced) return MessageResult::ignored;
            [[fallthrough]];
        case 2U:
        case 12U:
        case 22U:
            // CONFIRMED (grid_effect_handle_message.c:52-56): *result = 0.
            b.released=true;
            emit(EventKind::damaged,id,contact.source_owner,contact.damage,x,z);
            emit(EventKind::contact_reply,id,contact.source_owner,static_cast<std::int32_t>(ContactReply::none),x,z);
            return MessageResult::handled;
        case 5U:
        case 18U:
            b.released=true;
            emit(EventKind::damaged,id,contact.source_owner,contact.damage,x,z);
            return MessageResult::handled;
        case 0U:
            // Crushed by a unit: sound ramp 0x23 on the unit, parts flung along its heading.
            b.debris=1U;
            b.heading=contact.heading;
            b.released=true;
            emit(EventKind::crushed,id,contact.source_owner,contact.heading,x,z);
            emit(EventKind::sound,id,contact.source_owner,breakable_crush_sound,x,z);
            // CONFIRMED (grid_effect_handle_message.c:61-66): *result = 4.
            emit(EventKind::contact_reply,id,contact.source_owner,static_cast<std::int32_t>(ContactReply::scrape),x,z);
            return MessageResult::handled;
        default:
            return MessageResult::ignored;
        }
    }
    }
    return MessageResult::unsupported;
}

MessageResult World::drop_stage(EntityId id,Entry& entry,bool blasted) {
    // frame_effect_handle_message messages 3/7: only before the last stage;
    // release_effect + remove_effects_by_owner (+ flag 0x10 for message 3).
    // The released effect's update_effect_kind_5 then advances one stage
    // without the collapse animation, or continues a running collapse
    // (release_effect does not find an already active effect).
    PlacedObjectRecord& object=entry.object;
    if (static_cast<std::int32_t>(object.stage)>=static_cast<std::int32_t>(object.stage_count)-1) {
        return MessageResult::ignored;
    }
    emit(EventKind::owned_effects_removed,id,0xFFU,0,object.x,object.z);
    // CONFIRMED (frame_effect_handle_message.c:52-63): release_effect only;
    // no 0x80 set or 0x80 gate. update_placed_object advances the stage
    // (flag 1: 60-tick collapse then &0x6F; flag 1 clear: next update).
    schedule_placed_update(object);
    // 0x10 is set even if the object is already updating (schedule is a no-op).
    if (blasted) object.flags|=staged_blasted;
    return MessageResult::handled;
}

MessageResult World::fling_crushable(EntityId id,Entry& entry,float x,float z,const AngleTable& angles) {
    // slot_heading_effect_handle_message messages 3/7 (identical paths).
    PlacedObjectRecord& object=entry.object;
    CrushableState& c=entry.crushable;
    if ((object.flags&crushable_flattened)!=0U) return MessageResult::ignored;
    object.flags|=crushable_blast_flags;
    if (const std::optional<std::uint16_t> heading=fling_heading(object.x,object.z,x,z,angles)) {
        object.flags|=crushable_oriented;
        c.heading=*heading;
    }
    c.released=true;
    emit(EventKind::damaged,id,0xFFU,0,object.x,object.z);
    return MessageResult::handled;
}

MessageResult World::crushable_contact(EntityId id,Entry& entry,const Hit& contact,std::int32_t tick) {
    // pulse_effect_handle_message (func_80093590) source kind 0.
    PlacedObjectRecord& object=entry.object;
    CrushableState& c=entry.crushable;
    if (c.shake!=0U) {
        const std::int32_t jitter=(c.shake&1U)!=0U ? -2 : 2;
        emit(EventKind::unit_shaken,id,contact.source_owner,jitter,object.x,object.z);
        --c.shake;
        if (c.shake==0U) emit(EventKind::collision_cleared,id,0xFFU,crushable_motion_flag,object.x,object.z);
        emit(EventKind::contact_reply,id,contact.source_owner,static_cast<std::int32_t>(ContactReply::none),
             object.x,object.z);
        return MessageResult::handled;
    }
    ContactReply reply=ContactReply::pass;
    if (object.stage==0U) {
        c.released=true;
        if ((object.flags&crushable_flattened)==0U) {
            object.flags|=crushable_flattened;
            emit(EventKind::flattened,id,contact.source_owner,0,object.x,object.z);
        }
        if (c.last_contact+crushable_contact_interval<tick) { reply=ContactReply::crush; c.last_contact=tick; }
    } else if (c.last_contact+crushable_contact_interval<tick) {
        reply=ContactReply::scrape;
        c.last_contact=tick;
    }
    emit(EventKind::contact_reply,id,contact.source_owner,static_cast<std::int32_t>(reply),object.x,object.z);
    return MessageResult::handled;
}

MessageResult World::blast(EntityId id,float x,float z,std::span<OwnerRecord> owners,std::int32_t game_mode,
                           const AngleTable& angles) {
    Entry* entry=pool_.get(id);
    if (entry==nullptr) return MessageResult::stale;
    if (!entry->active) return MessageResult::inactive;
    switch (entry->kind) {
    case Kind::turret: {
        if constexpr (!turret_blast_policy) return MessageResult::unsupported;
        TurretRecord& turret=entry->turret;
        const float dx=x-turret.x;
        const float dz=z-turret.z;
        const float distance=std::sqrt(dx*dx+dz*dz);
        std::int32_t damage=turret_blast_base;
        // TEMPORARY POLICY: a non-finite distance takes the far-field damage.
        if (std::isfinite(distance) && !(turret_blast_radius<distance)) {
            damage=static_cast<std::int32_t>((1.0F-distance/turret_blast_radius)*turret_blast_scale)+turret_blast_base;
        }
        turret.health=static_cast<std::int16_t>(static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(turret.health)-static_cast<std::uint16_t>(damage)));
        emit(EventKind::damaged,id,0xFFU,damage,turret.x,turret.z);
        if (turret.health>0) return MessageResult::handled;
        turret.health=0;
        // TEMPORARY POLICY (turret_blast_policy): no score and no flag 1;
        // update_turret then rebuilds or removes the turret. func_800A2A44 is
        // the crate debris emitter, not a turret handler (UNRESOLVED).
        emit(EventKind::debris,id,turret.owner,turret_debris_effect,turret.x,turret.z);
        return MessageResult::handled;
    }
    case Kind::knockable: {
        const std::vector<PlacedObjectEvent> placed=damage_placed_object(entry->object,3U,0,0xFFU,false,owners,game_mode);
        for (const PlacedObjectEvent& e : placed) {
            emit(event_kind_for(e.kind),id,e.owner,e.value,entry->object.x,entry->object.z);
        }
        return placed.empty() ? MessageResult::ignored : MessageResult::handled;
    }
    case Kind::breakable: {
        BreakableRecord& b=entry->breakable;
        if (b.released) return MessageResult::ignored;
        const float bx=static_cast<float>(b.x),bz=static_cast<float>(b.z);
        if (const std::optional<std::uint16_t> heading=fling_heading(bx,bz,x,z,angles)) {
            b.debris=1U;
            b.heading=*heading;
        }
        b.flags|=breakable_blasted;
        b.released=true;
        emit(EventKind::damaged,id,0xFFU,0,bx,bz);
        return MessageResult::handled;
    }
    case Kind::staged:
        return drop_stage(id,*entry,true);
    case Kind::crushable:
        return fling_crushable(id,*entry,x,z,angles);
    }
    return MessageResult::unsupported;
}

MessageResult World::direct_damage(EntityId id,std::int32_t damage,float x,float z,const AngleTable& angles) {
    Entry* entry=pool_.get(id);
    if (entry==nullptr) return MessageResult::stale;
    if (!entry->active) return MessageResult::inactive;
    if (entry->kind==Kind::turret) {
        if constexpr (!turret_blast_policy) return MessageResult::unsupported;   // TEMPORARY POLICY
        TurretRecord& turret=entry->turret;
        turret.health=static_cast<std::int16_t>(static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(turret.health)-static_cast<std::uint16_t>(damage)));
        emit(EventKind::damaged,id,0xFFU,damage,turret.x,turret.z);
        if (turret.health>0) return MessageResult::handled;
        turret.health=0;
        emit(EventKind::debris,id,turret.owner,turret_debris_effect,turret.x,turret.z);
        return MessageResult::handled;
    }
    if (entry->kind==Kind::breakable) {
        BreakableRecord& b=entry->breakable;
        if (b.released) return MessageResult::ignored;
        const float bx=static_cast<float>(b.x),bz=static_cast<float>(b.z);
        if (const std::optional<std::uint16_t> heading=fling_heading(bx,bz,x,z,angles)) {
            b.debris=1U;
            b.heading=*heading;
        }
        b.released=true;
        emit(EventKind::damaged,id,0xFFU,damage,bx,bz);
        return MessageResult::handled;
    }
    if (entry->kind==Kind::staged) return drop_stage(id,*entry,false);
    if (entry->kind==Kind::crushable) return fling_crushable(id,*entry,x,z,angles);
    // Kind 6 message 7 (settle_slot_effect) is not modelled here.
    return MessageResult::unsupported;
}

MessageResult World::blast(EntityId id,float x,float z,std::span<OwnerRecord> owners,std::int32_t game_mode) {
    return blast(id,x,z,owners,game_mode,fallback_angles());
}

MessageResult World::direct_damage(EntityId id,std::int32_t damage,float x,float z) {
    return direct_damage(id,damage,x,z,fallback_angles());
}

bool World::answers_query(EntityId id,std::uint32_t message) const noexcept {
    const Entry* entry=pool_.get(id);
    return entry!=nullptr && entry->active && entry->kind==Kind::turret && (message==6U || message==37U);
}

void World::finish_breakable(EntityId id,Entry& entry) {
    BreakableRecord& b=entry.breakable;
    const float x=static_cast<float>(b.x),z=static_cast<float>(b.z);
    const PlacedDestroyResult result=destroy_placed_object(b.flags);
    if (result.explosion_variant>=0) emit(EventKind::explosion,id,0xFFU,result.explosion_variant,x,z);
    if (result.splash) emit(EventKind::splash,id,0xFFU,0,x,z);
    if (result.notify_area) emit(EventKind::notify_area,id,0xFFU,0,x,z);
    if ((b.flags&(breakable_crush_guard|breakable_blasted))==breakable_crush_guard) {
        emit(EventKind::explosion,id,0xFFU,breakable_rubble_variant,x,z);
        emit(EventKind::sound,id,0xFFU,breakable_rubble_sound,x,z);
    }
    b.flags&=static_cast<std::uint8_t>(~breakable_blasted);
    emit(EventKind::debris,id,0xFFU,b.debris,x,z);
    emit(EventKind::destroyed,id,0xFFU,0,x,z);
}

void World::update(const UpdateContext& context) {
    std::vector<EntityId> released;
    pool_.for_each([this,&context,&released](EntityId id,Entry& entry) {
        if (!entry.active) return;
        switch (entry.kind) {
        case Kind::turret: {
            if (context.angles==nullptr || context.rng==nullptr) return;
            TurretRecord& turret=entry.turret;
            TurretContext turret_context{.angles=*context.angles,.targets=context.targets,
                .owner=owner_config(context,turret.owner),.owner_effect_count=effect_count(context,turret.team),
                .tick=context.tick,.frame_delta=context.frame_delta,.search_range=context.search_range,
                .rng=*context.rng,.area_clear=context.area_clear,.line_of_sight=context.line_of_sight};
            const TurretUpdateResult result=update_turret(turret,turret_context);
            if (result.shot) {
                push_event(Event{.kind=EventKind::shot_fired,.entity=id,.owner=turret.owner,
                    .value=result.shot->projectile,.x=result.shot->x,.z=result.shot->z,.shot=result.shot});
                emit(EventKind::sound,id,turret.owner,result.shot->sound,result.shot->x,result.shot->z);
            }
            if (result.started_rebuild) emit(EventKind::rebuild_started,id,turret.owner,turret.last_fire,turret.x,turret.z);
            if (result.rebuilt) emit(EventKind::rebuilt,id,turret.owner,turret.type,turret.x,turret.z);
            if (result.removed) {
                emit(EventKind::removed,id,turret.owner,turret.type,turret.x,turret.z);
                released.push_back(id);
            }
            return;
        }
        case Kind::staged: {
            const std::vector<PlacedObjectEvent> placed=update_placed_object(entry.object);
            for (const PlacedObjectEvent& e : placed) {
                emit(EventKind::destroyed,id,e.owner,e.value,entry.object.x,entry.object.z);
            }
            return;
        }
        case Kind::knockable:
            return;
        case Kind::crushable: {
            // update_morphing_effect (func_8009201C): flag 2 spawns oriented
            // debris (flag 8, value = +0x1E heading) or the plain effect
            // (value -1), then frees the effect. Otherwise the released
            // effect plays the flatten morph (presentation only).
            CrushableState& c=entry.crushable;
            if (!c.released) return;
            c.released=false;
            if ((entry.object.flags&crushable_debris)==0U) return;
            const bool oriented=(entry.object.flags&crushable_oriented)!=0U;
            emit(EventKind::debris,id,0xFFU,oriented ? static_cast<std::int32_t>(c.heading) : -1,
                 entry.object.x,entry.object.z);
            emit(EventKind::removed,id,0xFFU,0,entry.object.x,entry.object.z);
            released.push_back(id);
            return;
        }
        case Kind::breakable:
            if (!entry.breakable.released) return;
            finish_breakable(id,entry);
            released.push_back(id);
            return;
        }
    });
    for (EntityId id : released) pool_.release(id);
}

std::vector<Snapshot> World::snapshots() const {
    std::vector<Snapshot> out;
    pool_.for_each([&](EntityId id,const Entry& entry) {
        Snapshot s{};
        s.id=id;
        s.kind=entry.kind;
        s.active=entry.active;
        s.bounds=entry.bounds;
        switch (entry.kind) {
        case Kind::turret:
            s.destroyed=turret_dead(entry.turret);
            s.subtype=entry.turret.type;
            s.team=entry.turret.team;
            s.fade=entry.turret.fade;
            s.health=entry.turret.health;
            s.heading=entry.turret.heading;
            s.x=entry.turret.x;
            s.z=entry.turret.z;
            s.scale=entry.turret.type<turret_types ? turret_scale[entry.turret.type] : 1.0F;
            break;
        case Kind::staged:
        case Kind::knockable:
        case Kind::crushable:
            s.destroyed=placed_object_destroyed(entry.object);
            s.subtype=static_cast<std::uint8_t>(entry.object.slot_mode&0xFFU);
            s.stage=entry.object.stage;
            s.stage_count=entry.object.stage_count;
            s.health=entry.object.health;
            s.x=entry.object.x;
            s.z=entry.object.z;
            break;
        case Kind::breakable:
            s.destroyed=entry.breakable.released;
            s.heading=entry.breakable.heading;
            s.x=static_cast<float>(entry.breakable.x);
            s.z=static_cast<float>(entry.breakable.z);
            break;
        }
        out.push_back(s);
    });
    return out;
}

std::vector<EntityId> World::at_point(float x,float z) const {
    std::vector<EntityId> out;
    pool_.for_each([&](EntityId id,const Entry& entry) {
        if (bounds_contain(entry.bounds,x,z)) out.push_back(id);
    });
    return out;
}

static_assert(world_capacity<=0xFFFFU);   // pool count is serialized as u16
static_assert(max_pending_events<=0xFFFFU);   // event count is serialized as u16 (push guard at World event queue)

void World::serialize(StateWriter& writer) const {
    writer.u8(serial_);
    writer.u16(static_cast<std::uint16_t>(events_.size()));
    writer.boolean(events_overflowed_);
    for (const Event& event : events_) event.serialize(writer);
    writer.u16(static_cast<std::uint16_t>(pool_.size()));
    // Every slot, free ones included, so release history (generations that
    // mint later EntityIds) is part of the hash.
    for (std::size_t slot=0U; slot<Pool::capacity; ++slot) {
        const bool occupied=pool_.slot_occupied(slot);
        writer.boolean(occupied);
        writer.u16(pool_.slot_generation(slot));
        if (!occupied) continue;
        const Entry* entry=pool_.get(pool_.slot_id(slot));
        if (entry!=nullptr) entry->serialize(writer);
    }
}

} // namespace battletanx::placed
