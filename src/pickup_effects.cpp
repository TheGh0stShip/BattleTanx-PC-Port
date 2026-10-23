#include "battletanx/pickup_effects.hpp"

namespace battletanx {
namespace {

// apply_pickup.c: secondary stock fields +0x488..+0x4A0 are ammo[4..16]; the
// "notice" word +0x474 is the secondary weapon kind, set to the slot when 0.
struct StockGrant { std::uint8_t kind, slot; std::uint16_t amount; };
inline constexpr std::array<StockGrant,9> stock_grants{{
    {2U,4U,5U},{3U,5U,5U},{1U,6U,1U},{15U,7U,5U},{7U,11U,5U},
    {9U,12U,5U},{10U,13U,2U},{19U,13U,1U},{16U,14U,3U}}};
inline constexpr std::uint8_t stored_health_slot=16U;  // +0x4A0, notice 16
inline constexpr std::uint8_t cloak_stock_slot=15U;    // +0x49E, notice 15

std::int32_t add_ammo(UnitRecord& unit,std::uint8_t slot,std::uint16_t amount,
                      PickupEffectResult& result) noexcept {
    // Retail u16 add with no clamp.
    const auto value=static_cast<std::uint16_t>(static_cast<std::uint16_t>(unit.ammo[slot])+amount);
    unit.ammo[slot]=static_cast<std::int16_t>(value);
    result.ammo_slot=slot;
    result.ammo_delta+=amount;
    return amount;
}

void add_stock(UnitRecord& unit,std::uint8_t slot,std::uint16_t amount,
               PickupEffectResult& result) noexcept {
    add_ammo(unit,slot,amount,result);
    if (unit.secondary_weapon.kind==0U) {
        unit.secondary_weapon.kind=slot;
        result.selected_secondary=true;
    }
    result.status=PickupEffectStatus::applied;
}

void add_health(UnitRecord& unit,float fraction,PickupEffectResult& result) noexcept {
    const std::int32_t before=unit.health;
    unit.health+=static_cast<std::int32_t>(static_cast<float>(unit.max_health)*fraction);
    if (unit.health>unit.max_health) unit.health=unit.max_health;
    result.health_delta=unit.health-before;
}

void primary_ammo(UnitRecord& unit,std::uint16_t small,std::uint16_t large,
                  PickupEffectResult& result) noexcept {
    const std::uint8_t weapon=unit.primary_weapon.kind;
    if (weapon==1U || weapon==3U) add_ammo(unit,weapon,small,result);
    else if (weapon==2U) add_ammo(unit,2U,large,result);
}

void shield(UnitRecord& unit,TimedPowerupState& timed,std::int32_t amount,
            PickupEffectResult& result) noexcept {
    // spawn_effect_kind_17 marker (effect 23, value18 = amount) or +amount on the
    // live marker. An effect-pool failure is not modelled.
    result.effect=timed.shield_live ? PickupEffectKind::shield_added : PickupEffectKind::shield_created;
    if (timed.shield_live) unit.shield_strength+=amount;
    else { unit.shield_strength=amount; timed.shield_live=true; }
    result.shield_delta=amount;
    result.status=PickupEffectStatus::applied;
}

} // namespace

void TimedPowerupState::serialize(StateWriter& writer) const {
    writer.s32(cloak_end_tick);
    writer.s32(cloak_flash);
    writer.s32(value10);
    writer.boolean(shield_live);
    writer.s32(last_fire_tick);
}

bool award_owner_life(OwnerRecord& owner,std::int32_t game_mode) noexcept {
    if (owner.computer) return false;  // +0x1A6 != 0
    // +0x34 is also incremented; it has no native field.
    if (game_mode!=7) ++owner.respawn_bank[0];   // +0x3A
    return true;
}

std::uint8_t add_owner_progress(OwnerRecord& owner,std::int32_t amount,std::int32_t game_mode) noexcept {
    const std::int32_t progress=owner.score+amount;
    const std::int32_t next=progress/progress_threshold;
    const std::int32_t previous=owner.score/progress_threshold;
    std::uint8_t lives=0U;
    // func_8007C700: modes 3..7. One call per crossing regardless of how many
    // thresholds were passed.
    if (previous<next && game_mode>=3 && game_mode<=7 && award_owner_life(owner,game_mode)) lives=1U;
    owner.score=progress;
    return lives;
}

PickupEffectResult apply_pickup_effect_complete(UnitRecord& unit,OwnerRecord& owner,std::uint8_t kind,
                                                const PickupEffectOptions& options,
                                                TimedPowerupState& timed) noexcept {
    PickupEffectResult result;
    result.kind=kind;
    if (!options.base.replay_active && options.announce) {
        result.announced=true;
        result.owner_event=static_cast<std::int32_t>(kind)+11;
        result.lives_awarded+=add_owner_progress(owner,pickup_announce_progress,options.game_mode);
        result.score_delta+=pickup_announce_progress;
    }
    for (const StockGrant& grant : stock_grants) {
        if (grant.kind!=kind) continue;
        result.effect=PickupEffectKind::secondary_stock;
        add_stock(unit,grant.slot,grant.amount,result);
        return result;
    }
    switch (kind) {
    case 0:
        if (unit.health>=unit.max_health) {
            result.effect=PickupEffectKind::stored_health;
            add_stock(unit,stored_health_slot,1U,result);
        } else {
            result.effect=PickupEffectKind::health;
            add_health(unit,0.3F,result);
            result.status=PickupEffectStatus::applied;
        }
        break;
    case 5:
        result.effect=PickupEffectKind::primary_ammo;
        primary_ammo(unit,30U,70U,result);
        if (result.ammo_delta!=0) result.status=PickupEffectStatus::applied;
        break;
    case 11:
        result.effect=PickupEffectKind::health_and_ammo;
        add_health(unit,0.2F,result);
        if (!options.base.ammo_with_crate_disabled) primary_ammo(unit,15U,35U,result);
        result.status=PickupEffectStatus::applied;
        break;
    case 4:
        result.effect=PickupEffectKind::map_reveal;
        unit.flags=static_cast<std::uint16_t>(unit.flags|flag_map_reveal);
        result.status=PickupEffectStatus::applied;
        break;
    case 8:
        shield(unit,timed,shield_large,result);
        break;
    case 18:
        shield(unit,timed,shield_small,result);
        break;
    case 12:
        if (options.cloak_pickups) {
            if (timed.cloak_end_tick!=0) {
                timed.cloak_end_tick+=pickup_cloak_ticks;
                result.effect=PickupEffectKind::cloak_extended;
            } else {
                timed.cloak_flash=pickup_cloak_flash;
                timed.cloak_end_tick=options.tick+pickup_cloak_ticks;
                result.effect=PickupEffectKind::cloak_started;
            }
            result.status=PickupEffectStatus::applied;
        } else {
            result.effect=PickupEffectKind::secondary_stock;
            add_stock(unit,cloak_stock_slot,1U,result);
        }
        break;
    case 13:
        result.effect=PickupEffectKind::extra_life;
        if (award_owner_life(owner,options.game_mode)) {
            ++result.lives_awarded;
            result.status=PickupEffectStatus::applied;
        }
        break;
    case 14:
        result.effect=PickupEffectKind::score;
        result.lives_awarded+=add_owner_progress(owner,pickup_score_bonus,options.game_mode);
        result.score_delta+=pickup_score_bonus;
        result.status=PickupEffectStatus::applied;
        break;
    default:
        result.status=PickupEffectStatus::no_retail_case;
        break;
    }
    return result;
}

void update_timed_powerups(TimedPowerupState& timed,std::int32_t tick) noexcept {
    if (timed.cloak_flash>0) {
        timed.cloak_flash-=0x20;
        if (timed.cloak_flash<0) timed.cloak_flash=0;
    }
    if (timed.cloak_end_tick<tick) {
        timed.cloak_end_tick=0;
        timed.value10=0;
    } else if (timed.cloak_end_tick-7<tick) {
        timed.cloak_flash=(tick-timed.cloak_end_tick)<<5;
    }
}

bool cloak_hides_unit(const TimedPowerupState& timed,std::int32_t tick) noexcept {
    return timed.cloak_end_tick!=0 && !(tick-cloak_fire_reveal_ticks<timed.last_fire_tick);
}

std::uint8_t spawn_grant_random_kind(std::uint32_t roll) noexcept {
    roll&=0x1FU;
    if (roll==0U) return 1U;
    if (roll<=6U) return 2U;
    if (roll<=12U) return 3U;
    if (roll<=14U) return 4U;
    if (roll<=19U) return 7U;
    if (roll<=21U) return 9U;
    if (roll<=23U) return 15U;
    if (roll<=25U) return 12U;
    if (roll<=29U) return 18U;
    return 19U;
}

SpawnGrantResult grant_spawn_weapon(UnitRecord& unit,OwnerRecord& owner,std::uint32_t event,
                                    const PickupEffectOptions& options,TimedPowerupState& timed,
                                    RetailRandom& rng) noexcept {
    SpawnGrantResult result;
    std::optional<std::uint8_t> kind;
    if (event==23U) return result;
    if (event==22U) kind=spawn_grant_random_kind(rng.next_u32());
    else if (event==1U) { if (unit.primary && options.tick<=0) kind=1U; }
    else kind=static_cast<std::uint8_t>(event);
    if (!kind) return result;
    PickupEffectOptions silent=options;
    silent.announce=false;
    result.granted_kind=kind;
    result.effect=apply_pickup_effect_complete(unit,owner,*kind,silent,timed);
    return result;
}

bool spawn_grant_applies(std::int32_t game_mode,std::uint16_t owner_index,
                         std::int32_t controller,bool skip_flag,bool reserve_unit) noexcept {
    switch (game_mode) {
    case 3: case 4: case 5: case 6:
        return owner_index==0U && controller!=9;
    case 0: case 1: case 2: case 9:
        return !(skip_flag && !reserve_unit);
    default:
        return false;
    }
}

std::uint8_t reroll_bank_pickup_kind(std::uint8_t kind,bool enabled,RetailRandom& rng) noexcept {
    if (!enabled) return kind;
    switch (kind) {
    case 5: case 6: case 7: case 9: case 10: case 13: case 14: case 16: break;
    default: return kind;
    }
    static constexpr std::array<std::uint8_t,11> table{0U,0U,0U,3U,3U,2U,2U,4U,8U,12U,15U};
    return table[rng.next_u32()%11U];
}

} // namespace battletanx
