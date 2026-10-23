#include "battletanx/weapon_runtime.hpp"

#include "battletanx/runtime_math.hpp"

#include <cmath>
#include <vector>

namespace battletanx {
namespace {

// update_bullet_effect: D_80125CD0 (ROM 0xB5CD0, s32[4]) gun impact sounds.
constexpr std::array<std::uint16_t,4> gun_impact_sounds{27U,28U,29U,30U};
constexpr std::uint8_t explosion_cannon=0x0BU;
constexpr std::uint8_t explosion_shell=0x09U;
constexpr std::uint8_t explosion_gun=0x17U;
constexpr std::uint8_t blast_arm_variant=0x0DU;
constexpr std::uint16_t sound_blast=5U;
constexpr float impact_backoff=35.0F;
constexpr std::int16_t missile_linger_ticks=15;
constexpr std::int32_t shield_ticks=900;
constexpr std::int32_t multiplayer_special_delay=20;
constexpr std::uint16_t flag_powered=0x2000U;
constexpr std::uint16_t flag_fast_cannon=0x0004U;
constexpr float blast_trigger_half_extent=10.0F;   // func_80106D18 box -10..10

bool human(const UnitRecord& unit) noexcept { return unit.control!=UnitControl::computer; }

void push_sound(SoundEventQueue& sounds,std::uint16_t id,float x,float y,float z,EntityId source,
                bool positional,bool local) {
    SoundEvent sound;
    sound.sound_id=id;
    sound.x=x;
    sound.y=y;
    sound.z=z;
    sound.source=source;
    sound.positional=positional;
    sound.local=local;
    sounds.push(sound);
}

void push_effect(WeaponContext& context,EffectEventKind kind,EntityId source,EntityId other,
                 std::uint8_t variant,std::uint8_t owner,float x,float y,float z,
                 RemovalReason reason=RemovalReason::none) {
    EffectEvent event;
    event.kind=kind;
    event.tick=context.base.combat.tick;
    event.source=source;
    event.other=other;
    event.variant=variant;
    event.owner=owner;
    event.reason=reason;
    event.gameplay_index=static_cast<std::uint32_t>(context.base.combat.events.events().size());
    event.x=x;
    event.y=y;
    event.z=z;
    context.effects.push(event);
}

void remove(WeaponContext& context,EntityId id,const WeaponProjectile& p,RemovalReason reason) {
    push_effect(context,EffectEventKind::projectile_removed,id,no_entity,p.effect_kind,p.attacker,
                p.x,p.y,p.z,reason);
    context.state.projectiles.release(id);
}

std::optional<SegmentHit> world_segment(const WorldCollision& world,float x0,float z0,float x1,float z1) {
    if (!world.segment) return std::nullopt;
    return world.segment(x0,z0,x1,z1);
}

float probe_range(WeaponContext& context,float x,float z,std::uint16_t heading,EntityId) {
    const AngleTable& angles=context.base.angles;
    const float end_x=x+angles.sine(heading)*retail::shell_range_probe;
    const float end_z=z+angles.cosine(heading)*retail::shell_range_probe;
    if (const auto hit=world_segment(context.base.world,x,z,end_x,end_z)) return hit->distance;
    return retail::shell_unbounded_range;
}

struct Muzzle {
    float x{}, y{}, z{};
    float forward_x{}, forward_z{};
    std::uint16_t heading{};
};

Muzzle muzzle_point(const AngleTable& angles,const UnitRecord& unit,float frame_degrees,
                    const MuzzleOffset& offset) {
    const std::uint16_t angle=degrees_to_retail_angle(frame_degrees);
    const float s=angles.sine(angle);
    const float c=angles.cosine(angle);
    Muzzle m;
    m.x=unit.x+offset.x*c+offset.z*s;
    m.z=unit.z-offset.x*s+offset.z*c;
    m.y=unit.y+offset.y;
    m.forward_x=s;
    m.forward_z=c;
    m.heading=direction_angle(s,c);
    return m;
}

void mark_fired(WeaponContext& context,EntityId unit_id,UnitRecord& unit,const WeaponDefinition& def,
                EntityId projectile,float x,float y,float z) {
    CombatContext& combat=context.base.combat;
    const bool local=human(unit);
    if (unit.control!=UnitControl::computer) context.state.flash_timer[unit_id.slot]=0xF0;
    GameplayEvent fired;
    fired.kind=GameplayEventKind::weapon_fired;
    fired.tick=combat.tick;
    fired.subject=unit_id;
    fired.other=projectile;
    fired.owner=unit.owner;
    fired.value=def.id;
    fired.x=x;
    fired.y=y;
    fired.z=z;
    combat.events.push(fired);
    if (def.fire_sound!=0U) {
        // Weapons 1/3 use the listener path for local units; 6 is local only.
        const bool positional=!((def.id==1U || def.id==3U || def.id==6U) && local);
        push_sound(combat.sounds,def.fire_sound,unit.x,unit.y,unit.z,unit_id,positional,local);
    }
}

std::optional<EntityId> spawn_bullet(WeaponContext& context,const Muzzle& m,const WeaponDefinition& def,
                                     std::uint8_t damage,std::uint8_t bullet_kind,std::uint8_t trail,
                                     const UnitRecord& unit,EntityId unit_id) {
    const auto id=context.state.projectiles.allocate();
    if (!id) return std::nullopt;
    WeaponProjectile& p=*context.state.projectiles.get(*id);
    p.effect_kind=34U;
    p.weapon=def.id;
    p.bullet_kind=bullet_kind;
    p.trail=trail;
    p.x=m.x;
    p.y=m.y;
    p.z=m.z;
    p.speed=def.speed;
    p.velocity_x=m.forward_x*def.speed;
    p.velocity_z=m.forward_z*def.speed;
    p.heading=m.heading;
    p.life=def.life;
    p.damage=damage;
    p.attacker=unit.owner;
    p.local=human(unit);
    p.ignore=unit_id;
    p.travelled=0.0F;
    p.range=probe_range(context,p.x,p.z,p.heading,unit_id);
    return id;
}

std::optional<EntityId> spawn_shell(WeaponContext& context,const Muzzle& m,const WeaponDefinition& def,
                                    float vx,float vz,float ax,float az,const UnitRecord& unit,
                                    EntityId unit_id) {
    const auto id=context.state.projectiles.allocate();
    if (!id) return std::nullopt;
    WeaponProjectile& p=*context.state.projectiles.get(*id);
    p.effect_kind=2U;
    p.weapon=def.id;
    p.bullet_kind=0U;
    p.trail=def.trail;
    p.x=m.x;
    p.y=m.y;
    p.z=m.z;
    p.velocity_x=vx;
    p.velocity_z=vz;
    p.accel_x=ax;
    p.accel_z=az;
    p.speed=def.speed;
    p.heading=m.heading;
    p.life=def.life;
    p.damage=def.damage;
    p.attacker=unit.owner;
    p.local=human(unit);
    p.ignore=unit_id;
    return id;
}

void consume_ammo(WeaponContext& context,UnitRecord& unit,WeaponState& weapon) {
    const CombatConfig& config=context.base.combat.config;
    if (weapon.kind>=unit.ammo.size()) return;
    if (context.multiplayer_rules) {
        if (weapon.kind>=first_special_weapon) --unit.ammo[weapon.kind];
    } else if (human(unit)) {
        if (weapon.kind>=first_special_weapon ||
            (!config.free_fire && !config.unlimited_ammo && (unit.flags&retail::flag_free_fire)==0U)) {
            --unit.ammo[weapon.kind];
        }
    }
    if (unit.ammo[weapon.kind]==0 && weapon.kind>=first_special_weapon) {
        unit.flags=static_cast<std::uint16_t>(unit.flags&~flag_powered);
        const std::uint8_t kind=weapon.kind;
        std::uint8_t next=static_cast<std::uint8_t>(kind+1U);
        if (next==weapon_id_count) next=first_special_weapon;
        while (unit.ammo[next]==0) {
            if (next==kind) { weapon.kind=0U; return; }
            ++next;
            if (next==weapon_id_count) next=first_special_weapon;
        }
        weapon.kind=next==kind ? std::uint8_t{0U} : next;
    }
}

struct HitOutcome {
    bool hit{};
    bool unit{};
    EntityId target{};
    float x{}, z{};
};

HitOutcome find_hit(WeaponContext& context,const WeaponProjectile& p,float end_x,float end_z,
                    bool units,bool world) {
    HitOutcome out;
    std::optional<UnitSegmentHit> unit_hit;
    std::optional<SegmentHit> world_hit;
    if (units) {
        unit_hit=find_unit_segment_hit(context.base.combat.units,context.base.angles,p.x,p.z,end_x,end_z,p.ignore);
    }
    if (world) world_hit=world_segment(context.base.world,p.x,p.z,end_x,end_z);
    if (unit_hit && (!world_hit || unit_hit->distance<=world_hit->distance)) {
        const float length=std::sqrt((end_x-p.x)*(end_x-p.x)+(end_z-p.z)*(end_z-p.z));
        const float t=length>0.0F ? unit_hit->distance/length : 0.0F;
        out.hit=true;
        out.unit=true;
        out.target=unit_hit->unit;
        out.x=p.x+(end_x-p.x)*t;
        out.z=p.z+(end_z-p.z)*t;
    } else if (world_hit) {
        out.hit=true;
        out.x=world_hit->x;
        out.z=world_hit->z;
    }
    return out;
}

ObjectResponse resolve_unit_hit(WeaponContext& context,EntityId projectile,const WeaponProjectile& p,
                                const HitOutcome& hit) {
    CombatContext& combat=context.base.combat;
    GameplayEvent impact;
    impact.kind=GameplayEventKind::projectile_impact;
    impact.tick=combat.tick;
    impact.subject=projectile;
    impact.other=hit.target;
    impact.owner=p.attacker;
    impact.value=p.weapon;
    impact.x=hit.x;
    impact.y=p.y;
    impact.z=hit.z;
    combat.events.push(impact);
    if (context.object_response) {
        ProjectileContact contact{projectile,hit.x,p.y,hit.z,p.heading,p.damage,p.attacker,p.local};
        return context.object_response(hit.target,contact);
    }
    return ObjectResponse{handle_projectile_hit(combat,hit.target,p.damage,p.attacker,p.local,projectile),0U};
}

void impact_feedback(WeaponContext& context,EntityId id,const WeaponProjectile& p,const HitOutcome& hit) {
    CombatContext& combat=context.base.combat;
    std::uint16_t sound=retail::sound_hit_other;
    if (p.bullet_kind==1U && p.effect_kind!=22U) {
        sound=gun_impact_sounds[context.random.next_u32()&3U];
    } else if (hit.unit) {
        sound=retail::sound_hit_unit;
    }
    push_sound(combat.sounds,sound,hit.x,p.y,hit.z,id,true,false);
    float x=hit.x;
    float z=hit.z;
    const float speed=std::sqrt(p.velocity_x*p.velocity_x+p.velocity_z*p.velocity_z);
    std::uint8_t variant=explosion_cannon;
    if (p.effect_kind==22U) {
        variant=explosion_cannon;
    } else if (p.bullet_kind==0U) {
        if (speed>0.0F) { x-=p.velocity_x*impact_backoff/speed; z-=p.velocity_z*impact_backoff/speed; }
    } else if (p.bullet_kind==2U) {
        variant=explosion_shell;
    } else {
        variant=explosion_gun;
        if (!hit.unit && speed>0.0F) { x-=p.velocity_x*impact_backoff/speed; z-=p.velocity_z*impact_backoff/speed; }
    }
    push_effect(context,EffectEventKind::impact_explosion,id,hit.target,variant,p.attacker,x,p.y,z);
}

// Response switch shared by bullets, shells and missiles. Returns true when the
// projectile impacted (and should end), false when it keeps flying.
enum class Step : std::uint8_t { fly, impact, impact_silent, ricochet_done, ricochet_inplace };

Step respond(WeaponContext& context,EntityId id,WeaponProjectile& p,const HitOutcome& hit) {
    if (!hit.unit) {
        if (p.bullet_kind!=1U || p.effect_kind==22U) {
            push_effect(context,EffectEventKind::impact_decal,id,no_entity,0U,p.attacker,hit.x,p.y,hit.z);
        }
        GameplayEvent impact;
        impact.kind=GameplayEventKind::projectile_impact;
        impact.tick=context.base.combat.tick;
        impact.subject=id;
        impact.owner=p.attacker;
        impact.value=p.weapon;
        impact.x=hit.x;
        impact.y=p.y;
        impact.z=hit.z;
        context.base.combat.events.push(impact);
        return Step::impact;
    }
    const ObjectResponse response=resolve_unit_hit(context,id,p,hit);
    switch (response.result) {
    case HitResponse::pass_through: return Step::fly;
    case HitResponse::impact_silent: return Step::impact_silent;
    case HitResponse::impact_decal:
        if (p.bullet_kind!=1U || p.effect_kind==22U) {
            push_effect(context,EffectEventKind::impact_decal,id,hit.target,0U,p.attacker,hit.x,p.y,hit.z);
        }
        return Step::impact;
    case HitResponse::ricochet:
        if (p.effect_kind==22U) return Step::impact;  // missiles treat 7 as impact
        if (p.effect_kind==34U) {
            const auto spawned=spawn_ricochet(context,p,hit.x,hit.z,response.angle,hit.target);
            push_effect(context,spawned ? EffectEventKind::ricochet_spawned : EffectEventKind::ricochet_dropped,
                        id,spawned ? *spawned : no_entity,p.effect_kind,p.attacker,hit.x,p.y,hit.z);
            return Step::ricochet_done;
        }
        {
            // update_shell_effect case 7: redirect in place.
            const float speed=std::sqrt(p.velocity_x*p.velocity_x+p.velocity_z*p.velocity_z);
            p.heading=response.angle;
            p.velocity_x=speed*context.base.angles.sine(p.heading);
            p.velocity_z=speed*context.base.angles.cosine(p.heading);
            p.accel_x=0.0F;
            p.accel_z=0.0F;
            p.attacker=no_owner;
            p.ignore=hit.target;
            push_effect(context,EffectEventKind::ricochet_spawned,id,id,p.effect_kind,p.attacker,p.x,p.y,p.z);
            return Step::ricochet_inplace;
        }
    default: return Step::impact;
    }
}

void update_ballistic(WeaponContext& context,EntityId id,WeaponProjectile& p) {
    if (--p.life<=0) { remove(context,id,p,RemovalReason::expired); return; }
    const float scale=context.base.frame_scale;
    const float end_x=p.x+p.velocity_x*scale;
    const float end_z=p.z+p.velocity_z*scale;
    bool units=true;
    bool world=true;
    if (p.effect_kind==34U) {
        p.travelled+=p.speed*scale;
        // Mode 4 (static) past the probed range, 0x400 (units) before it.
        const bool static_phase=p.travelled>p.range;
        units=!static_phase;
        world=static_phase;
    } else {
        p.velocity_x+=p.accel_x*scale;
        p.velocity_z+=p.accel_z*scale;
    }
    if (p.bullet_kind!=1U && p.trail!=0U) {
        push_effect(context,EffectEventKind::trail,id,no_entity,p.trail,p.attacker,end_x,p.y,end_z);
    }
    const HitOutcome hit=find_hit(context,p,end_x,end_z,units,world);
    Step step=hit.hit ? respond(context,id,p,hit) : Step::fly;
    if (step==Step::ricochet_done) { remove(context,id,p,RemovalReason::ricochet); return; }
    if (step==Step::ricochet_inplace) return;   // position kept this tick
    if (step==Step::impact || step==Step::impact_silent) {
        if (step==Step::impact) impact_feedback(context,id,p,hit);
        p.x=hit.x;
        p.z=hit.z;
        remove(context,id,p,RemovalReason::impact);
        return;
    }
    p.x=end_x;
    p.z=end_z;
    if (context.base.world.inside && !context.base.world.inside(end_x,end_z)) {
        remove(context,id,p,RemovalReason::left_world);
    }
}

void update_missile(WeaponContext& context,EntityId id,WeaponProjectile& p) {
    if (--p.life<=0) { remove(context,id,p,RemovalReason::expired); return; }
    if (!p.active) return;
    const float scale=context.base.frame_scale;
    const AngleTable& angles=context.base.angles;
    const float end_x=p.x+p.speed*angles.sine(p.heading)*scale;
    const float end_z=p.z+p.speed*angles.cosine(p.heading)*scale;
    if (p.trail!=0U) push_effect(context,EffectEventKind::trail,id,no_entity,p.trail,p.attacker,end_x,p.y,end_z);
    const HitOutcome hit=find_hit(context,p,end_x,end_z,true,true);
    const Step step=hit.hit ? respond(context,id,p,hit) : Step::fly;
    if (step==Step::impact || step==Step::impact_silent) {
        p.life=missile_linger_ticks;
        p.active=false;
        p.x=hit.x;
        p.z=hit.z;
        if (step==Step::impact) impact_feedback(context,id,p,hit);
        return;
    }
    if (context.base.world.inside && !context.base.world.inside(end_x,end_z)) {
        push_effect(context,EffectEventKind::impact_explosion,id,no_entity,explosion_cannon,p.attacker,p.x,p.y,p.z);
        remove(context,id,p,RemovalReason::left_world);
        return;
    }
    p.x=end_x;
    p.z=end_z;
}

void update_blast(WeaponContext& context,EntityId id,WeaponProjectile& p) {
    CombatContext& combat=context.base.combat;
    if (!p.triggered) {
        combat.units.for_each([&](EntityId,const UnitRecord& unit) {
            if (!unit.alive || p.triggered) return;
            if (std::fabs(unit.x-p.x)<=blast_trigger_half_extent+static_cast<float>(unit.footprint.max_x) &&
                std::fabs(unit.z-p.z)<=blast_trigger_half_extent+static_cast<float>(unit.footprint.max_z)) {
                p.triggered=true;
            }
        });
        if (!p.triggered) return;
    }
    if (p.start_tick==0) {
        push_effect(context,EffectEventKind::blast_armed,id,no_entity,blast_arm_variant,p.attacker,p.x,0.0F,p.z);
        push_sound(combat.sounds,sound_blast,p.x,0.0F,p.z,id,true,false);
        p.start_tick=combat.tick;
    } else if (p.start_tick+delayed_blast_ticks<combat.tick) {
        apply_splash_damage(context,p.x,p.z,splash_radius,splash_amount,p.attacker,id);
        remove(context,id,p,RemovalReason::detonated);
    }
}

} // namespace

void WeaponRuntimeState::serialize(StateWriter& writer) const {
    writer.u32(static_cast<std::uint32_t>(weapon_projectile_capacity));
    for (std::size_t slot=0U; slot<weapon_projectile_capacity; ++slot) {
        writer.u16(projectiles.slot_generation(slot));
        const bool occupied=projectiles.slot_occupied(slot);
        writer.boolean(occupied);
        if (!occupied) continue;
        const WeaponProjectile& p=*projectiles.get(projectiles.slot_id(slot));
        writer.u8(p.effect_kind);
        writer.u8(p.weapon);
        writer.u8(p.bullet_kind);
        writer.u8(p.trail);
        writer.f32(p.x); writer.f32(p.y); writer.f32(p.z);
        writer.f32(p.velocity_x); writer.f32(p.velocity_z);
        writer.f32(p.accel_x); writer.f32(p.accel_z);
        writer.f32(p.speed); writer.f32(p.travelled); writer.f32(p.range);
        writer.u16(p.heading);
        writer.s16(p.life);
        writer.u8(p.damage);
        writer.u8(p.attacker);
        writer.boolean(p.local);
        writer.boolean(p.active);
        writer.boolean(p.triggered);
        writer.s32(p.start_tick);
        writer.id(p.ignore);
    }
    for (std::size_t i=0U; i<unit_capacity; ++i) {
        writer.u8(hull_side[i]);
        writer.s32(last_mine_tick[i]);
        writer.s32(shield_end_tick[i]);
        writer.s32(flash_timer[i]);
        writer.f32(recoil[i]);
    }
}

WeaponFireStatus fire_weapon(WeaponContext& context,EntityId unit_id,WeaponState& weapon,
                             const FireRequest& request,const FireOptions& options) {
    CombatContext& combat=context.base.combat;
    UnitRecord* unit=combat.units.get(unit_id);
    if (unit==nullptr || !unit->alive) return WeaponFireStatus::not_ready;
    const auto def=weapon_definition(weapon.kind);
    if (!def) return WeaponFireStatus::unsupported;
    if (!def->auto_fire && !request.trigger_pressed) return WeaponFireStatus::not_ready;

    std::int32_t delay=def->cooldown;
    if (context.multiplayer_rules) {
        if (weapon.kind>=first_special_weapon) delay=multiplayer_special_delay;
    } else if ((unit->flags&flag_fast_cannon)!=0U && weapon.kind==3U) {
        delay=static_cast<std::uint8_t>(weapon_cooldown_table[3]/3U);
    }
    const std::int16_t ammo=unit->ammo[def->ammo_slot];
    const bool ready=(weapon.last_fire_tick+delay<combat.tick) || options.force;
    if (!(ready && ammo>0)) {
        if (ammo==0 && request.trigger_pressed) {
            if (human(*unit)) {
                push_sound(combat.sounds,retail::sound_empty_trigger,unit->x,unit->y,unit->z,unit_id,true,true);
            }
            GameplayEvent empty;
            empty.kind=GameplayEventKind::weapon_empty;
            empty.tick=combat.tick;
            empty.subject=unit_id;
            empty.owner=unit->owner;
            combat.events.push(empty);
            return WeaponFireStatus::empty;
        }
        return WeaponFireStatus::not_ready;
    }

    const AngleTable& angles=context.base.angles;
    const std::size_t slot=unit_id.slot;
    const bool bike=unit->kind==retail::UnitKind::bike;
    const MuzzleOffset& turret_offset=bike ? def->bike_muzzle : def->muzzle;
    WeaponFireStatus status=WeaponFireStatus::fired;
    switch (def->action) {
    case WeaponAction::none:
        break;
    case WeaponAction::repair:
        unit->health+=static_cast<std::int32_t>(static_cast<float>(unit->max_health)*0.3F);
        if (unit->max_health<unit->health) unit->health=unit->max_health;
        push_effect(context,EffectEventKind::repaired,unit_id,no_entity,0U,unit->owner,unit->x,unit->y,unit->z);
        break;
    case WeaponAction::shield: {
        std::int32_t& end=context.state.shield_end_tick[slot];
        end=end==0 ? combat.tick+shield_ticks : end+shield_ticks;
        context.state.flash_timer[slot]=0xF0;
        push_effect(context,EffectEventKind::shield_raised,unit_id,no_entity,0U,unit->owner,unit->x,unit->y,unit->z);
        break;
    }
    case WeaponAction::bullet: {
        Muzzle m;
        if (def->frame==MuzzleFrame::hull) {
            MuzzleOffset offset=def->muzzle;
            offset.x=(context.state.hull_side[slot]&1U)!=0U ? def->muzzle.x : -def->muzzle.x;
            context.state.hull_side[slot]^=1U;
            m=muzzle_point(angles,*unit,unit->heading_degrees,offset);
        } else {
            m=muzzle_point(angles,*unit,unit->turret_degrees,def->muzzle);
        }
        context.state.recoil[slot]+=def->recoil;
        const std::uint8_t damage=unit->kind==retail::UnitKind::goliath ? def->heavy_damage : def->damage;
        const std::uint8_t kind=def->id==2U ? std::uint8_t{1U} : std::uint8_t{0U};
        const auto id=spawn_bullet(context,m,*def,damage,kind,def->trail,*unit,unit_id);
        if (!id) { status=WeaponFireStatus::spawn_failed; break; }
        if (def->id!=2U) {
            push_effect(context,EffectEventKind::muzzle_flash,unit_id,*id,0x1DU,unit->owner,m.x,m.y,m.z);
        }
        mark_fired(context,unit_id,*unit,*def,*id,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::spread_shell: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,turret_offset);
        context.state.recoil[slot]+=def->recoil;
        const float a1=m.forward_x*def->speed;
        const float a0=m.forward_z*def->speed;
        const float a2=-a1;
        const float a3=a0;
        std::optional<EntityId> first;
        auto shot=[&](float vx,float vz,float ax,float az) {
            const auto id=spawn_shell(context,m,*def,vx,vz,ax,az,*unit,unit_id);
            return id;
        };
        if ((unit->flags&flag_powered)!=0U) {
            for (int i=0; i<3; ++i) {
                const float t=context.random.next_float(6.0F)-3.0F;
                const auto id=shot(a1+(a3*t)/10.0F,a0+(a2*t)/10.0F,(-a3*t)/200.0F,(-a2*t)/200.0F);
                if (i==0 && id) first=id;
            }
        } else {
            (void)shot(a1,a0,0.0F,0.0F);
            const float b1=a1+a3/10.0F;
            const float b0=a0+a2/10.0F;
            first=shot(b1,b0,-a3/200.0F,-a2/200.0F);
            (void)shot(b1-a3/5.0F,b0-a2/5.0F,a3/200.0F,a2/200.0F);
        }
        if (!first) { status=WeaponFireStatus::spawn_failed; break; }
        mark_fired(context,unit_id,*unit,*def,*first,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::missile: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,turret_offset);
        context.state.recoil[slot]+=def->recoil;
        const auto id=context.state.projectiles.allocate();
        if (!id) { status=WeaponFireStatus::spawn_failed; break; }
        WeaponProjectile& p=*context.state.projectiles.get(*id);
        p.effect_kind=22U;
        p.weapon=def->id;
        p.trail=def->trail;
        p.x=m.x;
        p.y=m.y;
        p.z=m.z;
        p.speed=options.boosted ? def->speed : 30.0F;
        p.heading=m.heading;
        p.life=def->life;
        p.damage=def->damage;
        p.attacker=unit->owner;
        p.local=human(*unit);
        p.ignore=unit_id;
        if ((unit->flags&flag_powered)!=0U) unit->ammo[11]=1;
        push_effect(context,EffectEventKind::muzzle_flash,unit_id,*id,0x1DU,unit->owner,m.x,m.y,m.z);
        mark_fired(context,unit_id,*unit,*def,*id,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::placed_blast: {
        if ((unit->flags&flag_powered)!=0U) return WeaponFireStatus::unsupported;
        const auto id=context.state.projectiles.allocate();
        if (!id) return WeaponFireStatus::spawn_failed;   // retail returns before bookkeeping
        WeaponProjectile& p=*context.state.projectiles.get(*id);
        p.effect_kind=29U;
        p.weapon=def->id;
        p.x=unit->x;
        p.z=unit->z;
        p.attacker=unit->owner;
        p.local=human(*unit);
        p.ignore=unit_id;
        p.start_tick=0;
        mark_fired(context,unit_id,*unit,*def,*id,p.x,p.y,p.z);
        break;
    }
    case WeaponAction::ring_burst: {
        const float r=def->range;
        const std::array<std::array<float,2>,4> points{{{unit->x+r,unit->z},{unit->x-r,unit->z},
                                                        {unit->x,unit->z+r},{unit->x,unit->z-r}}};
        for (const auto& point:points) {
            push_effect(context,EffectEventKind::ring_burst,unit_id,no_entity,def->trail,unit->owner,
                        point[0],50.0F,point[1]);
        }
        context.state.last_mine_tick[slot]=combat.tick;
        break;
    }
    case WeaponAction::effect12_burst:
    case WeaponAction::effect14:
    case WeaponAction::effect15:
    case WeaponAction::rear_deploy:
        return WeaponFireStatus::unsupported;
    }
    weapon.last_fire_tick=combat.tick;
    consume_ammo(context,*unit,weapon);
    return status;
}

std::size_t apply_splash_damage(WeaponContext& context,float x,float z,float radius,
                                std::int32_t amount,std::uint8_t attacker,EntityId source) {
    CombatContext& combat=context.base.combat;
    push_effect(context,EffectEventKind::splash,source,no_entity,0U,attacker,x,0.0F,z);
    std::vector<EntityId> hits;
    combat.units.for_each([&](EntityId id,const UnitRecord& unit) {
        if (!unit.alive) return;
        const float dx=unit.x-x;
        const float dz=unit.z-z;
        if (dx*dx+dz*dz<=radius*radius) hits.push_back(id);
    });
    for (const EntityId id:hits) {
        (void)apply_unit_damage(combat,id,amount,attacker,false,source);
        if (UnitRecord* unit=combat.units.get(id)) unit->hit_shake+=static_cast<float>(amount/4);
    }
    return hits.size();
}

std::optional<EntityId> spawn_ricochet(WeaponContext& context,const WeaponProjectile& from,
                                       float hit_x,float hit_z,std::uint16_t angle,EntityId hit) {
    const auto id=context.state.projectiles.allocate();
    if (!id) return std::nullopt;
    WeaponProjectile& p=*context.state.projectiles.get(*id);
    const AngleTable& angles=context.base.angles;
    p.effect_kind=34U;
    p.weapon=from.weapon;
    p.bullet_kind=from.bullet_kind;
    p.trail=from.trail;
    p.x=hit_x;
    p.y=from.y;
    p.z=hit_z;
    p.speed=from.speed;
    p.velocity_x=from.speed*angles.sine(angle);
    p.velocity_z=from.speed*angles.cosine(angle);
    p.heading=angle;
    p.life=0x70;
    p.damage=from.damage;
    p.attacker=no_owner;
    p.local=false;    // flags cleared
    p.ignore=hit;
    p.travelled=0.0F;
    p.range=probe_range(context,hit_x,hit_z,angle,hit);
    return id;
}

void update_weapon_projectiles(WeaponContext& context) {
    // Snapshot first: records spawned during this pass update next tick.
    std::vector<EntityId> live;
    for (std::size_t slot=0U; slot<weapon_projectile_capacity; ++slot) {
        if (context.state.projectiles.slot_occupied(slot)) live.push_back(context.state.projectiles.slot_id(slot));
    }
    for (const EntityId id:live) {
        WeaponProjectile* p=context.state.projectiles.get(id);
        if (p==nullptr) continue;
        switch (p->effect_kind) {
        case 34U:
        case 2U: update_ballistic(context,id,*p); break;
        case 22U: update_missile(context,id,*p); break;
        case 29U: update_blast(context,id,*p); break;
        default: break;
        }
    }
}

} // namespace battletanx
