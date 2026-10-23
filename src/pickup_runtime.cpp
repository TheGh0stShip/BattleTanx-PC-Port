#include "battletanx/pickup_runtime.hpp"

#include "battletanx/level_spawns.hpp"

#include "battletanx/pickup_effects.hpp"
#include "battletanx/powerup_state.hpp"
#include "battletanx/runtime_math.hpp"

#include <algorithm>

namespace battletanx {
namespace {

const AngleTable& host_angles() {
    // Deterministic polynomial table; callers with the retail table use the
    // AngleTable overload.
    static const AngleTable table=reference_angle_table();
    return table;
}

} // namespace

void PickupWeights::register_permanent(std::uint8_t kind) noexcept {
    if (kind==1U || kind>=weights.size()) return;
    ++weights[kind];
    ++total;
}

bool pickup_kind_allowed(std::uint8_t kind,const PickupOptions& options) noexcept {
    if (kind==0U) return true;
    if (kind==5U) return !options.ammo_with_crate_disabled;
    return options.extra_pickups_enabled;
}

bool pickup_kind_supported(std::uint8_t kind) noexcept {
    // Kinds whose retail case changes only the unit (13, 14 need the owner).
    switch (kind) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 7: case 8: case 9: case 10:
    case 11: case 12: case 15: case 16: case 18: case 19:
        return true;
    default:
        return false;
    }
}

bool pickup_overlaps(const PickupRecord& pickup,const UnitRecord& unit) noexcept {
    return pickup_overlaps(pickup,unit,host_angles());
}

bool pickup_overlaps(const PickupRecord& pickup,const UnitRecord& unit,const AngleTable& angles) noexcept {
    // Unit body: axis-aligned bounds of the rotated footprint (TEMPORARY POLICY;
    // the unit grid-body registration is not traced). Pickup body -35..35.
    const std::uint16_t angle=degrees_to_retail_angle(unit.heading_degrees);
    const float c=angles.cosine(angle);
    const float s=angles.sine(angle);
    const std::array<float,2> lx{static_cast<float>(unit.footprint.min_x),
                                 static_cast<float>(unit.footprint.max_x)};
    const std::array<float,2> lz{static_cast<float>(unit.footprint.min_z),
                                 static_cast<float>(unit.footprint.max_z)};
    float min_x=0.0F,max_x=0.0F,min_z=0.0F,max_z=0.0F;
    bool first=true;
    for (const float px : lx) {
        for (const float pz : lz) {
            const float wx=unit.x+px*c+pz*s;
            const float wz=unit.z-px*s+pz*c;
            if (first) { min_x=max_x=wx; min_z=max_z=wz; first=false; continue; }
            min_x=std::min(min_x,wx); max_x=std::max(max_x,wx);
            min_z=std::min(min_z,wz); max_z=std::max(max_z,wz);
        }
    }
    return min_x<=pickup.x+pickup_half_extent && max_x>=pickup.x-pickup_half_extent &&
           min_z<=pickup.z+pickup_half_extent && max_z>=pickup.z-pickup_half_extent;
}

bool apply_pickup_effect(UnitRecord& unit,std::uint8_t kind,const PickupOptions& options) noexcept {
    // Unit-only view of apply_pickup: no owner progress, no announce, no cloak
    // option (kind 12 stocks slot 15). The shield link is approximated by a
    // positive strength (TEMPORARY POLICY; use pickup_effects for the full state).
    if (!pickup_kind_supported(kind)) return false;
    OwnerRecord scratch_owner;
    TimedPowerupState scratch_timed;
    scratch_timed.shield_live=unit.shield_strength>0;
    PickupEffectOptions effect_options;
    effect_options.base=options;
    effect_options.game_mode=options.game_mode;
    effect_options.announce=false;
    const PickupEffectResult result=apply_pickup_effect_complete(unit,scratch_owner,kind,effect_options,scratch_timed);
    return result.status!=PickupEffectStatus::no_retail_case;
}

PickupCollectResult collect_pickups(std::span<PickupRecord> pickups,
                                    std::span<PickupCollector> collectors,
                                    std::int32_t tick,const PickupOptions& options,
                                    GameplayEventQueue& events,SoundEventQueue& sounds) {
    PickupCollectResult result;
    // Retail order: each unit's drive update walks its own contacts, so the
    // collector loop is the outer one.
    for (PickupCollector& collector : collectors) {
        UnitRecord* unit=collector.unit;
        if (unit==nullptr || !unit->alive) continue;
        for (PickupRecord& pickup : pickups) {
            if (pickup.removed || !pickup.active) continue;
            const powerup::ContactQuery query{
                .pickup_consumed=pickup.consumed,
                .contact_is_unit=true,
                .collector_class=collector.owner_class,
                .pickup_owner_class=pickup.owner_class,
                .unit_mode=collector.state_350,
            };
            if (!powerup::pickup_contact_eligible(query) || !pickup_overlaps(pickup,*unit)) continue;
            pickup.consumed=true;
            pickup.active=false;
            ++result.collected;
            const bool applied=apply_pickup_effect(*unit,pickup.kind,options);
            if (!applied) ++result.unsupported;
            if (!options.replay_active) {
                sounds.push(SoundEvent{retail::sound_pickup,unit->x,unit->y,unit->z,
                                       collector.id,true,unit->control==UnitControl::player});
            }
            GameplayEvent event;
            event.kind=GameplayEventKind::pickup_collected;
            event.tick=tick;
            event.subject=pickup.id;
            event.other=collector.id;
            event.owner=unit->owner;
            event.value=applied ? pickup.kind : (pickup.kind|pickup_unsupported_flag);
            event.x=pickup.x;
            event.z=pickup.z;
            events.push(event);
        }
    }
    return result;
}

void update_pickups(std::span<PickupRecord> pickups,std::int32_t tick,
                    const PickupOptions& options,RetailRandom& rng,
                    const PickupWeights* weights) {
    const bool campaign=campaign_mode(options.game_mode);
    for (PickupRecord& pickup : pickups) {
        if (pickup.removed) continue;
        if ((pickup.consumed && (campaign || !options.respawn_enabled)) ||
            (pickup.expire_tick!=0 && (pickup.consumed || tick>=pickup.expire_tick))) {
            pickup.removed=true;
            pickup.active=false;
            continue;
        }
        if (pickup.expire_tick!=0) continue;
        if (pickup.consumed) {
            pickup.consumed=false;
            pickup.active=false;
            const std::uint32_t delay=rng.next_u32()%pickup_respawn_range;
            pickup.respawn_tick=tick+static_cast<std::int32_t>(delay)+pickup_respawn_base;
            continue;
        }
        if (pickup.respawn_tick!=0 && pickup.respawn_tick<tick) {
            pickup.respawn_tick=0;
            if (weights!=nullptr) {
                const powerup::WeightRoll roll=powerup::roll_weighted_kind(*weights,rng);
                if (roll.kind) pickup.kind=*roll.kind;
            }
            pickup.active=true;
        }
    }
}

} // namespace battletanx
