#include "battletanx/combat.hpp"

namespace battletanx {
namespace {

bool campaign_mode(std::uint32_t mode) noexcept { return mode>=3U && mode<=6U; }

GameplayEvent unit_event(GameplayEventKind kind,const CombatContext& context,EntityId subject,
                         const UnitRecord& unit) {
    GameplayEvent event;
    event.kind=kind;
    event.tick=context.tick;
    event.subject=subject;
    event.owner=unit.owner;
    event.x=unit.x;
    event.y=unit.y;
    event.z=unit.z;
    return event;
}

OwnerRecord* owner_at(CombatContext& context,std::uint8_t index) noexcept {
    return index<owner_capacity && context.owners[index].present ? &context.owners[index] : nullptr;
}

} // namespace

std::int32_t scaled_damage(std::int32_t amount,float victim_damage_scale,
                           const float* attacker_armor_scale) noexcept {
    float value=static_cast<float>(amount)*(victim_damage_scale/retail::damage_scale_unit);
    std::int32_t result=static_cast<std::int32_t>(value);
    if (attacker_armor_scale!=nullptr && *attacker_armor_scale!=0.0F) {
        value=static_cast<float>(result)/(*attacker_armor_scale/retail::damage_scale_unit);
        result=static_cast<std::int32_t>(value);
    }
    return result==0 ? 1 : result;
}

DamageResult apply_unit_damage(CombatContext& context,EntityId victim,std::int32_t amount,
                               std::uint8_t attacker,bool direct,EntityId source) {
    UnitRecord* unit=context.units.get(victim);
    if (unit==nullptr) return DamageResult::ignored;
    if (context.config.damage_disabled) return DamageResult::ignored;
    if (context.config.computer_only_damage && unit->control!=UnitControl::computer) {
        return DamageResult::ignored;
    }
    if (!unit->alive) return DamageResult::ignored;
    if ((unit->flags&(retail::flag_invulnerable|retail::flag_campaign_shielded))!=0U) {
        return DamageResult::ignored;
    }
    if ((unit->flags&retail::flag_instant_death)!=0U) {
        destroy_unit(context,victim,attacker);
        return DamageResult::destroyed;
    }
    if (amount==0) return DamageResult::ignored;

    std::uint8_t scale_index=unit->owner;
    if (campaign_mode(context.config.game_mode) && (unit->flags&retail::flag_reserve_unit)!=0U) {
        scale_index=1U;
    }
    const OwnerRecord* scale_owner=owner_at(context,scale_index);
    const float damage_scale=scale_owner!=nullptr ? scale_owner->damage_scale : retail::damage_scale_unit;
    const OwnerRecord* attacking_owner=attacker<owner_capacity ? owner_at(context,attacker) : nullptr;
    std::int32_t remaining=scaled_damage(
        amount,damage_scale,attacking_owner!=nullptr ? &attacking_owner->armor_scale : nullptr);
    (void)direct;  // retail uses it only for the self-hit cooldown event

    if (unit->shield_strength>0) {
        const std::int32_t absorbed=remaining<unit->shield_strength ? remaining : unit->shield_strength;
        unit->shield_strength-=absorbed;
        remaining-=absorbed;
    }
    unit->health-=remaining;
    unit->last_hit_tick=context.tick;

    GameplayEvent damaged=unit_event(GameplayEventKind::unit_damaged,context,victim,*unit);
    damaged.other=source;
    damaged.value=remaining;
    context.events.push(damaged);

    if (unit->health<=0) {
        unit->health=0;
        destroy_unit(context,victim,attacker);
        return DamageResult::destroyed;
    }
    return DamageResult::survived;
}

bool destroy_unit(CombatContext& context,EntityId victim,std::uint8_t killer,bool crushed) {
    UnitRecord* unit=context.units.get(victim);
    if (unit==nullptr || !unit->alive) return false;
    OwnerRecord* owner=owner_at(context,unit->owner);
    const bool campaign=campaign_mode(context.config.game_mode);
    if (owner!=nullptr && !(campaign && (unit->flags&(0x0100U|0x0800U))!=0U)) ++owner->losses;

    if (killer<owner_capacity) {
        OwnerRecord* credited=owner_at(context,killer);
        if (credited!=nullptr && owner!=nullptr && credited->team!=owner->team) {
            ++credited->kills;
            const auto kind=static_cast<std::size_t>(unit->kind);
            std::int32_t score=retail::kill_score[kind];
            if (unit->kind==retail::UnitKind::goliath && (unit->flags&0x0004U)!=0U) {
                score=retail::goliath_flagged_kill_score;
            }
            credited->score+=score;
        } else if (owner!=nullptr) {
            ++owner->team_losses;
        }
    }

    if (crushed) unit->flags|=retail::flag_crushed;
    SoundEvent sound;
    sound.sound_id=crushed ? retail::sound_unit_crushed : retail::sound_unit_destroyed;
    sound.x=unit->x;
    sound.y=unit->y;
    sound.z=unit->z;
    sound.source=victim;
    sound.positional=true;
    context.sounds.push(sound);

    if (owner!=nullptr) {
        owner->live_units=static_cast<std::uint8_t>(owner->live_units-1U);
        if ((unit->flags&retail::flag_squad_member)!=0U && owner->squad_cap>=2U) --owner->squad_cap;
        if (owner->live_units>=6U) owner->live_units=0U;
    }
    unit->alive=false;
    unit->target=no_entity;

    GameplayEvent destroyed=unit_event(GameplayEventKind::unit_destroyed,context,victim,*unit);
    destroyed.value=killer;
    context.events.push(destroyed);
    return true;
}

HitResponse handle_projectile_hit(CombatContext& context,EntityId victim,std::uint8_t amount,
                                  std::uint8_t attacker,bool direct,EntityId projectile) {
    UnitRecord* unit=context.units.get(victim);
    if (unit==nullptr) return HitResponse::pass_through;
    const DamageResult result=apply_unit_damage(context,victim,amount,attacker,direct,projectile);
    unit=context.units.get(victim);
    if (unit!=nullptr && result!=DamageResult::ignored) {
        unit->hit_shake+=static_cast<float>(amount)/4.0F;
    }
    return result==DamageResult::destroyed ? HitResponse::impact_silent : HitResponse::impact;
}

} // namespace battletanx
