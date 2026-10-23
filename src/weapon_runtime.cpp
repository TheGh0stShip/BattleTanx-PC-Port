#include "battletanx/weapon_runtime.hpp"

#include "battletanx/runtime_math.hpp"

#include <cmath>

namespace battletanx {
namespace {

constexpr std::int32_t shield_ticks=900;
constexpr std::int32_t multiplayer_special_delay=20;
constexpr std::int32_t flash_ticks=0xF0;
constexpr std::uint16_t flag_powered=0x2000U;
constexpr std::uint16_t flag_fast_cannon=0x0004U;
constexpr std::uint8_t muzzle_flash_variant=0x1DU;
constexpr std::int32_t decoy_window_ticks=0x5A;
constexpr float decoy_height=50.0F;
constexpr std::uint16_t sound_missile=0x0AU;
constexpr std::uint16_t sound_beam=9U;
constexpr std::uint16_t sound_nuke_fire=0x2DU;
constexpr std::uint16_t sound_gun=0x22U;
constexpr std::uint16_t sound_spread=0x0AU;

bool human(const UnitRecord& unit) noexcept { return unit.control!=UnitControl::computer; }
bool powered(const UnitRecord& unit) noexcept { return (unit.flags&flag_powered)!=0U; }

// Binds the WeaponContext collaborators to base.hooks for one call.
class HookScope {
public:
    explicit HookScope(WeaponContext& context) : base_(context.base), previous_(context.base.hooks) {
        hooks_.effects=&context.effects;
        hooks_.random=&context.random;
        hooks_.object_response=context.object_response;
        hooks_.missile_control=context.missile_control;
        if (previous_!=nullptr) {
            hooks_.external_hit=previous_->external_hit;
            hooks_.external_response=previous_->external_response;
        }
        WeaponRuntimeState* state=&context.state;
        hooks_.proximity_recoil=[state](EntityId unit) {
            if (unit.slot<state->recoil.size()) state->recoil[unit.slot]-=6.0F;
        };
        base_.hooks=&hooks_;
    }
    ~HookScope() { base_.hooks=previous_; }
    HookScope(const HookScope&)=delete;
    HookScope& operator=(const HookScope&)=delete;
private:
    ProjectileContext& base_;
    const ProjectileHooks* previous_;
    ProjectileHooks hooks_{};
};

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
                 std::uint8_t variant,std::uint8_t owner,float x,float y,float z) {
    EffectEvent event;
    event.kind=kind;
    event.tick=context.base.combat.tick;
    event.source=source;
    event.other=other;
    event.variant=variant;
    event.owner=owner;
    event.gameplay_index=static_cast<std::uint32_t>(context.base.combat.events.events().size());
    event.x=x;
    event.y=y;
    event.z=z;
    context.effects.push(event);
}

struct Muzzle {
    float x{}, y{}, z{};
    float forward_x{}, forward_z{};
    std::uint16_t heading{};
};

// func_800A7D10 with the turret/hull matrix: rotate the offset by the frame.
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

void fired_event(WeaponContext& context,EntityId unit_id,const UnitRecord& unit,std::uint8_t weapon,
                 EntityId projectile,float x,float y,float z) {
    GameplayEvent fired;
    fired.kind=GameplayEventKind::weapon_fired;
    fired.tick=context.base.combat.tick;
    fired.subject=unit_id;
    fired.other=projectile;
    fired.owner=unit.owner;
    fired.value=weapon;
    fired.x=x;
    fired.y=y;
    fired.z=z;
    context.base.combat.events.push(fired);
}

void flash(WeaponContext& context,EntityId unit_id,const UnitRecord& unit) {
    if (unit.control!=UnitControl::computer) context.state.flash_timer[unit_id.slot]=flash_ticks;
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

} // namespace

void WeaponRuntimeState::serialize(StateWriter& writer) const {
    writer.u32(static_cast<std::uint32_t>(unit_capacity));
    for (std::size_t i=0U; i<unit_capacity; ++i) {
        writer.u8(hull_side[i]);
        writer.s32(last_mine_tick[i]);
        writer.s32(shield_end_tick[i]);
        writer.s32(flash_timer[i]);
        writer.f32(recoil[i]);
    }
}

bool decoy_allows_targeting(const WeaponRuntimeState& state,std::size_t unit_slot,std::int32_t tick) noexcept {
    if (unit_slot>=unit_capacity) return true;
    return tick-decoy_window_ticks>=state.last_mine_tick[unit_slot];
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
    if (def->deployable && context.deployables==nullptr) return WeaponFireStatus::unsupported;

    HookScope scope(context);
    const AngleTable& angles=context.base.angles;
    const std::size_t slot=unit_id.slot;
    const bool local=human(*unit);
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
        context.state.flash_timer[slot]=flash_ticks;
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
        BulletSpawn spawn;
        spawn.x=m.x;
        spawn.y=m.y;
        spawn.z=m.z;
        spawn.heading=m.heading;
        spawn.speed=def->speed;
        // Retail velocity = frame forward * speed (fire_unit_weapon.c:158-166).
        spawn.has_velocity=true;
        spawn.velocity_x=m.forward_x*def->speed;
        spawn.velocity_z=m.forward_z*def->speed;
        spawn.life=def->life;
        spawn.damage=unit->kind==retail::UnitKind::goliath ? def->heavy_damage : def->damage;
        spawn.weapon=def->id;
        spawn.bullet_kind=def->id==2U ? std::uint8_t{1U} : std::uint8_t{0U};
        spawn.trail=def->trail;
        spawn.attacker=unit->owner;
        spawn.local=local;
        spawn.ignore=unit_id;
        const auto id=spawn_bullet(context.base,spawn);
        if (!id) { status=WeaponFireStatus::spawn_failed; break; }
        if (def->id==2U) {
            push_sound(combat.sounds,sound_gun,unit->x,unit->y,unit->z,unit_id,true,local);
        } else {
            // Local units: listener call func_800796F0; others positional at 1.2.
            push_sound(combat.sounds,def->fire_sound,unit->x,unit->y,unit->z,unit_id,!local,local);
            push_effect(context,EffectEventKind::muzzle_flash,unit_id,*id,muzzle_flash_variant,unit->owner,m.x,m.y,m.z);
        }
        flash(context,unit_id,*unit);
        fired_event(context,unit_id,*unit,def->id,*id,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::spread_shell: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,turret_offset);
        context.state.recoil[slot]+=def->recoil;
        const float a1=m.forward_x*def->speed;
        const float a0=m.forward_z*def->speed;
        const float a2=-a1;
        const float a3=a0;
        auto shot=[&](float vx,float vz,float ax,float az) {
            ShellSpawn spawn;
            spawn.x=m.x;
            spawn.y=m.y;
            spawn.z=m.z;
            spawn.velocity_x=vx;
            spawn.velocity_z=vz;
            spawn.accel_x=ax;
            spawn.accel_z=az;
            spawn.heading=m.heading;
            spawn.damage=def->damage;
            spawn.weapon=def->id;
            spawn.trail=def->trail;
            spawn.attacker=unit->owner;
            spawn.local=local;
            spawn.ignore=unit_id;
            return spawn_shell(context.base,spawn);
        };
        std::optional<EntityId> first;
        if (powered(*unit)) {
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
        push_sound(combat.sounds,sound_spread,unit->x,unit->y,unit->z,unit_id,true,local);
        flash(context,unit_id,*unit);
        fired_event(context,unit_id,*unit,def->id,*first,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::beam: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,turret_offset);
        BeamSpawn beam;
        beam.x=m.x;
        beam.y=m.y;
        beam.z=m.z;
        beam.heading=degrees_to_retail_angle(unit->turret_degrees);
        beam.attacker=unit->owner;
        beam.variant=def->explosion;
        beam.damage=def->damage;
        beam.depth=powered(*unit) ? def->powered_depth : def->depth;
        beam.local=local;
        beam.ignore=unit_id;
        if (fire_beam(context.base,beam)) {
            push_sound(combat.sounds,sound_beam,unit->x,unit->y,unit->z,unit_id,true,local);
            flash(context,unit_id,*unit);
            fired_event(context,unit_id,*unit,def->id,no_entity,m.x,m.y,m.z);
        } else {
            status=WeaponFireStatus::spawn_failed;
        }
        if (powered(*unit)) unit->ammo[5]=1;   // retail writes slot 5 for ids 5 and 10
        break;
    }
    case WeaponAction::nuke: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,def->muzzle);
        const auto id=spawn_nuke(context.base,m.x,m.y,m.z,m.heading,unit->owner,unit_id);
        if (!id) { status=WeaponFireStatus::spawn_failed; break; }
        push_sound(combat.sounds,sound_nuke_fire,unit->x,unit->y,unit->z,unit_id,false,local);
        fired_event(context,unit_id,*unit,def->id,*id,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::grenade: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,def->muzzle);
        const auto id=spawn_grenade(context.base,context.random,m.x,m.y,m.z,m.heading,unit->owner,unit_id);
        if (!id) { status=WeaponFireStatus::spawn_failed; break; }
        fired_event(context,unit_id,*unit,def->id,*id,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::missile: {
        const Muzzle m=muzzle_point(angles,*unit,unit->turret_degrees,turret_offset);
        context.state.recoil[slot]+=def->recoil;
        MissileSpawn spawn;
        spawn.x=m.x;
        spawn.y=m.y;
        spawn.z=m.z;
        spawn.speed=options.boosted ? def->speed : 30.0F;
        spawn.heading=m.heading;
        spawn.damage=def->damage;
        spawn.trail=def->trail;
        spawn.attacker=unit->owner;
        spawn.local=local;
        // fire_unit_weapon.c:213-219: remote units get no homing and no bursts;
        // any powered non-remote unit (computer-controlled included) gets bursts.
        spawn.guided=!options.remote;
        spawn.bursts=(!options.remote && powered(*unit)) ? unit->ammo[11] : std::int16_t{0};
        spawn.ignore=unit_id;
        const auto id=spawn_missile(context.base,spawn);
        if (!id) { status=WeaponFireStatus::spawn_failed; break; }
        push_sound(combat.sounds,sound_missile,unit->x,unit->y,unit->z,unit_id,true,local);
        push_effect(context,EffectEventKind::muzzle_flash,unit_id,*id,muzzle_flash_variant,unit->owner,m.x,m.y,m.z);
        flash(context,unit_id,*unit);
        if (powered(*unit)) unit->ammo[11]=1;
        fired_event(context,unit_id,*unit,def->id,*id,m.x,m.y,m.z);
        break;
    }
    case WeaponAction::mine_deploy:
    case WeaponAction::unit_deploy: {
        const std::uint16_t heading=degrees_to_retail_angle(unit->heading_degrees);
        deployables::DeployerUnit deployer;
        deployer.id=static_cast<std::int16_t>(slot);
        deployer.unit_kind=static_cast<std::uint8_t>(unit->kind);
        deployer.team=unit->team;
        deployer.powered=powered(*unit);
        deployer.x=unit->x;
        deployer.z=unit->z;
        deployer.hull_forward_x=angles.sine(heading);
        deployer.hull_forward_z=angles.cosine(heading);
        deployer.heading=heading;
        deployer.heading_degrees=unit->heading_degrees;   // retail rounding path
        const deployables::DeployResult result=def->action==WeaponAction::mine_deploy
            ? deployables::fire_mine_weapon(*context.deployables,deployer)
            : deployables::fire_gun_buddy_weapon(*context.deployables,deployer);
        if (result.return_before_bookkeeping()) return WeaponFireStatus::blocked;
        if (result.consume_whole_stock) unit->ammo[def->ammo_slot]=1;
        fired_event(context,unit_id,*unit,def->id,result.spawned,unit->x,unit->y,unit->z);
        break;
    }
    case WeaponAction::decoy: {
        const float r=def->range;
        const std::array<std::array<float,2>,4> points{{{unit->x+r,unit->z},{unit->x-r,unit->z},
                                                        {unit->x,unit->z+r},{unit->x,unit->z-r}}};
        for (const auto& point:points) {
            push_effect(context,EffectEventKind::ring_burst,unit_id,no_entity,def->explosion,unit->owner,
                        point[0],decoy_height,point[1]);
        }
        context.state.last_mine_tick[slot]=combat.tick;
        break;
    }
    }
    weapon.last_fire_tick=combat.tick;
    consume_ammo(context,*unit,weapon);
    return status;
}

std::size_t apply_splash_damage(WeaponContext& context,float x,float z,float radius,
                                std::int32_t amount,std::uint8_t attacker,EntityId source) {
    HookScope scope(context);
    return apply_splash_damage(context.base,x,z,radius,amount,attacker,source);
}

void update_weapon_projectiles(WeaponContext& context) {
    HookScope scope(context);
    update_shells(context.base);
}

} // namespace battletanx
