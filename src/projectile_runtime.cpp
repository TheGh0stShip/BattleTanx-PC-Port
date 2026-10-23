#include "battletanx/projectile_runtime.hpp"

#include "battletanx/geometry.hpp"
#include "battletanx/runtime_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace battletanx {

std::optional<ProjectileVisual> projectile_visual(std::uint8_t effect_kind) noexcept {
    switch (effect_kind) {
    case 2U:  // func_8008D588 -> D_802D5A78
    case 34U: // func_8008EBA0 -> D_802D5A78
        return ProjectileVisual{0x24U,1.0F};
    case 22U: // func_8008DDCC -> D_802D5AB2
        return ProjectileVisual{0x41U,1.0F};
    case 14U: // func_800F2D58 -> D_802D5A7A
        return ProjectileVisual{0x25U,1.0F};
    case 15U: // func_800F3770 -> D_802D5A7A, D_80074F0C = 0.35
        return ProjectileVisual{0x25U,0.35F};
    default:
        return std::nullopt;
    }
}

namespace {

bool weapon_auto_fires(std::uint8_t kind) noexcept {
    return kind<retail::weapon_repeats.size() && retail::weapon_repeats[kind]!=0U;
}

std::int32_t weapon_delay(std::uint8_t kind) noexcept {
    return kind<retail::weapon_cooldown.size() ? retail::weapon_cooldown[kind] : 0;
}

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

std::optional<SegmentHit> world_segment(const WorldCollision& world,float x0,float z0,float x1,float z1) {
    if (!world.segment) return std::nullopt;
    return world.segment(x0,z0,x1,z1);
}

bool projectile_blocking_type(std::int32_t type) noexcept {
    return type==1 || type==19 || type==26 || type==27 || type==30;
}

float segment_length(float x0,float z0,float x1,float z1) noexcept {
    const float dx=x1-x0;
    const float dz=z1-z0;
    return std::sqrt(dx*dx+dz*dz);   // correctly rounded (std::hypot is not reproducible)
}

} // namespace

WorldCollision level_world_collision(const WorldLevel& world,const NavigationLevel& navigation,
                                     const AngleTable& angles) {
    WorldCollision collision;
    collision.segment=[&world,&navigation,&angles](float x0,float z0,float x1,float z1)
        -> std::optional<SegmentHit> {
        const PlanarPoint begin{x0,z0};
        const PlanarPoint end{x1,z1};
        float best=std::numeric_limits<float>::infinity();
        for (const WorldItem& item:world.items) {
            if (!projectile_blocking_type(item.type) || item.shape<0 ||
                static_cast<std::size_t>(item.shape)>=world.shapes.size()) continue;
            const WorldShape& shape=world.shapes[static_cast<std::size_t>(item.shape)];
            const auto fraction=segment_oriented_rect_hit_fraction(
                begin,end,{item.x,item.z,static_cast<float>(shape.min_x),
                           static_cast<float>(shape.max_x),static_cast<float>(shape.min_z),
                           static_cast<float>(shape.max_z),item.angle},angles);
            if (fraction) best=std::min(best,*fraction);
        }

        const std::uint16_t start=find_nearest_navigation_triangle(
            navigation,NavigationVertex{x0,z0});
        if (start!=0U && navigation_point_inside_triangle(
                navigation,NavigationVertex{x0,z0},start)) {
            const auto trace=trace_navigation_segment(
                navigation,start,NavigationVertex{x0,z0},NavigationVertex{x1,z1});
            if (trace.stop!=NavigationTraceStop::target) {
                float inside=0.0F;
                float outside=1.0F;
                for (int iteration=0; iteration<20; ++iteration) {
                    const float middle=(inside+outside)*0.5F;
                    const NavigationVertex point{
                        x0+(x1-x0)*middle,z0+(z1-z0)*middle};
                    const auto partial=trace_navigation_segment(
                        navigation,start,NavigationVertex{x0,z0},point);
                    if (partial.stop==NavigationTraceStop::target) inside=middle;
                    else outside=middle;
                }
                best=std::min(best,outside);
            }
        }
        if (!std::isfinite(best)) return std::nullopt;
        const float hit_x=x0+(x1-x0)*best;
        const float hit_z=z0+(z1-z0)*best;
        return SegmentHit{segment_length(x0,z0,hit_x,hit_z),hit_x,hit_z};
    };
    collision.inside=[&navigation](float x,float z) {
        const NavigationVertex point{x,z};
        const std::uint16_t triangle=find_nearest_navigation_triangle(navigation,point);
        return triangle!=0U && navigation_point_inside_triangle(navigation,point,triangle);
    };
    return collision;
}

std::optional<float> segment_footprint_distance(float x0,float z0,float x1,float z1,float cx,float cz,
                                                float sine,float cosine,
                                                const retail::Footprint& footprint) noexcept {
    const float s=sine;
    const float c=cosine;
    // World = local.x * (c, -s) + local.z * (s, c); invert by transposition.
    auto to_local=[&](float wx,float wz,float& lx,float& lz) {
        const float dx=wx-cx;
        const float dz=wz-cz;
        lx=dx*c-dz*s;
        lz=dx*s+dz*c;
    };
    float ax=0.0F,az=0.0F,bx=0.0F,bz=0.0F;
    to_local(x0,z0,ax,az);
    to_local(x1,z1,bx,bz);
    const float dx=bx-ax;
    const float dz=bz-az;
    float enter=0.0F;
    float leave=1.0F;
    auto clip=[&](float origin,float delta,float lo,float hi) {
        if (delta==0.0F) return origin>=lo && origin<=hi;
        float t0=(lo-origin)/delta;
        float t1=(hi-origin)/delta;
        if (t0>t1) { const float t=t0; t0=t1; t1=t; }
        if (t0>enter) enter=t0;
        if (t1<leave) leave=t1;
        return enter<=leave;
    };
    if (!clip(ax,dx,footprint.min_x,footprint.max_x)) return std::nullopt;
    if (!clip(az,dz,footprint.min_z,footprint.max_z)) return std::nullopt;
    if (!std::isfinite(enter)) return std::nullopt;
    const float length=std::sqrt(dx*dx+dz*dz);
    return enter*length;
}

std::optional<UnitSegmentHit> find_unit_segment_hit(const UnitPool& units,const AngleTable& angles,
                                                    float x0,float z0,float x1,float z1,
                                                    EntityId ignore) {
    std::optional<UnitSegmentHit> best;
    units.for_each([&](EntityId id,const UnitRecord& unit) {
        if (!unit.alive || id==ignore) return;
        const std::uint16_t heading=degrees_to_retail_angle(unit.heading_degrees);
        const auto distance=segment_footprint_distance(x0,z0,x1,z1,unit.x,unit.z,angles.sine(heading),
                                                       angles.cosine(heading),unit.footprint);
        if (!distance) return;
        if (!best || *distance<best->distance) best=UnitSegmentHit{id,*distance};
    });
    return best;
}

namespace {

// update_bullet_effect: D_80125CD0 (ROM 0xB5CD0, s32[4]) gun impact sounds.
constexpr std::array<std::uint16_t,4> gun_impact_sounds{27U,28U,29U,30U};
constexpr std::uint8_t explosion_cannon=0x0BU;
constexpr std::uint8_t explosion_shell=0x09U;
constexpr std::uint8_t explosion_gun=0x17U;
constexpr std::uint8_t explosion_unit_killed=0x13U;   // unit_handle_hit_message case 12
constexpr std::uint8_t explosion_nuke=0x08U;          // update_bouncing_debris
constexpr std::uint8_t explosion_grenade=0x1AU;       // update_bouncing_grenade
constexpr float impact_backoff=35.0F;
constexpr std::int16_t missile_linger_ticks=15;
constexpr float beam_length=4000.0F;                  // spawn_beam_effect
constexpr std::uint8_t beam_fork_depth=16U;           // depth > 16 forks
constexpr std::uint8_t missile_burst_variant=0x12U;   // update_missile_effect burst
constexpr std::uint8_t missile_burst_depth=5U;
constexpr std::uint16_t sound_missile_burst=9U;
constexpr float bounce_floor=15.0F;
constexpr float bounce_gravity=2.0F;
constexpr std::uint16_t bounce_spin=0x800U;
constexpr std::uint16_t sound_bounce=0x1AU;
// Nuke (effect 14): spawn_effect_kind_0e, D_80074E70 = 50.0, D_80074E74 = 10.0.
constexpr float nuke_speed=50.0F;
constexpr float nuke_lift=10.0F;
constexpr std::int16_t nuke_timer=0xC8;
constexpr std::int16_t nuke_warning_timer=0x32;
constexpr std::uint16_t sound_nuke_warning=0x14U;
constexpr std::uint16_t sound_nuke_detonate=0x0BU;
// Script 17 (gEffectScripts, D_80138F3C[8]): ramp def 18 starts after 256
// clock units; gEffectParticleDefs[7]: duration 0x0C00, radius 50 -> 8000,
// flags 0x10 (notify). apply_proximity_hit_to_target: 50 + (1-d/4000)*50.
constexpr std::int32_t nuke_ring_delay_units=256;
constexpr std::int32_t nuke_ring_duration_units=0x0C00;
constexpr std::int32_t nuke_ring_start=50;
constexpr std::int32_t nuke_ring_end=8000;
constexpr float nuke_falloff_distance=4000.0F;
constexpr std::int32_t nuke_base_damage=50;
// Grenade (effect 15): D_80074ED4..EE8 = 0.4, 30, 0.8, 0.2, 0.9, 10.
constexpr std::int16_t grenade_timer=0x50;
constexpr float grenade_splash_radius=100.0F;
constexpr std::int32_t grenade_splash_amount=22;
constexpr std::uint16_t sound_grenade=5U;

const ProjectileHooks& hooks_of(const ProjectileContext& context) {
    static const ProjectileHooks none{};
    return context.hooks!=nullptr ? *context.hooks : none;
}

void emit(ProjectileContext& context,EffectEventKind kind,EntityId source,EntityId other,
          std::uint8_t variant,std::uint8_t owner,float x,float y,float z,
          RemovalReason reason=RemovalReason::none,std::uint16_t angle=0U,float length=0.0F,
          float normal_x=0.0F,float normal_z=0.0F) {
    EffectEventQueue* queue=hooks_of(context).effects;
    if (queue==nullptr) return;
    EffectEvent event;
    event.kind=kind;
    event.tick=context.combat.tick;
    event.source=source;
    event.other=other;
    event.variant=variant;
    event.owner=owner;
    event.reason=reason;
    event.gameplay_index=static_cast<std::uint32_t>(context.combat.events.events().size());
    event.x=x;
    event.y=y;
    event.z=z;
    event.angle=angle;
    event.length=length;
    event.normal_x=normal_x;
    event.normal_z=normal_z;
    queue->push(event);
}

void remove(ProjectileContext& context,EntityId id,RemovalReason reason) {
    const ShellRecord* p=context.shells.get(id);
    if (p==nullptr) return;
    emit(context,EffectEventKind::projectile_removed,id,no_entity,p->effect_kind,p->attacker,
         p->x,p->y,p->z,reason);
    context.shells.release(id);
}

float probe_range(ProjectileContext& context,float x,float z,std::uint16_t heading) {
    const float end_x=x+context.angles.sine(heading)*retail::shell_range_probe;
    const float end_z=z+context.angles.cosine(heading)*retail::shell_range_probe;
    if (const auto hit=world_segment(context.world,x,z,end_x,end_z)) return hit->distance;
    return retail::shell_unbounded_range;
}

std::uint32_t next_random(ProjectileContext& context,std::uint32_t fallback) {
    RetailRandom* random=hooks_of(context).random;
    return random!=nullptr ? random->next_u32() : fallback;
}
float next_float(ProjectileContext& context,float range,float fallback) {
    RetailRandom* random=hooks_of(context).random;
    return random!=nullptr ? random->next_float(range) : fallback;
}

void push_impact_event(ProjectileContext& context,EntityId id,EntityId target,std::uint8_t owner,
                       std::int32_t value,float x,float y,float z) {
    GameplayEvent impact;
    impact.kind=GameplayEventKind::projectile_impact;
    impact.tick=context.combat.tick;
    impact.subject=id;
    impact.other=target;
    impact.owner=owner;
    impact.value=value;
    impact.x=x;
    impact.y=y;
    impact.z=z;
    context.combat.events.push(impact);
}

struct HitOutcome {
    bool hit{};
    bool unit{};
    bool external{};
    EntityId target{};
    std::uint16_t external_token{};
    float x{}, z{};
    float distance{};
    SegmentHit world{};
};

// find_unit_segment_hit that also skips units a beam already passed through.
std::optional<UnitSegmentHit> nearest_unit_excluding(const UnitPool& units,const AngleTable& angles,
                                                     float x0,float z0,float x1,float z1,EntityId ignore,
                                                     std::span<const EntityId> passed) {
    std::optional<UnitSegmentHit> best;
    units.for_each([&](EntityId id,const UnitRecord& unit) {
        if (!unit.alive || id==ignore) return;
        if (std::find(passed.begin(),passed.end(),id)!=passed.end()) return;
        const std::uint16_t heading=degrees_to_retail_angle(unit.heading_degrees);
        const auto distance=segment_footprint_distance(x0,z0,x1,z1,unit.x,unit.z,angles.sine(heading),
                                                       angles.cosine(heading),unit.footprint);
        if (!distance) return;
        if (!best || *distance<best->distance) best=UnitSegmentHit{id,*distance};
    });
    return best;
}

HitOutcome find_hit(ProjectileContext& context,float x0,float z0,float x1,float z1,EntityId ignore,
                    bool units,bool world,std::span<const EntityId> passed={}) {
    HitOutcome out;
    std::optional<UnitSegmentHit> unit_hit;
    std::optional<SegmentHit> world_hit;
    std::optional<ExternalSegmentHit> external_hit;
    if (units) {
        unit_hit=passed.empty()
            ? find_unit_segment_hit(context.combat.units,context.angles,x0,z0,x1,z1,ignore)
            : nearest_unit_excluding(context.combat.units,context.angles,x0,z0,x1,z1,ignore,passed);
    }
    if (world) world_hit=world_segment(context.world,x0,z0,x1,z1);
    if (units && hooks_of(context).external_hit)
        external_hit=hooks_of(context).external_hit(x0,z0,x1,z1);
    const float unit_distance=unit_hit ? unit_hit->distance : std::numeric_limits<float>::infinity();
    const float external_distance=external_hit ? external_hit->distance : std::numeric_limits<float>::infinity();
    const float world_distance=world_hit ? world_hit->distance : std::numeric_limits<float>::infinity();
    // Equal distances resolve to the unit (find_segment_hit object order).
    if (unit_hit && unit_distance<=external_distance && unit_distance<=world_distance) {
        const float length=segment_length(x0,z0,x1,z1);
        const float t=length>0.0F ? unit_hit->distance/length : 0.0F;
        out.hit=true;
        out.unit=true;
        out.target=unit_hit->unit;
        out.x=x0+(x1-x0)*t;
        out.z=z0+(z1-z0)*t;
        out.distance=unit_hit->distance;
    } else if (external_hit && external_distance<=world_distance) {
        const float length=segment_length(x0,z0,x1,z1);
        const float t=length>0.0F ? external_hit->distance/length : 0.0F;
        out.hit=true;
        out.external=true;
        out.external_token=external_hit->token;
        out.x=x0+(x1-x0)*t;
        out.z=z0+(z1-z0)*t;
        out.distance=external_hit->distance;
    } else if (world_hit) {
        out.hit=true;
        out.x=world_hit->x;
        out.z=world_hit->z;
        out.distance=world_hit->distance;
        out.world=*world_hit;
    }
    return out;
}

ObjectResponse unit_response(ProjectileContext& context,EntityId projectile,const ShellRecord& p,
                             const HitOutcome& hit,float y,bool direct) {
    const ProjectileHooks& hooks=hooks_of(context);
    if (hooks.object_response) {
        const ProjectileContact contact{projectile,hit.x,y,hit.z,p.heading,p.damage,p.attacker,direct,p.effect_kind};
        return hooks.object_response(hit.target,contact);
    }
    return ObjectResponse{handle_projectile_hit(context.combat,hit.target,p.damage,p.attacker,direct,projectile),0U};
}

ObjectResponse external_response(ProjectileContext& context,EntityId projectile,const ShellRecord& p,
                                 const HitOutcome& hit,float y,bool direct) {
    const ProjectileHooks& hooks=hooks_of(context);
    if (!hooks.external_response) return ObjectResponse{HitResponse::pass_through,0U};
    const ProjectileContact contact{projectile,hit.x,y,hit.z,p.heading,p.damage,p.attacker,direct,p.effect_kind};
    return hooks.external_response(hit.external_token,contact);
}

// Static-collision fallback normal: against the direction of travel
// (TEMPORARY POLICY while level_world_collision has no normals).
void hit_normal(const SegmentHit& hit,float vx,float vz,float& nx,float& nz) {
    if (hit.has_normal) { nx=hit.normal_x; nz=hit.normal_z; return; }
    const float length=std::sqrt(vx*vx+vz*vz);
    nx=length>0.0F ? -vx/length : 0.0F;
    nz=length>0.0F ? -vz/length : 0.0F;
}

void impact_feedback(ProjectileContext& context,EntityId id,const ShellRecord& p,const HitOutcome& hit) {
    std::uint16_t sound=retail::sound_hit_other;
    if (p.bullet_kind==1U) {
        sound=gun_impact_sounds[next_random(context,0U)&3U];   // no stream: TEMPORARY POLICY index 0
    } else if (hit.unit || hit.external) {
        sound=retail::sound_hit_unit;
    }
    push_sound(context.combat.sounds,sound,hit.x,p.y,hit.z,id,true,false);
    float x=hit.x;
    float z=hit.z;
    const float speed=std::sqrt(p.velocity_x*p.velocity_x+p.velocity_z*p.velocity_z);
    std::uint8_t variant=explosion_cannon;
    if (p.bullet_kind==0U) {
        if (speed>0.0F) { x-=p.velocity_x*impact_backoff/speed; z-=p.velocity_z*impact_backoff/speed; }
    } else if (p.bullet_kind==2U) {
        variant=explosion_shell;
    } else {
        variant=explosion_gun;
        if (!hit.unit && speed>0.0F) { x-=p.velocity_x*impact_backoff/speed; z-=p.velocity_z*impact_backoff/speed; }
    }
    emit(context,EffectEventKind::impact_explosion,id,hit.target,variant,p.attacker,x,p.y,z);
}

enum class Step : std::uint8_t { fly, impact, impact_silent, ricochet_done, ricochet_inplace };

// Response switch shared by bullets (34) and shells (2). Deterministic event
// order: handler (damage events) -> projectile_impact -> sound -> effects.
Step respond(ProjectileContext& context,EntityId id,ShellRecord& p,const HitOutcome& hit) {
    if (!hit.unit && !hit.external) {
        if (p.bullet_kind!=1U) {
            emit(context,EffectEventKind::impact_decal,id,no_entity,0U,p.attacker,hit.x,p.y,hit.z,
                 RemovalReason::none,0U,0.0F,hit.world.normal_x,hit.world.normal_z);
        }
        push_impact_event(context,id,no_entity,p.attacker,static_cast<std::int32_t>(HitResponse::impact),
                          hit.x,p.y,hit.z);
        return Step::impact;
    }
    const ObjectResponse response=hit.external ? external_response(context,id,p,hit,p.y,p.local)
                                               : unit_response(context,id,p,hit,p.y,p.local);
    if (response.result==HitResponse::pass_through) return Step::fly;
    push_impact_event(context,id,hit.target,p.attacker,static_cast<std::int32_t>(response.result),hit.x,p.y,hit.z);
    switch (response.result) {
    case HitResponse::impact_silent: return Step::impact_silent;
    case HitResponse::impact_decal:
        if (p.bullet_kind!=1U) {
            emit(context,EffectEventKind::impact_decal,id,hit.target,0U,p.attacker,hit.x,p.y,hit.z);
        }
        return Step::impact;
    case HitResponse::ricochet:
        if (p.effect_kind==effect_kind::bullet) {
            const auto spawned=spawn_ricochet(context,p,hit.x,hit.z,response.angle,hit.target);
            emit(context,spawned ? EffectEventKind::ricochet_spawned : EffectEventKind::ricochet_dropped,
                 id,spawned ? *spawned : no_entity,p.effect_kind,p.attacker,hit.x,p.y,hit.z);
            return Step::ricochet_done;
        }
        {
            // update_shell_effect case 7: redirect in place.
            const float speed=std::sqrt(p.velocity_x*p.velocity_x+p.velocity_z*p.velocity_z);
            p.heading=response.angle;
            p.velocity_x=speed*context.angles.sine(p.heading);
            p.velocity_z=speed*context.angles.cosine(p.heading);
            p.accel_x=0.0F;
            p.accel_z=0.0F;
            p.attacker=no_owner;
            p.ignore=hit.target;
            emit(context,EffectEventKind::ricochet_spawned,id,id,p.effect_kind,p.attacker,p.x,p.y,p.z);
            return Step::ricochet_inplace;
        }
    default: return Step::impact;
    }
}

void update_ballistic(ProjectileContext& context,EntityId id,ShellRecord& p) {
    if (--p.life<=0) { remove(context,id,RemovalReason::expired); return; }
    const float scale=context.frame_scale;
    const float end_x=p.x+p.velocity_x*scale;
    const float end_z=p.z+p.velocity_z*scale;
    bool units=true;
    bool world=true;
    if (p.effect_kind==effect_kind::bullet) {
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
        emit(context,EffectEventKind::trail,id,no_entity,p.trail,p.attacker,end_x,p.y,end_z);
    }
    const HitOutcome hit=find_hit(context,p.x,p.z,end_x,end_z,p.ignore,units,world);
    const Step step=hit.hit ? respond(context,id,p,hit) : Step::fly;
    if (step==Step::ricochet_done) { remove(context,id,RemovalReason::ricochet); return; }
    if (step==Step::ricochet_inplace) return;
    if (step==Step::impact || step==Step::impact_silent) {
        if (step==Step::impact) impact_feedback(context,id,p,hit);
        p.x=hit.x;
        p.z=hit.z;
        remove(context,id,RemovalReason::impact);
        return;
    }
    p.x=end_x;
    p.z=end_z;
    if (context.world.inside && !context.world.inside(end_x,end_z)) remove(context,id,RemovalReason::left_world);
}

// update_missile_effect (func_8008D768).
void update_missile(ProjectileContext& context,EntityId id,ShellRecord& p) {
    if (--p.life<=0) { remove(context,id,RemovalReason::expired); return; }
    if (!p.active) return;
    const ProjectileHooks& hooks=hooks_of(context);
    const float scale=context.frame_scale;
    MissileControl control{};
    if (p.guided) {
        if (hooks.missile_control) control=hooks.missile_control(p.attacker);
        if (control.release) {
            p.guided=false;
        } else {
            const auto delta=static_cast<std::int32_t>(static_cast<float>(control.steer)*10.0F*scale);
            p.heading=static_cast<std::uint16_t>(p.heading-static_cast<std::uint16_t>(delta));
            p.turn=static_cast<std::uint16_t>(-delta);
        }
    }
    const float sine=context.angles.sine(p.heading);
    const float cosine=context.angles.cosine(p.heading);
    const float end_x=p.x+p.speed*sine*scale;
    const float end_z=p.z+p.speed*cosine*scale;
    if (p.trail!=0U) {
        float tx=end_x;
        float tz=end_z;
        if (p.wobble) { tx+=sine*-45.0F; tz+=cosine*-45.0F; }
        emit(context,EffectEventKind::trail,id,no_entity,p.trail,p.attacker,tx,p.y,tz);
    }
    const HitOutcome hit=find_hit(context,p.x,p.z,end_x,end_z,p.ignore,true,true);
    bool impact=false;
    bool sound=true;
    float hit_y=p.y;
    if (hit.hit) {
        hit_y=p.y-5.0F+next_float(context,10.0F,5.0F);
        if (hit.unit) {
            const ObjectResponse response=unit_response(context,id,p,hit,hit_y,true);
            switch (response.result) {
            case HitResponse::impact:
            case HitResponse::ricochet: impact=true; break;
            case HitResponse::impact_silent: impact=true; sound=false; break;
            case HitResponse::impact_decal:
                impact=true;
                emit(context,EffectEventKind::impact_decal,id,hit.target,0U,p.attacker,hit.x,hit_y,hit.z);
                break;
            default: break;   // 1 (and 5): keeps flying
            }
            if (impact) {
                push_impact_event(context,id,hit.target,p.attacker,static_cast<std::int32_t>(response.result),
                                  hit.x,hit_y,hit.z);
            }
        } else {
            impact=true;
            emit(context,EffectEventKind::impact_decal,id,no_entity,0U,p.attacker,hit.x,hit_y,hit.z,
                 RemovalReason::none,0U,0.0F,hit.world.normal_x,hit.world.normal_z);
            push_impact_event(context,id,no_entity,p.attacker,0,hit.x,hit_y,hit.z);
        }
    }
    bool release=false;
    if (impact) {
        p.life=missile_linger_ticks;
        p.active=false;
        if (sound) {
            push_sound(context.combat.sounds,hit.unit ? retail::sound_hit_unit : retail::sound_hit_other,
                       hit.x,hit_y,hit.z,id,true,false);
            emit(context,EffectEventKind::impact_explosion,id,hit.target,explosion_cannon,p.attacker,hit.x,hit_y,hit.z);
        }
    } else if (context.world.inside && !context.world.inside(end_x,end_z)) {
        emit(context,EffectEventKind::impact_explosion,id,no_entity,explosion_cannon,p.attacker,p.x,p.y,p.z);
        release=true;
    } else {
        p.x=end_x;
        p.z=end_z;
    }
    if (p.guided && p.bursts>0 && control.burst) {
        BeamSpawn beam;
        beam.x=p.x;
        beam.y=p.y;
        beam.z=p.z;
        beam.heading=p.heading;
        beam.attacker=p.attacker;
        beam.variant=missile_burst_variant;
        beam.damage=p.damage;
        beam.depth=missile_burst_depth;
        beam.local=true;
        beam.ignore=no_entity;   // retail ignores the missile record itself
        const ShellRecord copy=p;
        if (fire_beam(context,beam)) {
            push_sound(context.combat.sounds,sound_missile_burst,copy.x,copy.y,copy.z,id,true,false);
            if (ShellRecord* again=context.shells.get(id)) --again->bursts;
        }
    }
    if (release) remove(context,id,RemovalReason::left_world);
}

void reflect(float nx,float nz,float& vx,float& vz) {
    const float dot=nx*-vx+nz*-vz;
    vx=(dot*nx+vx)*2.0F-vx;
    vz=(dot*nz+vz)*2.0F-vz;
}

// Wall test for nuke/grenade (find_segment_hit mode 0x10: static world).
std::optional<SegmentHit> bounce_wall(ProjectileContext& context,const ShellRecord& p,float nx,float nz) {
    return world_segment(context.world,p.x,p.z,nx,nz);
}

void nuke_ring(ProjectileContext& context,EntityId id,ShellRecord& p) {
    const std::int32_t elapsed=script_clock_units(context.combat.tick-p.phase_tick);
    if (elapsed>nuke_ring_duration_units) { remove(context,id,RemovalReason::finished); return; }
    const float ratio=static_cast<float>(nuke_ring_end-nuke_ring_start)/static_cast<float>(nuke_ring_duration_units);
    const auto radius=static_cast<std::int32_t>(static_cast<float>(nuke_ring_start)+static_cast<float>(elapsed)*ratio);
    const std::int32_t radius_sq=radius*radius;
    struct Candidate { EntityId unit; float distance_sq; std::size_t slot; };
    std::vector<Candidate> candidates;
    context.combat.units.for_each([&](EntityId unit_id,const UnitRecord& unit) {
        if (!unit.alive || (p.blast_hits&(1U<<unit_id.slot))!=0U) return;
        const float dx=p.x-unit.x;
        const float dz=p.z-unit.z;
        const float d=dx*dx+dz*dz;
        if (d<static_cast<float>(radius_sq)) candidates.push_back(Candidate{unit_id,d,unit_id.slot});
    });
    // notify_targets_in_radius visits targets nearest first; ties by slot.
    std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
        return a.distance_sq<b.distance_sq;
    });
    if (!candidates.empty()) {
        emit(context,EffectEventKind::splash,id,no_entity,explosion_nuke,p.attacker,p.x,p.y,p.z,
             RemovalReason::none,0U,static_cast<float>(radius));
    }
    for (const Candidate& c:candidates) {
        p.blast_hits|=1U<<c.slot;
        const float distance=std::sqrt(c.distance_sq);
        std::int32_t amount=nuke_base_damage;
        if (!(distance>nuke_falloff_distance)) {
            amount=static_cast<std::int32_t>((1.0F-distance/nuke_falloff_distance)*50.0F)+nuke_base_damage;
        }
        (void)apply_unit_damage(context.combat,c.unit,amount,p.attacker,false,id);
        if (const auto& recoil=hooks_of(context).proximity_recoil) recoil(c.unit);
    }
}

// update_bouncing_debris (func_800F2AE4) and the script-17 blast ring.
void update_nuke(ProjectileContext& context,EntityId id,ShellRecord& p) {
    if (p.phase==1U) {
        if (script_clock_units(context.combat.tick-p.phase_tick)>=nuke_ring_delay_units) {
            p.phase=2U;
            p.phase_tick=context.combat.tick;
        }
        return;
    }
    if (p.phase==2U) { nuke_ring(context,id,p); return; }
    --p.life;
    if (p.life==nuke_warning_timer) push_sound(context.combat.sounds,sound_nuke_warning,p.x,p.y,p.z,id,false,false);
    if (p.life<=0) {
        emit(context,EffectEventKind::impact_explosion,id,no_entity,explosion_nuke,p.attacker,p.x,p.y,p.z);
        push_sound(context.combat.sounds,sound_nuke_detonate,p.x,p.y,p.z,id,false,false);
        p.phase=1U;
        p.phase_tick=context.combat.tick;
        p.velocity_x=p.velocity_y=p.velocity_z=0.0F;
        return;
    }
    p.velocity_y-=bounce_gravity;
    p.spin=static_cast<std::uint16_t>(p.spin+bounce_spin);
    const float next_x=p.x+p.velocity_x;
    const float next_z=p.z+p.velocity_z;
    p.y+=p.velocity_y;
    if (p.y<=bounce_floor) {
        p.velocity_x-=p.velocity_x*0.05F;
        p.velocity_z-=p.velocity_z*0.05F;
        p.velocity_y=-p.velocity_y;
        p.y=bounce_floor;
        if (p.velocity_y>5.0F) push_sound(context.combat.sounds,sound_bounce,p.x,p.y,p.z,id,true,false);
    } else {
        p.velocity_x-=p.velocity_x*0.01F;
        p.velocity_z-=p.velocity_z*0.01F;
    }
    if (const auto wall=bounce_wall(context,p,next_x,next_z)) {
        float nx=0.0F,nz=0.0F;
        hit_normal(*wall,p.velocity_x,p.velocity_z,nx,nz);
        reflect(nx,nz,p.velocity_x,p.velocity_z);
        p.velocity_x-=p.velocity_x*0.5F;
        p.velocity_z-=p.velocity_z*0.5F;
        p.spin=static_cast<std::uint16_t>(0U-p.spin);
        p.ignore=no_entity;
        emit(context,EffectEventKind::bounce,id,no_entity,p.effect_kind,p.attacker,wall->x,p.y,wall->z);
    } else {
        p.x=next_x;
        p.z=next_z;
    }
}

// update_bouncing_grenade (func_800F3490).
void update_grenade(ProjectileContext& context,EntityId id,ShellRecord& p) {
    const float start_x=p.x;
    const float start_z=p.z;
    if (--p.life<=0) {
        emit(context,EffectEventKind::impact_explosion,id,no_entity,explosion_grenade,p.attacker,p.x,p.y,p.z);
        (void)apply_splash_damage(context,start_x,start_z,grenade_splash_radius,grenade_splash_amount,p.attacker,id);
        push_sound(context.combat.sounds,sound_grenade,p.x,p.y,p.z,id,true,false);
        remove(context,id,RemovalReason::detonated);
        return;
    }
    p.velocity_y-=bounce_gravity;
    p.spin=static_cast<std::uint16_t>(p.spin+bounce_spin);
    const float next_x=p.x+p.velocity_x;
    const float next_z=p.z+p.velocity_z;
    p.y+=p.velocity_y;
    if (p.y<=bounce_floor) {
        p.velocity_x-=p.velocity_x*0.15F;
        p.velocity_z-=p.velocity_z*0.15F;
        p.velocity_y=-p.velocity_y*0.8F;
        p.y=bounce_floor;
    } else {
        p.velocity_x-=p.velocity_x*0.01F;
        p.velocity_z-=p.velocity_z*0.01F;
    }
    if (const auto wall=bounce_wall(context,p,next_x,next_z)) {
        float nx=0.0F,nz=0.0F;
        hit_normal(*wall,p.velocity_x,p.velocity_z,nx,nz);
        float vx=p.velocity_x;
        float vz=p.velocity_z;
        reflect(nx,nz,vx,vz);
        vx*=next_float(context,0.6F,0.3F)+0.7F;
        vz*=next_float(context,0.6F,0.3F)+0.7F;
        p.velocity_x=vx-vx*0.6F;
        p.velocity_z=vz-vz*0.6F;
        p.ignore=no_entity;
        p.spin=static_cast<std::uint16_t>(0U-p.spin);
        if (p.velocity_y>5.0F) push_sound(context.combat.sounds,sound_bounce,p.x,p.y,p.z,id,true,false);
        emit(context,EffectEventKind::bounce,id,no_entity,p.effect_kind,p.attacker,wall->x,p.y,wall->z);
    } else {
        p.x=next_x;
        p.z=next_z;
    }
}

// effect_delayed_spawn_tick (func_800F1670).
void update_beam_fork(ProjectileContext& context,EntityId id,ShellRecord& p) {
    if (p.life>0) { --p.life; return; }
    BeamSpawn beam;
    beam.x=p.x;
    beam.y=p.y;
    beam.z=p.z;
    beam.heading=p.heading;
    beam.attacker=p.attacker;
    beam.variant=p.variant;
    beam.damage=p.damage;
    beam.depth=p.depth;
    beam.local=true;
    beam.ignore=p.ignore;
    remove(context,id,RemovalReason::finished);   // release first: the beam may fork into this slot
    (void)fire_beam(context,beam);
}

void spawn_beam_fork(ProjectileContext& context,float x,float y,float z,std::uint16_t angle,
                     const BeamSpawn& from,std::int16_t delay) {
    const auto id=context.shells.allocate();
    if (!id) return;   // retail ignores the -1 from spawn_effect_kind_0d
    ShellRecord& p=*context.shells.get(*id);
    p.effect_kind=effect_kind::beam_fork;
    p.weapon=0U;
    p.x=x;
    p.y=y;
    p.z=z;
    p.heading=angle;
    p.attacker=from.attacker;
    p.variant=from.variant;
    p.damage=from.damage;
    p.depth=static_cast<std::uint8_t>(from.depth-1U);
    p.life=delay;
    p.ignore=from.ignore;
    p.speed=0.0F;
}

} // namespace

std::int32_t script_clock_units(std::int32_t ticks) noexcept {
    // osGetTime counts at 46.875 MHz; the scripts read (count >> 16).
    if (ticks<=0) return 0;
    const std::int64_t units=static_cast<std::int64_t>(ticks)*46875000LL/(30LL*65536LL);
    return units>0x7FFFFFFFLL ? 0x7FFFFFFF : static_cast<std::int32_t>(units);
}

std::optional<EntityId> spawn_bullet(ProjectileContext& context,const BulletSpawn& spawn) {
    const auto id=context.shells.allocate();
    if (!id) return std::nullopt;
    ShellRecord& p=*context.shells.get(*id);
    p.effect_kind=effect_kind::bullet;
    p.weapon=spawn.weapon;
    p.bullet_kind=spawn.bullet_kind;
    p.trail=spawn.trail;
    p.x=spawn.x;
    p.y=spawn.y;
    p.z=spawn.z;
    p.speed=spawn.speed;
    p.heading=spawn.heading;
    if (spawn.has_velocity) {
        p.velocity_x=spawn.velocity_x;
        p.velocity_z=spawn.velocity_z;
    } else {
        p.velocity_x=context.angles.sine(spawn.heading)*spawn.speed;
        p.velocity_z=context.angles.cosine(spawn.heading)*spawn.speed;
    }
    p.life=spawn.life;
    p.damage=spawn.damage;
    p.attacker=spawn.attacker;
    p.local=spawn.local;
    p.ignore=spawn.ignore;
    p.travelled=0.0F;
    p.range=probe_range(context,p.x,p.z,p.heading);
    return id;
}

std::optional<EntityId> spawn_shell(ProjectileContext& context,const ShellSpawn& spawn) {
    const auto id=context.shells.allocate();
    if (!id) return std::nullopt;
    ShellRecord& p=*context.shells.get(*id);
    p.effect_kind=effect_kind::shell;
    p.weapon=spawn.weapon;
    p.bullet_kind=2U;
    p.trail=spawn.trail;
    p.x=spawn.x;
    p.y=spawn.y;
    p.z=spawn.z;
    p.velocity_x=spawn.velocity_x;
    p.velocity_z=spawn.velocity_z;
    p.accel_x=spawn.accel_x;
    p.accel_z=spawn.accel_z;
    p.speed=std::sqrt(spawn.velocity_x*spawn.velocity_x+spawn.velocity_z*spawn.velocity_z);
    p.heading=spawn.heading;
    p.life=retail::shell_life_ticks;
    p.damage=spawn.damage;
    p.attacker=spawn.attacker;
    p.local=spawn.local;
    p.ignore=spawn.ignore;
    return id;
}

std::optional<EntityId> spawn_missile(ProjectileContext& context,const MissileSpawn& spawn) {
    const auto id=context.shells.allocate();
    if (!id) return std::nullopt;
    ShellRecord& p=*context.shells.get(*id);
    p.effect_kind=effect_kind::missile;
    p.weapon=11U;
    p.bullet_kind=0U;
    p.trail=spawn.trail;
    p.x=spawn.x;
    p.y=spawn.y;
    p.z=spawn.z;
    // D_80071BAC = 2.5, D_80071BB0 = 1.5.
    p.speed=spawn.bursts>0 ? spawn.speed/2.5F : (spawn.bursts<0 ? spawn.speed/1.5F : spawn.speed);
    p.heading=spawn.heading;
    p.life=spawn.bursts!=0 ? std::int16_t{8000} : std::int16_t{400};
    p.damage=spawn.damage;
    p.attacker=spawn.attacker;
    p.local=spawn.local;
    p.active=true;
    p.guided=spawn.guided;
    p.turn=0U;
    p.bursts=spawn.bursts;
    p.wobble=spawn.bursts!=0;
    p.ignore=spawn.ignore;
    return id;
}

std::optional<EntityId> spawn_nuke(ProjectileContext& context,float x,float y,float z,
                                   std::uint16_t heading,std::uint8_t attacker,EntityId ignore) {
    const auto id=context.shells.allocate();
    if (!id) return std::nullopt;
    ShellRecord& p=*context.shells.get(*id);
    p.effect_kind=effect_kind::nuke;
    p.weapon=6U;
    p.x=x;
    p.y=y;
    p.z=z;
    p.heading=heading;
    p.velocity_x=context.angles.sine(heading)*nuke_speed;
    p.velocity_y=nuke_lift;
    p.velocity_z=context.angles.cosine(heading)*nuke_speed;
    p.life=nuke_timer;
    p.spin=0U;
    p.attacker=attacker;
    p.ignore=ignore;
    p.damage=0U;
    p.speed=0.0F;
    push_sound(context.combat.sounds,sound_nuke_warning,x,y,z,*id,false,false);
    return id;
}

std::optional<EntityId> spawn_grenade(ProjectileContext& context,RetailRandom& random,float x,float y,
                                      float z,std::uint16_t heading,std::uint8_t attacker,EntityId ignore) {
    const auto id=context.shells.allocate();
    if (!id) return std::nullopt;
    ShellRecord& p=*context.shells.get(*id);
    p.effect_kind=effect_kind::grenade;
    p.weapon=7U;
    const float jitter_x=random.next_float(0.4F);
    p.velocity_x=context.angles.sine(heading)*30.0F*(jitter_x+0.8F);
    p.velocity_y=(random.next_float(0.2F)+0.9F)*10.0F;
    p.velocity_z=context.angles.cosine(heading)*30.0F*(random.next_float(0.4F)+0.8F);
    p.x=x;
    p.y=y;
    p.z=z;
    p.heading=heading;
    p.life=grenade_timer;
    p.spin=0U;
    p.attacker=attacker;
    p.ignore=ignore;
    p.damage=static_cast<std::uint8_t>(grenade_splash_amount);
    p.speed=0.0F;
    return id;
}

bool fire_beam(ProjectileContext& context,const BeamSpawn& spawn) {
    if (spawn.depth==0U) return false;
    const float dir_x=context.angles.sine(spawn.heading);
    const float dir_z=context.angles.cosine(spawn.heading);
    float start_x=spawn.x;
    float start_z=spawn.z;
    float end_x=start_x+dir_x*beam_length;
    float end_z=start_z+dir_z*beam_length;
    bool found=false;
    bool done=false;
    HitOutcome hit;
    std::array<EntityId,unit_capacity> passed{};
    std::size_t passed_count=0U;
    // Bounded: every pass-through excludes one unit; at most unit_capacity+1 rounds.
    for (std::size_t round=0U; !done && round<=unit_capacity; ++round) {
        // Retail re-runs find_segment_hit from hit+dir over all objects
        // (spawn_beam_effect.c case 1). Native equivalent of re-testing from
        // inside a passed unit: exclude passed units, keep testing the rest.
        hit=find_hit(context,start_x,start_z,end_x,end_z,spawn.ignore,true,true,
                     std::span<const EntityId>(passed.data(),passed_count));
        found=hit.hit;
        if (!hit.hit) { done=true; break; }
        if (!hit.unit) {
            emit(context,EffectEventKind::impact_decal,no_entity,no_entity,0U,spawn.attacker,hit.x,spawn.y,hit.z,
                 RemovalReason::none,0U,0.0F,hit.world.normal_x,hit.world.normal_z);
            done=true;
            break;
        }
        ObjectResponse response{HitResponse::impact,0U};
        const ProjectileHooks& hooks=hooks_of(context);
        if (hooks.object_response) {
            const ProjectileContact contact{no_entity,hit.x,spawn.y,hit.z,spawn.heading,spawn.damage,
                                            spawn.attacker,spawn.local,effect_kind::beam};
            response=hooks.object_response(hit.target,contact);
        } else {
            // unit_handle_hit_message case 12: damage, result 0.
            const DamageResult result=apply_unit_damage(context.combat,hit.target,spawn.damage,spawn.attacker,
                                                        spawn.local,no_entity);
            if (result==DamageResult::destroyed) {
                emit(context,EffectEventKind::impact_explosion,no_entity,hit.target,explosion_unit_killed,
                     spawn.attacker,hit.x,spawn.y,hit.z);
            }
        }
        push_impact_event(context,no_entity,hit.target,spawn.attacker,static_cast<std::int32_t>(response.result),
                          hit.x,spawn.y,hit.z);
        switch (response.result) {
        case HitResponse::pass_through:
            start_x=hit.x+dir_x;
            start_z=hit.z+dir_z;
            found=false;
            if (passed_count<passed.size()) passed[passed_count++]=hit.target;
            if ((start_x-end_x)*(start_x-end_x)+(start_z-end_z)*(start_z-end_z)<100.0F) done=true;
            break;
        case HitResponse::impact_decal:
            emit(context,EffectEventKind::impact_decal,no_entity,hit.target,0U,spawn.attacker,hit.x,spawn.y,hit.z);
            done=true;
            break;
        case HitResponse::ricochet: {
            BeamSpawn sub=spawn;
            sub.x=hit.x+context.angles.sine(response.angle);
            sub.z=hit.z+context.angles.cosine(response.angle);
            sub.heading=response.angle;
            sub.attacker=no_owner;
            sub.depth=static_cast<std::uint8_t>(spawn.depth-1U);
            sub.ignore=no_entity;
            const bool spawned=fire_beam(context,sub);
            emit(context,spawned ? EffectEventKind::ricochet_spawned : EffectEventKind::ricochet_dropped,
                 no_entity,hit.target,effect_kind::beam,spawn.attacker,hit.x,spawn.y,hit.z);
            found=false;
            done=true;
            end_x=hit.x;
            end_z=hit.z;
            break;
        }
        default:   // 0 and (TEMPORARY POLICY) codes the retail switch omits
            done=true;
            break;
        }
    }
    if (found) {
        emit(context,EffectEventKind::impact_explosion,no_entity,hit.target,spawn.variant,spawn.attacker,
             hit.x,spawn.y,hit.z);
        end_x=hit.x;
        end_z=hit.z;
        if (spawn.depth>beam_fork_depth && hooks_of(context).random!=nullptr) {
            float nx=0.0F,nz=0.0F;
            hit_normal(hit.world,dir_x,dir_z,nx,nz);
            const std::uint16_t angle=direction_angle(nx,nz);
            const auto r1=next_random(context,0U);
            const auto a1=static_cast<std::uint16_t>(angle+(r1&0x1FFFU)-0x1000U);
            const auto d1=static_cast<std::int16_t>(next_random(context,0U)%3U);
            spawn_beam_fork(context,hit.x+context.angles.sine(a1),spawn.y,hit.z+context.angles.cosine(a1),a1,spawn,d1);
            const auto r2=next_random(context,0U);
            const auto a2=static_cast<std::uint16_t>(angle+(r2&0xFFFU)-0x800U);
            const auto d2=static_cast<std::int16_t>(next_random(context,0U)%3U);
            spawn_beam_fork(context,hit.x+context.angles.sine(a2),spawn.y,hit.z+context.angles.cosine(a2),a2,spawn,d2);
        }
    }
    emit(context,EffectEventKind::beam,no_entity,no_entity,spawn.variant,spawn.attacker,spawn.x,spawn.y,spawn.z,
         RemovalReason::none,spawn.heading,segment_length(spawn.x,spawn.z,end_x,end_z));
    return true;
}

std::size_t apply_splash_damage(ProjectileContext& context,float x,float z,float radius,
                                std::int32_t amount,std::uint8_t attacker,EntityId source) {
    CombatContext& combat=context.combat;
    emit(context,EffectEventKind::splash,source,no_entity,0U,attacker,x,0.0F,z,RemovalReason::none,0U,radius);
    std::array<EntityId,unit_capacity> hits{};
    std::size_t count=0U;
    combat.units.for_each([&](EntityId id,const UnitRecord& unit) {
        if (!unit.alive || count>=hits.size()) return;
        const float dx=unit.x-x;
        const float dz=unit.z-z;
        if (dx*dx+dz*dz<=radius*radius) hits[count++]=id;
    });
    for (std::size_t i=0U; i<count; ++i) {
        (void)apply_unit_damage(combat,hits[i],amount,attacker,false,source);
        if (UnitRecord* unit=combat.units.get(hits[i])) unit->hit_shake+=static_cast<float>(amount/4);
    }
    return count;
}

std::optional<EntityId> spawn_ricochet(ProjectileContext& context,const ShellRecord& from,
                                       float hit_x,float hit_z,std::uint16_t angle,EntityId hit) {
    BulletSpawn spawn;
    spawn.x=hit_x;
    spawn.y=from.y;
    spawn.z=hit_z;
    spawn.heading=angle;
    spawn.speed=from.speed;
    spawn.life=0x70;
    spawn.damage=from.damage;
    spawn.weapon=from.weapon;
    spawn.bullet_kind=from.bullet_kind;
    spawn.trail=from.trail;
    spawn.attacker=no_owner;
    spawn.local=false;
    spawn.ignore=hit;
    return spawn_bullet(context,spawn);
}

FireResult fire_unit_weapon(ProjectileContext& context,EntityId unit_id,WeaponState& weapon,
                            const FireRequest& request) {
    CombatContext& combat=context.combat;
    UnitRecord* unit=combat.units.get(unit_id);
    if (unit==nullptr || !unit->alive) return FireResult::not_ready;
    if (!weapon_auto_fires(weapon.kind) && !request.trigger_pressed) return FireResult::not_ready;
    const std::int32_t delay=weapon_delay(weapon.kind);
    const std::int16_t ammo=weapon.kind<unit->ammo.size() ? unit->ammo[weapon.kind] : std::int16_t{0};
    const bool human=unit->control!=UnitControl::computer;
    if (!(weapon.last_fire_tick+delay<combat.tick && ammo>0)) {
        if (ammo==0 && request.trigger_pressed) {
            if (human) {
                push_sound(combat.sounds,retail::sound_empty_trigger,unit->x,unit->y,unit->z,unit_id,true,true);
            }
            GameplayEvent empty;
            empty.kind=GameplayEventKind::weapon_empty;
            empty.tick=combat.tick;
            empty.subject=unit_id;
            empty.owner=unit->owner;
            combat.events.push(empty);
            return FireResult::empty;
        }
        return FireResult::not_ready;
    }
    if (weapon.kind!=1U && weapon.kind!=3U) return FireResult::not_ready;  // full set: fire_weapon

    const std::uint16_t turret=degrees_to_retail_angle(unit->turret_degrees);
    const float forward_x=context.angles.sine(turret);
    const float forward_z=context.angles.cosine(turret);
    BulletSpawn spawn;
    spawn.x=unit->x+forward_x*retail::muzzle_forward;
    spawn.y=unit->y+retail::muzzle_height;
    spawn.z=unit->z+forward_z*retail::muzzle_forward;
    spawn.heading=direction_angle(forward_x,forward_z);
    // Bit-identical to the pre-module path (velocity = forward * shell_speed);
    // BattleSession hashes velocity_x/z.
    spawn.has_velocity=true;
    spawn.velocity_x=forward_x*retail::shell_speed;
    spawn.velocity_z=forward_z*retail::shell_speed;
    spawn.damage=unit->kind==retail::UnitKind::goliath ? retail::goliath_shell_damage : retail::shell_damage;
    spawn.weapon=weapon.kind;
    spawn.trail=0x0FU;
    spawn.attacker=unit->owner;
    spawn.local=human;
    spawn.ignore=unit_id;
    FireResult result=FireResult::spawn_failed;
    if (const auto shell_id=spawn_bullet(context,spawn)) {
        result=FireResult::fired;
        const std::uint16_t sound=weapon.kind==1U ? retail::sound_cannon : retail::sound_cannon_alt;
        push_sound(combat.sounds,sound,unit->x,unit->y,unit->z,unit_id,!human,human);
        GameplayEvent fired;
        fired.kind=GameplayEventKind::weapon_fired;
        fired.tick=combat.tick;
        fired.subject=unit_id;
        fired.other=*shell_id;
        fired.owner=unit->owner;
        fired.value=weapon.kind;
        fired.x=spawn.x;
        fired.y=spawn.y;
        fired.z=spawn.z;
        combat.events.push(fired);
    }
    weapon.last_fire_tick=combat.tick;
    if (human && weapon.kind<unit->ammo.size()) {
        const bool consumes=weapon.kind>=4U ||
            (!combat.config.free_fire && !combat.config.unlimited_ammo &&
             (unit->flags&retail::flag_free_fire)==0U);
        if (consumes) --unit->ammo[weapon.kind];
    }
    return result;
}

void update_shells(ProjectileContext& context) {
    // Snapshot first: records spawned during this pass update next call.
    std::array<EntityId,shell_capacity> live{};
    std::size_t count=0U;
    for (std::size_t slot=0U; slot<shell_capacity; ++slot) {
        if (context.shells.slot_occupied(slot)) live[count++]=context.shells.slot_id(slot);
    }
    for (std::size_t i=0U; i<count; ++i) {
        ShellRecord* p=context.shells.get(live[i]);
        if (p==nullptr) continue;
        switch (p->effect_kind) {
        case effect_kind::bullet:
        case effect_kind::shell: update_ballistic(context,live[i],*p); break;
        case effect_kind::missile: update_missile(context,live[i],*p); break;
        case effect_kind::nuke: update_nuke(context,live[i],*p); break;
        case effect_kind::grenade: update_grenade(context,live[i],*p); break;
        case effect_kind::beam_fork: update_beam_fork(context,live[i],*p); break;
        default: remove(context,live[i],RemovalReason::none); break;   // not a projectile kind
        }
    }
}

void serialize_projectiles(StateWriter& writer,const ShellPool& pool) {
    writer.u32(static_cast<std::uint32_t>(shell_capacity));
    for (std::size_t slot=0U; slot<shell_capacity; ++slot) {
        writer.u16(pool.slot_generation(slot));
        const bool occupied=pool.slot_occupied(slot);
        writer.boolean(occupied);
        if (!occupied) continue;
        const ShellRecord& p=*pool.get(pool.slot_id(slot));
        writer.u8(p.effect_kind);
        writer.u8(p.weapon);
        writer.u8(p.bullet_kind);
        writer.u8(p.trail);
        writer.f32(p.x); writer.f32(p.y); writer.f32(p.z);
        writer.f32(p.velocity_x); writer.f32(p.velocity_y); writer.f32(p.velocity_z);
        writer.f32(p.accel_x); writer.f32(p.accel_z);
        writer.f32(p.speed); writer.f32(p.travelled); writer.f32(p.range);
        writer.u16(p.heading);
        writer.s16(p.life);
        writer.u8(p.damage);
        writer.u8(p.attacker);
        writer.boolean(p.local);
        writer.id(p.ignore);
        writer.u16(p.spin);
        writer.u16(p.turn);
        writer.boolean(p.active);
        writer.boolean(p.guided);
        writer.boolean(p.wobble);
        writer.s16(p.bursts);
        writer.u8(p.phase);
        writer.s32(p.phase_tick);
        writer.u32(p.blast_hits);
        writer.u8(p.depth);
        writer.u8(p.variant);
    }
}

} // namespace battletanx
