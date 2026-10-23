#include "battletanx/powerup_state.hpp"

#include <algorithm>
#include <cstddef>

namespace battletanx::powerup {

KindSource pickup_kind_source(std::uint8_t kind) noexcept {
    switch (kind) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 7: case 8: case 9: case 10:
    case 12: case 13: case 14: case 15: case 22:
        return KindSource::world_item;
    case 11:
        return KindSource::wreck_drop;
    case 16: case 18: case 19:
        return KindSource::spawn_grant_only;
    default:
        return KindSource::unused;
    }
}

std::uint8_t retail_unit_mode(UnitControl control,bool squad_follower) noexcept {
    if (squad_follower) return unit_mode_follower;
    return control==UnitControl::computer ? unit_mode_computer_leader : unit_mode_player;
}

bool pickup_contact_eligible(const ContactQuery& query) noexcept {
    return !query.pickup_consumed && query.contact_is_unit &&
           static_cast<std::int32_t>(query.collector_class)!=query.pickup_owner_class &&
           query.unit_mode!=unit_mode_follower;
}

bool unit_targetable(const TargetabilityInput& input) noexcept {
    if (input.value10!=0) return false;
    if (input.carrying_pickup) return true;
    const std::int64_t tick=input.tick;
    if (tick-mine_untargetable_ticks<input.last_mine_tick) return false;
    if (input.cloak_end_tick!=0) return tick-cloak_fire_reveal_ticks<input.last_fire_tick;
    return true;
}

TargetabilityInput targetability_from(const TimedPowerupState& timed,std::int32_t tick,
                                      bool carrying_pickup,std::int32_t last_mine_tick) noexcept {
    return TargetabilityInput{
        .tick=tick,
        .value10=timed.value10,
        .carrying_pickup=carrying_pickup,
        .last_mine_tick=last_mine_tick,
        .cloak_end_tick=timed.cloak_end_tick,
        .last_fire_tick=timed.last_fire_tick,
    };
}

ShieldAbsorb absorb_shield_damage(TimedPowerupState& timed,UnitRecord& unit,std::int32_t amount) noexcept {
    ShieldAbsorb result;
    result.remaining_damage=amount;
    if (!timed.shield_live || amount<=0) return result;
    if (unit.shield_strength>=amount) {
        unit.shield_strength-=amount;
        result.absorbed=amount;
        result.remaining_damage=0;
    } else {
        result.absorbed=unit.shield_strength;
        result.remaining_damage=amount-unit.shield_strength;
        unit.shield_strength=0;
    }
    result.depleted=unit.shield_strength==0;
    return result;
}

bool weapon_consumes_ammo(std::uint8_t weapon_kind,std::uint8_t unit_mode,
                          bool unit_flag_40,const AmmoRules& rules) noexcept {
    if (rules.cloak_pickups) return weapon_kind>=4U;
    if (unit_mode==unit_mode_follower) return false;
    return weapon_kind>=4U || (!rules.global_infinite && !rules.unlimited_ammo && !unit_flag_40);
}

WeightRoll roll_weighted_kind(const PickupWeights& weights,RetailRandom& rng) noexcept {
    WeightRoll result;
    if (weights.total==0U) return result;
    result.rng_drawn=true;
    std::uint32_t roll=rng.next_u32()%weights.total;
    std::size_t i=0U;
    while (i<weights.weights.size() && roll>=weights.weights[i]) roll-=weights.weights[i++];
    if (i<weights.weights.size()) result.kind=static_cast<std::uint8_t>(i);
    return result;
}

void reset_weights(PickupWeights& weights) noexcept {
    weights.weights.fill(0U);
    weights.total=0U;
}

DistributionResult distribute_pickups_interleaved(
    std::span<const PickupPlacement> spots,
    const std::array<std::int32_t,pickup_spawn_count_kinds>& counts,
    std::int32_t game_mode,bool reroll_option,RetailRandom& rng) {
    DistributionResult result;
    if (campaign_mode(game_mode)) {
        result.status=DistributionStatus::campaign_mode;
        return result;
    }
    for (const std::int32_t count : counts) {
        if (count<0) {
            result.status=DistributionStatus::negative_count;
            return result;
        }
        result.total+=count;
    }
    const std::size_t spot_count=std::min(spots.size(),max_random_pickup_spots);
    if (result.total==0) return result;
    if (spot_count==0U) {
        result.status=DistributionStatus::no_spots;
        return result;
    }
    if (static_cast<std::int64_t>(spot_count)<result.total) {
        result.status=DistributionStatus::not_enough_spots;
        return result;
    }
    // Single implementation: level_spawns::distribute_random_pickups (retail
    // order verified by X5b). The checks above only add status reporting.
    LevelSpawnPlan plan;
    plan.random_distribution_allowed=true;
    plan.random_spots.assign(spots.begin(),spots.begin()+static_cast<std::ptrdiff_t>(spot_count));
    result.placed=distribute_random_pickups(plan,counts,rng,reroll_option);
    return result;
}

std::optional<PickupRecord> make_wreck_drop(EntityId id,float x,float z,std::int32_t tick,
                                            std::uint8_t victim_class,const PickupOptions& options) noexcept {
    if (!pickup_kind_allowed(wreck_drop_kind,options)) return std::nullopt;
    return PickupRecord{
        .id=id,
        .kind=wreck_drop_kind,
        .x=x,
        .z=z,
        .active=true,
        .consumed=false,
        .respawn_tick=0,
        .expire_tick=tick+wreck_drop_ticks,
        .owner_class=static_cast<std::int32_t>(victim_class),
        .removed=false,
    };
}

PickupLifecycle pickup_lifecycle(const PickupRecord& pickup,const PickupOptions& options) noexcept {
    if (pickup.expire_tick!=0) return PickupLifecycle::timed;
    if (campaign_mode(options.game_mode) || !options.respawn_enabled) return PickupLifecycle::removed_on_collect;
    return PickupLifecycle::respawns;
}

EffectOutcome apply_effect_request(const EffectRequest& request,UnitRecord& unit,OwnerRecord& owner,
                                   TimedPowerupState& timed,const PickupEffectOptions& options) noexcept {
    EffectOutcome outcome;
    outcome.request=request;
    const std::int32_t health=unit.health;
    const auto ammo=unit.ammo;
    const std::uint8_t secondary=unit.secondary_weapon.kind;
    const std::int32_t shield=unit.shield_strength;
    const bool shield_live=timed.shield_live;
    const std::uint16_t flags=unit.flags;
    const std::int32_t cloak_end=timed.cloak_end_tick;
    const std::int32_t cloak_flash=timed.cloak_flash;
    const std::int32_t score=owner.score;
    const std::int16_t bank=owner.respawn_bank[0];

    PickupEffectOptions effective=options;
    effective.announce=options.announce && request.announce;
    outcome.result=apply_pickup_effect_complete(unit,owner,request.kind,effective,timed);

    outcome.unit.health=static_cast<std::int32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(unit.health)-health,INT32_MIN,INT32_MAX));
    for (std::size_t i=0U; i<ammo.size(); ++i) {
        // u16 wrap is preserved in the record; the delta is the modular difference.
        outcome.unit.ammo[i]=static_cast<std::uint16_t>(static_cast<std::uint16_t>(unit.ammo[i])-
                                                        static_cast<std::uint16_t>(ammo[i]));
    }
    if (secondary!=unit.secondary_weapon.kind) outcome.unit.secondary_selected=unit.secondary_weapon.kind;
    outcome.unit.shield=unit.shield_strength-shield;
    outcome.unit.shield_created=!shield_live && timed.shield_live;
    outcome.unit.flags_set=static_cast<std::uint16_t>(unit.flags&~flags);
    outcome.unit.cloak_end_tick_delta=timed.cloak_end_tick-cloak_end;
    if (timed.cloak_flash!=cloak_flash) outcome.unit.cloak_flash_set=timed.cloak_flash;
    outcome.owner.score=owner.score-score;
    outcome.owner.lives=outcome.result.lives_awarded;
    outcome.owner.respawn_bank0=static_cast<std::int16_t>(owner.respawn_bank[0]-bank);
    outcome.owner.timed_event=outcome.result.owner_event;
    outcome.play_sound=outcome.result.announced;
    return outcome;
}

std::vector<EffectOutcome> collect_with_effects(std::span<PickupRecord> pickups,std::span<const Collector> collectors,
                                                const PickupEffectOptions& options,
                                                GameplayEventQueue& events,SoundEventQueue& sounds) {
    std::vector<EffectOutcome> outcomes;
    for (const Collector& collector : collectors) {
        UnitRecord* unit=collector.unit;
        if (unit==nullptr || collector.owner==nullptr || collector.timed==nullptr || !unit->alive) continue;
        for (PickupRecord& pickup : pickups) {
            if (pickup.removed || !pickup.active) continue;
            const ContactQuery query{
                .pickup_consumed=pickup.consumed,
                .contact_is_unit=true,
                .collector_class=collector.owner_class,
                .pickup_owner_class=pickup.owner_class,
                .unit_mode=collector.unit_mode,
            };
            if (!pickup_contact_eligible(query) || !pickup_overlaps(pickup,*unit)) continue;
            pickup.consumed=true;
            pickup.active=false;
            const EffectRequest request{.pickup=pickup.id,.collector=collector.id,.kind=pickup.kind,.announce=true};
            EffectOutcome outcome=apply_effect_request(request,*unit,*collector.owner,*collector.timed,options);
            if (outcome.play_sound) {
                sounds.push(SoundEvent{retail::sound_pickup,unit->x,unit->y,unit->z,
                                       collector.id,true,unit->control==UnitControl::player});
            }
            GameplayEvent event;
            event.kind=GameplayEventKind::pickup_collected;
            event.tick=options.tick;
            event.subject=pickup.id;
            event.other=collector.id;
            event.owner=unit->owner;
            event.value=outcome.result.status==PickupEffectStatus::no_retail_case ?
                        (pickup.kind|pickup_unsupported_flag) : pickup.kind;
            event.x=pickup.x;
            event.z=pickup.z;
            events.push(event);
            outcomes.push_back(outcome);
            if (!unit->alive) break;
        }
    }
    return outcomes;
}

} // namespace battletanx::powerup
