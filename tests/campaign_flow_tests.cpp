#include "battletanx/campaign_flow.hpp"

#include "runtime_test_support.hpp"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <cstdint>
#include <vector>

using namespace battletanx;
using namespace battletanx::campaign_flow;

namespace {

// Retail order of gOpponentWaves mission ids and game kinds.
constexpr std::array<std::int32_t,17> kOrder{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,16};
constexpr std::array<std::int32_t,17> kKinds{5,4,5,4,7,4,3,3,7,4,3,3,7,4,3,7,3};

FlowCommand cmd(FlowCommandKind kind) {
    FlowCommand c;
    c.kind=kind;
    return c;
}

FlowCommand resume_at(std::uint8_t mission_id) {
    FlowCommand c=cmd(FlowCommandKind::resume);
    c.record=default_campaign_record();
    c.record.mission_id=mission_id;
    c.record.carry=1234;
    c.record.bonus=56;
    return c;
}

FlowCommand battle(bool won,std::int32_t score,std::int16_t bonus) {
    FlowCommand c=cmd(FlowCommandKind::battle_finished);
    c.outcome.won=won;
    c.outcome.player_score=score;
    c.outcome.player_bonus=bonus;
    return c;
}

FlowCommand choose(FlowResultChoice choice) {
    FlowCommand c=cmd(FlowCommandKind::result_choice);
    c.choice=choice;
    return c;
}

bool has(const std::vector<FlowTransition>& t,FlowTransitionKind kind) {
    for (const FlowTransition& x : t) if (x.kind==kind) return true;
    return false;
}

const FlowTransition* find_kind(const std::vector<FlowTransition>& t,FlowTransitionKind kind) {
    for (const FlowTransition& x : t) if (x.kind==kind) return &x;
    return nullptr;
}

// Drives intro/briefing to a launched battle; returns the launch transition kind seen.
bool reach_battle(CampaignFlow& flow) {
    if (flow.phase()==FlowPhase::chapter_intro) {
        FlowCommand c=cmd(FlowCommandKind::intro_finished);
        if (!has(flow.apply(c),FlowTransitionKind::briefing)) return false;
    }
    if (flow.phase()!=FlowPhase::briefing) return false;
    return has(flow.apply(cmd(FlowCommandKind::briefing_finished)),FlowTransitionKind::battle_launch);
}

// Passes the current wave the way a player would: win (or endurance end).
std::vector<FlowTransition> pass_wave(CampaignFlow& flow,std::int32_t score,bool save) {
    const bool endurance=flow.current_wave() && flow.current_wave()->game_kind==7;
    (void)flow.apply(battle(!endurance,score,7));
    return flow.apply(choose(save ? FlowResultChoice::save_and_continue : FlowResultChoice::continue_without_save));
}

} // namespace

RUNTIME_TEST(catalog_order_matches_flow_assumptions) {
    const auto waves=campaign_wave_definitions();
    for (std::size_t i=0U; i<waves.size(); ++i) {
        REQUIRE(waves[i].mission_id==kOrder[i]);
        REQUIRE(waves[i].game_kind==kKinds[i]);
    }
    return true;
}

RUNTIME_TEST(new_campaign_uses_defaults_and_plays_intro_15) {
    CampaignFlow flow;
    FlowCommand c=cmd(FlowCommandKind::start_new);
    c.record=default_campaign_record();
    c.record.carry=999;
    c.record.unit_type=3;
    c.record.difficulty=CampaignDifficulty::hard;
    const auto t=flow.apply(c);
    REQUIRE(t.size()==1U);
    REQUIRE(t[0].kind==FlowTransitionKind::intro);
    REQUIRE(t[0].sequence_id==15U);
    REQUIRE(flow.wave_index()==0U);
    REQUIRE(flow.carry()==0);
    REQUIRE(flow.bonus()==0);
    REQUIRE(flow.options().unit_type==9);
    REQUIRE(flow.options().difficulty==CampaignDifficulty::hard);
    return true;
}

RUNTIME_TEST(every_wave_transition_in_order_with_chapters) {
    CampaignFlow flow;
    FlowCommand c=cmd(FlowCommandKind::start_new);
    c.record=default_campaign_record();
    (void)flow.apply(c);
    std::vector<std::uint8_t> intros;
    for (std::size_t i=0U; i<kOrder.size(); ++i) {
        REQUIRE(static_cast<std::size_t>(flow.wave_index())==i);
        REQUIRE(flow.current_wave()->mission_id==kOrder[i]);
        if (flow.phase()==FlowPhase::chapter_intro) intros.push_back(*flow.current_wave()->chapter_sequence);
        REQUIRE(reach_battle(flow));
        const auto t=pass_wave(flow,static_cast<std::int32_t>(100U*(i+1U)),false);
        const FlowTransition* inter=find_kind(t,FlowTransitionKind::intermission);
        REQUIRE(inter!=nullptr);
        REQUIRE(inter->carry==static_cast<std::int32_t>(100U*(i+1U)));
        if (i+1U<kOrder.size()) {
            const FlowTransition* next=find_kind(t,FlowTransitionKind::next_wave);
            REQUIRE(next!=nullptr);
            REQUIRE(next->mission_id==kOrder[i+1U]);
            const bool chapter=i+1U==5U || i+1U==9U || i+1U==13U;
            REQUIRE(has(t,FlowTransitionKind::intro)==chapter);
            REQUIRE(has(t,FlowTransitionKind::briefing)==!chapter);
        } else {
            const FlowTransition* done=find_kind(t,FlowTransitionKind::completion);
            REQUIRE(done!=nullptr);
            REQUIRE(done->sequence_id==completion_frontend_state);
        }
    }
    REQUIRE((intros==std::vector<std::uint8_t>{15U,16U,17U,18U}));
    REQUIRE(flow.phase()==FlowPhase::completed);
    REQUIRE(flow.current_wave()==nullptr);
    return true;
}

RUNTIME_TEST(resume_ids_map_to_wave_indices) {
    for (std::size_t i=0U; i<kOrder.size(); ++i) {
        CampaignFlow flow;
        const auto t=flow.apply(resume_at(static_cast<std::uint8_t>(kOrder[i])));
        REQUIRE(!t.empty());
        REQUIRE(static_cast<std::size_t>(flow.wave_index())==i);
        REQUIRE(flow.carry()==1234);
        REQUIRE(flow.bonus()==56);
        const bool chapter=i==0U || i==5U || i==9U || i==13U;
        REQUIRE(t[0].kind==(chapter ? FlowTransitionKind::intro : FlowTransitionKind::briefing));
    }
    return true;
}

RUNTIME_TEST(invalid_saved_mission_id_returns_to_menu) {
    for (const std::uint8_t id : {std::uint8_t{0U},std::uint8_t{18U},std::uint8_t{0xFFU}}) {
        CampaignFlow flow;
        const auto t=flow.apply(resume_at(id));
        REQUIRE(t.size()==1U);
        REQUIRE(t[0].kind==FlowTransitionKind::return_to_menu);
        REQUIRE(t[0].reject==FlowRejectReason::invalid_mission_id);
        REQUIRE(t[0].menu==FlowMenuReason::invalid_resume);
        REQUIRE(flow.phase()==FlowPhase::menu);
    }
    return true;
}

RUNTIME_TEST(launch_applies_carry_bonus_unit_and_difficulty) {
    CampaignFlow flow;
    FlowCommand c=resume_at(2U);
    c.record.unit_type=4;
    c.record.difficulty=CampaignDifficulty::normal;
    (void)flow.apply(c);
    const auto t=flow.apply(cmd(FlowCommandKind::briefing_finished));
    const FlowTransition* l=find_kind(t,FlowTransitionKind::battle_launch);
    REQUIRE(l!=nullptr);
    const CampaignWaveLaunch expect=make_campaign_wave_launch(*find_campaign_wave(2),1234,56,1U,4);
    REQUIRE(l->launch.player_score==expect.player_score);
    REQUIRE(l->launch.player_bonus==expect.player_bonus);
    REQUIRE(l->launch.player_unit_kind==expect.player_unit_kind);
    REQUIRE(l->launch.skill_group==1U);
    return true;
}

RUNTIME_TEST(loss_offers_retry_and_quit_only) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(2U));
    REQUIRE(reach_battle(flow));
    const auto t=flow.apply(battle(false,10,1));
    REQUIRE(t[0].kind==FlowTransitionKind::result);
    REQUIRE(t[0].choices[2] && t[0].choices[3]);
    REQUIRE(!t[0].choices[0] && !t[0].choices[1]);
    const auto bad=flow.apply(choose(FlowResultChoice::continue_without_save));
    REQUIRE(bad.size()==1U && bad[0].reject==FlowRejectReason::choice_not_offered);
    const auto q=flow.apply(choose(FlowResultChoice::quit));
    REQUIRE(q[0].kind==FlowTransitionKind::return_to_menu && q[0].menu==FlowMenuReason::wave_lost);
    REQUIRE(flow.wave_index()==1U);
    return true;
}

RUNTIME_TEST(retry_relaunches_same_snapshot) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(3U));
    REQUIRE(reach_battle(flow));
    (void)flow.apply(battle(false,9999,99));
    const auto t=flow.apply(choose(FlowResultChoice::retry));
    REQUIRE(t.size()==1U && t[0].kind==FlowTransitionKind::retry);
    REQUIRE(t[0].launch.player_score==make_campaign_wave_launch(*find_campaign_wave(3),1234,56,0U,9).player_score);
    REQUIRE(flow.carry()==1234);
    REQUIRE(flow.retries()==1U);
    REQUIRE(flow.phase()==FlowPhase::battle);
    const auto w=pass_wave(flow,50,false);
    REQUIRE(has(w,FlowTransitionKind::next_wave));
    REQUIRE(flow.current_wave()->mission_id==4);
    return true;
}

RUNTIME_TEST(endurance_advances_on_loss) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(5U));
    REQUIRE(flow.current_wave()->game_kind==7);
    REQUIRE(reach_battle(flow));
    const auto r=flow.apply(battle(false,70,30));
    REQUIRE(r[0].choices[0] && r[0].choices[1]);
    REQUIRE(!r[0].choices[2] && !r[0].choices[3]);
    const auto t=flow.apply(choose(FlowResultChoice::continue_without_save));
    REQUIRE(has(t,FlowTransitionKind::next_wave));
    REQUIRE(flow.current_wave()->mission_id==6);
    REQUIRE(flow.carry()==70 && flow.bonus()==30);
    return true;
}

RUNTIME_TEST(save_request_carries_next_mission_and_options) {
    CampaignFlow flow;
    FlowCommand c=resume_at(14U);
    c.record.powerups.set(true);
    c.record.base_defense.set(true);
    c.record.unlimited_ammo.set(true);
    c.record.control_scheme=2U;
    c.record.sfx_volume=3;
    c.record.music_volume=4;
    c.record.bindings[16]=0xABCDU;
    (void)flow.apply(c);
    REQUIRE(reach_battle(flow));
    const auto t=pass_wave(flow,4321,true);
    const FlowTransition* s=find_kind(t,FlowTransitionKind::save_request);
    REQUIRE(s!=nullptr && s->save.has_value());
    REQUIRE(s->save->clear.mission_id==15U);
    REQUIRE(s->save->clear.carry==4321);
    REQUIRE(s->save->clear.bonus==7);
    REQUIRE(s->save->clear.powerups==1U && s->save->clear.base_defense==1U && s->save->clear.unlimited_ammo==1U);
    REQUIRE(s->save->clear.control_scheme==2U);
    REQUIRE(s->save->clear.sfx_volume==3 && s->save->clear.music_volume==4);
    REQUIRE(s->save->clear.bindings[16]==0xABCDU);
    REQUIRE(!has(t,FlowTransitionKind::next_wave));
    REQUIRE(flow.phase()==FlowPhase::save_pending);
    FlowCommand done=cmd(FlowCommandKind::save_finished);
    done.accepted=false; // write failure does not change the flow
    const auto n=flow.apply(done);
    REQUIRE(has(n,FlowTransitionKind::next_wave));
    REQUIRE(flow.current_wave()->mission_id==15);
    return true;
}

RUNTIME_TEST(wave_15_saves_17_then_17_saves_16) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(15U));
    REQUIRE(reach_battle(flow));
    auto t=pass_wave(flow,1,true);
    REQUIRE(find_kind(t,FlowTransitionKind::save_request)->save->clear.mission_id==17U);
    (void)flow.apply(cmd(FlowCommandKind::save_finished));
    REQUIRE(flow.current_wave()->mission_id==17);
    REQUIRE(reach_battle(flow));
    t=pass_wave(flow,2,true);
    REQUIRE(find_kind(t,FlowTransitionKind::save_request)->save->clear.mission_id==16U);
    (void)flow.apply(cmd(FlowCommandKind::save_finished));
    REQUIRE(flow.current_wave()->mission_id==16);
    return true;
}

RUNTIME_TEST(final_wave_win_has_no_save_and_completes) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(16U));
    REQUIRE(reach_battle(flow));
    const auto r=flow.apply(battle(true,5,5));
    REQUIRE(r[0].choices[0] && !r[0].choices[1]);
    const auto t=flow.apply(choose(FlowResultChoice::continue_without_save));
    REQUIRE(has(t,FlowTransitionKind::completion));
    REQUIRE(flow.phase()==FlowPhase::completed);
    // Replay after completion is allowed.
    const auto again=flow.apply(resume_at(16U));
    REQUIRE(again[0].kind==FlowTransitionKind::briefing);
    REQUIRE(flow.retries()==0U);
    return true;
}

RUNTIME_TEST(intro_cancel_only_for_sequence_15) {
    CampaignFlow a;
    (void)a.apply(resume_at(1U));
    FlowCommand c=cmd(FlowCommandKind::intro_finished);
    c.accepted=false;
    const auto t=a.apply(c);
    REQUIRE(t[0].kind==FlowTransitionKind::return_to_menu && t[0].menu==FlowMenuReason::intro_cancelled);
    CampaignFlow b;
    (void)b.apply(resume_at(6U));
    REQUIRE(b.phase()==FlowPhase::chapter_intro);
    const auto u=b.apply(c);
    REQUIRE(u[0].kind==FlowTransitionKind::briefing);
    return true;
}

RUNTIME_TEST(abort_and_wrong_phase) {
    CampaignFlow flow;
    REQUIRE(flow.apply(cmd(FlowCommandKind::briefing_finished))[0].reject==FlowRejectReason::wrong_phase);
    (void)flow.apply(resume_at(2U));
    REQUIRE(flow.apply(resume_at(3U))[0].reject==FlowRejectReason::already_started);
    REQUIRE(reach_battle(flow));
    FlowCommand c=cmd(FlowCommandKind::battle_finished);
    c.outcome.aborted=true;
    const auto t=flow.apply(c);
    REQUIRE(t[0].kind==FlowTransitionKind::return_to_menu && t[0].menu==FlowMenuReason::battle_aborted);
    return true;
}

RUNTIME_TEST(pause_restart_is_retry_not_abort) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(3U));
    REQUIRE(reach_battle(flow));
    FlowCommand c=cmd(FlowCommandKind::battle_finished);
    c.outcome.restarted=true;
    c.outcome.aborted=true; // retail sets both D_80125799 and D_80125798
    c.outcome.player_score=777;
    const auto t=flow.apply(c);
    REQUIRE(t.size()==1U && t[0].kind==FlowTransitionKind::retry);
    REQUIRE(t[0].launch.player_score==make_campaign_wave_launch(*find_campaign_wave(3),1234,56,0U,9).player_score);
    REQUIRE(flow.phase()==FlowPhase::battle);
    REQUIRE(flow.carry()==1234);
    REQUIRE(flow.retries()==1U);
    REQUIRE(flow.easy_restart_count()==1U);
    return true;
}

RUNTIME_TEST(retry_counters_reset_per_wave_and_easy_only) {
    CampaignFlow flow;
    FlowCommand c=resume_at(2U);
    c.record.difficulty=CampaignDifficulty::normal;
    (void)flow.apply(c);
    REQUIRE(reach_battle(flow));
    (void)flow.apply(battle(false,1,1));
    (void)flow.apply(choose(FlowResultChoice::retry));
    REQUIRE(flow.retries()==1U && flow.easy_restart_count()==0U);
    FlowCommand easy=cmd(FlowCommandKind::set_options);
    easy.options=flow.options();
    easy.options.difficulty=CampaignDifficulty::easy;
    (void)flow.apply(easy);
    (void)flow.apply(battle(false,1,1));
    (void)flow.apply(choose(FlowResultChoice::retry));
    REQUIRE(flow.retries()==2U && flow.easy_restart_count()==1U);
    (void)pass_wave(flow,5,false);
    REQUIRE(flow.current_wave()->mission_id==3);
    REQUIRE(flow.retries()==0U && flow.easy_restart_count()==0U);
    return true;
}

RUNTIME_TEST(unknown_result_code_offers_no_choice) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(2U));
    REQUIRE(reach_battle(flow));
    FlowCommand c=battle(false,1,1);
    c.outcome.result_code=1;
    const auto t=flow.apply(c);
    REQUIRE(t[0].kind==FlowTransitionKind::result);
    for (const bool offered : t[0].choices) REQUIRE(!offered);
    REQUIRE(flow.apply(choose(FlowResultChoice::retry))[0].reject==FlowRejectReason::choice_not_offered);
    REQUIRE(flow.phase()==FlowPhase::result);
    return true;
}

RUNTIME_TEST(invalid_resume_keeps_live_options) {
    CampaignFlow flow;
    FlowCommand o=cmd(FlowCommandKind::set_options);
    o.options.music_volume=5;
    (void)flow.apply(o);
    FlowCommand bad=resume_at(0U);
    bad.record.music_volume=1;
    (void)flow.apply(bad);
    REQUIRE(flow.options().music_volume==5);
    REQUIRE(flow.carry()==0);
    return true;
}

RUNTIME_TEST(checksum_tracks_state) {
    CampaignFlow a;
    CampaignFlow b;
    REQUIRE(a.checksum()==b.checksum());
    (void)a.apply(resume_at(2U));
    (void)b.apply(resume_at(2U));
    REQUIRE(a.checksum()==b.checksum());
    FlowCommand o=cmd(FlowCommandKind::set_options);
    o.options=a.options();
    o.options.music_volume=1;
    (void)b.apply(o);
    REQUIRE(a.checksum()!=b.checksum());
    StateWriter w;
    a.serialize(w);
    REQUIRE(w.bytes().size()==4U+1U+1U+4U+2U+(4U+6U+8U+4U*campaign_binding_words)+1U+2U+2U+4U+2U+1U+2U+4U+4U+1U);
    return true;
}

// update_match_state.c:342-344: a won endurance wave skips the dialog and
// reuses the previous dialog's save choice (D_8012579B).
RUNTIME_TEST(won_endurance_wave_reuses_previous_save_choice) {
    for (const bool save : {true,false}) {
        CampaignFlow flow;
        (void)flow.apply(resume_at(4U));
        REQUIRE(flow.current_wave()->game_kind!=7);
        REQUIRE(reach_battle(flow));
        auto t=pass_wave(flow,11,save);
        REQUIRE(flow.last_save_choice()==save);
        REQUIRE(has(t,FlowTransitionKind::save_request)==save);
        if (save) (void)flow.apply(cmd(FlowCommandKind::save_finished));
        REQUIRE(flow.current_wave()->mission_id==5);
        REQUIRE(flow.current_wave()->game_kind==7);
        REQUIRE(reach_battle(flow));
        t=flow.apply(battle(true,22,3));
        REQUIRE(!t.empty() && t[0].kind==FlowTransitionKind::result);
        REQUIRE(t[0].won && t[0].auto_advanced && t[0].save_choice==save);
        for (const bool c : t[0].choices) REQUIRE(!c);
        REQUIRE(has(t,FlowTransitionKind::intermission));
        const FlowTransition* s=find_kind(t,FlowTransitionKind::save_request);
        REQUIRE((s!=nullptr)==save);
        if (save) {
            REQUIRE(s->save->clear.mission_id==6U);
            REQUIRE(s->save->clear.carry==22);
            REQUIRE(flow.phase()==FlowPhase::save_pending);
        } else {
            REQUIRE(has(t,FlowTransitionKind::next_wave));
        }
        // A result_choice is not accepted: the dialog never opened.
        REQUIRE(flow.apply(choose(FlowResultChoice::continue_without_save))[0].kind==FlowTransitionKind::rejected);
    }
    return true;
}

RUNTIME_TEST(every_dialog_button_rewrites_save_choice) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(4U));
    REQUIRE(reach_battle(flow));
    (void)pass_wave(flow,1,true);
    REQUIRE(flow.last_save_choice());
    (void)flow.apply(cmd(FlowCommandKind::save_finished));
    REQUIRE(reach_battle(flow));
    // Endurance loss (result 0) opens the dialog; continue clears the flag.
    (void)flow.apply(battle(false,1,1));
    (void)flow.apply(choose(FlowResultChoice::continue_without_save));
    REQUIRE(!flow.last_save_choice());
    REQUIRE(reach_battle(flow));
    (void)flow.apply(battle(false,1,1)); // non-endurance loss
    (void)flow.apply(choose(FlowResultChoice::retry));
    REQUIRE(!flow.last_save_choice());
    // Pause restart leaves the flag untouched; a new start clears it.
    CampaignFlow b;
    (void)b.apply(resume_at(4U));
    REQUIRE(reach_battle(b));
    (void)pass_wave(b,1,true);
    (void)b.apply(cmd(FlowCommandKind::save_finished));
    REQUIRE(reach_battle(b));
    FlowCommand restart=battle(false,0,0);
    restart.outcome.restarted=true;
    (void)b.apply(restart);
    REQUIRE(b.last_save_choice());
    FlowCommand quit=battle(false,0,0);
    quit.outcome.aborted=true;
    (void)b.apply(quit);
    (void)b.apply(resume_at(4U));
    REQUIRE(!b.last_save_choice());
    return true;
}

// X7: mission 16 never saves, even when the previous (endurance) dialog chose save.
RUNTIME_TEST(mission_16_completes_without_save_after_saved_wave_15) {
    CampaignFlow flow;
    (void)flow.apply(resume_at(17U));
    REQUIRE(flow.current_wave()->game_kind==7);
    REQUIRE(reach_battle(flow));
    auto t=pass_wave(flow,9,true);
    REQUIRE(find_kind(t,FlowTransitionKind::save_request)->save->clear.mission_id==16U);
    (void)flow.apply(cmd(FlowCommandKind::save_finished));
    REQUIRE(flow.last_save_choice());
    REQUIRE(reach_battle(flow));
    const auto r=flow.apply(battle(true,5,5));
    REQUIRE(!r[0].auto_advanced);
    REQUIRE(r[0].choices[0] && !r[0].choices[1] && !r[0].choices[2] && !r[0].choices[3]);
    REQUIRE(flow.apply(choose(FlowResultChoice::save_and_continue))[0].kind==FlowTransitionKind::rejected);
    t=flow.apply(choose(FlowResultChoice::continue_without_save));
    REQUIRE(!has(t,FlowTransitionKind::save_request));
    REQUIRE(has(t,FlowTransitionKind::completion));
    REQUIRE(!flow.last_save_choice());
    REQUIRE(flow.phase()==FlowPhase::completed);
    return true;
}

RUNTIME_TEST(public_result_choice_rule) {
    const auto waves=campaign_wave_definitions();
    const CampaignWaveDefinition& endurance=waves[4];
    const CampaignWaveDefinition& plain=waves[3];
    const CampaignWaveDefinition& last=waves[16];
    REQUIRE(result_auto_advances(endurance,flow_result_code_won));
    REQUIRE(!result_auto_advances(endurance,flow_result_code_selectable));
    REQUIRE(!result_auto_advances(plain,flow_result_code_won));
    for (const bool c : offered_result_choices(endurance,flow_result_code_won)) REQUIRE(!c);
    const auto e=offered_result_choices(endurance,flow_result_code_selectable);
    REQUIRE(e[0] && e[1] && !e[2] && !e[3]);
    const auto w=offered_result_choices(plain,flow_result_code_won);
    REQUIRE(w[0] && w[1] && !w[2] && !w[3]);
    const auto l=offered_result_choices(plain,flow_result_code_selectable);
    REQUIRE(!l[0] && !l[1] && l[2] && l[3]);
    const auto f=offered_result_choices(last,flow_result_code_won);
    REQUIRE(f[0] && !f[1]);
    for (const bool c : offered_result_choices(plain,1)) REQUIRE(!c);
    return true;
}

RUNTIME_TEST_MAIN()
