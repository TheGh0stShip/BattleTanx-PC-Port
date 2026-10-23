#include "battletanx/combat.hpp"
#include "battletanx/projectile_runtime.hpp"
#include "battletanx/runtime_math.hpp"

#include "runtime_test_support.hpp"

#include <algorithm>

namespace {
using namespace battletanx;

struct Arena {
    UnitPool units;
    std::array<OwnerRecord,owner_capacity> owners{};
    GameplayEventQueue events;
    SoundEventQueue sounds;
    CombatConfig config;
    ShellPool shells;
    WorldCollision world;
    AngleTable angles=reference_angle_table();
    std::int32_t tick{100};

    Arena() {
        for (std::size_t i=0U; i<2U; ++i) {
            owners[i].present=true;
            owners[i].team=static_cast<std::uint8_t>(i);
            owners[i].squad_cap=1U;
        }
        owners[1].computer=true;
    }
    CombatContext combat() { return CombatContext{units,owners,events,sounds,config,tick}; }
    EntityId add(std::uint8_t owner,float x,float z,std::uint16_t heading,UnitControl control) {
        const EntityId id=*units.allocate();
        *units.get(id)=make_unit(retail::UnitKind::tank,owner,owners[owner].team,x,z,heading,0,control);
        ++owners[owner].live_units;
        return id;
    }
};

template <typename Kind>
std::size_t count(const GameplayEventQueue& queue,Kind kind) {
    std::size_t n=0U;
    for (const auto& event : queue.events()) n+=event.kind==kind ? 1U : 0U;
    return n;
}
} // namespace

RUNTIME_TEST(tank_defaults_match_spawn_unit) {
    const UnitRecord unit=make_unit(retail::UnitKind::tank,0U,0U,10.0F,20.0F,0x4000U,5,UnitControl::player);
    REQUIRE(unit.max_health==100 && unit.health==100);
    REQUIRE(unit.primary_weapon.kind==1U);
    REQUIRE(unit.primary_weapon.last_fire_tick==-1000);
    REQUIRE(unit.ammo[1]==70);
    REQUIRE(unit.footprint.min_x==-35 && unit.footprint.max_z==81);
    REQUIRE(runtime_test::near(unit.heading_degrees,90.0));
    REQUIRE(make_unit(retail::UnitKind::bike,0U,0U,0,0,0,0,UnitControl::player).max_health==30);
    REQUIRE(make_unit(retail::UnitKind::goliath,0U,0U,0,0,0,0,UnitControl::player).max_health==200);
    return true;
}

RUNTIME_TEST(player_tank_adapter_preserves_tread_animation_state) {
    PlayerTank tank;
    tank.track_left=-39;
    tank.track_right=41;
    tank.track_phase_left=0xD8U;
    tank.track_phase_right=0x27U;
    UnitRecord unit=unit_from_player_tank(tank,0U,0U,0);
    REQUIRE(unit.track_left==-39 && unit.track_right==41);
    REQUIRE(unit.track_phase_left==0xD8U && unit.track_phase_right==0x27U);
    PlayerTank restored;
    sync_player_tank_from_unit(restored,unit);
    REQUIRE(restored.track_left==-39 && restored.track_right==41);
    REQUIRE(restored.track_phase_left==0xD8U && restored.track_phase_right==0x27U);
    return true;
}

RUNTIME_TEST(scaled_damage_truncates_and_clamps_to_one) {
    REQUIRE(scaled_damage(17,100.0F,nullptr)==17);
    REQUIRE(scaled_damage(17,80.0F,nullptr)==13);          // 13.6 truncated
    const float armor=120.0F;
    REQUIRE(scaled_damage(17,100.0F,&armor)==14);          // 17/1.2 = 14.16
    REQUIRE(scaled_damage(1,10.0F,nullptr)==1);            // 0 becomes 1
    REQUIRE(human_owner_scale(0U)==80.0F && human_owner_scale(2U)==120.0F && human_owner_scale(1U)==100.0F);
    return true;
}

RUNTIME_TEST(seventeen_damage_destroys_a_tank_on_the_sixth_hit) {
    Arena arena;
    const EntityId target=arena.add(1U,0.0F,0.0F,0U,UnitControl::computer);
    auto context=arena.combat();
    for (int hit=1; hit<=5; ++hit) {
        REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::survived);
        REQUIRE(arena.units.get(target)->health==100-17*hit);
    }
    REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::destroyed);
    const UnitRecord& unit=*arena.units.get(target);
    REQUIRE(!unit.alive && unit.health==0);
    REQUIRE(arena.owners[1].losses==1 && arena.owners[1].live_units==0U);
    REQUIRE(arena.owners[0].kills==1 && arena.owners[0].score==1000);
    REQUIRE(count(arena.events,GameplayEventKind::unit_destroyed)==1U);
    REQUIRE(arena.sounds.events().back().sound_id==retail::sound_unit_destroyed);
    REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::ignored);
    return true;
}

RUNTIME_TEST(friendly_fire_damages_but_does_not_credit_a_kill) {
    Arena arena;
    arena.owners[1].team=0U;
    const EntityId target=arena.add(1U,0.0F,0.0F,0U,UnitControl::computer);
    auto context=arena.combat();
    arena.units.get(target)->health=10;
    REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::destroyed);
    REQUIRE(arena.owners[0].kills==0 && arena.owners[0].score==0);
    REQUIRE(arena.owners[1].team_losses==1);
    return true;
}

RUNTIME_TEST(invulnerability_and_global_switches_ignore_damage) {
    Arena arena;
    const EntityId target=arena.add(1U,0.0F,0.0F,0U,UnitControl::computer);
    auto context=arena.combat();
    arena.units.get(target)->flags=retail::flag_campaign_shielded;
    REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::ignored);
    arena.units.get(target)->flags=0U;
    arena.config.damage_disabled=true;
    REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::ignored);
    arena.config.damage_disabled=false;
    arena.units.get(target)->flags=retail::flag_instant_death;
    REQUIRE(apply_unit_damage(context,target,1,0U,true)==DamageResult::destroyed);
    return true;
}

RUNTIME_TEST(shield_absorbs_before_health) {
    Arena arena;
    const EntityId target=arena.add(1U,0.0F,0.0F,0U,UnitControl::computer);
    auto context=arena.combat();
    arena.units.get(target)->shield_strength=10;
    REQUIRE(apply_unit_damage(context,target,17,0U,true)==DamageResult::survived);
    REQUIRE(arena.units.get(target)->shield_strength==0);
    REQUIRE(arena.units.get(target)->health==93);
    return true;
}

RUNTIME_TEST(rotated_footprint_segment_test) {
    const retail::Footprint box=retail::unit_footprint[0];
    // Unrotated: a segment along +z through the centre enters at z=-73.
    auto hit=segment_footprint_distance(0.0F,-200.0F,0.0F,200.0F,0.0F,0.0F,0.0F,1.0F,box);
    REQUIRE(hit && runtime_test::near(*hit,127.0,1e-3));
    // A segment at x=50 misses the unrotated box (half width 35)...
    REQUIRE(!segment_footprint_distance(50.0F,-200.0F,50.0F,200.0F,0.0F,0.0F,0.0F,1.0F,box));
    // ...but hits it once rotated 90 degrees (length now along x).
    hit=segment_footprint_distance(50.0F,-200.0F,50.0F,200.0F,0.0F,0.0F,1.0F,0.0F,box);
    REQUIRE(hit && runtime_test::near(*hit,165.0,1e-3));
    return true;
}

RUNTIME_TEST(nearest_unit_hit_ignores_owner_and_breaks_ties_by_slot) {
    Arena arena;
    const EntityId shooter=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId far=arena.add(1U,0.0F,600.0F,0U,UnitControl::computer);
    const EntityId near=arena.add(1U,0.0F,300.0F,0U,UnitControl::computer);
    auto hit=find_unit_segment_hit(arena.units,arena.angles,0.0F,0.0F,0.0F,1000.0F,shooter);
    REQUIRE(hit && hit->unit==near);
    arena.units.get(near)->alive=false;
    hit=find_unit_segment_hit(arena.units,arena.angles,0.0F,0.0F,0.0F,1000.0F,shooter);
    REQUIRE(hit && hit->unit==far);
    // Two units at the same distance: the lower slot wins.
    arena.units.get(near)->alive=true;
    arena.units.get(near)->z=600.0F;
    hit=find_unit_segment_hit(arena.units,arena.angles,0.0F,0.0F,0.0F,1000.0F,shooter);
    REQUIRE(hit && hit->unit==far && far.slot<near.slot);
    REQUIRE(!find_unit_segment_hit(arena.units,arena.angles,500.0F,0.0F,500.0F,1000.0F,shooter));
    return true;
}

RUNTIME_TEST(cannon_cooldown_ammo_and_events) {
    Arena arena;
    const EntityId shooter=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    auto combat=arena.combat();
    ProjectileContext context{combat,arena.shells,arena.world,arena.angles};
    UnitRecord& unit=*arena.units.get(shooter);
    REQUIRE(fire_unit_weapon(context,shooter,unit.primary_weapon,{true,false})==FireResult::fired);
    REQUIRE(unit.ammo[1]==69 && unit.primary_weapon.last_fire_tick==100);
    REQUIRE(arena.sounds.events().size()==1U && arena.sounds.events()[0].sound_id==1U);
    REQUIRE(!arena.sounds.events()[0].positional);
    // Retail compares last + 30 < tick, so tick 130 is still cooling down.
    combat.tick=130;
    REQUIRE(fire_unit_weapon(context,shooter,unit.primary_weapon,{true,false})==FireResult::not_ready);
    combat.tick=131;
    REQUIRE(fire_unit_weapon(context,shooter,unit.primary_weapon,{true,false})==FireResult::fired);
    REQUIRE(arena.shells.size()==2U);
    const ShellRecord& shell=*arena.shells.get(EntityId{0U,1U});
    REQUIRE(runtime_test::near(shell.z,45.0) && runtime_test::near(shell.y,36.0));
    REQUIRE(runtime_test::near(shell.velocity_z,110.0) && shell.damage==0x11U && shell.life==0x70);
    REQUIRE(count(arena.events,GameplayEventKind::weapon_fired)==2U);
    return true;
}

RUNTIME_TEST(empty_trigger_emits_click_and_no_shell) {
    Arena arena;
    const EntityId shooter=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    auto combat=arena.combat();
    ProjectileContext context{combat,arena.shells,arena.world,arena.angles};
    UnitRecord& unit=*arena.units.get(shooter);
    unit.ammo[1]=0;
    REQUIRE(fire_unit_weapon(context,shooter,unit.primary_weapon,{true,true})==FireResult::empty);
    REQUIRE(arena.shells.size()==0U);
    REQUIRE(arena.sounds.events().size()==1U && arena.sounds.events()[0].sound_id==0x20U);
    REQUIRE(count(arena.events,GameplayEventKind::weapon_fired)==0U);
    REQUIRE(unit.primary_weapon.last_fire_tick==-1000);
    return true;
}

RUNTIME_TEST(unlimited_ammo_and_computer_units_do_not_consume) {
    Arena arena;
    const EntityId player=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId enemy=arena.add(1U,500.0F,0.0F,0U,UnitControl::computer);
    arena.config.unlimited_ammo=true;
    auto combat=arena.combat();
    ProjectileContext context{combat,arena.shells,arena.world,arena.angles};
    REQUIRE(fire_unit_weapon(context,player,arena.units.get(player)->primary_weapon,{true,false})==FireResult::fired);
    REQUIRE(fire_unit_weapon(context,enemy,arena.units.get(enemy)->primary_weapon,{true,false})==FireResult::fired);
    REQUIRE(arena.units.get(player)->ammo[1]==70 && arena.units.get(enemy)->ammo[1]==70);
    REQUIRE(arena.sounds.events()[1].positional);
    return true;
}

RUNTIME_TEST(shell_travels_hits_and_is_removed) {
    Arena arena;
    const EntityId shooter=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    const EntityId target=arena.add(1U,0.0F,500.0F,0U,UnitControl::computer);
    auto combat=arena.combat();
    ProjectileContext context{combat,arena.shells,arena.world,arena.angles};
    REQUIRE(fire_unit_weapon(context,shooter,arena.units.get(shooter)->primary_weapon,{true,false})==FireResult::fired);
    int updates=0;
    while (arena.shells.size()>0U && updates<200) { update_shells(context); ++updates; }
    // Muzzle z=45, box front z=500-73=427: impact during the 4th move (45+4*110=485).
    REQUIRE(updates==4);
    REQUIRE(arena.units.get(target)->health==83);
    REQUIRE(count(arena.events,GameplayEventKind::projectile_impact)==1U);
    REQUIRE(arena.sounds.events().back().sound_id==retail::sound_hit_unit);
    const auto& impact=arena.events.events().back();
    REQUIRE(impact.other==target && runtime_test::near(impact.z,427.0,1e-2));
    return true;
}

RUNTIME_TEST(shell_expires_after_life_and_respects_world_range) {
    Arena arena;
    const EntityId shooter=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    auto combat=arena.combat();
    ProjectileContext context{combat,arena.shells,arena.world,arena.angles};
    REQUIRE(fire_unit_weapon(context,shooter,arena.units.get(shooter)->primary_weapon,{true,false})==FireResult::fired);
    int updates=0;
    while (arena.shells.size()>0U) { update_shells(context); ++updates; }
    REQUIRE(updates==0x70);   // 111 moves, removed on the 112th update
    // A wall 300 units ahead: the probe sets range, then the static test hits it.
    WorldCollision wall;
    wall.segment=[](float,float z0,float,float z1)->std::optional<SegmentHit> {
        if (z0<=300.0F && z1>=300.0F) return SegmentHit{300.0F-z0,0.0F,300.0F};
        return std::nullopt;
    };
    ProjectileContext walled{combat,arena.shells,wall,arena.angles};
    combat.tick=500;
    REQUIRE(fire_unit_weapon(walled,shooter,arena.units.get(shooter)->primary_weapon,{true,false})==FireResult::fired);
    const EntityId shell=arena.shells.slot_id(0U);
    REQUIRE(runtime_test::near(arena.shells.get(shell)->range,255.0,1e-3));
    updates=0;
    while (arena.shells.size()>0U) { update_shells(walled); ++updates; }
    REQUIRE(updates==3);
    REQUIRE(arena.sounds.events().back().sound_id==retail::sound_hit_other);
    return true;
}

RUNTIME_TEST(level_collision_finds_nearest_static_shape_and_navigation_boundary) {
    WorldLevel world;
    world.shapes.push_back(WorldShape{1U,0U,-20,-10,20,10});
    WorldItem wall;
    wall.type=1;
    wall.shape=0;
    wall.z=300.0F;
    world.items.push_back(wall);

    NavigationLevel navigation;
    navigation.vertices={{0.0F,0.0F},{-1000.0F,-1000.0F},
                         {1000.0F,-1000.0F},{0.0F,1000.0F}};
    NavigationTriangle unused;
    NavigationTriangle ground;
    ground.corners={NavigationCorner{1U,0U},NavigationCorner{2U,0U},NavigationCorner{3U,0U}};
    navigation.triangles={unused,ground};
    if (!navigation_point_inside_triangle(navigation,{0.0F,0.0F},1U)) {
        std::swap(navigation.triangles[1].corners[1],navigation.triangles[1].corners[2]);
    }
    const AngleTable angles=reference_angle_table();
    const WorldCollision collision=level_world_collision(world,navigation,angles);
    const auto wall_hit=collision.segment(0.0F,0.0F,0.0F,900.0F);
    REQUIRE(wall_hit && runtime_test::near(wall_hit->distance,290.0,1e-3));
    REQUIRE(runtime_test::near(wall_hit->z,290.0,1e-3));

    world.items.clear();
    const WorldCollision boundary=level_world_collision(world,navigation,angles);
    const auto edge_hit=boundary.segment(0.0F,0.0F,0.0F,2000.0F);
    REQUIRE(edge_hit && runtime_test::near(edge_hit->z,1000.0,0.01));
    REQUIRE(boundary.inside(0.0F,0.0F));
    REQUIRE(!boundary.inside(0.0F,1500.0F));
    return true;
}

RUNTIME_TEST(shell_pool_exhaustion_still_starts_cooldown) {
    Arena arena;
    const EntityId shooter=arena.add(0U,0.0F,0.0F,0U,UnitControl::player);
    while (arena.shells.allocate()) {}
    auto combat=arena.combat();
    ProjectileContext context{combat,arena.shells,arena.world,arena.angles};
    UnitRecord& unit=*arena.units.get(shooter);
    REQUIRE(fire_unit_weapon(context,shooter,unit.primary_weapon,{true,false})==FireResult::spawn_failed);
    REQUIRE(unit.primary_weapon.last_fire_tick==100 && unit.ammo[1]==69);
    return true;
}

RUNTIME_TEST(retail_angle_conversion_wraps_through_signed_word) {
    REQUIRE(degrees_to_retail_angle(0.0F)==0U);
    REQUIRE(degrees_to_retail_angle(90.0F)==0x4000U);
    REQUIRE(degrees_to_retail_angle(360.0F)==0U);
    REQUIRE(degrees_to_retail_angle(-1.0F)==0xFF4AU);
    REQUIRE(direction_angle(0.0F,1.0F)==0U);
    REQUIRE(direction_angle(1.0F,0.0F)==0x4000U);
    REQUIRE(direction_angle(0.0F,-1.0F)==0x8000U);
    REQUIRE(direction_angle(-1.0F,0.0F)==0xC000U);
    REQUIRE(direction_angle(1.0F,1.0F)==0x2000U);
    REQUIRE(angle_delta(0xFF00U,0x0100U)==0x200);
    return true;
}

RUNTIME_TEST_MAIN()
