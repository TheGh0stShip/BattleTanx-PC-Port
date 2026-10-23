#include "battletanx/pickup_runtime.hpp"

#include "battletanx/level_spawns.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace battletanx {
namespace {

void add_ammo(UnitRecord& unit,std::size_t slot,std::uint16_t amount) noexcept {
    // Retail ammo is u16 with no clamp; keep the 16-bit wrap.
    const auto value=static_cast<std::uint16_t>(static_cast<std::uint16_t>(unit.ammo[slot])+amount);
    unit.ammo[slot]=static_cast<std::int16_t>(value);
}

void add_health_fraction(UnitRecord& unit,float fraction) noexcept {
    unit.health+=static_cast<std::int32_t>(static_cast<float>(unit.max_health)*fraction);
    if (unit.health>unit.max_health) unit.health=unit.max_health;
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
    return kind==0U || kind==5U || kind==11U;
}

bool pickup_overlaps(const PickupRecord& pickup,const UnitRecord& unit) noexcept {
    // Unit body: axis-aligned bounds of the rotated footprint.
    const float radians=unit.heading_degrees*(std::numbers::pi_v<float>/180.0F);
    const float c=std::cos(radians);
    const float s=std::sin(radians);
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
    const std::uint8_t weapon=unit.primary_weapon.kind;
    switch (kind) {
    case 0:
        // At full health retail stores the pack (+0x4A0); not modelled here.
        if (unit.health<unit.max_health) add_health_fraction(unit,0.3F);
        return true;
    case 11:
        add_health_fraction(unit,0.2F);
        if (!options.ammo_with_crate_disabled) {
            if (weapon==1U || weapon==3U) add_ammo(unit,weapon,15U);
            else if (weapon==2U) add_ammo(unit,2U,35U);
        }
        return true;
    case 5:
        if (weapon==1U || weapon==3U) add_ammo(unit,weapon,30U);
        else if (weapon==2U) add_ammo(unit,2U,70U);
        return true;
    default:
        return false;
    }
}

PickupCollectResult collect_pickups(std::span<PickupRecord> pickups,
                                    std::span<PickupCollector> collectors,
                                    std::int32_t tick,const PickupOptions& options,
                                    GameplayEventQueue& events,SoundEventQueue& sounds) {
    PickupCollectResult result;
    for (PickupRecord& pickup : pickups) {
        if (pickup.removed || !pickup.active || pickup.consumed) continue;
        for (PickupCollector& collector : collectors) {
            UnitRecord* unit=collector.unit;
            if (unit==nullptr || !unit->alive) continue;
            if (static_cast<std::int32_t>(collector.owner_class)==pickup.owner_class) continue;
            if (collector.state_350==2U) continue;
            if (!pickup_overlaps(pickup,*unit)) continue;
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
            break;
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
            if (weights!=nullptr && weights->total!=0U) {
                std::uint32_t roll=rng.next_u32()%weights->total;
                std::size_t i=0U;
                while (i<weights->weights.size() && roll>=weights->weights[i]) {
                    roll-=weights->weights[i++];
                }
                pickup.kind=static_cast<std::uint8_t>(i);
            }
            pickup.active=true;
        }
    }
}

} // namespace battletanx
