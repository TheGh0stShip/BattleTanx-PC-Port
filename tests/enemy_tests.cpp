#include "battletanx/enemy_control.hpp"
#include "battletanx/runtime_math.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <cstdlib>

using namespace battletanx;

namespace {

constexpr std::int32_t now=1000;

UnitRecord unit_at(std::uint8_t team,float x,float z,UnitControl control,float heading=0.0F) {
    UnitRecord unit{};
    unit.kind=retail::UnitKind::tank;
    unit.team=team;
    unit.owner=team;
    unit.alive=true;
    unit.control=control;
    unit.x=x;
    unit.z=z;
    unit.heading_degrees=heading;
    unit.turret_degrees=heading;
    unit.spawn_tick=0;
    return unit;
}

EntityId add(UnitPool& pool,const UnitRecord& unit) {
    const EntityId id=*pool.allocate();
    *pool.get(id)=unit;
    return id;
}

// Applies the port's drive convention: heading -= rotation * k.
float turned_heading(float heading,float rotation) {
    float h=heading-rotation*0.18F*0.15F;
    while (h<0.0F) h+=360.0F;
    while (h>=360.0F) h-=360.0F;
    return h;
}

} // namespace

RUNTIME_TEST(nearest_single_player) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId player=add(pool,unit_at(0,100,0,UnitControl::player));
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==player);
    return true;
}

RUNTIME_TEST(nearest_ignores_dead) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    UnitRecord dead=unit_at(0,100,0,UnitControl::player);
    dead.alive=false;
    add(pool,dead);
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==no_entity);
    return true;
}

RUNTIME_TEST(nearest_ignores_friendly) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    add(pool,unit_at(1,10,0,UnitControl::computer));
    const EntityId far=add(pool,unit_at(0,900,0,UnitControl::player));
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==far);
    return true;
}

RUNTIME_TEST(nearest_picks_closer) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    add(pool,unit_at(0,300,300,UnitControl::player));
    const EntityId close=add(pool,unit_at(0,-50,40,UnitControl::player));
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==close);
    return true;
}

RUNTIME_TEST(nearest_tie_keeps_lower_slot) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId first=add(pool,unit_at(0,100,0,UnitControl::player));
    add(pool,unit_at(0,-100,0,UnitControl::player));
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==first);
    return true;
}

RUNTIME_TEST(nearest_spawn_grace) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    UnitRecord fresh=unit_at(0,100,0,UnitControl::player);
    fresh.spawn_tick=now-89;
    const EntityId id=add(pool,fresh);
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==no_entity);
    pool.get(id)->spawn_tick=now-90; // now - 0x5A >= stamp
    REQUIRE(find_nearest_enemy_unit(pool,self,now,16000.0F)==id);
    return true;
}

RUNTIME_TEST(nearest_range_rejection) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    // limit = (int)(1000^2*0.92) = 920000; 959^2=919681 inside, 960^2=921600 outside.
    const EntityId id=add(pool,unit_at(0,959,0,UnitControl::player));
    REQUIRE(find_nearest_enemy_unit(pool,self,now,1000.0F)==id);
    pool.get(id)->x=960.0F;
    REQUIRE(find_nearest_enemy_unit(pool,self,now,1000.0F)==no_entity);
    return true;
}

RUNTIME_TEST(stale_target_handle) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId target=add(pool,unit_at(0,100,0,UnitControl::player));
    REQUIRE(target_still_valid(pool,self,target,now));
    pool.release(target);
    const EntityId reused=add(pool,unit_at(0,100,0,UnitControl::player));
    REQUIRE(reused.slot==target.slot);
    REQUIRE(!target_still_valid(pool,self,target,now));
    REQUIRE(!target_still_valid(pool,self,self,now));
    return true;
}

RUNTIME_TEST(target_destroyed_reacquires_or_none) {
    UnitPool pool;
    const AngleTable angles=reference_angle_table();
    RetailRandom rng{1U};
    EnemyController controller;
    std::array<EnemyCommand,unit_capacity> out{};
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId a=add(pool,unit_at(0,0,100,UnitControl::player));
    const EntityId b=add(pool,unit_at(0,0,800,UnitControl::player));
    REQUIRE(controller.update(pool,angles,rng,now,out)==1U);
    REQUIRE(out[0].unit==self && out[0].target==a);
    pool.get(a)->alive=false;
    REQUIRE(controller.update(pool,angles,rng,now+1,out)==1U);
    REQUIRE(out[0].target==b);
    pool.release(b);
    REQUIRE(controller.update(pool,angles,rng,now+2,out)==1U);
    REQUIRE(out[0].target==no_entity);
    REQUIRE(out[0].input.throttle==0.0F && out[0].input.rotation==0.0F && !out[0].input.fire);
    return true;
}

RUNTIME_TEST(steering_sign_reduces_error) {
    const AngleTable angles=reference_angle_table();
    for (const float goal_x : {500.0F,-500.0F}) {
        UnitRecord unit=unit_at(1,0,0,UnitControl::computer,10.0F);
        const std::int32_t before=std::abs(heading_error_to(unit.heading_degrees,0,0,goal_x,300));
        const DriveInput drive=compute_enemy_drive(unit,goal_x,300,angles,EnemyDriveTuning{});
        REQUIRE(drive.rotation!=0.0F);
        unit.heading_degrees=turned_heading(unit.heading_degrees,drive.rotation);
        const std::int32_t after=std::abs(heading_error_to(unit.heading_degrees,0,0,goal_x,300));
        REQUIRE(after<before);
        // Forward vector after the turn points closer to the goal.
        const std::uint16_t h=degrees_to_retail_angle(unit.heading_degrees);
        REQUIRE((angles.sine(h)>0.0F)==(goal_x>0.0F) || after<before);
    }
    return true;
}

RUNTIME_TEST(aligned_full_throttle_zero_rotation) {
    const AngleTable angles=reference_angle_table();
    const UnitRecord unit=unit_at(1,0,0,UnitControl::computer,0.0F);
    const DriveInput drive=compute_enemy_drive(unit,0,1000,angles,EnemyDriveTuning{});
    REQUIRE(runtime_test::near(drive.throttle,80.0));
    REQUIRE(runtime_test::near(drive.rotation,0.0));
    EnemyDriveTuning half{};
    half.speed=8.0F;
    REQUIRE(runtime_test::near(compute_enemy_drive(unit,0,1000,angles,half).throttle,40.0));
    return true;
}

RUNTIME_TEST(behind_turns_in_place) {
    const AngleTable angles=reference_angle_table();
    const UnitRecord unit=unit_at(1,0,0,UnitControl::computer,0.0F);
    const DriveInput drive=compute_enemy_drive(unit,10,-1000,angles,EnemyDriveTuning{});
    REQUIRE(drive.throttle==0.0F);
    REQUIRE(std::abs(drive.rotation)==200.0F);
    return true;
}

RUNTIME_TEST(approach_blend_within_40) {
    const AngleTable angles=reference_angle_table();
    const UnitRecord unit=unit_at(1,0,0,UnitControl::computer,0.0F);
    DriveTracker tracker{};
    tracker.distance=100.0F;
    (void)compute_enemy_drive(unit,0,30,angles,EnemyDriveTuning{},&tracker); // delta<0 latches
    REQUIRE(tracker.approaching && runtime_test::near(tracker.distance,30.0));
    const DriveInput drive=compute_enemy_drive(unit,0,30,angles,EnemyDriveTuning{},&tracker);
    // (1 + (1 - 30/40)) / 2 = 0.625 -> 50
    REQUIRE(runtime_test::near(drive.throttle,50.0));
    return true;
}

RUNTIME_TEST(aim_rate_law) {
    UnitRecord unit=unit_at(1,0,0,UnitControl::computer,0.0F);
    const EnemyAim far=compute_enemy_aim(unit,1000,-10);
    REQUIRE(std::abs(far.hull_rotation)==40.0F && far.aim==0.0F && !far.in_fire_window);
    const EnemyAim aligned=compute_enemy_aim(unit,0,1000);
    REQUIRE(aligned.in_fire_window && aligned.hull_rotation==0.0F);
    unit.kind=retail::UnitKind::bike;
    unit.turret_degrees=90.0F;
    const EnemyAim bike=compute_enemy_aim(unit,0,1000);
    REQUIRE(std::abs(bike.aim)==135.0F && bike.hull_rotation==0.0F);
    return true;
}

RUNTIME_TEST(fire_only_aligned_and_in_range) {
    RetailRandom rng{7U};
    const UnitRecord self=unit_at(1,0,0,UnitControl::computer);
    const UnitRecord target=unit_at(0,0,300,UnitControl::player);
    const EnemyFirePolicy policy{};
    REQUIRE(decide_enemy_fire(self,target,0,300,now,retail::never_fired_tick,policy,rng).fire);
    REQUIRE(decide_enemy_fire(self,target,399,300,now,retail::never_fired_tick,policy,rng).fire);
    REQUIRE(!decide_enemy_fire(self,target,400,300,now,retail::never_fired_tick,policy,rng).fire);
    REQUIRE(!decide_enemy_fire(self,target,0,501,now,retail::never_fired_tick,policy,rng).fire);

    UnitPool pool;
    const AngleTable angles=reference_angle_table();
    EnemyController controller;
    std::array<EnemyCommand,4> out{};
    add(pool,unit_at(1,0,0,UnitControl::computer,0.0F));
    const EntityId player=add(pool,unit_at(0,0,400,UnitControl::player));
    REQUIRE(controller.update(pool,angles,rng,now,out)==1U && out[0].input.fire);
    pool.get(player)->x=400.0F; // misaligned
    REQUIRE(controller.update(pool,angles,rng,now+40,out)==1U && !out[0].input.fire);
    pool.get(player)->x=0.0F;
    pool.get(player)->z=2000.0F; // out of range: chases
    REQUIRE(controller.update(pool,angles,rng,now+80,out)==1U && !out[0].input.fire);
    REQUIRE(out[0].input.throttle>0.0F);
    return true;
}

RUNTIME_TEST(controller_skips_players_and_does_not_mutate) {
    UnitPool pool;
    const AngleTable angles=reference_angle_table();
    RetailRandom rng{3U};
    EnemyController controller;
    std::array<EnemyCommand,4> out{};
    const EntityId player=add(pool,unit_at(0,0,0,UnitControl::player));
    add(pool,unit_at(1,0,900,UnitControl::computer));
    const UnitRecord before=*pool.get(player);
    REQUIRE(controller.update(pool,angles,rng,now,out)==1U);
    REQUIRE(out[0].target==player);
    REQUIRE(pool.get(player)->x==before.x && pool.get(player)->heading_degrees==before.heading_degrees);
    return true;
}

RUNTIME_TEST(determinism_same_seed) {
    auto run=[](std::uint32_t seed) {
        UnitPool pool;
        const AngleTable angles=reference_angle_table();
        RetailRandom rng{seed};
        EnemyControllerConfig config{};
        config.fire.allow_specials=true;
        EnemyController controller{config};
        UnitRecord tough=unit_at(0,0,200,UnitControl::player);
        tough.flags=enemy_retail::flag_ai_class4;
        add(pool,tough);
        add(pool,unit_at(1,0,0,UnitControl::computer));
        add(pool,unit_at(1,30,-20,UnitControl::computer,5.0F));
        std::array<EnemyCommand,unit_capacity> out{};
        std::uint32_t digest=0U;
        for (std::int32_t t=now; t<now+200; ++t) {
            const std::size_t n=controller.update(pool,angles,rng,t,out);
            for (std::size_t i=0; i<n; ++i) {
                digest=digest*31U+out[i].unit.slot+(out[i].input.fire?2U:0U)+(out[i].input.special?5U:0U)+
                       static_cast<std::uint32_t>(static_cast<std::int32_t>(out[i].input.rotation));
            }
        }
        return std::pair{digest,rng.seed()};
    };
    REQUIRE(run(12345U)==run(12345U));
    REQUIRE(run(12345U).second!=run(54321U).second);
    return true;
}

RUNTIME_TEST_MAIN()
