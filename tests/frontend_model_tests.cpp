#include "battletanx/frontend_model.hpp"
#include "runtime_test_support.hpp"

#include <cstddef>
#include <initializer_list>

using namespace battletanx;
using namespace battletanx::frontend;

namespace {

FrontendStep press(FrontendState& state,std::uint32_t bits) { return update_frontend(state,MenuInput{.pressed=bits,.raw_pressed=0U}); }

bool has(const FrontendStep& step,CommandKind kind) {
    for (const TransitionCommand& command : step.commands) if (command.kind==kind) return true;
    return false;
}

CampaignRecord saved_record() {
    CampaignRecord record=default_campaign_record();
    record.mission_id=5U;
    record.unit_type=3;
    record.difficulty=CampaignDifficulty::hard;
    record.sfx_volume=2;
    return record;
}

FrontendState at_options() {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::ok_no_save,nullptr);
    press(state,buttons::confirm);   // title -> setup menu, cursor on start game (6)
    press(state,buttons::up);        // 6 -> 5 (load game, disabled without a record) -> 4
    press(state,buttons::up);        // credits
    press(state,buttons::up);        // options (2)
    press(state,buttons::confirm);
    return state;
}

} // namespace

RUNTIME_TEST(pak_decisions_cover_every_probe) {
    REQUIRE(decide_pak_load(PakProbe::ok_with_save)==PakDecision::load_record);
    REQUIRE(decide_pak_load(PakProbe::ok_no_save)==PakDecision::use_defaults);
    REQUIRE(decide_pak_load(PakProbe::damaged)==PakDecision::repair_offer);
    REQUIRE(decide_pak_load(PakProbe::full)==PakDecision::manage_notes);
    REQUIRE(decide_pak_load(PakProbe::no_pak)==PakDecision::error_retry_or_continue);
    REQUIRE(decide_pak_load(PakProbe::checksum_bad)==PakDecision::error_retry_or_continue);
    return true;
}

RUNTIME_TEST(save_derived_defaults_select_continue_and_unit) {
    FrontendState state=make_frontend_state();
    const CampaignRecord record=saved_record();
    const FrontendStep step=apply_pak_probe(state,PakProbe::ok_with_save,&record);
    REQUIRE(has(step,CommandKind::load_campaign_record));
    REQUIRE(item_enabled(state,ItemId::campaign_continue));
    press(state,buttons::confirm);
    REQUIRE(state.screen==Screen::campaign_menu);
    REQUIRE(state.cursor==static_cast<std::int32_t>(frontend_setup::SetupRow::load_game));
    REQUIRE(frontend_setup::setup_row_enabled(state.setup_menu,frontend_setup::SetupRow::load_game));
    press(state,buttons::confirm);
    REQUIRE(state.screen==Screen::unit_select);
    REQUIRE(state.gangs.gang[0]==3);
    return true;
}

RUNTIME_TEST(pak_error_cancel_continues_without_save) {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::no_pak,nullptr);
    press(state,buttons::confirm);
    REQUIRE(state.screen==Screen::pak_menu);
    REQUIRE(has(press(state,buttons::confirm),CommandKind::repoll_pak));
    const FrontendStep step=press(state,buttons::cancel);
    REQUIRE(has(step,CommandKind::use_default_record));
    REQUIRE(!state.save_enabled);
    REQUIRE(state.screen==Screen::campaign_menu);
    REQUIRE(!item_enabled(state,ItemId::campaign_continue));
    REQUIRE(!frontend_setup::setup_row_enabled(state.setup_menu,frontend_setup::SetupRow::load_game));
    const std::vector<DrawCommand> draw=frontend_draw(state);
    REQUIRE(draw.size()==7U);
    REQUIRE(draw[5].item==ItemId::setup_row && !draw[5].enabled);
    REQUIRE(draw[6].selected);
    return true;
}

RUNTIME_TEST(options_rows_wrap_and_volumes_clamp) {
    FrontendState state=at_options();
    REQUIRE(state.screen==Screen::options);
    REQUIRE(state.cursor==0);
    press(state,buttons::up);
    REQUIRE(state.cursor==7);
    press(state,buttons::down);
    REQUIRE(state.cursor==0);
    for (int i=0;i<10;++i) press(state,buttons::right);
    REQUIRE(state.record.sfx_volume==7);
    for (int i=0;i<10;++i) press(state,buttons::left);
    REQUIRE(state.record.sfx_volume==0);
    return true;
}

RUNTIME_TEST(options_difficulty_wraps_and_flags_toggle) {
    FrontendState state=at_options();
    press(state,buttons::down);
    press(state,buttons::down);
    REQUIRE(state.cursor==2);
    press(state,buttons::left);
    REQUIRE(state.record.difficulty==CampaignDifficulty::hard);
    press(state,buttons::right);
    REQUIRE(state.record.difficulty==CampaignDifficulty::easy);
    press(state,buttons::down);
    press(state,buttons::right);
    REQUIRE(state.record.powerups.raw==1U);
    press(state,buttons::right);
    REQUIRE(state.record.powerups.raw==0U);
    return true;
}

RUNTIME_TEST(options_exit_and_help_freeze) {
    FrontendState state=at_options();
    press(state,buttons::help);
    REQUIRE(state.help_visible);
    press(state,buttons::down);
    REQUIRE(state.cursor==0);
    press(state,buttons::help);
    press(state,buttons::up);
    REQUIRE(state.cursor==7);
    const FrontendStep step=press(state,buttons::confirm);
    REQUIRE(has(step,CommandKind::write_campaign_record));
    REQUIRE(state.screen==Screen::campaign_menu);
    return true;
}

RUNTIME_TEST(new_campaign_flow_to_mission) {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::ok_no_save,nullptr);
    press(state,buttons::confirm);
    press(state,buttons::confirm);  // start game
    REQUIRE(state.screen==Screen::difficulty);
    press(state,buttons::down);
    press(state,buttons::confirm);
    REQUIRE(state.record.difficulty==CampaignDifficulty::normal);
    REQUIRE(state.screen==Screen::unit_select);
    REQUIRE(state.gangs.gang[0]==9);
    press(state,buttons::right);
    REQUIRE(state.gangs.gang[0]==0);
    press(state,buttons::right);  // locked index 1 is skipped
    REQUIRE(state.gangs.gang[0]==2);
    press(state,buttons::left);
    REQUIRE(state.gangs.gang[0]==0);
    press(state,buttons::confirm);
    REQUIRE(state.record.unit_type==0);
    REQUIRE(state.screen==Screen::controller_preset);
    REQUIRE(state.cursor==0);
    for (int i=0;i<frontend_setup::preset_row;++i) press(state,buttons::down);
    REQUIRE(state.cursor==frontend_setup::preset_row);
    press(state,buttons::left);
    REQUIRE(state.controls.preset_selected==3);
    REQUIRE(state.controls.preset_saved==3);
    REQUIRE(press(state,buttons::confirm).commands.empty()); // confirm on row 9 re-applies the preset
    press(state,buttons::down);
    REQUIRE(state.cursor==frontend_setup::done_row);
    const FrontendStep step=press(state,buttons::confirm);
    REQUIRE(has(step,CommandKind::start_mission));
    REQUIRE(has(step,CommandKind::write_campaign_record));
    REQUIRE(state.screen==Screen::in_mission);
    REQUIRE(state.record.control_scheme==3U);
    REQUIRE(state.record.bindings==frontend_setup::preset_bindings[3]);
    return true;
}

RUNTIME_TEST(controls_bind_raw_button_and_exit_only_from_done) {
    FrontendState state=make_frontend_state();
    state.screen=Screen::controller_preset;
    const std::uint32_t before=state.controls.words[0];
    (void)update_frontend(state,MenuInput{.pressed=0U,.raw_pressed=0x8000U});
    REQUIRE(state.controls.words[0]==0x8000U);
    REQUIRE(state.controls.edited);
    REQUIRE(state.record.bindings[0]==before); // stored only on done
    REQUIRE(press(state,buttons::cancel).commands.empty());
    REQUIRE(state.screen==Screen::controller_preset);
    for (int i=0;i<frontend_setup::done_row;++i) press(state,buttons::down);
    const FrontendStep step=press(state,buttons::cancel);
    REQUIRE(has(step,CommandKind::start_mission));
    REQUIRE(!has(step,CommandKind::write_campaign_record)); // save disabled
    REQUIRE(state.record.bindings[0]==0x8000U);
    return true;
}

RUNTIME_TEST(setup_menu_rows_and_unsupported_modes) {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::ok_no_save,nullptr);
    press(state,buttons::confirm);
    press(state,buttons::up);
    press(state,buttons::up);  // credits (3)
    REQUIRE(state.cursor==3);
    FrontendStep step=press(state,buttons::confirm);
    REQUIRE(has(step,CommandKind::setup_row_unsupported));
    REQUIRE(state.screen==Screen::campaign_menu);
    press(state,buttons::up);
    press(state,buttons::up);  // playmode (1)
    press(state,buttons::right);
    REQUIRE(state.setup_menu.playmode==1);
    step=press(state,buttons::alt_confirm); // 0x40 accepts start game from any row
    REQUIRE(state.cursor==static_cast<std::int32_t>(frontend_setup::SetupRow::start_game));
    REQUIRE(has(step,CommandKind::setup_row_unsupported));
    REQUIRE(state.screen==Screen::campaign_menu);
    press(state,buttons::up);
    press(state,buttons::up);
    press(state,buttons::up);
    press(state,buttons::up);
    press(state,buttons::left); // back to campaign playmode 0
    REQUIRE(state.setup_menu.playmode==0);
    press(state,buttons::alt_confirm);
    REQUIRE(state.screen==Screen::difficulty);
    press(state,buttons::cancel);
    REQUIRE(state.screen==Screen::campaign_menu);
    press(state,buttons::cancel); // TEMPORARY POLICY
    REQUIRE(state.screen==Screen::title);
    return true;
}

RUNTIME_TEST(unit_select_uses_host_gang_draws_and_back) {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::ok_no_save,nullptr);
    state.computer_gang_draws={0U,0U,5U,7U};
    press(state,buttons::confirm);
    press(state,buttons::confirm);
    press(state,buttons::confirm);
    REQUIRE(state.screen==Screen::unit_select);
    REQUIRE(state.gangs.gang[1]==0 && state.gangs.gang[2]==6 && state.gangs.gang[3]==8);
    REQUIRE(!setup_draw(state).empty());
    press(state,buttons::cancel);
    REQUIRE(state.screen==Screen::campaign_menu);
    return true;
}

RUNTIME_TEST(pause_wraps_and_confirms) {
    FrontendState state=make_frontend_state();
    state.screen=Screen::in_mission;
    enter_pause(state);
    REQUIRE(state.screen==Screen::pause);
    press(state,buttons::up);
    REQUIRE(state.cursor==2);
    press(state,buttons::down);
    REQUIRE(state.cursor==0);
    press(state,buttons::down);
    const FrontendStep restart=press(state,buttons::confirm);
    REQUIRE(has(restart,CommandKind::restart_mission));
    REQUIRE(has(restart,CommandKind::easy_restart_hook));
    REQUIRE(state.screen==Screen::in_mission);
    enter_pause(state);
    press(state,buttons::up);
    REQUIRE(has(press(state,buttons::confirm),CommandKind::quit_to_title));
    REQUIRE(state.screen==Screen::title);
    return true;
}

RUNTIME_TEST(result_choices) {
    FrontendState state=make_frontend_state();
    REQUIRE(has(enter_result(state,ResultKind::defeat),CommandKind::stop_unit_sounds));
    REQUIRE(has(press(state,buttons::confirm),CommandKind::restart_mission));
    (void)enter_result(state,ResultKind::defeat);
    REQUIRE(has(press(state,buttons::cancel),CommandKind::quit_to_title));
    (void)enter_result(state,ResultKind::victory);
    REQUIRE(has(press(state,buttons::confirm),CommandKind::advance_mission));
    return true;
}

namespace {
campaign_flow::FlowTransition flow_result(std::uint8_t wave_index,bool won,std::initializer_list<campaign_flow::FlowResultChoice> offered) {
    campaign_flow::FlowTransition t;
    t.kind=campaign_flow::FlowTransitionKind::result;
    t.wave_index=wave_index;
    t.won=won;
    for (const campaign_flow::FlowResultChoice c : offered) t.choices[static_cast<std::size_t>(c)]=true;
    return t;
}
bool advanced_with(const FrontendStep& step,campaign_flow::FlowResultChoice choice) {
    for (const TransitionCommand& c : step.commands)
        if (c.kind==CommandKind::advance_mission && c.value==static_cast<std::int32_t>(choice)) return true;
    return false;
}
} // namespace

RUNTIME_TEST(victory_cancel_saves_confirm_does_not) {
    using campaign_flow::FlowResultChoice;
    FrontendState state=make_frontend_state();
    state.save_enabled=true;
    const auto win=flow_result(1U,true,{FlowResultChoice::continue_without_save,FlowResultChoice::save_and_continue});
    (void)enter_result(state,win);
    const FrontendStep plain=press(state,buttons::confirm);
    REQUIRE(advanced_with(plain,FlowResultChoice::continue_without_save));
    REQUIRE(!has(plain,CommandKind::write_campaign_record));
    (void)enter_result(state,win);
    REQUIRE(item_enabled(state,ItemId::result_save_continue));
    const FrontendStep saved=press(state,buttons::cancel);
    REQUIRE(advanced_with(saved,FlowResultChoice::save_and_continue));
    REQUIRE(has(saved,CommandKind::write_campaign_record));
    return true;
}

RUNTIME_TEST(final_wave_cancel_ignored_endurance_win_cancel_continues) {
    using campaign_flow::FlowResultChoice;
    FrontendState state=make_frontend_state();
    state.save_enabled=true;
    (void)enter_result(state,flow_result(16U,true,{FlowResultChoice::continue_without_save}));
    REQUIRE(press(state,buttons::cancel).commands.empty());
    REQUIRE(state.screen==Screen::result);
    // Won endurance wave: the flow auto-advances; no dialog opens.
    campaign_flow::FlowTransition skipped=flow_result(4U,true,{});
    skipped.auto_advanced=true;
    REQUIRE(enter_result(state,skipped).commands.empty());
    REQUIRE(state.screen==Screen::in_mission);
    // Endurance normal end (loss): cancel saves.
    (void)enter_result(state,flow_result(4U,false,{FlowResultChoice::continue_without_save,FlowResultChoice::save_and_continue}));
    REQUIRE(state.result==ResultKind::victory);
    REQUIRE(advanced_with(press(state,buttons::cancel),FlowResultChoice::save_and_continue));
    return true;
}

RUNTIME_TEST(no_choice_result_accepts_nothing) {
    FrontendState state=make_frontend_state();
    (void)enter_result(state,flow_result(1U,false,{}));
    REQUIRE(press(state,buttons::confirm|buttons::cancel).commands.empty());
    REQUIRE(state.screen==Screen::result);
    REQUIRE(frontend_draw(state).empty());
    return true;
}

RUNTIME_TEST(new_campaign_keeps_base_defense_and_bindings) {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::ok_no_save,nullptr);
    state.record.base_defense.set(true);
    state.record.bindings[3]=0x1234U;
    press(state,buttons::confirm);
    press(state,buttons::confirm);
    press(state,buttons::confirm);
    REQUIRE(state.screen==Screen::unit_select);
    REQUIRE(state.record.base_defense.raw==1U);
    REQUIRE(state.record.bindings[3]==0x1234U);
    return true;
}

RUNTIME_TEST(attract_reported_unsupported_and_draws_identifiers) {
    FrontendState state=make_frontend_state();
    press(state,buttons::down);
    REQUIRE(has(press(state,buttons::confirm),CommandKind::attract_unsupported));
    REQUIRE(state.screen==Screen::title);
    const std::vector<DrawCommand> draw=frontend_draw(state);
    REQUIRE(draw.size()==2U);
    REQUIRE(draw[1].item==ItemId::title_attract);
    REQUIRE(draw[1].selected);
    REQUIRE(!draw[1].enabled);
    return true;
}

RUNTIME_TEST(serialize_is_deterministic) {
    FrontendState a=make_frontend_state();
    FrontendState b=make_frontend_state();
    StateWriter wa;
    StateWriter wb;
    serialize(wa,a);
    serialize(wb,b);
    REQUIRE(wa.bytes()==wb.bytes());
    press(b,buttons::down);
    StateWriter wc;
    serialize(wc,b);
    REQUIRE(wa.bytes()!=wc.bytes());
    return true;
}

RUNTIME_TEST_MAIN()
