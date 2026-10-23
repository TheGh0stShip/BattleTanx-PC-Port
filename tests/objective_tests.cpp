#include "battletanx/objective_runtime.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;

namespace {

ObjectiveConfig two_teams(std::uint32_t kind,bool bases=false) {
    ObjectiveConfig config;
    config.game_kind=kind;
    config.owners[0]=ObjectiveOwner{true,false,0U,static_cast<std::uint8_t>(bases ? 1U : 0U)};
    config.owners[1]=ObjectiveOwner{true,true,1U,static_cast<std::uint8_t>(bases ? 1U : 0U)};
    config.owners[2]=ObjectiveOwner{true,true,1U,0U};
    config.result_frames=3;
    return config;
}

ObjectiveRuntime make(const ObjectiveConfig& config) { return *ObjectiveRuntime::create(config).runtime; }

ObjectiveInput live(std::int32_t tick,bool a=true,bool b=true,bool c=true) {
    ObjectiveInput input;
    input.tick=tick;
    input.squad_alive={a,b,c,false};
    return input;
}

ObjectiveStatus run_delay(ObjectiveRuntime& objective,std::int32_t tick,bool p0=true) {
    ObjectiveResult result;
    for (std::int32_t i=0; i<3; ++i) {
        ObjectiveInput input=live(tick+i,p0,false,false);
        input.match=MatchState::result_delay;
        result=objective.step(input);
    }
    return result.status;
}

GameplayEvent kill(std::uint8_t victim,std::int32_t killer) {
    GameplayEvent event;
    event.kind=GameplayEventKind::unit_destroyed;
    event.owner=victim;
    event.value=killer;
    return event;
}

std::string digest(const ObjectiveRuntime& objective) {
    StateWriter writer;
    objective.serialize(writer);
    return writer.hex();
}

} // namespace

RUNTIME_TEST(kind5_elimination_success_after_delay) {
    ObjectiveRuntime objective=make(two_teams(5U));
    REQUIRE(objective.step(live(1)).status==ObjectiveStatus::ongoing);
    const ObjectiveResult decided=objective.step(live(2,true,false,false));
    REQUIRE(decided.decided_this_step && decided.next_state==MatchState::result_delay);
    REQUIRE(objective.phase()==ObjectivePhase::resolving);
    REQUIRE(run_delay(objective,3)==ObjectiveStatus::succeeded);
    REQUIRE(objective.result(0)==OwnerResult::won && objective.result(1)==OwnerResult::eliminated);
    return true;
}

RUNTIME_TEST(kind2_human_wipe_fails) {
    ObjectiveRuntime objective=make(two_teams(2U));
    REQUIRE(objective.step(live(1,false,true,true)).next_state==MatchState::result_delay);
    REQUIRE(run_delay(objective,2,false)==ObjectiveStatus::failed);
    return true;
}

RUNTIME_TEST(kind7_death_during_result_delay_fails) {
    ObjectiveRuntime objective=make(two_teams(7U));
    (void)objective.step(live(1,true,false,false));
    REQUIRE(run_delay(objective,2,false)==ObjectiveStatus::failed);
    return true;
}

RUNTIME_TEST(kind3_capture_all_queens_succeeds) {
    ObjectiveRuntime objective=make(two_teams(3U,true));
    REQUIRE(objective.total_bases()==2U && objective.team_queens(0)==1U);
    REQUIRE(objective.step(live(1)).status==ObjectiveStatus::ongoing);
    REQUIRE(objective.on_queen_taken(1U,1U)==ObjectiveEventStatus::ignored);   // own queen
    REQUIRE(objective.on_queen_taken(1U,0U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.step(live(2)).status==ObjectiveStatus::ongoing);
    REQUIRE(objective.on_queen_stored(0U)==ObjectiveEventStatus::applied);
    const ObjectiveResult result=objective.step(live(3));
    REQUIRE(result.status==ObjectiveStatus::succeeded && result.next_state==MatchState::result_banner);
    REQUIRE(objective.capture_reached() && objective.result(1)==OwnerResult::eliminated);
    return true;
}

RUNTIME_TEST(kind3_enemy_wipe_alone_does_not_end) {
    ObjectiveRuntime objective=make(two_teams(3U,true));
    REQUIRE(objective.step(live(1,true,false,false)).status==ObjectiveStatus::ongoing);
    return true;
}

RUNTIME_TEST(kind6_owner0_death_fails) {
    ObjectiveRuntime objective=make(two_teams(6U,true));
    REQUIRE(objective.step(live(1,false,true,true)).status==ObjectiveStatus::failed);
    return true;
}

RUNTIME_TEST(kind6_enemy_capture_fails) {
    ObjectiveRuntime objective=make(two_teams(6U,true));
    REQUIRE(objective.on_queen_taken(0U,1U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.on_queen_stored(1U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.step(live(1)).status==ObjectiveStatus::failed);
    REQUIRE(objective.result(1)==OwnerResult::won);
    return true;
}

RUNTIME_TEST(kind4_queen_contact_succeeds) {
    ObjectiveRuntime objective=make(two_teams(4U,true));
    REQUIRE(objective.on_queen_contact(1U)==ObjectiveEventStatus::ignored);
    REQUIRE(objective.step(live(1,true,false,false)).status==ObjectiveStatus::ongoing);   // no elimination end
    REQUIRE(objective.on_queen_contact(0U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.step(live(2)).status==ObjectiveStatus::succeeded);
    return true;
}

RUNTIME_TEST(kind4_team0_wipe_fails_even_with_contact) {
    ObjectiveRuntime objective=make(two_teams(4U,true));
    REQUIRE(objective.on_queen_contact(0U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.step(live(1,false,true,true)).next_state==MatchState::result_delay);
    REQUIRE(run_delay(objective,2,false)==ObjectiveStatus::failed);
    return true;
}

RUNTIME_TEST(kind9_kill_race) {
    ObjectiveRuntime objective=make(two_teams(9U));
    REQUIRE(objective.on_event(kill(1U,0))==ObjectiveEventStatus::applied);
    REQUIRE(objective.on_self_destruct(0U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.on_self_destruct(0U)==ObjectiveEventStatus::applied);
    REQUIRE(objective.kill_counter(0)==0);   // clamped
    REQUIRE(objective.on_event(kill(2U,1))==ObjectiveEventStatus::ignored);   // same team
    for (int i=0; i<6; ++i) (void)objective.on_event(kill(1U,0));
    REQUIRE(objective.step(live(1)).status==ObjectiveStatus::ongoing);
    (void)objective.on_event(kill(2U,0));
    REQUIRE(objective.step(live(2)).next_state==MatchState::result_delay);
    REQUIRE(run_delay(objective,3)==ObjectiveStatus::succeeded);
    REQUIRE(objective.result(1)==OwnerResult::eliminated);
    return true;
}

RUNTIME_TEST(kind9_teammate_counter_shared) {
    ObjectiveRuntime objective=make(two_teams(9U));
    for (int i=0; i<7; ++i) (void)objective.on_event(kill(0U,1));
    REQUIRE(objective.kill_counter(2)==7 && objective.result(2)==OwnerResult::won);
    (void)objective.step(live(1));
    REQUIRE(run_delay(objective,2)==ObjectiveStatus::failed);
    return true;
}

RUNTIME_TEST(pause_freezes) {
    ObjectiveRuntime objective=make(two_teams(5U));
    const std::string before=digest(objective);
    ObjectiveInput paused=live(5,true,false,false);
    paused.match=MatchState::paused;
    REQUIRE(objective.step(paused).status==ObjectiveStatus::ongoing);
    REQUIRE(digest(objective)==before);
    ObjectiveRuntime capture=make(two_teams(3U,true));
    (void)capture.step(live(1,false,true,true));
    REQUIRE(capture.on_queen_stored(0U)==ObjectiveEventStatus::frozen);
    return true;
}

RUNTIME_TEST(terminal_state_freezes) {
    ObjectiveRuntime objective=make(two_teams(6U,true));
    (void)objective.step(live(1,false,true,true));
    const std::string before=digest(objective);
    REQUIRE(objective.step(live(2)).status==ObjectiveStatus::failed);
    REQUIRE(objective.on_queen_stored(0U)==ObjectiveEventStatus::frozen);
    REQUIRE(digest(objective)==before);
    return true;
}

RUNTIME_TEST(replay_determinism_checksum) {
    const auto run=[] {
        ObjectiveRuntime objective=make(two_teams(9U));
        for (std::int32_t t=0; t<20; ++t) {
            if (t%3==0) (void)objective.on_event(kill(1U,0));
            if (t%7==0) (void)objective.on_self_destruct(0U);
            (void)objective.step(live(t));
        }
        return digest(objective);
    };
    REQUIRE(run()==run());
    ObjectiveRuntime other=make(two_teams(9U));
    REQUIRE(digest(other)!=run());
    return true;
}

RUNTIME_TEST(malformed_config_rejected) {
    ObjectiveConfig config=two_teams(10U);
    REQUIRE(ObjectiveRuntime::create(config).error==ObjectiveConfigError::unknown_game_kind);
    config=two_teams(3U);
    REQUIRE(ObjectiveRuntime::create(config).error==ObjectiveConfigError::no_capture_bases);
    config=two_teams(5U); config.owners[1].team=4U;
    REQUIRE(ObjectiveRuntime::create(config).error==ObjectiveConfigError::team_out_of_range);
    config=two_teams(5U); config.owners[0].present=false;
    REQUIRE(ObjectiveRuntime::create(config).error==ObjectiveConfigError::owner0_missing);
    config=two_teams(3U,true); config.owners[1].bases=2U;
    REQUIRE(ObjectiveRuntime::create(config).error==ObjectiveConfigError::base_count_invalid);
    config=two_teams(9U); config.kill_target=0;
    REQUIRE(!ObjectiveRuntime::create(config).runtime.has_value());
    return true;
}

RUNTIME_TEST(unsupported_kinds_report_reason) {
    ObjectiveRuntime demo=make(two_teams(8U));
    const ObjectiveResult result=demo.step(live(1,false,false,false));
    REQUIRE(result.status==ObjectiveStatus::unsupported && result.reason==UnsupportedReason::demo_playback);
    REQUIRE(make(two_teams(1U)).step(live(1)).reason==UnsupportedReason::no_live_objective);
    return true;
}

RUNTIME_TEST(mission_adapter) {
    ObjectiveRuntime objective=make(two_teams(3U,true));
    MissionConfig mc; mc.game_kind=3U;
    MissionState mission(mc);
    REQUIRE(feed_mission_state(objective,mission)==MissionFeed::nothing_to_feed);
    (void)objective.on_queen_taken(1U,0U);
    (void)objective.on_queen_stored(0U);
    (void)objective.step(live(1));
    REQUIRE(feed_mission_state(objective,mission)==MissionFeed::objective_signalled);
    ObjectiveRuntime rescue=make(two_teams(4U,true));
    MissionConfig mc4; mc4.game_kind=4U;
    MissionState mission4(mc4);
    (void)rescue.on_queen_contact(0U);
    REQUIRE(feed_mission_state(rescue,mission4)==MissionFeed::not_representable);
    return true;
}

RUNTIME_TEST_MAIN()
