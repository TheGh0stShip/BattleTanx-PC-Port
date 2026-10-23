#include "battletanx/campaign_harness.hpp"
#include "runtime_test_support.hpp"

#include <vector>

using namespace battletanx;
using namespace battletanx::campaign_harness;

namespace {

using campaign_flow::FlowResultChoice;
using music_selection::MusicTransition;

struct RecordingFrontend final : FrontendAdapter {
    std::vector<Phase> phases;
    std::vector<bool> won;
    void present(Phase phase,std::uint8_t,bool w) override { phases.push_back(phase); won.push_back(w); }
};
struct RecordingMusic final : MusicAdapter {
    std::vector<MusicTransition> picks;
    std::vector<std::int32_t> missions;
    void select(MusicTransition transition,std::int32_t mission) override { picks.push_back(transition); missions.push_back(mission); }
};
// Scripted save: answers commit with `on_commit`, poll with `on_poll`.
struct CountingSave final : SaveAdapter {
    SaveStatus on_commit=SaveStatus::committed;
    SaveStatus on_poll=SaveStatus::committed;
    std::vector<std::uint8_t> missions;
    std::vector<std::uint8_t> next_indices;
    SaveStatus commit(const CampaignRecord& staged,std::uint8_t next) override {
        missions.push_back(staged.mission_id);
        next_indices.push_back(next);
        return on_commit;
    }
    SaveStatus poll() override { return on_poll; }
};

std::size_t slot(FlowResultChoice c) { return static_cast<std::size_t>(c); }

// Wins the current wave with objective events matching its win rule.
bool win_current_wave(Coordinator& harness,std::int32_t score) {
    if (!harness.confirm_briefing().ok()) return false;
    const auto& request=harness.pending_request();
    if (!request || request->launch.wave_index!=harness.wave_index()) return false;
    ObjectiveEventKind win=ObjectiveEventKind::teams_reduced;
    if (request->objective==CampaignObjectiveKind::goal_slot) win=ObjectiveEventKind::goal_slot_reached;
    if (request->objective==CampaignObjectiveKind::player_win_flag) win=ObjectiveEventKind::player_win_flag;
    if (!harness.post_objective_event(win).ok()) return false;
    if (!harness.submit_outcome(BattleOutcome{.kind=OutcomeKind::from_objective_events,.score=score,.bonus=1}).ok())
        return false;
    // A won endurance wave auto-advances past the result screen.
    return harness.last_result_won() && harness.phase()!=Phase::awaiting_battle;
}

std::uint8_t first_wave_where(bool endurance) {
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions())
        if ((wave.game_kind==7)==endurance) return wave.wave_index;
    return static_cast<std::uint8_t>(campaign_wave_count);
}

} // namespace

RUNTIME_TEST(every_catalog_wave_launches_from_synthetic_inputs) {
    const Coordinator harness(complete_synthetic_inputs(),Adapters{});
    for (std::uint8_t i=0U; i<campaign_wave_count; ++i) REQUIRE(harness.check_wave_inputs(i).ok());
    REQUIRE(harness.check_wave_inputs(static_cast<std::uint8_t>(campaign_wave_count)).code==ErrorCode::wave_out_of_catalog);
    return true;
}

RUNTIME_TEST(missing_inputs_report_typed_errors) {
    DecodedInputs inputs=complete_synthetic_inputs();
    inputs.waves[0].world_stream=false;
    inputs.waves[1].spawn_table=false;
    inputs.waves[2].briefing_image=false;
    Coordinator harness(inputs,Adapters{});
    REQUIRE(harness.check_wave_inputs(0U).code==ErrorCode::missing_world_stream);
    REQUIRE(harness.check_wave_inputs(1U).code==ErrorCode::missing_spawn_table);
    if (campaign_wave_definitions()[2].briefing_image>=0)
        REQUIRE(harness.check_wave_inputs(2U).code==ErrorCode::missing_briefing_image);
    const Error error=harness.start_new(Settings{});
    REQUIRE(error.code==ErrorCode::missing_world_stream);
    REQUIRE(error.wave_index==0U);
    REQUIRE(harness.phase()==Phase::menu);
    for (std::uint8_t i=0U; i<campaign_wave_count; ++i) {
        if (!campaign_wave_definitions()[i].navigation_available) continue;
        DecodedInputs nav=complete_synthetic_inputs();
        nav.waves[i].navigation=false;
        REQUIRE(Coordinator(nav,Adapters{}).check_wave_inputs(i).code==ErrorCode::missing_navigation);
    }
    return true;
}

RUNTIME_TEST(phase_gates_reject_out_of_order_commands) {
    Coordinator harness(complete_synthetic_inputs(),Adapters{});
    REQUIRE(harness.confirm_briefing().code==ErrorCode::wrong_phase);
    REQUIRE(harness.countdown_finished().code==ErrorCode::wrong_phase);
    REQUIRE(harness.submit_outcome(BattleOutcome{}).code==ErrorCode::wrong_phase);
    REQUIRE(harness.post_objective_event(ObjectiveEventKind::teams_reduced).code==ErrorCode::wrong_phase);
    REQUIRE(harness.answer_save(true).code==ErrorCode::wrong_phase);
    REQUIRE(harness.poll_save().code==ErrorCode::wrong_phase);
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(harness.start_new(Settings{}).code==ErrorCode::wrong_phase);
    REQUIRE(harness.acknowledge_result(FlowResultChoice::continue_without_save).code==ErrorCode::wrong_phase);
    for (const bool b : harness.offered_choices()) REQUIRE(!b);
    return true;
}

RUNTIME_TEST(offered_choices_follow_flow_rule) {
    bool saw_kind7=false;
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
        const ResultChoices win=campaign_flow::offered_result_choices(wave,campaign_flow::flow_result_code_won);
        const ResultChoices loss=campaign_flow::offered_result_choices(wave,campaign_flow::flow_result_code_selectable);
        if (wave.game_kind==7) {
            saw_kind7=true;
            REQUIRE(campaign_flow::result_auto_advances(wave,campaign_flow::flow_result_code_won));
            for (const bool b : win) REQUIRE(!b);
            REQUIRE(loss[slot(FlowResultChoice::continue_without_save)] && loss[slot(FlowResultChoice::save_and_continue)]);
            REQUIRE(!loss[slot(FlowResultChoice::retry)] && !loss[slot(FlowResultChoice::quit)]);
        } else {
            REQUIRE(win[slot(FlowResultChoice::continue_without_save)]);
            REQUIRE(!win[slot(FlowResultChoice::retry)] && !win[slot(FlowResultChoice::quit)]);
            REQUIRE(win[slot(FlowResultChoice::save_and_continue)]==wave.save_prompt_after_win);
            REQUIRE(loss[slot(FlowResultChoice::retry)] && loss[slot(FlowResultChoice::quit)]);
            REQUIRE(!loss[slot(FlowResultChoice::continue_without_save)]);
        }
    }
    REQUIRE(saw_kind7);
    return true;
}

RUNTIME_TEST(loss_choice_decides_retry_or_quit) {
    const std::uint8_t plain=first_wave_where(false);
    REQUIRE(plain<campaign_wave_count);
    RecordingFrontend frontend;
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=nullptr,.frontend=&frontend,.music=nullptr});
    CampaignRecord record=default_campaign_record();
    record.mission_id=static_cast<std::uint8_t>(campaign_wave_definitions()[plain].mission_id);
    REQUIRE(harness.resume(record,0).ok());
    const std::uint8_t wave=harness.wave_index();
    REQUIRE(harness.confirm_briefing().ok());
    REQUIRE(harness.post_objective_event(ObjectiveEventKind::player_squad_lost).ok());
    REQUIRE(harness.submit_outcome(BattleOutcome{}).ok());
    REQUIRE(!harness.last_result_won());
    REQUIRE(!frontend.won.back());
    REQUIRE(harness.acknowledge_result(FlowResultChoice::continue_without_save).code==ErrorCode::choice_not_offered);
    REQUIRE(harness.phase()==Phase::result);
    REQUIRE(harness.acknowledge_result(FlowResultChoice::retry).ok());
    REQUIRE(harness.phase()==Phase::briefing);
    REQUIRE(harness.wave_index()==wave);
    REQUIRE(harness.confirm_briefing().ok());
    REQUIRE(harness.submit_outcome(BattleOutcome{.kind=OutcomeKind::lost,.score=0,.bonus=0}).ok());
    REQUIRE(harness.acknowledge_result(FlowResultChoice::quit).ok());
    REQUIRE(harness.phase()==Phase::menu);
    REQUIRE(frontend.phases.back()==Phase::menu);
    return true;
}

RUNTIME_TEST(failed_save_keeps_prompt_and_old_record) {
    CountingSave save;
    save.on_commit=SaveStatus::failed;
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=&save,.frontend=nullptr,.music=nullptr});
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(win_current_wave(harness,10));
    REQUIRE(harness.offered_choices()[slot(FlowResultChoice::save_and_continue)]);
    REQUIRE(harness.acknowledge_result(FlowResultChoice::save_and_continue).code==ErrorCode::save_rejected);
    REQUIRE(harness.phase()==Phase::save_prompt);
    REQUIRE(!harness.saved_record().has_value());
    REQUIRE(save.next_indices.back()==1U);
    REQUIRE(harness.answer_save(true).code==ErrorCode::save_rejected);
    REQUIRE(save.missions.size()==2U);
    REQUIRE(harness.answer_save(false).ok());
    REQUIRE(harness.wave_index()==1U);
    REQUIRE(harness.saves_committed()==0U);
    return true;
}

RUNTIME_TEST(asynchronous_save_waits_for_poll) {
    CountingSave save;
    save.on_commit=SaveStatus::pending;
    save.on_poll=SaveStatus::pending;
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=&save,.frontend=nullptr,.music=nullptr});
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(win_current_wave(harness,20));
    REQUIRE(harness.acknowledge_result(FlowResultChoice::save_and_continue).ok());
    REQUIRE(harness.phase()==Phase::save_pending);
    REQUIRE(harness.poll_save().ok());
    REQUIRE(harness.phase()==Phase::save_pending);
    REQUIRE(!harness.saved_record().has_value());
    save.on_poll=SaveStatus::failed;
    REQUIRE(harness.poll_save().code==ErrorCode::save_rejected);
    REQUIRE(harness.phase()==Phase::save_prompt);
    REQUIRE(harness.answer_save(true).ok());
    REQUIRE(harness.phase()==Phase::save_pending);
    save.on_poll=SaveStatus::committed;
    REQUIRE(harness.poll_save().ok());
    REQUIRE(harness.phase()==Phase::briefing);
    REQUIRE(harness.wave_index()==1U);
    REQUIRE(harness.saved_record().has_value());
    REQUIRE(harness.saved_record()->carry==20);
    REQUIRE(harness.saves_committed()==1U);
    return true;
}

RUNTIME_TEST(music_follows_retail_load_and_countdown_order) {
    RecordingMusic music;
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=nullptr,.frontend=nullptr,.music=&music});
    REQUIRE(harness.start_new(Settings{}).ok());
    const std::int32_t mission=campaign_wave_definitions()[0].mission_id;
    REQUIRE(music.picks.size()==1U && music.picks[0]==MusicTransition::briefing);
    REQUIRE(harness.confirm_briefing().ok());
    REQUIRE(music.picks.back()==MusicTransition::wave_loading);
    REQUIRE(harness.countdown_finished().ok());
    REQUIRE(music.picks.back()==MusicTransition::countdown_finished);
    REQUIRE(music.missions.back()==mission);
    REQUIRE(harness.countdown_finished().code==ErrorCode::wrong_phase);
    REQUIRE(harness.submit_outcome(BattleOutcome{.kind=OutcomeKind::lost,.score=0,.bonus=0}).ok());
    REQUIRE(music.picks.back()==MusicTransition::results);
    if (harness.offered_choices()[slot(FlowResultChoice::retry)]) {
        REQUIRE(harness.acknowledge_result(FlowResultChoice::retry).ok());
        REQUIRE(harness.confirm_briefing().ok());
        REQUIRE(music.picks.back()==MusicTransition::wave_restarted);
    }
    return true;
}

RUNTIME_TEST(objective_events_are_capped) {
    Coordinator harness(complete_synthetic_inputs(),Adapters{});
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(harness.confirm_briefing().ok());
    for (std::uint32_t i=0U; i<max_objective_events; ++i)
        REQUIRE(harness.post_objective_event(ObjectiveEventKind::player_squad_lost).ok());
    REQUIRE(!harness.events_overflowed());
    REQUIRE(harness.post_objective_event(ObjectiveEventKind::teams_reduced).ok());
    REQUIRE(harness.events_overflowed());
    REQUIRE(harness.submit_outcome(BattleOutcome{}).ok());
    REQUIRE(!harness.last_result_won());
    return true;
}

RUNTIME_TEST(full_synthetic_campaign_runs_through_all_17_waves) {
    RecordingFrontend frontend;
    RecordingMusic music;
    CountingSave save;
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=&save,.frontend=&frontend,.music=&music});
    REQUIRE(harness.start_new(Settings{.difficulty=CampaignDifficulty::hard,.selected_unit_kind=2}).ok());
    std::vector<std::uint8_t> visited;
    std::uint32_t guard=0U;
    while (harness.phase()!=Phase::completed) {
        REQUIRE(++guard<=campaign_wave_count);
        REQUIRE(harness.phase()==Phase::briefing);
        const std::uint8_t wave=harness.wave_index();
        visited.push_back(wave);
        const std::size_t saves_before=save.next_indices.size();
        REQUIRE(win_current_wave(harness,static_cast<std::int32_t>(wave)*100));
        REQUIRE(harness.record().carry==static_cast<std::int32_t>(wave)*100);
        REQUIRE(harness.record().unit_type==2);
        if (harness.phase()==Phase::result) {
            REQUIRE(frontend.won.back());
            const bool can_save=harness.offered_choices()[slot(FlowResultChoice::save_and_continue)];
            REQUIRE(can_save==(wave+1U<campaign_wave_count));
            REQUIRE(harness.acknowledge_result(can_save ? FlowResultChoice::save_and_continue
                                                        : FlowResultChoice::continue_without_save).ok());
        } else {
            // Won endurance wave: carried save choice (the previous dialog saved).
            REQUIRE(campaign_wave_definitions()[wave].game_kind==7);
            REQUIRE(harness.last_save_choice());
        }
        if (wave+1U<campaign_wave_count) {
            REQUIRE(save.next_indices.size()==saves_before+1U);
            REQUIRE(save.next_indices.back()==wave+1U);
            REQUIRE(harness.phase()==Phase::briefing);
            REQUIRE(harness.wave_index()==wave+1U);
        } else {
            // Final wave (mission 16): no save, straight to completion.
            REQUIRE(save.next_indices.size()==saves_before);
        }
    }
    REQUIRE(visited.size()==campaign_wave_count);
    for (std::size_t i=0U; i<visited.size(); ++i) REQUIRE(visited[i]==i);
    REQUIRE(harness.waves_cleared()==campaign_wave_count);
    // 16 saves (after waves 0..15), none after the final wave.
    REQUIRE(harness.saves_committed()+1U==campaign_wave_count);
    REQUIRE(save.missions.size()==harness.saves_committed());
    REQUIRE(save.next_indices.back()==campaign_wave_count-1U);
    const auto waves=campaign_wave_definitions();
    REQUIRE(static_cast<std::int32_t>(save.missions.back())==waves[campaign_wave_count-1U].mission_id);
    REQUIRE(harness.saved_record().has_value());
    REQUIRE(harness.saved_record()->carry==static_cast<std::int32_t>(campaign_wave_count-2U)*100);
    for (const std::uint8_t next : save.next_indices) REQUIRE(next<campaign_wave_count);
    REQUIRE(frontend.phases.back()==Phase::completed);
    REQUIRE(!harness.pending_request().has_value());
    return true;
}

RUNTIME_TEST(pause_restart_relaunches_same_wave) {
    RecordingMusic music;
    RecordingFrontend frontend;
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=nullptr,.frontend=&frontend,.music=&music});
    REQUIRE(harness.restart_battle().code==ErrorCode::wrong_phase);
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(harness.restart_battle().code==ErrorCode::wrong_phase);   // briefing
    REQUIRE(harness.confirm_briefing().ok());
    REQUIRE(harness.countdown_finished().ok());
    REQUIRE(harness.post_objective_event(ObjectiveEventKind::player_squad_lost).ok());
    const std::uint32_t first=harness.pending_request()->sequence;
    const CampaignRecord before=harness.record();
    REQUIRE(harness.restart_battle().ok());
    REQUIRE(harness.phase()==Phase::awaiting_battle);
    REQUIRE(harness.wave_index()==0U);
    REQUIRE(harness.pending_request().has_value());
    REQUIRE(harness.pending_request()->sequence==first+1U);
    REQUIRE(harness.pending_request()->launch.wave_index==0U);
    REQUIRE(music.picks.back()==MusicTransition::wave_restarted);
    REQUIRE(frontend.phases.back()==Phase::awaiting_battle);
    REQUIRE(harness.record().carry==before.carry);
    REQUIRE(harness.waves_cleared()==0U);
    // Countdown and events reset: the dropped loss event does not count.
    REQUIRE(harness.countdown_finished().ok());
    REQUIRE(harness.submit_outcome(BattleOutcome{.kind=OutcomeKind::won,.score=9,.bonus=0}).ok());
    REQUIRE(harness.last_result_won());
    REQUIRE(harness.restart_battle().code==ErrorCode::wrong_phase);   // result
    return true;
}

RUNTIME_TEST(resume_starts_at_record_wave) {
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
        Coordinator harness(complete_synthetic_inputs(),Adapters{});
        CampaignRecord record=default_campaign_record();
        record.mission_id=static_cast<std::uint8_t>(wave.mission_id);
        REQUIRE(harness.resume(record,0).ok());
        REQUIRE(harness.phase()==Phase::briefing);
        REQUIRE(harness.wave_index()==campaign_resume_for_record(record).wave->wave_index);
    }
    Coordinator harness(complete_synthetic_inputs(),Adapters{});
    CampaignRecord bad=default_campaign_record();
    bad.mission_id=0xFFU;
    if (campaign_resume_for_record(bad).status==CampaignResumeStatus::unknown_mission)
        REQUIRE(harness.resume(bad,0).code==ErrorCode::resume_mission_unknown);
    // Mission id 0 (never written by retail) still resumes as completed.
    CampaignRecord done=default_campaign_record();
    done.mission_id=0U;
    REQUIRE(campaign_resume_for_record(done).status==CampaignResumeStatus::campaign_complete);
    Coordinator finished(complete_synthetic_inputs(),Adapters{});
    REQUIRE(finished.resume(done,0).ok());
    REQUIRE(finished.phase()==Phase::completed);
    return true;
}

RUNTIME_TEST(checksum_is_deterministic_and_tracks_state) {
    Coordinator a(complete_synthetic_inputs(),Adapters{});
    Coordinator b(complete_synthetic_inputs(),Adapters{});
    REQUIRE(a.checksum()==b.checksum());
    REQUIRE(a.start_new(Settings{}).ok());
    REQUIRE(a.checksum()!=b.checksum());
    REQUIRE(b.start_new(Settings{}).ok());
    REQUIRE(a.checksum()==b.checksum());
    REQUIRE(a.confirm_briefing().ok());
    REQUIRE(b.confirm_briefing().ok());
    REQUIRE(a.countdown_finished().ok());
    REQUIRE(a.checksum()!=b.checksum());
    REQUIRE(b.countdown_finished().ok());
    REQUIRE(a.checksum()==b.checksum());
    REQUIRE(a.post_objective_event(ObjectiveEventKind::teams_reduced).ok());
    REQUIRE(a.checksum()!=b.checksum());
    return true;
}

RUNTIME_TEST_MAIN()
