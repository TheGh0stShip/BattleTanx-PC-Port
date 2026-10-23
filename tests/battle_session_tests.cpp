#include "battletanx/battle_session.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/runtime_math.hpp"

#include "runtime_test_support.hpp"

#include <iostream>
#include <string>

namespace {
using namespace battletanx;

SessionConfig config_with_seed(std::uint32_t seed) {
    SessionConfig config;
    config.rng_seed=seed;
    return config;
}

const AngleTable& angles() {
    static const AngleTable table=reference_angle_table();
    return table;
}

struct Battle {
    BattleSession session;
    EntityId player{};
    EntityId enemy{};

    explicit Battle(float enemy_distance=800.0F)
        : session(config_with_seed(0x1234U),angles(),
                  open_ground_unit_motion(angles())) {
        session.add_owner(0U,OwnerSetup{0U,false,1U,0,false});
        session.add_owner(1U,OwnerSetup{1U,true,5U,0,false});
        player=*session.spawn_unit(UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U});
        enemy=*session.spawn_unit(UnitSpawn{retail::UnitKind::tank,1U,0.0F,enemy_distance,0x8000U,0U});
        // Keep the deterministic success fixture alive through one shell that
        // was already in flight when the enemy was destroyed. Result-delay
        // damage remains enabled and is covered by the objective tests.
        session.units().get(player)->shield_strength=20;
    }
};

SessionInput hold_fire(std::int32_t frame) {
    SessionInput input;
    input.advance=frame==0;
    input.drive.fire=true;
    input.drive.fire_pressed=frame==0;
    return input;
}

struct Outcome {
    std::string checksum;
    MissionPhase phase{};
    std::int32_t frames{};
    std::size_t player_shots{}, enemy_shots{}, impacts{}, destroyed{};
    std::size_t first_destroyed_index{};
    bool succeeded_event{}, failed_event{};
};

template <typename InputFor>
Outcome run(Battle& battle,std::int32_t max_frames,InputFor input_for) {
    Outcome outcome;
    std::size_t index=0U;
    for (std::int32_t frame=0; frame<max_frames; ++frame) {
        battle.session.step(input_for(frame));
        for (const GameplayEvent& event : battle.session.gameplay_events()) {
            ++index;
            switch (event.kind) {
            case GameplayEventKind::weapon_fired:
                if (event.owner==0U) ++outcome.player_shots; else ++outcome.enemy_shots;
                break;
            case GameplayEventKind::projectile_impact: ++outcome.impacts; break;
            case GameplayEventKind::unit_destroyed:
                if (outcome.destroyed++==0U) outcome.first_destroyed_index=index;
                break;
            case GameplayEventKind::mission_succeeded: outcome.succeeded_event=true; break;
            case GameplayEventKind::mission_failed: outcome.failed_event=true; break;
            default: break;
            }
        }
        battle.session.clear_events();
        outcome.frames=frame+1;
        const MissionPhase phase=battle.session.mission().phase();
        if (phase==MissionPhase::succeeded || phase==MissionPhase::failed) break;
    }
    outcome.phase=battle.session.mission().phase();
    outcome.checksum=battle.session.checksum();
    return outcome;
}

constexpr const char* expected_battle_checksum=
    "9d000ceff2f9f7ccce3c6e67ce55f4ddbafe461c77858aa2a9247b07e104a415";
} // namespace

RUNTIME_TEST(headless_battle_player_destroys_enemy_and_succeeds) {
    Battle battle;
    const Outcome outcome=run(battle,4000,hold_fire);
    std::cout << "battle checksum " << outcome.checksum << " frames " << outcome.frames
              << " player shots " << outcome.player_shots << " enemy shots " << outcome.enemy_shots
              << " impacts " << outcome.impacts << '\n';
    REQUIRE(outcome.phase==MissionPhase::succeeded);
    REQUIRE(outcome.succeeded_event && !outcome.failed_event);
    REQUIRE(outcome.player_shots>=6U);
    REQUIRE(outcome.destroyed==1U);
    REQUIRE(outcome.impacts>=6U);
    REQUIRE(!battle.session.units().contains(battle.enemy));     // released after destruction
    const UnitRecord* player=battle.session.units().get(battle.player);
    REQUIRE(player!=nullptr && player->alive);
    REQUIRE(player->ammo[1]==70-static_cast<std::int16_t>(outcome.player_shots));
    REQUIRE(battle.session.owners()[0].kills==1 && battle.session.owners()[0].score==1000);
    REQUIRE(battle.session.mission().result(0)==OwnerResult::won);
    return true;
}

RUNTIME_TEST(headless_battle_checksum_is_stable) {
    Battle first;
    Battle second;
    const Outcome a=run(first,4000,hold_fire);
    const Outcome b=run(second,4000,hold_fire);
    REQUIRE(a.checksum==b.checksum);
    REQUIRE(a.frames==b.frames);
    REQUIRE(a.checksum==expected_battle_checksum);
    return true;
}

RUNTIME_TEST(one_tick_input_change_diverges) {
    Battle first;
    Battle second;
    const Outcome a=run(first,300,hold_fire);
    const Outcome b=run(second,300,[](std::int32_t frame) {
        SessionInput input=hold_fire(frame);
        if (frame==150) input.drive.rotation=80.0F;   // one tick of hull rotation
        return input;
    });
    REQUIRE(a.checksum!=b.checksum);
    return true;
}

RUNTIME_TEST(bike_fires_alternating_machine_guns_in_session) {
    BattleSession session(config_with_seed(9U),angles(),open_ground_unit_motion(angles()));
    session.add_owner(0U,OwnerSetup{0U,false,1U,0,false});
    session.add_owner(1U,OwnerSetup{1U,true,1U,0,false});
    const EntityId bike=*session.spawn_unit(
        UnitSpawn{retail::UnitKind::bike,0U,0.0F,0.0F,0U,0U});
    session.spawn_unit(UnitSpawn{retail::UnitKind::tank,1U,0.0F,5000.0F,0U,0U});
    session.mission().force_state(MatchState::live,0);
    SessionInput fire;
    fire.drive.fire=true;
    fire.drive.fire_pressed=true;
    session.step(fire);
    fire.drive.fire_pressed=false;
    for (int i=0; i<4; ++i) session.step(fire);
    const UnitRecord* unit=session.units().get(bike);
    REQUIRE(unit!=nullptr && unit->primary_weapon.kind==2U && unit->ammo[2]==148);
    std::size_t projectile_count=0U;
    session.projectiles().for_each(
        [&](EntityId,const WeaponProjectile&) { ++projectile_count; });
    REQUIRE(projectile_count==2U);
    const WeaponProjectile* left=session.projectiles().get(session.projectiles().slot_id(0U));
    const WeaponProjectile* right=session.projectiles().get(session.projectiles().slot_id(1U));
    REQUIRE(left!=nullptr && right!=nullptr);
    REQUIRE(left->weapon==2U && left->bullet_kind==1U && left->damage==2U);
    REQUIRE(right->weapon==2U && right->bullet_kind==1U && right->damage==2U);
    REQUIRE(left->x<0.0F && right->x>0.0F);
    return true;
}

RUNTIME_TEST(campaign_spawner_fills_first_free_squad_slot) {
    BattleSession session(config_with_seed(0x1234U),angles(),open_ground_unit_motion(angles()));
    session.add_owner(0U,OwnerSetup{0U,false,1U,0,false});
    session.add_owner(1U,OwnerSetup{1U,true,5U,0,false});
    (void)session.spawn_unit(
        UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U,0U});
    (void)session.spawn_unit(
        UnitSpawn{retail::UnitKind::tank,1U,0.0F,50000.0F,0U,0U,0U});
    SpawnerDefinition definition=unpack_spawner(
        static_cast<std::uint16_t>(1U|(1U<<2U)),0U,false);
    definition.x=1000.0F;
    definition.z=1000.0F;
    REQUIRE(session.add_spawner(definition));
    session.set_spawner_budget(1U,1);
    session.mission().force_state(MatchState::live,0);
    std::size_t owner_units=1U;
    for (int frame=0; frame<1000 && owner_units==1U; ++frame) {
        session.step(SessionInput{});
        owner_units=0U;
        session.units().for_each([&](EntityId,const UnitRecord& unit) {
            if (unit.alive && unit.owner==1U) ++owner_units;
        });
        session.clear_events();
    }
    REQUIRE(owner_units==2U);
    bool found=false;
    session.units().for_each([&](EntityId,const UnitRecord& unit) {
        if (unit.owner==1U && unit.squad_slot==1U) {
            found=unit.kind==retail::UnitKind::bike &&
                  runtime_test::near(unit.x,1000.0) && runtime_test::near(unit.z,1000.0);
        }
    });
    REQUIRE(found);
    return true;
}

RUNTIME_TEST(pending_spawner_reserve_keeps_owner_in_elimination_match) {
    BattleSession session(config_with_seed(1U),angles(),open_ground_unit_motion(angles()));
    session.add_owner(0U,OwnerSetup{0U,false,1U,0,false});
    session.add_owner(1U,OwnerSetup{1U,true,5U,0,false});
    (void)session.spawn_unit(
        UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U,0U});
    const EntityId enemy=*session.spawn_unit(
        UnitSpawn{retail::UnitKind::tank,1U,0.0F,5000.0F,0U,0U,0U});
    SpawnerDefinition definition=unpack_spawner(
        static_cast<std::uint16_t>(1U|(2U<<4U)),0U,false);
    definition.x=5000.0F;
    definition.z=5000.0F;
    REQUIRE(session.add_spawner(definition));
    session.set_spawner_budget(1U,1);
    session.mission().force_state(MatchState::live,0);
    session.units().get(enemy)->flags=retail::flag_instant_death;
    REQUIRE(session.apply_damage(enemy,1,0U,true)==DamageResult::destroyed);
    session.step(SessionInput{});
    REQUIRE(session.mission().phase()==MissionPhase::active);
    REQUIRE(!session.owner_unit(1U));
    return true;
}

RUNTIME_TEST(idle_player_is_destroyed_and_mission_fails) {
    Battle battle;
    const Outcome outcome=run(battle,6000,[](std::int32_t frame) {
        SessionInput input;
        input.advance=frame==0;
        return input;
    });
    REQUIRE(outcome.player_shots==0U);
    REQUIRE(outcome.enemy_shots>=6U);
    REQUIRE(outcome.phase==MissionPhase::failed);
    REQUIRE(outcome.failed_event);
    REQUIRE(battle.session.mission().result(0)==OwnerResult::eliminated);
    return true;
}

RUNTIME_TEST(player_respawns_from_bank_at_respawn_point) {
    BattleSession session(config_with_seed(7U),angles(),
                          open_ground_unit_motion(angles()));
    session.add_owner(0U,OwnerSetup{0U,false,1U,1,false});
    session.add_owner(1U,OwnerSetup{1U,true,5U,0,false});
    const EntityId player=*session.spawn_unit(UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U});
    session.spawn_unit(UnitSpawn{retail::UnitKind::tank,1U,0.0F,5000.0F,0U,0U});
    session.set_respawn_point(0U,RespawnPoint{100.0F,-200.0F,0x4000U});
    session.mission().force_state(MatchState::live,0);
    session.units().get(player)->flags=retail::flag_instant_death;
    session.step(SessionInput{});
    REQUIRE(session.apply_damage(player,1,1U,false)==DamageResult::destroyed);
    bool respawned=false;
    std::int32_t frames=0;
    while (!respawned && frames<200) {
        session.step(SessionInput{});
        for (const GameplayEvent& event : session.gameplay_events()) {
            if (event.kind==GameplayEventKind::player_respawned) respawned=true;
        }
        session.clear_events();
        ++frames;
    }
    REQUIRE(respawned);
    REQUIRE(frames>=31 && frames<=33);
    const auto unit=session.owner_unit(0U);
    REQUIRE(unit && !(*unit==player));
    REQUIRE(runtime_test::near(session.units().get(*unit)->x,100.0));
    REQUIRE(session.units().get(*unit)->health==100);
    REQUIRE(session.owners()[0].respawn_bank[0]==0);
    REQUIRE(session.mission().phase()==MissionPhase::active);
    return true;
}

RUNTIME_TEST(campaign_record_round_trip_after_battle) {
    Battle battle;
    const Outcome outcome=run(battle,4000,hold_fire);
    REQUIRE(outcome.phase==MissionPhase::succeeded);
    CampaignRecord record=default_campaign_record();
    record.mission_id=static_cast<std::uint8_t>(record.mission_id+1U);
    record.carry=battle.session.owners()[0].score;
    const ControllerPakRecord bytes=serialize_campaign_record(record);
    REQUIRE(controller_pak_checksum_valid(bytes));
    const auto parsed=parse_campaign_record(bytes);
    REQUIRE(parsed && parsed->mission_id==2U && parsed->carry==1000);
    REQUIRE(serialize_campaign_record(*parsed).bytes==bytes.bytes);
    return true;
}

RUNTIME_TEST(paused_session_freezes_tick_and_units) {
    Battle battle;
    for (int frame=0; frame<200; ++frame) battle.session.step(hold_fire(frame));
    const std::int32_t tick=battle.session.tick();
    SessionInput pause;
    pause.start_pressed=true;
    battle.session.step(pause);
    REQUIRE(battle.session.mission().state()==MatchState::paused);
    const std::string frozen=battle.session.checksum();
    for (int frame=0; frame<30; ++frame) battle.session.step(SessionInput{});
    REQUIRE(battle.session.checksum()==frozen);
    REQUIRE(battle.session.tick()==tick);
    battle.session.step(pause);
    REQUIRE(battle.session.mission().state()==MatchState::live);
    return true;
}

RUNTIME_TEST(player_collects_health_pickup_during_battle) {
    Battle battle(4000.0F);
    PickupRecord health;
    health.id=EntityId{0U,1U};
    health.kind=0U;              // +30% health
    health.x=0.0F;
    health.z=0.0F;               // under the player tank
    REQUIRE(battle.session.add_pickup(health));
    battle.session.mission().force_state(MatchState::live,0);
    battle.session.units().get(battle.player)->shield_strength=0;
    REQUIRE(battle.session.apply_damage(battle.player,50,1U,false)==DamageResult::survived);
    battle.session.step(SessionInput{});
    REQUIRE(battle.session.units().get(battle.player)->health==80);   // 50 + (s32)(100*0.3)
    REQUIRE(battle.session.pickups()[0].consumed);
    bool event=false;
    bool sound=false;
    for (const GameplayEvent& e : battle.session.gameplay_events()) event|=e.kind==GameplayEventKind::pickup_collected;
    for (const SoundEvent& e : battle.session.sound_events()) sound|=e.sound_id==retail::sound_pickup;
    REQUIRE(event && sound);
    return true;
}

RUNTIME_TEST(kill_race_uses_destroy_events_and_ends_at_seven) {
    SessionConfig config=config_with_seed(9U);
    config.mission.game_kind=9U;
    BattleSession session(config,angles(),open_ground_unit_motion(angles()));
    session.add_owner(0U,OwnerSetup{0U,false,1U,0,false});
    session.add_owner(1U,OwnerSetup{1U,true,7U,0,false});
    (void)session.spawn_unit(UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U});
    for (std::uint8_t i=0U; i<7U; ++i) {
        const EntityId enemy=*session.spawn_unit(UnitSpawn{
            retail::UnitKind::tank,1U,static_cast<float>(i)*200.0F,1000.0F,0U,0U});
        session.units().get(enemy)->flags=retail::flag_instant_death;
        REQUIRE(session.apply_damage(enemy,1,0U,false)==DamageResult::destroyed);
    }
    session.mission().force_state(MatchState::live,0);
    session.step(SessionInput{});
    REQUIRE(session.objective()!=nullptr);
    REQUIRE(session.objective()->kill_counter(0U)==7);
    REQUIRE(session.mission().state()==MatchState::result_delay);
    for (int i=0; i<90; ++i) session.step(SessionInput{});
    REQUIRE(session.mission().state()==MatchState::result_banner);
    REQUIRE(session.mission().result(0U)==OwnerResult::won);
    return true;
}

RUNTIME_TEST(queen_rescue_contact_completes_kind_four) {
    SessionConfig config=config_with_seed(4U);
    config.mission.game_kind=4U;
    BattleSession session(config,angles(),open_ground_unit_motion(angles()));
    session.add_owner(0U,OwnerSetup{0U,false,1U,0,false});
    session.add_owner(1U,OwnerSetup{1U,true,1U,0,false});
    (void)session.spawn_unit(UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U});
    (void)session.spawn_unit(UnitSpawn{retail::UnitKind::tank,1U,0.0F,1000.0F,0U,0U});
    session.mission().force_state(MatchState::live,0);
    REQUIRE(session.queen_contact(0U)==ObjectiveEventStatus::applied);
    session.step(SessionInput{});
    REQUIRE(session.mission().state()==MatchState::result_banner);
    REQUIRE(session.mission().phase()==MissionPhase::succeeded);
    return true;
}

RUNTIME_TEST(queen_capture_moves_between_team_bases) {
    SessionConfig config=config_with_seed(3U);
    config.mission.game_kind=3U;
    BattleSession session(config,angles(),open_ground_unit_motion(angles()));
    OwnerSetup player{0U,false,1U,0,false};
    player.objective_base=true;
    OwnerSetup enemy{1U,true,1U,0,false};
    enemy.objective_base=true;
    session.add_owner(0U,player);
    session.add_owner(1U,enemy);
    (void)session.spawn_unit(UnitSpawn{retail::UnitKind::tank,0U,0.0F,0.0F,0U,0U});
    (void)session.spawn_unit(UnitSpawn{retail::UnitKind::tank,1U,0.0F,1000.0F,0U,0U});
    session.mission().force_state(MatchState::live,0);
    REQUIRE(session.queen_taken(1U,0U)==ObjectiveEventStatus::applied);
    REQUIRE(session.queen_stored(0U)==ObjectiveEventStatus::applied);
    session.step(SessionInput{});
    REQUIRE(session.objective()->team_queens(0U)==2U);
    REQUIRE(session.mission().state()==MatchState::result_banner);
    REQUIRE(session.mission().phase()==MissionPhase::succeeded);
    return true;
}

RUNTIME_TEST_MAIN()
