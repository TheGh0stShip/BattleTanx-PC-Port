#include "battletanx/runtime_math.hpp"
#include "battletanx/unit_kinds.hpp"
#include "runtime_test_support.hpp"

#include <algorithm>

using namespace battletanx;
using runtime_test::near;

namespace {

struct TurretFixture {
    AngleTable angles{reference_angle_table()};
    TurretOwnerConfig owner{};
    RetailRandom rng{3U};
    std::vector<UnitRecord> units;
    std::vector<TurretTarget> targets;
    TurretFixture() { owner.fire_delay={10,10,10,10}; owner.effect_kind={2,2,2,2}; }
    TurretContext context(std::int32_t tick) {
        targets.clear();
        for (const UnitRecord& unit : units) targets.push_back(TurretTarget{&unit,true,false});
        return TurretContext{angles,targets,owner,1U,tick,1.0F,turret_search_range,rng,{},{}};
    }
};

} // namespace

RUNTIME_TEST(definitions_cover_three_kinds_only) {
    REQUIRE(!unit_kind_definition(3U));
    const auto tank=unit_kind_definition(0U);
    const auto bike=unit_kind_definition(1U);
    const auto goliath=unit_kind_definition(2U);
    REQUIRE(tank && bike && goliath);
    REQUIRE(tank->max_health==100 && bike->max_health==30 && goliath->max_health==200);
    REQUIRE(tank->primary_weapon==1U && bike->primary_weapon==2U && goliath->primary_weapon==3U);
    REQUIRE(bike->primary_ammo==150 && goliath->primary_ammo==70);
    REQUIRE(bike->footprint.min_z==-38 && goliath->footprint.max_x==60);
    REQUIRE(bike->crushed_by_goliath && goliath->rail_capable && !tank->rail_capable);
    REQUIRE(goliath->kill_score==3000 && bike->kill_score==500);
    return true;
}

RUNTIME_TEST(movement_tables_use_bike_tank_goliath_order) {
    const auto bike=*unit_kind_definition(1U);
    const auto tank=*unit_kind_definition(0U);
    const auto goliath=*unit_kind_definition(2U);
    REQUIRE(near(bike.movement.accel,0.22) && near(tank.movement.accel,0.18) && near(goliath.movement.accel,0.16));
    REQUIRE(near(bike.movement.grip,230.0) && near(tank.movement.grip,300.0));
    REQUIRE(near(bike.movement.drag,100.0) && near(goliath.movement.drag,250.0));
    REQUIRE(near(bike.movement.inverse_mass,0.02) && near(tank.movement.inverse_mass,0.01));
    REQUIRE(near(tank.movement.inverse_inertia,1.0/(100.0/12.0*6554.0),1e-9));
    return true;
}

RUNTIME_TEST(base_unit_creation_equips_each_vehicle_kind) {
    const UnitRecord tank=make_unit(
        retail::UnitKind::tank,0U,0U,0.0F,0.0F,0U,0,UnitControl::player);
    const UnitRecord bike=make_unit(
        retail::UnitKind::bike,0U,0U,0.0F,0.0F,0U,0,UnitControl::player);
    const UnitRecord goliath=make_unit(
        retail::UnitKind::goliath,0U,0U,0.0F,0.0F,0U,0,UnitControl::player);
    REQUIRE(tank.primary_weapon.kind==1U && tank.ammo[1]==70);
    REQUIRE(bike.primary_weapon.kind==2U && bike.ammo[2]==150);
    REQUIRE(goliath.primary_weapon.kind==3U && goliath.ammo[3]==70);
    REQUIRE(tank.primary_weapon.last_fire_tick==retail::never_fired_tick);
    REQUIRE(bike.primary_weapon.last_fire_tick==retail::never_fired_tick);
    REQUIRE(goliath.primary_weapon.last_fire_tick==retail::never_fired_tick);
    return true;
}

RUNTIME_TEST(model_sets_match_retail_vehicle_parts) {
    REQUIRE(!unit_model_set(3U,false));
    const auto tank=unit_model_set(0U,false);
    const auto damaged_tank=unit_model_set(0U,true);
    const auto bike=unit_model_set(1U,false);
    const auto damaged_bike=unit_model_set(1U,true);
    const auto goliath=unit_model_set(2U,false);
    const auto damaged_goliath=unit_model_set(2U,true);
    REQUIRE(tank && damaged_tank && bike && damaged_bike && goliath && damaged_goliath);
    REQUIRE(tank->part_count==4U && tank->shadow_slot==0x52U);
    REQUIRE(tank->parts[2].slot==0x0DU && damaged_tank->parts[2].slot==0x13U);
    REQUIRE(tank->parts[2].transform==UnitModelTransform::turret);
    REQUIRE(bike->part_count==5U && bike->shadow_slot==0x53U);
    REQUIRE(bike->parts[2].slot==0x19U && bike->parts[3].slot==0x1CU &&
            bike->parts[4].slot==0x1DU);
    REQUIRE(damaged_bike->parts[2].slot==0x21U &&
            damaged_bike->parts[3].slot==0x22U && damaged_bike->parts[4].slot==0x1EU);
    REQUIRE(goliath->part_count==4U && goliath->shadow_slot==0x54U);
    REQUIRE(goliath->parts[0].slot==0x29U &&
            goliath->parts[0].transform==UnitModelTransform::turret);
    REQUIRE(damaged_goliath->parts[0].slot==0x2DU &&
            damaged_goliath->parts[3].slot==0x2EU);
    return true;
}

RUNTIME_TEST(vehicle_movement_uses_per_kind_retail_constants) {
    NavigationLevel navigation;
    navigation.vertices={{},{-200000.0F,-200000.0F},{200000.0F,-200000.0F},
                         {0.0F,400000.0F}};
    NavigationTriangle triangle;
    triangle.corners[0].vertex=1U;
    triangle.corners[1].vertex=2U;
    triangle.corners[2].vertex=3U;
    navigation.triangles={{},triangle};
    if (!navigation_point_inside_triangle(navigation,{0.0F,0.0F},1U)) {
        std::swap(navigation.triangles[1].corners[1],navigation.triangles[1].corners[2]);
    }
    WorldLevel world;
    const AngleTable angles=reference_angle_table();
    const DriveInput forward{80.0F};
    const auto move=[&](std::uint8_t kind) {
        const auto definition=*unit_kind_definition(kind);
        PlayerTank vehicle;
        vehicle.navigation_triangle=1U;
        const VehicleDriveParameters parameters{
            definition.movement.accel,definition.movement.inverse_mass,
            definition.movement.grip,definition.movement.drag,definition.footprint};
        update_player_tank(vehicle,forward,world,navigation,angles,parameters);
        return vehicle;
    };
    const PlayerTank tank=move(0U);
    const PlayerTank bike=move(1U);
    const PlayerTank goliath=move(2U);
    REQUIRE(near(tank.velocity_z,36.0) && near(tank.z,5.4));
    REQUIRE(near(bike.velocity_z,55.2) && near(bike.z,8.28));
    REQUIRE(near(goliath.velocity_z,27.6) && near(goliath.z,4.14));
    REQUIRE(bike.z>tank.z && tank.z>goliath.z);
    return true;
}

RUNTIME_TEST(make_unit_of_kind_spawn_defaults) {
    UnitKindSpawnOptions o;
    o.flags=retail::flag_spawn_protected;
    const auto bike=make_unit_of_kind(1U,0U,0U,10.0F,20.0F,0U,500,UnitControl::computer,o);
    REQUIRE(bike && bike->unit.ammo[2]==150 && bike->unit.primary_weapon.kind==2U);
    REQUIRE(bike->timed.cloak_end_tick==560 && !bike->rail);
    REQUIRE(!make_unit_of_kind(9U,0U,0U,0.0F,0.0F,0U,0,UnitControl::computer,o));
    UnitKindSpawnOptions g;
    g.local_owner=true;
    g.goliath_health_bonus=true;
    g.reveal_cheat=true;
    const auto goliath=make_unit_of_kind(2U,0U,0U,0.0F,0.0F,0U,0,UnitControl::player,g);
    REQUIRE(goliath->unit.max_health==300 && goliath->unit.health==300);
    REQUIRE((goliath->unit.flags&0x80U)!=0U);
    return true;
}

RUNTIME_TEST(goliath_snaps_to_rail_zone) {
    const AngleTable angles=reference_angle_table();
    UnitKindSpawnOptions o;
    o.game_mode=5;
    o.local_owner=true;
    o.goliath_health_bonus=true;
    o.angles=&angles;
    o.rail=RailZone{true,0.0F,0.0F,1000.0F,0.0F};
    const auto g=make_unit_of_kind(2U,0U,0U,300.0F,50.0F,0U,0,UnitControl::player,o);
    REQUIRE(g && g->rail && g->rail_claimed);
    REQUIRE((g->unit.flags&0x0804U)==0x0804U);
    REQUIRE(near(g->unit.x,300.0) && near(g->unit.z,0.0) && near(g->rail->travel,300.0));
    REQUIRE(g->unit.max_health==400);   // rail goliath doubles
    REQUIRE(near(g->unit.heading_degrees,0.0,0.01) || near(g->unit.heading_degrees,360.0,0.01));
    o.owner_rail_taken=true;
    REQUIRE(!make_unit_of_kind(2U,0U,0U,300.0F,50.0F,0U,0,UnitControl::player,o)->rail);
    o.owner_rail_taken=false;
    o.game_mode=2;
    REQUIRE(!make_unit_of_kind(2U,0U,0U,300.0F,50.0F,0U,0,UnitControl::player,o)->rail);
    return true;
}

RUNTIME_TEST(rail_unit_accelerates_bounces_and_exits) {
    UnitRecord unit=make_unit(retail::UnitKind::goliath,0U,0U,0.0F,0.0F,0U,0,UnitControl::player);
    unit.flags=flag_rail_spawn;
    RailState rail{0.0F,0.0F,1.0F,0.0F,100.0F,0.0F,0.0F,0};
    RailInput in;
    in.move=-100.0F;
    update_rail_unit(unit,rail,in,5,1,1.0F,false);
    REQUIRE(near(rail.velocity,6.625) && near(unit.x,6.625));
    for (int i=0; i<20; ++i) update_rail_unit(unit,rail,in,5,2+i,1.0F,false);
    REQUIRE(near(rail.travel,100.0) || rail.velocity<0.0F);
    in.move=0.0F;
    in.boost=true;
    update_rail_unit(unit,rail,in,5,100,1.0F,false);
    REQUIRE(rail.exit_tick==100);
    const RailStepResult r=update_rail_unit(unit,rail,in,5,161,1.0F,false);
    REQUIRE(r.exited && (unit.flags&flag_rail_unit)==0U);
    return true;
}

RUNTIME_TEST(turret_spawn_uses_type_tables) {
    TurretFixture f;
    TurretContext c=f.context(0);
    const auto t=spawn_turret(1U,1U,2U,0U,0.0F,0.0F,false,true,c);
    REQUIRE(t && t->health==100 && t->fade==0xFFU && (t->flags&turret_scaled)!=0U && t->aim_time==-1);
    REQUIRE(!spawn_turret(1U,1U,4U,0U,0.0F,0.0F,false,false,c));
    c.area_clear=[](float,float,float half) { return half<50.0F; };
    REQUIRE(!spawn_turret(1U,1U,2U,0U,0.0F,0.0F,true,false,c));
    REQUIRE(spawn_turret(1U,1U,1U,0U,0.0F,0.0F,true,false,c));
    return true;
}

RUNTIME_TEST(turret_targets_nearest_enemy_in_range) {
    TurretFixture f;
    f.units.push_back(make_unit(retail::UnitKind::tank,0U,1U,10.0F,0.0F,0U,0,UnitControl::player)); // same team
    f.units.push_back(make_unit(retail::UnitKind::tank,1U,0U,0.0F,900.0F,0U,0,UnitControl::player));
    f.units.push_back(make_unit(retail::UnitKind::tank,2U,0U,0.0F,500.0F,0U,0,UnitControl::player));
    f.units.push_back(make_unit(retail::UnitKind::tank,3U,0U,0.0F,3000.0F,0U,0,UnitControl::player));
    TurretContext c=f.context(0);
    TurretRecord t;
    t.team=1U;
    find_turret_target(t,c);
    REQUIRE(t.target==2U);
    f.units.resize(1);
    f.units.push_back(make_unit(retail::UnitKind::tank,3U,0U,0.0F,3000.0F,0U,0,UnitControl::player));
    auto second_context=f.context(0);
    find_turret_target(t,second_context);
    REQUIRE(t.target==no_turret_target);
    return true;
}

RUNTIME_TEST(turret_fades_turns_and_fires) {
    TurretFixture f;
    f.units.push_back(make_unit(retail::UnitKind::tank,0U,0U,0.0F,500.0F,0U,0,UnitControl::player));
    TurretContext c=f.context(0);
    auto t=*spawn_turret(1U,1U,1U,0x0800U,0.0F,0.0F,false,false,c);
    REQUIRE(t.target==0U);
    int fades=0;
    while (t.fade!=0U) {
        auto fade_context=f.context(fades);
        update_turret(t,fade_context);
        ++fades;
    }
    REQUIRE(fades==16);
    auto turn_context=f.context(100);
    const TurretUpdateResult r=update_turret(t,turn_context);
    REQUIRE(t.heading==0x0700U || t.heading==0x0600U || r.shot);
    std::optional<TurretShot> shot;
    for (int i=0; i<20 && !shot; ++i) {
        auto fire_context=f.context(200+i);
        shot=update_turret(t,fire_context).shot;
    }
    REQUIRE(shot && shot->speed==48 && shot->sound==1U && shot->projectile==0);
    REQUIRE(shot->velocity_z>40.0F);
    return true;
}

RUNTIME_TEST(turret_damage_scores_and_return_fire) {
    std::array<OwnerRecord,4> owners{};
    owners[0].team=0U;
    owners[1].team=1U;
    TurretRecord t;
    t.team=1U;
    t.owner=1U;
    t.type=3U;
    t.health=100;
    TurretDamageResult r=damage_turret(t,2U,40,0U,0x1000U,owners,50,5);
    REQUIRE(r.handled && r.return_fire && t.aim_time==50 && t.aim_heading==0x9000U && t.health==60);
    REQUIRE(!damage_turret(t,6U,40,0U,0U,owners,51,5).handled);
    r=damage_turret(t,22U,80,0U,0U,owners,52,5);
    REQUIRE(r.destroyed && r.score==2500 && owners[0].score==2500 && (t.flags&turret_destroyed)!=0U);
    return true;
}

RUNTIME_TEST(turret_rebuild_or_removal) {
    TurretFixture f;
    TurretContext c=f.context(1000);
    TurretRecord t;
    t.type=1U;
    t.fade=0U;
    t.health=0;
    REQUIRE(update_turret(t,c).removed && t.removed);
    TurretRecord s;
    s.type=1U;
    s.fade=0U;
    s.health=0;
    s.flags=turret_scaled;
    REQUIRE(update_turret(s,c).started_rebuild);
    REQUIRE(s.last_fire==1000+1800 && (s.flags&turret_rebuilding)!=0U);  // owner count 1 -> 1.0
    auto rebuild_context=f.context(2801);
    REQUIRE(update_turret(s,rebuild_context).rebuilt && s.health==50 && s.fade==0xFFU);
    return true;
}

RUNTIME_TEST(placed_base_stages_and_score) {
    std::array<OwnerRecord,4> owners{};
    owners[0].team=0U;
    owners[2].team=1U;
    PlacedObjectRecord base=make_placed_object(PlacedObjectClass::staged,13U,0U,3U,0.0F,0.0F);
    REQUIRE(base.health==17 && base.stage_count==3U);
    auto ev=damage_placed_object(base,2U,10,0U,false,owners,5);
    REQUIRE(ev.size()==1U && ev[0].kind==PlacedObjectEventKind::base_alert && ev[0].owner==2U);
    ev=damage_placed_object(base,34U,10,0U,false,owners,5);
    REQUIRE((base.flags&0x80U)!=0U && base.health==17);
    REQUIRE(damage_placed_object(base,2U,10,0U,false,owners,5).empty());
    for (int i=0; i<placed_collapse_ticks; ++i) ev=update_placed_object(base);
    REQUIRE(base.stage==1U && (base.flags&0x80U)==0U && ev.empty());
    ev=damage_placed_object(base,2U,20,0U,false,owners,5);
    REQUIRE(ev.back().kind==PlacedObjectEventKind::score_awarded && owners[0].score==1000);
    for (int i=0; i<placed_collapse_ticks; ++i) ev=update_placed_object(base);
    REQUIRE(placed_object_destroyed(base) && ev.size()==1U && ev[0].kind==PlacedObjectEventKind::destroyed);
    return true;
}

RUNTIME_TEST(placed_object_classes_and_destroy_effect) {
    std::array<OwnerRecord,4> owners{};
    PlacedObjectRecord shield=make_placed_object(PlacedObjectClass::staged,20U,0x20U,2U,0.0F,0.0F);
    REQUIRE(shield.health==1);
    damage_placed_object(shield,2U,50,0U,false,owners,5);
    REQUIRE(shield.health==1);
    damage_placed_object(shield,2U,50,0U,true,owners,5);
    REQUIRE((shield.flags&0x80U)!=0U);
    PlacedObjectRecord fence=make_placed_object(PlacedObjectClass::knockable,3U,0U,1U,0.0F,0.0F);
    REQUIRE(damage_placed_object(fence,2U,5,0U,false,owners,5).empty());
    REQUIRE(damage_placed_object(fence,3U,5,0U,false,owners,5).size()==1U && placed_object_destroyed(fence));
    PlacedObjectRecord car=make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,0.0F,0.0F);
    REQUIRE(damage_placed_object(car,44U,1,0U,false,owners,5).size()==1U && placed_object_destroyed(car));
    REQUIRE(destroy_placed_object(0U).explosion_variant==5);
    const PlacedDestroyResult big=destroy_placed_object(0x10U);
    REQUIRE(big.explosion_variant==22 && big.splash);
    REQUIRE(destroy_placed_object(1U).explosion_variant==-1 && !destroy_placed_object(1U).notify_area);
    StateWriter w;
    car.serialize(w);
    REQUIRE(w.bytes().size()==23U);
    return true;
}

RUNTIME_TEST_MAIN()
