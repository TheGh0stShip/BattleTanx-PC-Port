#include "battletanx/runtime_math.hpp"
#include "battletanx/unit_runtime.hpp"
#include "battletanx/weapon_runtime.hpp"

#include "runtime_test_support.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

// Synthetic arenas only. Units face +z (heading 0): forward = (0, 1).

namespace {
using namespace battletanx;

struct Arena {
    UnitPool units;
    std::array<OwnerRecord,owner_capacity> owners{};
    GameplayEventQueue events{1024U};
    SoundEventQueue sounds{1024U};
    CombatConfig config;
    ShellPool shells;
    WorldCollision world;
    AngleTable angles=reference_angle_table();
    WeaponRuntimeState state;
    EffectEventQueue effects{4096U};
    RetailRandom random{1U};
    std::int32_t tick{100};
    std::function<ObjectResponse(EntityId,const ProjectileContact&)> response{};
    std::function<MissileControl(std::uint8_t)> control{};

    Arena() {
        for (std::size_t i=0U; i<2U; ++i) {
            owners[i].present=true;
            owners[i].team=static_cast<std::uint8_t>(i);
            owners[i].squad_cap=1U;
        }
        owners[1].computer=true;
    }
    EntityId add(std::uint8_t owner,float x,float z,UnitControl control_kind) {
        const EntityId id=*units.allocate();
        *units.get(id)=make_unit(retail::UnitKind::tank,owner,owners[owner].team,x,z,0U,0,control_kind);
        ++owners[owner].live_units;
        return id;
    }
    template <typename Body>
    void run(Body&& body) {
        CombatContext combat{units,owners,events,sounds,config,tick};
        ProjectileContext base{combat,shells,world,angles,1.0F};
        WeaponContext context{base,state,effects,random};
        context.object_response=response;
        context.missile_control=control;
        body(context);
    }
    WeaponFireStatus fire(EntityId unit,std::uint8_t weapon,std::int16_t ammo=5) {
        UnitRecord& u=*units.get(unit);
        u.primary_weapon.kind=weapon;
        u.ammo[weapon]=ammo;
        WeaponFireStatus status{};
        run([&](WeaponContext& c) { status=fire_weapon(c,unit,u.primary_weapon,FireRequest{true,true}); });
        return status;
    }
    void step(int n=1) {
        for (int i=0; i<n; ++i) {
            run([&](WeaponContext& c) { update_weapon_projectiles(c); });
            ++tick;
        }
    }
    std::vector<std::uint8_t> hash() const {
        StateWriter writer;
        serialize_projectiles(writer,shells);
        state.serialize(writer);
        return writer.bytes();
    }
};

std::size_t count_effects(const EffectEventQueue& q,EffectEventKind kind) {
    std::size_t n=0U;
    for (const auto& e:q.events()) n+=e.kind==kind ? 1U : 0U;
    return n;
}
std::size_t count_events(const GameplayEventQueue& q,GameplayEventKind kind) {
    std::size_t n=0U;
    for (const auto& e:q.events()) n+=e.kind==kind ? 1U : 0U;
    return n;
}
const ShellRecord* first_record(const ShellPool& pool) {
    const ShellRecord* r=nullptr;
    pool.for_each([&](EntityId,const ShellRecord& p) { if (r==nullptr) r=&p; });
    return r;
}

} // namespace

RUNTIME_TEST(exhaustive_weapon_table) {
    struct Row { WeaponAction action; std::uint8_t effect; std::uint8_t damage; };
    const std::array<Row,weapon_id_count> rows{{
        {WeaponAction::none,0U,0U},{WeaponAction::bullet,34U,0x11U},{WeaponAction::bullet,34U,2U},
        {WeaponAction::bullet,34U,0x11U},{WeaponAction::spread_shell,2U,0x0EU},{WeaponAction::beam,12U,0x26U},
        {WeaponAction::nuke,14U,0U},{WeaponAction::grenade,15U,22U},{WeaponAction::none,0U,0U},
        {WeaponAction::none,0U,0U},{WeaponAction::beam,12U,0x26U},{WeaponAction::missile,22U,0x28U},
        {WeaponAction::mine_deploy,29U,20U},{WeaponAction::unit_deploy,27U,0U},{WeaponAction::decoy,38U,0U},
        {WeaponAction::shield,0U,0U},{WeaponAction::repair,0U,0U}}};
    for (std::uint8_t id=0U; id<weapon_id_count; ++id) {
        const auto def=weapon_definition(id);
        REQUIRE(def && def->id==id && def->ammo_slot==id);
        REQUIRE(def->action==rows[id].action && def->effect_kind==rows[id].effect && def->damage==rows[id].damage);
        REQUIRE(def->cooldown==retail::weapon_cooldown[id]);
        REQUIRE(def->auto_fire==(retail::weapon_repeats[id]!=0U));
    }
    REQUIRE(!weapon_definition(17U));
    REQUIRE(weapon_definition(5U)->explosion==0x12U && weapon_definition(10U)->explosion==8U);
    REQUIRE(weapon_definition(5U)->depth==4U && weapon_definition(5U)->powered_depth==0x16U);
    REQUIRE(weapon_definition(14U)->untargetable_ticks==0x5A);
    REQUIRE(weapon_definition(12U)->deployable && weapon_definition(13U)->deployable);
    REQUIRE(weapon_cooldown_table[16]==1U);
    return true;
}

RUNTIME_TEST(pickup_grants_and_random_event_table) {
    REQUIRE(pickup_weapon_grant(2U)->weapon==4U && pickup_weapon_grant(2U)->amount==5);
    REQUIRE(pickup_weapon_grant(7U)->weapon==11U);
    REQUIRE(!pickup_weapon_grant(0U));
    REQUIRE(random_grant_pickup_kind(0U)==1U && random_grant_pickup_kind(31U)==19U);
    return true;
}

RUNTIME_TEST(cannon_bullet_lifecycle_and_cooldown) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    const EntityId t=a.add(1U,0.0F,500.0F,UnitControl::computer);
    REQUIRE(a.fire(p,1U,70)==WeaponFireStatus::fired);
    REQUIRE(a.units.get(p)->ammo[1]==69 && a.shells.size()==1U);
    REQUIRE(first_record(a.shells)->effect_kind==effect_kind::bullet);
    // Second press in the same tick: cooldown 30.
    WeaponFireStatus again{};
    a.run([&](WeaponContext& c) { again=fire_weapon(c,p,a.units.get(p)->primary_weapon,FireRequest{true,true}); });
    REQUIRE(again==WeaponFireStatus::not_ready);
    int n=0;
    while (a.shells.size()>0U && n<200) { a.step(); ++n; }
    REQUIRE(n==4 && a.units.get(t)->health<100);
    REQUIRE(count_events(a.events,GameplayEventKind::projectile_impact)==1U);
    REQUIRE(count_effects(a.effects,EffectEventKind::projectile_removed)==1U);
    return true;
}

RUNTIME_TEST(machine_gun_alternates_barrels) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    std::vector<float> xs;
    for (int i=0; i<2; ++i) {
        a.tick+=10;
        REQUIRE(a.fire(p,2U,10)==WeaponFireStatus::fired);
    }
    a.shells.for_each([&](EntityId,const ShellRecord& s) { xs.push_back(s.x); });
    REQUIRE(xs.size()==2U && runtime_test::near(xs[0],-xs[1],1e-3) && runtime_test::near(std::fabs(xs[0]),11.1,1e-3));
    return true;
}

RUNTIME_TEST(spread_fires_three_shells) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    REQUIRE(a.fire(p,4U)==WeaponFireStatus::fired);
    std::size_t shells=0U;
    a.shells.for_each([&](EntityId,const ShellRecord& s) { shells+=s.effect_kind==effect_kind::shell ? 1U : 0U; });
    REQUIRE(shells==3U && a.units.get(p)->ammo[4]==4);
    return true;
}

RUNTIME_TEST(beam_is_instant_and_uses_no_pool_slot) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    const EntityId t=a.add(1U,0.0F,600.0F,UnitControl::computer);
    REQUIRE(a.fire(p,5U)==WeaponFireStatus::fired);
    REQUIRE(a.shells.size()==0U && a.units.get(t)->health<100);
    REQUIRE(count_effects(a.effects,EffectEventKind::beam)==1U);
    REQUIRE(a.sounds.events().back().sound_id==9U);
    return true;
}

RUNTIME_TEST(beam_ricochet_response_spawns_sub_beam) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    (void)a.add(1U,0.0F,600.0F,UnitControl::computer);
    a.response=[](EntityId,const ProjectileContact&) { return ObjectResponse{HitResponse::ricochet,0x8000U}; };
    REQUIRE(a.fire(p,10U)==WeaponFireStatus::fired);
    // Depth 4: original + up to 3 reflections (the sub-beam can re-hit).
    const std::size_t beams=count_effects(a.effects,EffectEventKind::beam);
    REQUIRE(beams>=2U && beams<=4U);
    REQUIRE(count_effects(a.effects,EffectEventKind::ricochet_spawned)>=1U);
    return true;
}

RUNTIME_TEST(powered_beam_consumes_stock_and_forks) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    (void)a.add(1U,0.0F,600.0F,UnitControl::computer);
    a.units.get(p)->flags|=0x2000U;
    REQUIRE(a.fire(p,5U,4)==WeaponFireStatus::fired);
    REQUIRE(a.units.get(p)->ammo[5]==0 && a.units.get(p)->primary_weapon.kind==0U);
    REQUIRE((a.units.get(p)->flags&0x2000U)==0U);
    // depth 0x16 > 16: two delayed fork records in the single pool.
    REQUIRE(a.shells.size()==2U);
    a.step(4);
    REQUIRE(count_effects(a.effects,EffectEventKind::beam)>=3U);
    return true;
}

RUNTIME_TEST(grenade_fuse_and_message7_splash) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    REQUIRE(a.fire(p,7U)==WeaponFireStatus::fired);
    const ShellRecord g=*first_record(a.shells);
    REQUIRE(g.effect_kind==effect_kind::grenade && g.life==0x50);
    REQUIRE(g.velocity_z>=24.0F && g.velocity_z<=36.0F);
    // Park a victim where the grenade will stop: run to detonation.
    a.step(0x4F);
    REQUIRE(a.shells.size()==1U);
    const ShellRecord before=*first_record(a.shells);
    const EntityId v=a.add(1U,before.x,before.z,UnitControl::computer);
    a.step();
    REQUIRE(a.shells.size()==0U && a.units.get(v)->health<100);
    REQUIRE(count_effects(a.effects,EffectEventKind::splash)==1U);
    return true;
}

RUNTIME_TEST(nuke_detonates_then_ring_damages_once) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    REQUIRE(a.fire(p,6U,1)==WeaponFireStatus::fired);
    a.step(0xC8);
    const ShellRecord* n=first_record(a.shells);
    REQUIRE(n!=nullptr && n->phase==1U);
    const EntityId v=a.add(1U,n->x+300.0F,n->z,UnitControl::computer);
    int guard=0;
    while (a.shells.size()>0U && guard<400) { a.step(); ++guard; }
    REQUIRE(a.shells.size()==0U);
    const std::int32_t health=a.units.get(v)!=nullptr ? a.units.get(v)->health : 0;
    REQUIRE(health<100);
    // apply_proximity_hit_to_target.c: one ring hit, recoil -= 6.
    REQUIRE(a.state.recoil[v.slot]==-6.0F);
    REQUIRE(script_clock_units(30)==715);
    return true;
}

RUNTIME_TEST(missile_owner_guidance_and_release) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    bool release=false;
    a.control=[&](std::uint8_t) { return MissileControl{10,release,false}; };
    REQUIRE(a.fire(p,11U)==WeaponFireStatus::fired);
    REQUIRE(first_record(a.shells)->guided && first_record(a.shells)->life==400);
    a.step();
    REQUIRE(first_record(a.shells)->heading==static_cast<std::uint16_t>(0U-100U));
    release=true;
    a.step();
    const std::uint16_t h=first_record(a.shells)->heading;
    REQUIRE(!first_record(a.shells)->guided);
    a.step();
    REQUIRE(first_record(a.shells)->heading==h);
    return true;
}

RUNTIME_TEST(remote_missile_is_unguided_without_bursts) {
    Arena a;
    const EntityId p=a.add(1U,0.0F,0.0F,UnitControl::computer);
    UnitRecord& u=*a.units.get(p);
    u.flags|=0x2000U;
    u.primary_weapon.kind=11U;
    u.ammo[11]=3;
    WeaponFireStatus status{};
    a.run([&](WeaponContext& c) {
        status=fire_weapon(c,p,u.primary_weapon,FireRequest{true,true},FireOptions{.force=false,.boosted=true,.remote=true});
    });
    REQUIRE(status==WeaponFireStatus::fired);
    REQUIRE(!first_record(a.shells)->guided && first_record(a.shells)->bursts==0);
    return true;
}

RUNTIME_TEST(bullet_velocity_is_forward_times_speed) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    a.units.get(p)->turret_degrees=37.0F;
    REQUIRE(a.fire(p,1U)==WeaponFireStatus::fired);
    const std::uint16_t turret=degrees_to_retail_angle(37.0F);
    const float speed=weapon_definition(1U)->speed;
    REQUIRE(first_record(a.shells)->velocity_x==a.angles.sine(turret)*speed);
    REQUIRE(first_record(a.shells)->velocity_z==a.angles.cosine(turret)*speed);
    return true;
}

RUNTIME_TEST(projectile_visuals_match_retail_draw_slots) {
    REQUIRE((projectile_visual(2U)==ProjectileVisual{0x24U,1.0F}));
    REQUIRE((projectile_visual(34U)==ProjectileVisual{0x24U,1.0F}));
    REQUIRE((projectile_visual(22U)==ProjectileVisual{0x41U,1.0F}));
    REQUIRE((projectile_visual(14U)==ProjectileVisual{0x25U,1.0F}));
    const auto grenade=projectile_visual(15U);
    REQUIRE(grenade.has_value());
    REQUIRE(grenade->model_slot==0x25U);
    REQUIRE(runtime_test::near(grenade->scale,0.35,1e-6));
    REQUIRE(!projectile_visual(12U));
    return true;
}

RUNTIME_TEST(powered_missile_bursts_fire_beams) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    a.units.get(p)->flags|=0x2000U;
    a.control=[](std::uint8_t) { return MissileControl{0,false,true}; };
    REQUIRE(a.fire(p,11U,3)==WeaponFireStatus::fired);
    REQUIRE(a.units.get(p)->ammo[11]==0);
    REQUIRE(first_record(a.shells)->bursts==3 && first_record(a.shells)->life==8000);
    REQUIRE(runtime_test::near(first_record(a.shells)->speed,16.0,1e-4));
    a.step(4);
    REQUIRE(first_record(a.shells)->bursts==0);
    REQUIRE(count_effects(a.effects,EffectEventKind::beam)==3U);
    return true;
}

RUNTIME_TEST(deployables_absent_is_unsupported_without_mutation) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    for (std::uint8_t w:{std::uint8_t{12U},std::uint8_t{13U}}) {
        const auto before_tick=a.units.get(p)->primary_weapon.last_fire_tick;
        REQUIRE(a.fire(p,w,2)==WeaponFireStatus::unsupported);
        REQUIRE(a.units.get(p)->ammo[w]==2 && a.units.get(p)->primary_weapon.last_fire_tick==before_tick);
    }
    REQUIRE(a.shells.size()==0U && a.events.events().empty());
    return true;
}

RUNTIME_TEST(decoy_blocks_targeting_and_deals_no_damage) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    const EntityId v=a.add(1U,90.0F,0.0F,UnitControl::computer);
    REQUIRE(a.fire(p,14U)==WeaponFireStatus::fired);
    REQUIRE(count_effects(a.effects,EffectEventKind::ring_burst)==4U);
    REQUIRE(a.units.get(v)->health==100);
    REQUIRE(!decoy_allows_targeting(a.state,p.slot,a.tick+89));
    REQUIRE(decoy_allows_targeting(a.state,p.slot,a.tick+90));
    return true;
}

RUNTIME_TEST(pool_exhaustion_still_consumes_and_starts_cooldown) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    while (a.shells.allocate()) {}
    REQUIRE(a.fire(p,4U)==WeaponFireStatus::spawn_failed);
    REQUIRE(a.units.get(p)->ammo[4]==4 && a.units.get(p)->primary_weapon.last_fire_tick==a.tick);
    return true;
}

RUNTIME_TEST(bullet_ricochet_with_full_pool_is_dropped) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    (void)a.add(1U,0.0F,300.0F,UnitControl::computer);
    a.response=[](EntityId,const ProjectileContact&) { return ObjectResponse{HitResponse::ricochet,0x8000U}; };
    REQUIRE(a.fire(p,1U,70)==WeaponFireStatus::fired);
    while (a.shells.allocate()) {}
    a.step(3);
    REQUIRE(count_effects(a.effects,EffectEventKind::ricochet_dropped)>=1U);   // filler records also hit
    return true;
}

RUNTIME_TEST(static_hit_response_and_event_order) {
    Arena a;
    a.world.segment=[](float,float z0,float,float z1)->std::optional<SegmentHit> {
        if (z0<=200.0F && z1>=200.0F) return SegmentHit{200.0F-z0,0.0F,200.0F};
        return std::nullopt;
    };
    const EntityId p=a.add(0U,0.0F,0.0F,UnitControl::player);
    REQUIRE(a.fire(p,1U,70)==WeaponFireStatus::fired);
    a.step(3);
    REQUIRE(a.shells.size()==0U);
    REQUIRE(count_effects(a.effects,EffectEventKind::impact_decal)==1U);
    std::uint32_t last=0U;
    for (const auto& e:a.effects.events()) { REQUIRE(e.gameplay_index>=last); last=e.gameplay_index; }
    return true;
}

RUNTIME_TEST(serialization_is_deterministic) {
    Arena a;
    Arena b;
    for (Arena* x:{&a,&b}) {
        const EntityId p=x->add(0U,0.0F,0.0F,UnitControl::player);
        (void)x->fire(p,7U);
        x->step(5);
    }
    REQUIRE(a.hash()==b.hash());
    a.step();
    REQUIRE(a.hash()!=b.hash());
    return true;
}

RUNTIME_TEST_MAIN()
