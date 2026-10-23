#include "battletanx/mission_state.hpp"

#include "runtime_test_support.hpp"

namespace {
using namespace battletanx;

struct Match {
    std::array<OwnerRecord,owner_capacity> owners{};
    UnitPool units;
    GameplayEventQueue events;
    MissionState mission;
    std::int32_t tick{};

    explicit Match(MissionConfig config={}) : mission(config) {
        owners[0].present=true; owners[0].team=0U; owners[0].squad_cap=1U; owners[0].live_units=1U;
        owners[1].present=true; owners[1].team=1U; owners[1].squad_cap=5U; owners[1].live_units=2U;
        owners[1].computer=true;
    }
    void frame(bool start=false,bool advance=false) {
        mission.step(owners,units,tick,start,advance,events);
        ++tick;
    }
    void go_live() {
        frame(false,true);
        while (mission.state()!=MatchState::live) frame();
    }
};
} // namespace

RUNTIME_TEST(title_then_countdown_then_live) {
    Match match;
    REQUIRE(match.mission.state()==MatchState::title);
    REQUIRE(match.mission.phase()==MissionPhase::preparing);
    match.frame();
    REQUIRE(match.mission.state()==MatchState::title);
    match.frame(false,true);
    REQUIRE(match.mission.state()==MatchState::countdown);
    REQUIRE(match.mission.frames_remaining()==120);   // 5 * 24
    int frames=0;
    while (match.mission.state()==MatchState::countdown) { match.frame(); ++frames; }
    REQUIRE(frames==120);
    REQUIRE(match.mission.phase()==MissionPhase::active);
    REQUIRE(match.mission.live_tick()==121);
    return true;
}

RUNTIME_TEST(split_screen_countdown_is_shorter) {
    MissionConfig config;
    config.countdown_unit=18U;
    Match match(config);
    match.frame(false,true);
    REQUIRE(match.mission.frames_remaining()==90);
    return true;
}

RUNTIME_TEST(kind5_success_when_last_enemy_squad_is_gone) {
    Match match;
    match.go_live();
    match.frame();
    REQUIRE(match.mission.state()==MatchState::live);
    match.owners[1].live_units=0U;   // all enemy tanks destroyed, no respawn bank
    match.frame();
    REQUIRE(match.mission.state()==MatchState::result_delay);
    for (int i=0; i<90; ++i) match.frame();
    REQUIRE(match.mission.state()==MatchState::result_banner);
    REQUIRE(match.mission.result(0)==OwnerResult::won);
    REQUIRE(match.mission.result(1)==OwnerResult::eliminated);
    match.frame();
    REQUIRE(match.mission.phase()==MissionPhase::succeeded);
    REQUIRE(match.events.events().size()==1U);
    REQUIRE(match.events.events()[0].kind==GameplayEventKind::mission_succeeded);
    for (int i=0; i<89; ++i) match.frame();   // 90 banner frames including the one above
    REQUIRE(match.mission.state()==MatchState::round_over);
    REQUIRE(match.mission.frames_remaining()==2000);
    return true;
}

RUNTIME_TEST(kind5_failure_when_player_has_no_tanks_or_bank) {
    Match match;
    match.go_live();
    match.owners[0].live_units=0U;
    match.frame();
    REQUIRE(match.mission.result(0)==OwnerResult::eliminated);
    REQUIRE(match.mission.state()==MatchState::result_delay);
    for (int i=0; i<92; ++i) match.frame();
    REQUIRE(match.mission.phase()==MissionPhase::failed);
    REQUIRE(match.events.events().back().kind==GameplayEventKind::mission_failed);
    return true;
}

RUNTIME_TEST(respawn_bank_keeps_player_alive_and_schedules_after_30_ticks) {
    Match match;
    match.owners[0].respawn_bank[0]=2;
    match.go_live();
    match.owners[0].live_units=0U;   // player tank destroyed
    const std::int32_t death_tick=match.tick;
    match.frame();
    REQUIRE(match.mission.state()==MatchState::live);
    REQUIRE(match.mission.respawn_due_tick(0)==death_tick+30);
    while (match.mission.respawns().empty()) match.frame();
    // Retail fires once due < tick: 31 frames after scheduling.
    REQUIRE(match.tick-1==death_tick+31);
    REQUIRE(match.mission.respawns()[0].owner==0U);
    REQUIRE(match.owners[0].respawn_bank[0]==1);
    return true;
}

RUNTIME_TEST(pause_freezes_and_resumes) {
    Match match;
    match.go_live();
    match.frame(true);
    REQUIRE(match.mission.state()==MatchState::paused);
    REQUIRE(match.mission.phase()==MissionPhase::paused);
    REQUIRE(!match.mission.simulation_running());
    match.owners[1].live_units=0U;
    match.frame();
    REQUIRE(match.mission.state()==MatchState::paused);   // outcome checks only run live
    match.frame(true);
    REQUIRE(match.mission.state()==MatchState::live);
    match.frame();
    REQUIRE(match.mission.state()==MatchState::result_delay);
    return true;
}

RUNTIME_TEST(kind3_waits_for_objective_or_player_squad) {
    MissionConfig config;
    config.game_kind=3U;
    Match match(config);
    match.go_live();
    match.owners[1].live_units=0U;   // last team standing alone does not end kind 3
    match.frame();
    REQUIRE(match.mission.state()==MatchState::live);
    match.mission.set_objective_reached();
    match.frame();
    REQUIRE(match.mission.state()==MatchState::result_banner);
    REQUIRE(match.mission.result(0)==OwnerResult::won);
    return true;
}

RUNTIME_TEST(results_freeze_after_leaving_live_state) {
    Match match;
    match.go_live();
    match.owners[1].live_units=0U;
    match.frame();
    match.owners[0].live_units=0U;   // player dies during the result delay
    for (int i=0; i<95; ++i) match.frame();
    REQUIRE(match.mission.result(0)==OwnerResult::won);
    return true;
}

RUNTIME_TEST_MAIN()
