#include "battletanx/runtime_math.hpp"
#include "battletanx/weapon_runtime.hpp"

#include "runtime_test_support.hpp"

#include <vector>


namespace {
using namespace battletanx;

struct Arena {
    UnitPool units;
    std::array<OwnerRecord,owner_capacity> owners{};
    GameplayEventQueue events;
    SoundEventQueue sounds;
    CombatConfig config;
    WorldCollision world;
    AngleTable angles=reference_angle_table();
    WeaponRuntimeState state;
    EffectEventQueue effects{512U};
    RetailRandom random{1U};
    std::int32_t tick{100};

    Arena() {
        for (std::size_t i=0U; i<2U; ++i) {
            owners[i].present=true;
            owners[i].team=static_cast<std::uint8_t>(i);
            owners[i].squad_cap=1U;
        }
        owners[1].computer=true;
    }
    EntityId add(std::uint8_t owner,float x,float z,std::uint16_t heading,UnitControl control) {
        const EntityId id=*units.allocate();
        *units.get(id)=make_unit(retail::UnitKind::tank,owner,owners[owner].team,x,z,heading,0,control);
        ++owners[owner].live_units;
        return id;
    }
    template <typename Body>
    void run(Body&& body) {
        CombatContext combat{units,owners,events,sounds,config,tick};
        WeaponBaseContext base{combat,world,angles,1.0F};
        WeaponContext context{base,state,effects,random};
        body(context);
    }
};

FireRequest press() { return FireRequest{true,true}; }

std::size_t count_effects(const EffectEventQueue& q,EffectEventKind kind) {
    std::size_t n=0U;
    for (const auto& e:q.events()) n+=e.kind==kind ? 1U : 0U;
    return n;
}

} // namespace

RUNTIME_TEST(definitions_cover_every_weapon_id) {
    for (std::uint8_t id=0U; id<weapon_id_count; ++id) {
        const auto def=weapon_definition(id);
        REQUIRE(def && def->id==id && def->ammo_slot==id);
    }
    REQUIRE(!weapon_definition(17U));
    return true;
}

RUNTIME_TEST(cooldown_and_repeat_tables_match_rom_bytes) {
    // D_80125EC0 / D_80125ED4.
    REQUIRE(weapon_definition(1U)->cooldown==30U && weapon_definition(1U)->auto_fire);
    REQUIRE(weapon_definition(2U)->cooldown==3U && weapon_definition(2U)->auto_fire);
    REQUIRE(weapon_definition(4U)->cooldown==50U && !weapon_definition(4U)->auto_fire);
    REQUIRE(weapon_definition(8U)->auto_fire);
    REQUIRE(weapon_definition(15U)->cooldown==120U && weapon_definition(16U)->cooldown==1U);
    return true;
}

RUNTIME_TEST(bullet_fields_follow_spawn_args) {
    const auto cannon=*weapon_definition(1U);
    REQUIRE(cannon.effect_kind==34U && cannon.damage==0x11U && cannon.heavy_damage==0x28U);
    REQUIRE(cannon.trail==0x0FU && cannon.life==0x70 && runtime_test::near(cannon.speed,110.0));
    const auto gun=*weapon_definition(2U);
    REQUIRE(gun.damage==2U && gun.trail==0U && gun.fire_sound==0x22U && gun.frame==MuzzleFrame::hull);
    REQUIRE(weapon_definition(11U)->effect_kind==22U && weapon_definition(11U)->damage==0x28U);
    REQUIRE(weapon_definition(5U)->evidence==Evidence::unresolved);
    return true;
}

RUNTIME_TEST(pickup_grants_and_random_event_table) {
    REQUIRE(pickup_weapon_grant(2U)->weapon==4U && pickup_weapon_grant(2U)->amount==5);
    REQUIRE(pickup_weapon_grant(7U)->weapon==11U);
    REQUIRE(pickup_weapon_grant(10U)->amount==2 && pickup_weapon_grant(19U)->amount==1);
    REQUIRE(!pickup_weapon_grant(0U));
    REQUIRE(random_grant_pickup_kind(0U)==1U);
    REQUIRE(random_grant_pickup_kind(6U)==2U && random_grant_pickup_kind(7U)==3U);
    REQUIRE(random_grant_pickup_kind(31U)==19U && random_grant_pickup_kind(0x3EU)==19U);
    return true;
}

RUNTIME_TEST(cannon_spawns_bullet_and_consumes_ammo) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState& w=a.units.get(p)->primary_weapon;
    WeaponFireStatus s{};
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,press()); });
    REQUIRE(s==WeaponFireStatus::fired);
    REQUIRE(a.state.projectiles.size()==1U);
    REQUIRE(a.units.get(p)->ammo[1]==69);
    REQUIRE(w.last_fire_tick==100);
    REQUIRE(count_effects(a.effects,EffectEventKind::muzzle_flash)==1U);
    REQUIRE(runtime_test::near(a.state.recoil[p.slot],-2.5));
    return true;
}

RUNTIME_TEST(cooldown_blocks_until_strictly_past) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState& w=a.units.get(p)->primary_weapon;
    w.last_fire_tick=70;
    WeaponFireStatus s{};
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,press()); });
    REQUIRE(s==WeaponFireStatus::not_ready);    // 70+30 < 100 is false
    a.tick=101;
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,press()); });
    REQUIRE(s==WeaponFireStatus::fired);
    return true;
}

RUNTIME_TEST(edge_weapon_needs_press_and_empty_clicks) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState w{4U,-1000};
    WeaponFireStatus s{};
    a.units.get(p)->ammo[4]=1;
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,FireRequest{true,false}); });
    REQUIRE(s==WeaponFireStatus::not_ready);
    a.units.get(p)->ammo[4]=0;
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,press()); });
    REQUIRE(s==WeaponFireStatus::empty);
    REQUIRE(a.sounds.events().back().sound_id==retail::sound_empty_trigger);
    return true;
}

RUNTIME_TEST(spread_fires_three_shells_with_side_accel) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState w{4U,-1000};
    a.units.get(p)->ammo[4]=5;
    WeaponFireStatus s{};
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,press()); });
    REQUIRE(s==WeaponFireStatus::fired);
    REQUIRE(a.state.projectiles.size()==3U);
    float ax_sum=0.0F;
    a.state.projectiles.for_each([&](EntityId,const WeaponProjectile& pr) {
        ax_sum+=pr.accel_x;
    });
    REQUIRE(runtime_test::near(ax_sum,0.0,1e-3));
    REQUIRE(a.units.get(p)->ammo[4]==4);
    return true;
}

RUNTIME_TEST(exhausted_special_advances_to_next_stocked) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    UnitRecord& u=*a.units.get(p);
    WeaponState w{16U,-1000};
    u.ammo[16]=1;
    u.ammo[5]=3;
    u.health=50;
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,w,press()); });
    REQUIRE(u.health==80);
    REQUIRE(w.kind==5U);   // 16 -> wraps to 4 -> 5
    WeaponState only{15U,-1000};
    u.ammo[15]=1;
    u.ammo[5]=0;
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,only,press()); });
    REQUIRE(only.kind==0U);
    REQUIRE(a.state.shield_end_tick[p.slot]==100+900);
    return true;
}

RUNTIME_TEST(unrecovered_weapon_reports_unsupported_without_side_effects) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState w{6U,-1000};
    a.units.get(p)->ammo[6]=1;
    WeaponFireStatus s{};
    a.run([&](WeaponContext& c) { s=fire_weapon(c,p,w,press()); });
    REQUIRE(s==WeaponFireStatus::unsupported);
    REQUIRE(a.units.get(p)->ammo[6]==1 && w.last_fire_tick==-1000);
    return true;
}

RUNTIME_TEST(machine_gun_alternates_hull_sides) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState w{2U,-1000};
    a.units.get(p)->ammo[2]=10;
    std::vector<float> xs;
    for (int i=0; i<2; ++i) {
        a.tick+=10;
        a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,w,press()); });
    }
    a.state.projectiles.for_each([&](EntityId,const WeaponProjectile& pr) { xs.push_back(pr.x); });
    REQUIRE(xs.size()==2U);
    REQUIRE(runtime_test::near(xs[0],-11.1,1e-3) && runtime_test::near(xs[1],11.1,1e-3));
    return true;
}

RUNTIME_TEST(destroying_hit_orders_impact_damage_destroy_credit_then_removal) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId e=a.add(1U,0.0F,120.0F,0U,UnitControl::computer);
    a.units.get(e)->health=5;
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,a.units.get(p)->primary_weapon,press()); });
    a.events.clear();
    a.sounds.clear();
    a.effects.clear();
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    const auto ev=a.events.events();
    REQUIRE(ev.size()==3U);
    REQUIRE(ev[0].kind==GameplayEventKind::projectile_impact);
    REQUIRE(ev[1].kind==GameplayEventKind::unit_damaged);
    REQUIRE(ev[2].kind==GameplayEventKind::unit_destroyed);
    REQUIRE(a.owners[0].kills==1);     // credit applied before effects
    // Response 6: no impact sound or explosion; destroy sound only.
    REQUIRE(a.sounds.events().size()==1U && a.sounds.events()[0].sound_id==retail::sound_unit_destroyed);
    REQUIRE(count_effects(a.effects,EffectEventKind::impact_explosion)==0U);
    const auto fx=a.effects.events();
    REQUIRE(fx.back().kind==EffectEventKind::projectile_removed);
    REQUIRE(fx.back().reason==RemovalReason::impact && fx.back().gameplay_index==3U);
    REQUIRE(a.state.projectiles.size()==0U);
    return true;
}

RUNTIME_TEST(surviving_hit_emits_sound_and_explosion_after_damage) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId e=a.add(1U,0.0F,120.0F,0U,UnitControl::computer);
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,a.units.get(p)->primary_weapon,press()); });
    a.events.clear(); a.sounds.clear(); a.effects.clear();
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    REQUIRE(a.units.get(e)->health==100-0x11);
    REQUIRE(a.sounds.events().size()==1U && a.sounds.events()[0].sound_id==retail::sound_hit_unit);
    std::vector<EffectEventKind> kinds;
    for (const auto& f:a.effects.events()) kinds.push_back(f.kind);
    REQUIRE(kinds.size()==3U);
    REQUIRE(kinds[0]==EffectEventKind::trail && kinds[1]==EffectEventKind::impact_explosion &&
            kinds[2]==EffectEventKind::projectile_removed);
    REQUIRE(a.effects.events()[1].variant==0x0BU && a.effects.events()[1].gameplay_index==2U);
    return true;
}

RUNTIME_TEST(pass_through_keeps_flying) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    (void)a.add(1U,0.0F,120.0F,0U,UnitControl::computer);
    a.run([&](WeaponContext& c) {
        c.object_response=[](EntityId,const ProjectileContact&) { return ObjectResponse{HitResponse::pass_through,0U}; };
        (void)fire_weapon(c,p,a.units.get(p)->primary_weapon,press());
        update_weapon_projectiles(c);
    });
    REQUIRE(a.state.projectiles.size()==1U);
    return true;
}

RUNTIME_TEST(bullet_ricochet_spawns_new_unowned_bullet) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId e=a.add(1U,0.0F,120.0F,0U,UnitControl::computer);
    a.run([&](WeaponContext& c) {
        c.object_response=[](EntityId,const ProjectileContact&) { return ObjectResponse{HitResponse::ricochet,0x4000U}; };
        (void)fire_weapon(c,p,a.units.get(p)->primary_weapon,press());
        update_weapon_projectiles(c);
    });
    REQUIRE(a.state.projectiles.size()==1U);
    const WeaponProjectile* r=nullptr;
    a.state.projectiles.for_each([&](EntityId,const WeaponProjectile& pr) { r=&pr; });
    REQUIRE(r!=nullptr && r->attacker==no_owner && r->ignore==e && r->life==0x70);
    REQUIRE(r->heading==0x4000U && runtime_test::near(r->velocity_x,110.0,1e-2));
    REQUIRE(runtime_test::near(r->range,retail::shell_unbounded_range));
    REQUIRE(count_effects(a.effects,EffectEventKind::ricochet_spawned)==1U);
    REQUIRE(a.units.get(e)->health==100);
    return true;
}

RUNTIME_TEST(ricochet_with_exhausted_pool_drops_and_removes) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    (void)a.add(1U,0.0F,120.0F,0U,UnitControl::computer);
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,a.units.get(p)->primary_weapon,press()); });
    while (!a.state.projectiles.full()) {
        const EntityId id=*a.state.projectiles.allocate();
        a.state.projectiles.get(id)->effect_kind=0U;   // inert filler
    }
    const std::size_t before=a.state.projectiles.size();
    a.run([&](WeaponContext& c) {
        c.object_response=[](EntityId,const ProjectileContact&) { return ObjectResponse{HitResponse::ricochet,0U}; };
        update_weapon_projectiles(c);
    });
    REQUIRE(a.state.projectiles.size()==before-1U);
    REQUIRE(count_effects(a.effects,EffectEventKind::ricochet_dropped)==1U);
    return true;
}

RUNTIME_TEST(shell_ricochet_redirects_in_place) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId e=a.add(1U,0.0F,90.0F,0U,UnitControl::computer);
    WeaponState w{4U,-1000};
    a.units.get(p)->ammo[4]=5;
    a.run([&](WeaponContext& c) {
        c.object_response=[](EntityId,const ProjectileContact&) { return ObjectResponse{HitResponse::ricochet,0x8000U}; };
        (void)fire_weapon(c,p,w,press());
        update_weapon_projectiles(c);
    });
    std::size_t redirected=0U;
    a.state.projectiles.for_each([&](EntityId,const WeaponProjectile& pr) {
        if (pr.heading==0x8000U && pr.attacker==no_owner && pr.ignore==e && pr.accel_x==0.0F) ++redirected;
    });
    REQUIRE(redirected>=1U);
    return true;
}

RUNTIME_TEST(wall_impact_uses_static_response) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    a.world.segment=[](float x0,float z0,float x1,float z1) -> std::optional<SegmentHit> {
        if (z0<=200.0F && z1>=200.0F) return SegmentHit{200.0F-z0,x0+(x1-x0)*0.0F,200.0F};
        return std::nullopt;
    };
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,a.units.get(p)->primary_weapon,press()); });
    a.sounds.clear();
    for (int i=0; i<3; ++i) a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    REQUIRE(a.state.projectiles.size()==0U);
    REQUIRE(count_effects(a.effects,EffectEventKind::impact_decal)==1U);
    REQUIRE(a.sounds.events().size()==1U && a.sounds.events()[0].sound_id==retail::sound_hit_other);
    return true;
}

RUNTIME_TEST(splash_is_flat_and_hits_self_and_friends) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId f=a.add(0U,107.0F,0.0F,0U,UnitControl::player);
    const EntityId far=a.add(1U,0.0F,109.0F,0U,UnitControl::computer);
    std::size_t n=0U;
    a.run([&](WeaponContext& c) { n=apply_splash_damage(c,0.0F,0.0F,splash_radius,splash_amount,0U,no_entity); });
    REQUIRE(n==2U);
    REQUIRE(a.units.get(p)->health==80 && a.units.get(f)->health==80);
    REQUIRE(a.units.get(far)->health==100);
    REQUIRE(runtime_test::near(a.units.get(p)->hit_shake,5.0));
    return true;
}

RUNTIME_TEST(placed_blast_arms_then_detonates_after_eight_ticks) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState w{12U,-1000};
    a.units.get(p)->ammo[12]=2;
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,w,press()); });
    REQUIRE(a.state.projectiles.size()==1U);
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });   // owner on top: triggers, arms
    REQUIRE(count_effects(a.effects,EffectEventKind::blast_armed)==1U);
    a.tick=108;
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    REQUIRE(a.state.projectiles.size()==1U);
    a.tick=109;
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    REQUIRE(a.state.projectiles.size()==0U);
    REQUIRE(a.units.get(p)->health==80);
    return true;
}

RUNTIME_TEST(missile_lingers_fifteen_ticks_after_impact) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    (void)a.add(1U,0.0F,300.0F,0U,UnitControl::computer);
    WeaponState w{11U,-1000};
    a.units.get(p)->ammo[11]=1;
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,w,press(),FireOptions{false,false}); });
    const WeaponProjectile* m=nullptr;
    a.state.projectiles.for_each([&](EntityId,const WeaponProjectile& pr) { m=&pr; });
    REQUIRE(m!=nullptr && runtime_test::near(m->speed,30.0));
    for (int i=0; i<20 && m->active; ++i) a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    REQUIRE(!m->active && m->life==15);
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    REQUIRE(m->life==14);
    REQUIRE(w.kind==0U);   // last missile spent, nothing stocked
    return true;
}

RUNTIME_TEST(ring_burst_records_mine_time_and_four_effects) {
    Arena a;
    const EntityId p=a.add(0U,0.0F,0.0F,0U,UnitControl::player);
    WeaponState w{14U,-1000};
    a.units.get(p)->ammo[14]=3;
    a.run([&](WeaponContext& c) { (void)fire_weapon(c,p,w,press()); });
    REQUIRE(count_effects(a.effects,EffectEventKind::ring_burst)==4U);
    REQUIRE(a.state.last_mine_tick[p.slot]==100);
    return true;
}

RUNTIME_TEST(serialization_is_deterministic_and_sensitive) {
    Arena a, b;
    for (Arena* x:{&a,&b}) {
        const EntityId p=x->add(0U,0.0F,0.0F,0U,UnitControl::player);
        x->run([&](WeaponContext& c) { (void)fire_weapon(c,p,x->units.get(p)->primary_weapon,press()); });
    }
    StateWriter wa, wb;
    a.state.serialize(wa);
    b.state.serialize(wb);
    REQUIRE(wa.hex()==wb.hex());
    a.run([&](WeaponContext& c) { update_weapon_projectiles(c); });
    StateWriter wc;
    a.state.serialize(wc);
    REQUIRE(wc.hex()!=wa.hex());
    return true;
}

RUNTIME_TEST_MAIN()
