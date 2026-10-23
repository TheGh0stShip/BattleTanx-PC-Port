#include "battletanx/campaign_harness_bindings.hpp"
#include "runtime_test_support.hpp"

#include <vector>

using namespace battletanx;
using namespace battletanx::campaign_harness;
using namespace battletanx::campaign_harness_bindings;

namespace {

using campaign_flow::FlowResultChoice;
using music_selection::MusicTransition;

// Synthetic sink: records every write; attempts listed in fail_on fail and
// attempts listed in defer_on are deferred (1-based attempt numbers).
struct ScriptedSink final : PakWriteSink {
    std::vector<std::uint32_t> fail_on;
    std::vector<std::uint32_t> defer_on;
    std::uint32_t attempts=0U;
    std::vector<ControllerPakRecord> stored;
    SinkWrite write(const ControllerPakRecord& bytes) override {
        ++attempts;
        for (const std::uint32_t n : fail_on) if (n==attempts) return SinkWrite::failed;
        for (const std::uint32_t n : defer_on) if (n==attempts) return SinkWrite::deferred;
        stored.push_back(bytes);
        return SinkWrite::stored;
    }
};

constexpr frontend::MenuInput confirm_input{.pressed=frontend::buttons::confirm,.raw_pressed=0U};
constexpr frontend::MenuInput cancel_input{.pressed=frontend::buttons::cancel,.raw_pressed=0U};
constexpr std::size_t slot(FlowResultChoice c) { return static_cast<std::size_t>(c); }
constexpr std::array<FlowResultChoice,4> all_choices{FlowResultChoice::continue_without_save,
    FlowResultChoice::save_and_continue,FlowResultChoice::retry,FlowResultChoice::quit};

ObjectiveEventKind winning_event(CampaignObjectiveKind objective) {
    if (objective==CampaignObjectiveKind::goal_slot) return ObjectiveEventKind::goal_slot_reached;
    if (objective==CampaignObjectiveKind::player_win_flag) return ObjectiveEventKind::player_win_flag;
    return ObjectiveEventKind::teams_reduced;
}

// Plays an already launched battle (Phase::awaiting_battle) to its result.
bool play_wave_after_launch(Coordinator& harness,bool win,std::int32_t score) {
    if (!harness.countdown_finished().ok()) return false;
    const auto& request=harness.pending_request();
    if (!request) return false;
    const ObjectiveEventKind event=win ? winning_event(request->objective) : ObjectiveEventKind::player_squad_lost;
    if (!harness.post_objective_event(event).ok()) return false;
    return harness.submit_outcome(BattleOutcome{.kind=OutcomeKind::from_objective_events,.score=score,.bonus=2}).ok();
}

bool play_wave(Coordinator& harness,bool win,std::int32_t score) {
    return harness.confirm_briefing().ok() && play_wave_after_launch(harness,win,score);
}

std::uint8_t first_wave_where(bool endurance) {
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions())
        if ((wave.game_kind==7)==endurance) return wave.wave_index;
    return static_cast<std::uint8_t>(campaign_wave_count);
}

bool has_item(const std::vector<frontend::DrawCommand>& draw,frontend::ItemId item) {
    for (const frontend::DrawCommand& d : draw) if (d.item==item) return true;
    return false;
}

} // namespace

RUNTIME_TEST(flow_binding_agrees_with_catalog_rule) {
    const FlowBinding flow;
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
        for (const bool won : {true,false}) {
            const ResultChoices offered=campaign_flow::offered_result_choices(wave,
                won ? campaign_flow::flow_result_code_won : campaign_flow::flow_result_code_selectable);
            if (campaign_flow::result_auto_advances(wave,campaign_flow::flow_result_code_won) && won) {
                // Won endurance: no choice offered; the carried choice still advances.
                REQUIRE(flow.decide(wave,true,FlowResultChoice::continue_without_save)==FlowDecision::advance);
                REQUIRE(flow.decide(wave,true,FlowResultChoice::save_and_continue)==FlowDecision::advance);
            }
            for (const FlowResultChoice choice : all_choices) {
                if (!offered[slot(choice)]) continue;
                REQUIRE(flow.decide(wave,won,choice)==default_flow_decision(wave,choice));
            }
        }
    }
    const CampaignWaveDefinition& last=campaign_wave_definitions().back();
    REQUIRE(flow.decide(last,true,FlowResultChoice::continue_without_save)==FlowDecision::campaign_complete);
    return true;
}

RUNTIME_TEST(save_binding_replaces_failed_request_and_commits_on_success) {
    ScriptedSink sink;
    sink.fail_on={1U};
    const CampaignRecord initial=default_campaign_record();
    SaveBinding save(initial,&sink);
    CampaignRecord staged=initial;
    staged.mission_id=static_cast<std::uint8_t>(campaign_wave_definitions()[1].mission_id);
    staged.carry=1234;
    staged.bonus=7;
    REQUIRE(save.commit(staged,1U)==SaveStatus::failed);
    REQUIRE(save.last_attempt()==SaveAttempt::write_failed);
    REQUIRE(save.poll()==SaveStatus::failed);
    REQUIRE(save.transaction().pending().has_value());
    REQUIRE(save.transaction().failed_attempts()==1U);
    REQUIRE(save.transaction().authoritative().carry==initial.carry);
    const std::uint32_t sequence=save.transaction().pending()->sequence;
    // Identical retry rewrites the same request.
    REQUIRE(save.commit(staged,1U)==SaveStatus::committed);
    REQUIRE(save.last_attempt()==SaveAttempt::committed);
    REQUIRE(!save.transaction().pending().has_value());
    REQUIRE(save.transaction().authoritative().carry==1234);
    REQUIRE(save.transaction().authoritative().mission_id==staged.mission_id);
    REQUIRE(sink.stored.size()==1U);
    REQUIRE(controller_pak_checksum_valid(sink.stored[0]));
    const auto parsed=parse_campaign_record(sink.stored[0]);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->carry==1234);
    REQUIRE(parsed->bonus==7);
    // A different clear replaces the failed pending request (fresh sequence).
    sink.fail_on={3U};   // third write overall
    staged.carry=99;
    REQUIRE(save.commit(staged,1U)==SaveStatus::failed);
    const std::uint32_t stale=save.transaction().pending()->sequence;
    REQUIRE(stale!=sequence);
    staged.carry=100;
    REQUIRE(save.commit(staged,1U)==SaveStatus::committed);
    REQUIRE(save.transaction().authoritative().carry==100);
    // Last valid save is next index 16 (after wave 15). Retail never saves
    // after the final wave, so index 17 is refused and nothing is written.
    const std::uint32_t before=sink.attempts;
    REQUIRE(save.commit(staged,campaign_save::max_save_next_wave_index)==SaveStatus::committed);
    REQUIRE(save.commit(staged,campaign_save::save_wave_limit)==SaveStatus::failed);
    REQUIRE(save.last_attempt()==SaveAttempt::request_refused);
    REQUIRE(sink.attempts==before+1U);
    REQUIRE(save.transaction().authoritative().carry==100);
    SaveBinding no_sink(initial,nullptr);
    REQUIRE(no_sink.commit(staged,1U)==SaveStatus::failed);
    REQUIRE(no_sink.last_attempt()==SaveAttempt::no_sink);
    return true;
}

RUNTIME_TEST(save_binding_deferred_write_completes_later) {
    ScriptedSink sink;
    sink.defer_on={1U,2U};
    const CampaignRecord initial=default_campaign_record();
    SaveBinding save(initial,&sink);
    CampaignRecord staged=initial;
    staged.carry=55;
    REQUIRE(save.finish_write(true)==campaign_save::AckResult::no_pending);
    REQUIRE(save.commit(staged,1U)==SaveStatus::pending);
    REQUIRE(save.poll()==SaveStatus::pending);
    REQUIRE(save.commit(staged,1U)==SaveStatus::pending); // in flight; not rewritten
    REQUIRE(sink.attempts==1U);
    REQUIRE(save.finish_write(false)==campaign_save::AckResult::write_failed);
    REQUIRE(save.poll()==SaveStatus::failed);
    REQUIRE(save.commit(staged,1U)==SaveStatus::pending);
    REQUIRE(save.finish_write(true)==campaign_save::AckResult::committed);
    REQUIRE(save.poll()==SaveStatus::committed);
    REQUIRE(save.transaction().authoritative().carry==55);
    return true;
}

RUNTIME_TEST(save_binding_abandons_stuck_deferred_write) {
    ScriptedSink sink;
    sink.defer_on={1U,2U};
    const CampaignRecord initial=default_campaign_record();
    SaveBinding save(initial,&sink);
    CampaignRecord staged=initial;
    staged.carry=66;
    REQUIRE(!save.abandon_write());
    // Explicit abandon.
    REQUIRE(save.commit(staged,1U)==SaveStatus::pending);
    REQUIRE(save.abandon_write());
    REQUIRE(save.last_attempt()==SaveAttempt::abandoned);
    REQUIRE(!save.transaction().pending().has_value());
    REQUIRE(save.poll()==SaveStatus::failed);
    REQUIRE(save.finish_write(true)==campaign_save::AckResult::no_pending);
    REQUIRE(save.transaction().authoritative().carry==initial.carry);
    // Poll limit: the third poll gives up.
    save.set_deferred_poll_limit(3U);
    REQUIRE(save.commit(staged,1U)==SaveStatus::pending);
    REQUIRE(save.poll()==SaveStatus::pending);
    REQUIRE(save.poll()==SaveStatus::pending);
    REQUIRE(save.poll()==SaveStatus::failed);
    REQUIRE(save.last_attempt()==SaveAttempt::abandoned);
    REQUIRE(!save.transaction().pending().has_value());
    // A fresh request after abandoning writes normally.
    REQUIRE(save.commit(staged,1U)==SaveStatus::committed);
    REQUIRE(save.transaction().authoritative().carry==66);
    REQUIRE(sink.attempts==3U);
    return true;
}

RUNTIME_TEST(coordinator_escapes_stuck_save_through_abandon) {
    ScriptedSink sink;
    sink.defer_on={1U};
    SaveBinding save(default_campaign_record(),&sink);
    FrontendBinding front(true);
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=&save,.frontend=&front,.music=nullptr});
    REQUIRE(abandon_result_save(harness,save).action==ResultAction::wrong_phase);
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(play_wave(harness,true,40));
    REQUIRE(drive_result(harness,front,cancel_input).action==ResultAction::save_pending);
    const ResultDrive abandoned=abandon_result_save(harness,save);
    REQUIRE(abandoned.action==ResultAction::save_failed);
    REQUIRE(harness.phase()==Phase::save_prompt);
    REQUIRE(harness.saves_committed()==0U);
    REQUIRE(retry_save(harness).action==ResultAction::continue_saved);
    REQUIRE(harness.saves_committed()==1U);
    REQUIRE(harness.wave_index()==1U);
    return true;
}

RUNTIME_TEST(pause_restart_binding_and_record_sync) {
    MusicBinding music;
    FrontendBinding front(true);
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=nullptr,.frontend=&front,.music=&music});
    front.attach(&harness);
    REQUIRE(restart_battle(harness).action==ResultAction::wrong_phase);
    REQUIRE(harness.start_new(Settings{.difficulty=CampaignDifficulty::hard,.selected_unit_kind=3}).ok());
    REQUIRE(front.state().record.difficulty==CampaignDifficulty::hard);
    REQUIRE(front.state().record.unit_type==3);
    REQUIRE(harness.confirm_briefing().ok());
    REQUIRE(restart_battle(harness).action==ResultAction::retry);
    REQUIRE(harness.phase()==Phase::awaiting_battle);
    REQUIRE(front.state().screen==frontend::Screen::in_mission);
    REQUIRE(play_wave_after_launch(harness,true,12));
    REQUIRE(front.state().record.carry==12);
    // Without attach the host syncs explicitly.
    FrontendBinding manual(true);
    CampaignRecord record=default_campaign_record();
    record.difficulty=CampaignDifficulty::easy;
    manual.sync_record(record);
    REQUIRE(manual.state().record.difficulty==CampaignDifficulty::easy);
    return true;
}

RUNTIME_TEST(music_binding_applies_retail_transitions) {
    const std::int32_t mission=campaign_wave_definitions()[0].mission_id;
    MusicBinding music(5U);
    music.select(MusicTransition::wave_loading,mission);
    REQUIRE(music.last_commands().size()==1U);
    REQUIRE(music.last_commands()[0].kind==music_selection::AudioCommandKind::stop_music);
    music.select(MusicTransition::countdown_finished,mission);
    const auto track=music_selection::music_track_for_level(mission);
    if (track) {
        REQUIRE(music.state().current_track==*track);
        REQUIRE(music.last_commands().size()>=1U);
    }
    music.select(MusicTransition::briefing,mission);
    REQUIRE(music.last_commands().size()==0U);
    REQUIRE(music.selections()==3U);
    return true;
}

RUNTIME_TEST(full_synthetic_campaign_through_real_save_transaction) {
    for (std::uint8_t w=1U; w<=3U; ++w) REQUIRE(campaign_wave_definitions()[w].game_kind!=7);
    ScriptedSink sink;
    sink.fail_on={3U};   // third save write fails once, then is retried
    sink.defer_on={5U};  // fifth write completes asynchronously
    const CampaignRecord initial=default_campaign_record();
    const FlowBinding flow;
    SaveBinding save(initial,&sink);
    FrontendBinding front(true);
    MusicBinding music;
    Coordinator harness(complete_synthetic_inputs(),
                        Adapters{.flow=&flow,.save=&save,.frontend=&front,.music=&music});
    REQUIRE(harness.start_new(Settings{.difficulty=CampaignDifficulty::normal,.selected_unit_kind=2}).ok());
    REQUIRE(front.state().screen==frontend::Screen::in_mission);
    std::uint32_t expected_saves=0U;
    bool saw_failure=false;
    bool saw_pending=false;
    for (std::uint8_t i=0U; i<campaign_wave_count; ++i) {
        const CampaignWaveDefinition& wave=campaign_wave_definitions()[i];
        REQUIRE(harness.phase()==Phase::briefing);
        REQUIRE(harness.wave_index()==i);
        REQUIRE(play_wave(harness,true,1000+i));
        if (harness.phase()!=Phase::result) {
            // Won endurance wave: auto-advanced with the carried save choice.
            REQUIRE(wave.game_kind==7);
            if (harness.phase()==Phase::save_pending) {
                saw_pending=true;
                REQUIRE(save.finish_write(true)==campaign_save::AckResult::committed);
                REQUIRE(poll_result_save(harness).action==ResultAction::continue_saved);
            }
            REQUIRE(harness.phase()!=Phase::save_prompt);
            ++expected_saves;
            REQUIRE(harness.saves_committed()==expected_saves);
            REQUIRE(save.transaction().authoritative().carry==1000+i);
            REQUIRE(!save.transaction().pending().has_value());
            continue;
        }
        REQUIRE(front.state().screen==frontend::Screen::result);
        REQUIRE(has_item(frontend::frontend_draw(front.state()),frontend::ItemId::result_continue));
        const bool can_save=harness.offered_choices()[slot(FlowResultChoice::save_and_continue)];
        REQUIRE(can_save==(i+1U<campaign_wave_count));
        // Save button (cancel, 0x20) where a save is offered; else continue.
        ResultDrive drive=drive_result(harness,front,can_save ? cancel_input : confirm_input);
        if (drive.action==ResultAction::save_failed) {
            saw_failure=true;
            REQUIRE(harness.phase()==Phase::save_prompt);
            REQUIRE(harness.saves_committed()==expected_saves);
            REQUIRE(save.transaction().pending().has_value());
            REQUIRE(save.transaction().failed_attempts()==1U);
            drive=retry_save(harness);
        }
        if (drive.action==ResultAction::save_pending) {
            saw_pending=true;
            REQUIRE(harness.phase()==Phase::save_pending);
            REQUIRE(poll_result_save(harness).action==ResultAction::save_pending);
            REQUIRE(save.finish_write(true)==campaign_save::AckResult::committed);
            drive=poll_result_save(harness);
        }
        if (drive.action==ResultAction::continue_saved) {
            REQUIRE(can_save);
            ++expected_saves;
            REQUIRE(save.transaction().authoritative().carry==1000+i);
            REQUIRE(save.transaction().authoritative().mission_id==wave.saved_mission_id_after_clear);
        } else {
            REQUIRE(drive.action==ResultAction::continue_unsaved);
            REQUIRE(i+1U==campaign_wave_count);
        }
        REQUIRE(harness.saves_committed()==expected_saves);
        REQUIRE(!save.transaction().pending().has_value());
    }
    // 16 saves (after waves 0..15); completion after mission 16 writes nothing.
    REQUIRE(expected_saves+1U==campaign_wave_count);
    REQUIRE(saw_failure);
    REQUIRE(saw_pending);
    REQUIRE(harness.phase()==Phase::completed);
    REQUIRE(front.state().screen==frontend::Screen::title);
    REQUIRE(harness.waves_cleared()==campaign_wave_count);
    REQUIRE(sink.attempts==expected_saves+1U);
    REQUIRE(harness.saved_record().has_value());
    REQUIRE(harness.saved_record()->carry==save.transaction().authoritative().carry);
    REQUIRE(save.transaction().authoritative().carry==1000+static_cast<std::int32_t>(campaign_wave_count)-2);
    REQUIRE(static_cast<std::int32_t>(save.transaction().authoritative().mission_id)==
            campaign_wave_definitions()[campaign_wave_count-1U].mission_id);
    REQUIRE(campaign_resume_for_record(save.transaction().authoritative()).status==CampaignResumeStatus::resume_wave);
    StateWriter state;
    save.serialize(state);
    front.serialize(state);
    music.serialize(state);
    harness.serialize(state);
    return true;
}

RUNTIME_TEST(frontend_result_choices_drive_retry_continue_and_quit) {
    const std::uint8_t plain=first_wave_where(false);
    REQUIRE(plain<campaign_wave_count);
    ScriptedSink sink;
    const FlowBinding flow;
    SaveBinding save(default_campaign_record(),&sink);
    FrontendBinding front(true);
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=&flow,.save=&save,.frontend=&front,.music=nullptr});
    CampaignRecord resume=default_campaign_record();
    resume.mission_id=static_cast<std::uint8_t>(campaign_wave_definitions()[plain].mission_id);
    REQUIRE(harness.resume(resume,1).ok());
    REQUIRE(harness.wave_index()==plain);
    REQUIRE(drive_result(harness,front,confirm_input).action==ResultAction::wrong_phase);

    // Defeat: retry relaunches the same wave.
    REQUIRE(play_wave(harness,false,5));
    REQUIRE(!harness.last_result_won());
    const auto draw=frontend::frontend_draw(front.state());
    REQUIRE(has_item(draw,frontend::ItemId::result_retry));
    REQUIRE(has_item(draw,frontend::ItemId::result_quit));
    REQUIRE(!has_item(draw,frontend::ItemId::result_continue));
    REQUIRE(drive_result(harness,front,frontend::MenuInput{}).action==ResultAction::none);
    REQUIRE(harness.phase()==Phase::result);
    REQUIRE(drive_result(harness,front,confirm_input).action==ResultAction::retry);
    REQUIRE(harness.phase()==Phase::briefing);
    REQUIRE(harness.wave_index()==plain);
    REQUIRE(sink.attempts==0U);

    // Defeat: quit is a flow decision and returns to the menu without saving.
    REQUIRE(play_wave(harness,false,6));
    REQUIRE(drive_result(harness,front,cancel_input).action==ResultAction::quit);
    REQUIRE(harness.phase()==Phase::menu);
    REQUIRE(front.state().screen==frontend::Screen::campaign_menu);
    REQUIRE(harness.saves_committed()==0U);

    // Victory: confirm continues without saving.
    REQUIRE(harness.resume(resume,1).ok());
    REQUIRE(play_wave(harness,true,77));
    const ResultDrive won=drive_result(harness,front,confirm_input);
    REQUIRE(won.action==ResultAction::continue_unsaved);
    REQUIRE(sink.attempts==0U);
    const CampaignWaveDefinition& wave=campaign_wave_definitions()[plain];
    if (const CampaignWaveDefinition* next=next_campaign_wave(wave)) REQUIRE(harness.wave_index()==next->wave_index);
    return true;
}

RUNTIME_TEST(frontend_without_save_continues_unsaved) {
    ScriptedSink sink;
    SaveBinding save(default_campaign_record(),&sink);
    FrontendBinding front(false);
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=nullptr,.save=&save,.frontend=&front,.music=nullptr});
    REQUIRE(harness.start_new(Settings{}).ok());
    REQUIRE(play_wave(harness,true,10));
    REQUIRE(drive_result(harness,front,confirm_input).action==ResultAction::continue_unsaved);
    REQUIRE(sink.attempts==0U);
    REQUIRE(harness.saves_committed()==0U);
    return true;
}

RUNTIME_TEST(endurance_loss_is_presented_as_continue) {
    const std::uint8_t endurance=first_wave_where(true);
    if (endurance>=campaign_wave_count) return true;
    ScriptedSink sink;
    const FlowBinding flow;
    SaveBinding save(default_campaign_record(),&sink);
    FrontendBinding front(true);
    Coordinator harness(complete_synthetic_inputs(),Adapters{.flow=&flow,.save=&save,.frontend=&front,.music=nullptr});
    CampaignRecord resume=default_campaign_record();
    resume.mission_id=static_cast<std::uint8_t>(campaign_wave_definitions()[endurance].mission_id);
    REQUIRE(harness.resume(resume,0).ok());
    if (harness.wave_index()!=endurance) return true; // duplicate mission id; covered elsewhere
    REQUIRE(play_wave(harness,false,3));
    REQUIRE(harness.offered_choices()[slot(FlowResultChoice::save_and_continue)]);
    const ResultAction action=drive_result(harness,front,confirm_input).action;
    REQUIRE(action==ResultAction::continue_unsaved);
    REQUIRE(harness.phase()!=Phase::result);
    return true;
}

RUNTIME_TEST_MAIN()
