#include "battletanx/projectile_runtime.hpp"

#include "battletanx/geometry.hpp"
#include "battletanx/runtime_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace battletanx {
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
    return std::hypot(x1-x0,z1-z0);
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
                push_sound(combat.sounds,retail::sound_empty_trigger,unit->x,unit->y,unit->z,unit_id,
                           true,true);
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
    if (weapon.kind!=1U && weapon.kind!=3U) return FireResult::not_ready;  // other weapons unrecovered here

    const std::uint16_t turret=degrees_to_retail_angle(unit->turret_degrees);
    const float forward_x=context.angles.sine(turret);
    const float forward_z=context.angles.cosine(turret);
    FireResult result=FireResult::spawn_failed;
    if (const auto shell_id=context.shells.allocate()) {
        ShellRecord& shell=*context.shells.get(*shell_id);
        shell.x=unit->x+forward_x*retail::muzzle_forward;
        shell.y=unit->y+retail::muzzle_height;
        shell.z=unit->z+forward_z*retail::muzzle_forward;
        shell.velocity_x=forward_x*retail::shell_speed;
        shell.velocity_z=forward_z*retail::shell_speed;
        shell.heading=direction_angle(forward_x,forward_z);
        shell.damage=unit->kind==retail::UnitKind::goliath ? retail::goliath_shell_damage
                                                           : retail::shell_damage;
        shell.attacker=unit->owner;
        shell.local=human;
        shell.ignore=unit_id;
        const float end_x=shell.x+context.angles.sine(shell.heading)*retail::shell_range_probe;
        const float end_z=shell.z+context.angles.cosine(shell.heading)*retail::shell_range_probe;
        if (const auto hit=world_segment(context.world,shell.x,shell.z,end_x,end_z)) {
            shell.range=hit->distance;
        }
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
        fired.x=shell.x;
        fired.y=shell.y;
        fired.z=shell.z;
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
    CombatContext& combat=context.combat;
    context.shells.for_each([&](EntityId shell_id,ShellRecord& shell) {
        if (--shell.life<=0) { context.shells.release(shell_id); return; }
        const float end_x=shell.x+shell.velocity_x*context.frame_scale;
        const float end_z=shell.z+shell.velocity_z*context.frame_scale;
        shell.travelled+=shell.speed*context.frame_scale;
        const bool static_phase=shell.travelled>shell.range;
        std::optional<UnitSegmentHit> unit_hit;
        std::optional<SegmentHit> world_hit;
        if (static_phase) {
            world_hit=world_segment(context.world,shell.x,shell.z,end_x,end_z);
        } else {
            unit_hit=find_unit_segment_hit(combat.units,context.angles,shell.x,shell.z,end_x,end_z,shell.ignore);
        }
        if (!unit_hit && !world_hit) {
            shell.x=end_x;
            shell.z=end_z;
            if (context.world.inside && !context.world.inside(end_x,end_z)) context.shells.release(shell_id);
            return;
        }
        HitResponse response=HitResponse::impact;
        float hit_x=end_x;
        float hit_z=end_z;
        EntityId target{};
        if (unit_hit) {
            target=unit_hit->unit;
            const float length=std::sqrt((end_x-shell.x)*(end_x-shell.x)+(end_z-shell.z)*(end_z-shell.z));
            const float t=length>0.0F ? unit_hit->distance/length : 0.0F;
            hit_x=shell.x+(end_x-shell.x)*t;
            hit_z=shell.z+(end_z-shell.z)*t;
            response=handle_projectile_hit(combat,target,shell.damage,shell.attacker,shell.local,shell_id);
            if (response==HitResponse::pass_through) {
                shell.x=end_x;
                shell.z=end_z;
                return;
            }
        } else {
            hit_x=world_hit->x;
            hit_z=world_hit->z;
        }
        GameplayEvent impact;
        impact.kind=GameplayEventKind::projectile_impact;
        impact.tick=combat.tick;
        impact.subject=shell_id;
        impact.other=target;
        impact.owner=shell.attacker;
        impact.value=static_cast<std::int32_t>(response);
        impact.x=hit_x;
        impact.y=shell.y;
        impact.z=hit_z;
        combat.events.push(impact);
        if (response!=HitResponse::impact_silent) {
            SoundEvent sound;
            sound.sound_id=unit_hit ? retail::sound_hit_unit : retail::sound_hit_other;
            sound.x=hit_x;
            sound.y=shell.y;
            sound.z=hit_z;
            sound.source=shell_id;
            sound.positional=true;
            combat.sounds.push(sound);
        }
        context.shells.release(shell_id);
    });
}

} // namespace battletanx
