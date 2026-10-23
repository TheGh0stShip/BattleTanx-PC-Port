#include "battletanx/enemy_control.hpp"

#include "battletanx/runtime_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace battletanx {
namespace {

// The AI command paths branch on "kind == 1" for the tank cone and the hull
// aim axis; the port's UnitKind::tank is the hull-driven tank class.
bool ai_tank_path(const UnitRecord& unit) noexcept {
    return unit.kind==retail::UnitKind::tank;
}

float squared_xz(const UnitRecord& a,float x,float z) noexcept {
    const float dx=a.x-x;
    const float dz=a.z-z;
    return dx*dx+dz*dz;
}

// Horizontal distance; sqrt is an IEEE basic operation, so it is
// deterministic across conforming hosts.
float xz_distance(float ax,float az,float bx,float bz) noexcept {
    const float dx=ax-bx;
    const float dz=az-bz;
    return std::sqrt(dx*dx+dz*dz);
}

} // namespace

bool unit_is_targetable(const UnitRecord& unit,std::int32_t tick) noexcept {
    // func_8009FB8C: (now - 0x5A) >= +0x4B0 spawn stamp.
    return unit.alive && tick-enemy_retail::spawn_grace_ticks>=unit.spawn_tick;
}

EntityId find_nearest_enemy_unit(const UnitPool& units,EntityId self,std::int32_t tick,
                                 float max_range) {
    const UnitRecord* searcher=units.get(self);
    if (searcher==nullptr) return no_entity;
    float best=enemy_retail::nearest_initial_best;
    EntityId nearest=no_entity;
    units.for_each([&](EntityId id,const UnitRecord& unit) {
        if (id==self || unit.team==searcher->team) return;
        if (!unit_is_targetable(unit,tick)) return;
        const float distance=squared_xz(unit,searcher->x,searcher->z);
        if (distance<best) {
            best=distance;
            nearest=id;
        }
    });
    // (f32)(s32)((f32)(range*range) * 0.92f) < best -> none.
    const float limit_f=max_range*max_range*enemy_retail::nearest_range_scale;
    const float limit=limit_f>=2147483520.0F ? 2147483520.0F
                                             : static_cast<float>(static_cast<std::int32_t>(limit_f));
    return limit<best ? no_entity : nearest;
}

bool target_still_valid(const UnitPool& units,EntityId self,EntityId target,std::int32_t tick) {
    if (target==self) return false;
    const UnitRecord* me=units.get(self);
    const UnitRecord* other=units.get(target);
    if (me==nullptr || other==nullptr) return false;
    if (other->team==me->team) return false;
    return unit_is_targetable(*other,tick);
}

std::int32_t heading_error_to(float heading_degrees,float from_x,float from_z,
                              float to_x,float to_z) noexcept {
    const std::uint16_t heading=degrees_to_retail_angle(heading_degrees);
    const std::uint16_t bearing=direction_angle(to_x-from_x,to_z-from_z);
    return angle_delta(heading,bearing);
}

DriveInput compute_enemy_drive(const UnitRecord& self,float goal_x,float goal_z,
                               const AngleTable& /*angles*/,const EnemyDriveTuning& tuning,
                               DriveTracker* tracker) {
    DriveInput out{};
    const std::int32_t angle=heading_error_to(self.heading_degrees,self.x,self.z,goal_x,goal_z);
    const std::int32_t magnitude=std::abs(angle);
    const std::int32_t limit=ai_tank_path(self) ? enemy_retail::drive_cone_tank
                                                : enemy_retail::drive_cone_other;
    float align=0.0F;
    if (magnitude<limit) {
        const auto num=static_cast<float>(limit-magnitude);
        const auto den=static_cast<float>(limit);
        align=(num*(num*num))/(den*(den*den));
    }
    float throttle=0.0F;
    // Chase mode uses the far distance (16000) for the park test.
    const bool near=enemy_retail::drive_far_distance<tuning.close_range;
    if (!near) {
        throttle=align;
        if (tracker!=nullptr) {
            if (tracker->approaching && tracker->distance<enemy_retail::drive_blend_distance) {
                throttle=(align+(1.0F-tracker->distance/enemy_retail::drive_blend_distance))/2.0F;
            }
            const float current=xz_distance(self.x,self.z,goal_x,goal_z);
            tracker->delta=current-tracker->distance;
            tracker->distance=current;
            if (tracker->delta<0.0F) tracker->approaching=true;
        }
    }
    float turn=(1.0F-align)*enemy_retail::drive_turn_scale;
    if (angle>0) turn=-turn;
    out.rotation=turn;
    out.throttle=throttle*enemy_retail::drive_throttle_scale*tuning.speed/
                 enemy_retail::drive_speed_divisor;
    return out;
}

EnemyAim compute_enemy_aim(const UnitRecord& self,float target_x,float target_z) {
    EnemyAim result{};
    const bool tank=ai_tank_path(self);
    const float facing=tank ? self.heading_degrees : self.turret_degrees;
    result.angle_error=heading_error_to(facing,self.x,self.z,target_x,target_z);
    const std::int32_t magnitude=std::abs(result.angle_error);
    float rate=magnitude<enemy_retail::aim_saturation
                   ? static_cast<float>(magnitude)/static_cast<float>(enemy_retail::aim_saturation)
                   : 1.0F;
    result.in_fire_window=magnitude<enemy_retail::fire_aim_window;
    if (tank) {
        rate*=enemy_retail::aim_hull_scale;
        result.hull_rotation=result.angle_error<0 ? rate : -rate;
    } else {
        if ((self.flags&enemy_retail::flag_ai_class4)==0U) rate*=enemy_retail::aim_turret_scale;
        result.aim=result.angle_error<0 ? -rate : rate;
    }
    return result;
}

FireDecision decide_enemy_fire(const UnitRecord& self,const UnitRecord& target,
                               std::int32_t aim_error,float distance,std::int32_t tick,
                               std::int32_t last_fire_tick,const EnemyFirePolicy& policy,
                               RetailRandom& rng) {
    FireDecision decision{};
    if (std::abs(aim_error)>=enemy_retail::fire_aim_window) return decision;
    if (!(distance<=policy.engagement_range)) return decision;
    const bool cooling=tick-last_fire_tick<policy.cooldown_ticks;
    if (cooling && !ai_tank_path(self)) return decision;

    bool fire=true;
    bool special=false;
    if (policy.allow_specials) {
        const float span=policy.mission_minutes*static_cast<float>(enemy_retail::ticks_per_minute);
        float ramp=span>0.0F ? static_cast<float>(tick)/span : 1.0F;
        ramp=std::clamp(ramp,0.0F,1.0F);
        if ((target.flags&enemy_retail::flag_ai_class4)!=0U) {
            if (rng.next_float(1.0F)<enemy_retail::chance_vs_class4_slope*ramp+
                                         enemy_retail::chance_vs_class4_base) {
                fire=false; special=true;
                decision.special_slot=enemy_retail::special_slot_vs_class4;
            }
        } else if ((self.flags&enemy_retail::flag_ai_class4)!=0U) {
            if (rng.next_float(1.0F)<enemy_retail::chance_class4_slope*ramp+
                                         enemy_retail::chance_class4_base) {
                fire=false; special=true;
                decision.special_slot=enemy_retail::special_slot_class4;
            }
        }
    }
    if (special && cooling) { fire=true; special=false; }
    decision.fire=fire;
    decision.special=special;
    decision.stamp_last_fire=!cooling && (fire || special);
    return decision;
}

std::size_t EnemyController::update(const UnitPool& units,const AngleTable& angles,
                                    RetailRandom& rng,std::int32_t tick,
                                    std::span<EnemyCommand> out) {
    std::size_t written=0U;
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=units.slot_id(slot);
        SlotState& state=state_[slot];
        if (state.unit!=id) state=SlotState{id,no_entity,{},retail::never_fired_tick};
        if (!id.valid()) continue;
        const UnitRecord& unit=*units.get(id);
        if (!unit.alive || unit.control!=UnitControl::computer) continue;
        if (written>=out.size()) break;

        switch (config_.goal_policy) {
        case EnemyGoalPolicy::chase_nearest_enemy_temporary:
            if (!target_still_valid(units,id,state.target,tick)) {
                state.target=find_nearest_enemy_unit(units,id,tick,config_.acquire_range);
                state.tracker=DriveTracker{};
            }
            break;
        }

        EnemyCommand& command=out[written++];
        command=EnemyCommand{};
        command.unit=id;
        command.target=state.target;
        const UnitRecord* target=units.get(state.target);
        if (target==nullptr) continue;

        const float distance=xz_distance(unit.x,unit.z,target->x,target->z);
        const EnemyAim aim=compute_enemy_aim(unit,target->x,target->z);
        const bool parked=config_.park_in_engagement_range &&
                          distance<=enemy_retail::engagement_range;
        if (parked) {
            command.input.rotation=aim.hull_rotation;
        } else {
            command.input=compute_enemy_drive(unit,target->x,target->z,angles,config_.drive,
                                              &state.tracker);
        }
        command.input.aim=aim.aim;
        const FireDecision fire=decide_enemy_fire(unit,*target,aim.angle_error,distance,tick,
                                                  state.last_fire_tick,config_.fire,rng);
        command.fire_decided=fire.fire || fire.special;
        command.input.fire=command.input.fire_pressed=fire.fire;
        command.input.special=command.input.special_pressed=fire.special;
        if (fire.stamp_last_fire) state.last_fire_tick=tick;
    }
    return written;
}

} // namespace battletanx
