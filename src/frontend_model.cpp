#include "battletanx/frontend_model.hpp"

#include "battletanx/campaign_catalog.hpp"

#include <cstddef>
#include <initializer_list>

namespace battletanx::frontend {
namespace {

void emit(FrontendStep& step,CommandKind kind,std::int32_t value=0) {
    step.commands.push_back(TransitionCommand{.kind=kind,.value=value});
}

std::int32_t wrap(std::int32_t value,std::int32_t count) noexcept {
    if (value<0) return count-1;
    if (value>=count) return 0;
    return value;
}

std::int32_t clamp_volume(std::int32_t value) noexcept {
    if (value<0) return 0;
    if (value>volume_max) return volume_max;
    return value;
}

// Vertical delta exactly as update_options_menu_input / draw_pause_menu: up=-1, down=+1, both=0.
std::int32_t vertical(std::uint32_t pressed) noexcept {
    std::int32_t delta=0;
    if ((pressed&buttons::up)!=0U) delta=-1;
    if ((pressed&buttons::down)!=0U) ++delta;
    return delta;
}

std::int32_t horizontal(std::uint32_t pressed) noexcept {
    std::int32_t delta=0;
    if ((pressed&buttons::left)!=0U) delta=-1;
    if ((pressed&buttons::right)!=0U) ++delta;
    return delta;
}

std::int32_t difficulty_index(CampaignDifficulty difficulty) noexcept {
    const auto raw=static_cast<std::int32_t>(difficulty);
    return raw>=0 && raw<=2 ? raw : 0;
}

CampaignDifficulty difficulty_from(std::int32_t index) noexcept {
    switch (index) {
    case 1: return CampaignDifficulty::normal;
    case 2: return CampaignDifficulty::hard;
    default: return CampaignDifficulty::easy;
    }
}

std::int32_t unit_from_record(const CampaignRecord& record,bool locked_available) noexcept {
    std::int32_t unit=record.unit_type;
    if (unit<0 || unit>=unit_choice_count) unit=unit_choice_count-1; // TEMPORARY POLICY
    if (!locked_available && unit==locked_unit_index) unit=0;
    return unit;
}

void go(FrontendState& state,Screen screen,std::int32_t cursor=0) {
    state.screen=screen;
    state.cursor=cursor;
    state.help_visible=false;
}

namespace fs=frontend_setup;

// TEMPORARY POLICY: Load Game is offered only when a record was actually read
// (retail enables it from D_80134E74 alone and reads the pak on accept).
void enter_setup_menu(FrontendState& state,fs::SetupRow row) {
    go(state,Screen::campaign_menu);
    state.setup_menu.save_enabled=state.save_enabled && state.record_loaded;
    state.setup_menu.help_visible=false;
    if (!fs::setup_row_enabled(state.setup_menu,row)) row=fs::SetupRow::start_game;
    state.setup_menu.cursor=static_cast<std::int32_t>(row);
    state.cursor=state.setup_menu.cursor;
}

void enter_default_setup_menu(FrontendState& state) {
    enter_setup_menu(state,state.record_loaded ? fs::SetupRow::load_game : fs::SetupRow::start_game);
}

// poll_player_bundle_selection entry: one human slot, computer slots drawn
// from the host-supplied RetailRandom values.
void enter_gang_select(FrontendState& state) {
    state.gangs=fs::make_gang_select(1,state.locked_unit_available);
    state.gangs.gang[0]=unit_from_record(state.record,state.locked_unit_available);
    fs::assign_computer_gangs(state.gangs,state.computer_gang_draws);
    go(state,Screen::unit_select);
}

// update_player_slot_configuration entry. Bindings edited earlier in the
// session (D_8012579D) are kept; otherwise the record preset and bindings load.
void enter_controls(FrontendState& state) {
    if (!state.controls.edited) {
        state.controls=fs::make_controls_setup(static_cast<std::int32_t>(state.record.control_scheme));
        (void)fs::load_controls_from_record(state.controls,state.record);
    }
    state.controls.row=0;
    state.controls.help_visible=false;
    state.controls.preset_selected=state.controls.preset_saved;
    go(state,Screen::controller_preset);
}

std::uint8_t toggle_flag(std::uint8_t raw,std::int32_t delta) noexcept {
    return static_cast<std::uint8_t>((static_cast<std::int32_t>(raw)+delta)&1);
}

void update_title(FrontendState& state,std::uint32_t pressed,FrontendStep& step) {
    state.cursor=wrap(state.cursor+vertical(pressed),2);
    if ((pressed&buttons::confirm)==0U) return;
    if (state.cursor==1) {
        emit(step,CommandKind::attract_unsupported);
        return;
    }
    if (state.pak==PakDecision::load_record || state.pak==PakDecision::use_defaults) enter_default_setup_menu(state);
    else go(state,Screen::pak_menu);
}

void update_pak(FrontendState& state,std::uint32_t pressed,FrontendStep& step) {
    // Every error page in poll_controller_pak_screen: 0x20 -> result 4 (leave,
    // usually clearing D_80134E74), 0x10 -> act and result 5 (re-poll).
    if ((pressed&buttons::cancel)!=0U) {
        state.save_enabled=false;
        state.record=default_campaign_record();
        state.record_loaded=false;
        state.pak=PakDecision::use_defaults;
        emit(step,CommandKind::use_default_record);
        enter_default_setup_menu(state);
        return;
    }
    if ((pressed&buttons::confirm)==0U) return;
    if (state.pak==PakDecision::repair_offer) emit(step,CommandKind::repair_pak);
    // value 1: delete the highlighted note first (delete_controller_pak_file)
    emit(step,CommandKind::repoll_pak,state.pak==PakDecision::manage_notes ? 1 : 0);
}

// update_game_setup_menu_input via frontend_setup::update_main_menu.
void update_campaign_menu(FrontendState& state,std::uint32_t pressed,FrontendStep& step) {
    const fs::MainMenuStep result=fs::update_main_menu(state.setup_menu,pressed);
    state.cursor=state.setup_menu.cursor;
    state.help_visible=state.setup_menu.help_visible;
    if (result.cursor_sound) emit(step,CommandKind::play_cursor_sound);
    if (result.value_sound) emit(step,CommandKind::play_value_sound);
    if (!result.accepted) {
        // TEMPORARY POLICY: the retail setup menu has no cancel; it returns to the title.
        if ((pressed&buttons::cancel)!=0U && !state.setup_menu.help_visible) go(state,Screen::title);
        return;
    }
    switch (result.row) {
    case fs::SetupRow::start_game:
        if (state.setup_menu.playmode!=fs::campaign_playmode) {
            emit(step,CommandKind::setup_row_unsupported,static_cast<std::int32_t>(result.row));
            return;
        }
        go(state,Screen::difficulty,difficulty_index(state.record.difficulty));
        return;
    case fs::SetupRow::load_game:
        if (!state.record_loaded) return;
        enter_gang_select(state);
        return;
    case fs::SetupRow::options:
        go(state,Screen::options);
        return;
    case fs::SetupRow::players:
    case fs::SetupRow::playmode:
    case fs::SetupRow::credits:
    case fs::SetupRow::input_code:
    default:
        emit(step,CommandKind::setup_row_unsupported,static_cast<std::int32_t>(result.row));
        return;
    }
}

void update_difficulty(FrontendState& state,std::uint32_t pressed) {
    state.cursor=wrap(state.cursor+vertical(pressed),3);
    if ((pressed&buttons::cancel)!=0U) { enter_setup_menu(state,fs::SetupRow::start_game); return; }
    if ((pressed&buttons::confirm)==0U) return;
    // New campaign: start from the default record's progress but keep every
    // option value and the bindings; reset_session_state_defaults touches
    // neither (STRONG INFERENCE).
    const CampaignRecord kept=state.record;
    state.record=default_campaign_record();
    state.record.sfx_volume=kept.sfx_volume;
    state.record.music_volume=kept.music_volume;
    state.record.powerups=kept.powerups;
    state.record.powerup_regen=kept.powerup_regen;
    state.record.unlimited_ammo=kept.unlimited_ammo;
    state.record.control_scheme=kept.control_scheme;
    state.record.base_defense=kept.base_defense;
    state.record.bindings=kept.bindings;
    state.record.difficulty=difficulty_from(state.cursor);
    enter_gang_select(state);
}

void update_unit_select(FrontendState& state,std::uint32_t pressed) {
    const fs::GangSelectResult result=fs::update_gang_select(state.gangs,{pressed,0U,0U,0U});
    state.help_visible=state.gangs.help_visible;
    if (result==fs::GangSelectResult::back) {
        enter_default_setup_menu(state);
        return;
    }
    if (result!=fs::GangSelectResult::done) return;
    state.record.unit_type=state.gangs.gang[0];
    enter_controls(state);
}

// Rows 0..8 bind raw buttons, row 9 picks the preset, row 10 is done
// (confirm or cancel). There is no way back to unit_select (CONFIRMED: the
// retail screen only exits from the done row).
void update_preset(FrontendState& state,MenuInput input,FrontendStep& step) {
    const fs::ControlsResult result=fs::update_controls_setup(state.controls,
        fs::ControlsInput{.menu_pressed=input.pressed,.raw_pressed=input.raw_pressed});
    state.cursor=state.controls.row;
    state.help_visible=state.controls.help_visible;
    if (result!=fs::ControlsResult::done) return;
    fs::store_controls_in_record(state.controls,state.record);
    if (state.save_enabled) emit(step,CommandKind::write_campaign_record);
    emit(step,CommandKind::start_mission,static_cast<std::int32_t>(state.record.mission_id));
    go(state,Screen::in_mission);
}

void update_options(FrontendState& state,std::uint32_t pressed,FrontendStep& step) {
    if ((pressed&buttons::help)!=0U) state.help_visible=!state.help_visible;
    if (state.help_visible) return;
    const std::int32_t row_delta=vertical(pressed);
    if (row_delta!=0) emit(step,CommandKind::play_cursor_sound);
    state.cursor=wrap(state.cursor+row_delta,options_row_count);
    const std::int32_t delta=horizontal(pressed);
    if (delta!=0) emit(step,CommandKind::play_value_sound);
    CampaignRecord& record=state.record;
    bool leave=false;
    switch (state.cursor) {
    case 0:
        record.sfx_volume=clamp_volume(record.sfx_volume+delta);
        emit(step,CommandKind::apply_sfx_volume,record.sfx_volume);
        break;
    case 1:
        record.music_volume=clamp_volume(record.music_volume+delta);
        emit(step,CommandKind::apply_music_volume,record.music_volume);
        break;
    case 2: {
        // u8 arithmetic: 0xFF wraps to 2, >=3 wraps to 0.
        const auto raw=static_cast<std::uint8_t>(static_cast<std::int32_t>(record.difficulty)+delta);
        record.difficulty=difficulty_from(raw==0xFFU ? 2 : (raw>=3U ? 0 : raw));
        break;
    }
    case 3: record.powerups.raw=toggle_flag(record.powerups.raw,delta); break;
    case 4: record.powerup_regen.raw=toggle_flag(record.powerup_regen.raw,delta); break;
    case 5: record.control_scheme=toggle_flag(record.control_scheme,delta); break;
    case 6: record.unlimited_ammo.raw=toggle_flag(record.unlimited_ammo.raw,delta); break;
    default:
        if ((pressed&(buttons::right|buttons::confirm|buttons::alt_confirm))!=0U) leave=true;
        break;
    }
    if ((pressed&buttons::cancel)!=0U) leave=true;
    if (!leave) return;
    if (state.save_enabled) emit(step,CommandKind::write_campaign_record);
    enter_setup_menu(state,fs::SetupRow::options);
}

void update_pause(FrontendState& state,std::uint32_t pressed,FrontendStep& step) {
    if ((pressed&buttons::confirm)!=0U) {
        switch (state.cursor) {
        case 1:
            if (state.record.difficulty==CampaignDifficulty::easy) emit(step,CommandKind::easy_restart_hook);
            emit(step,CommandKind::restart_mission);
            break;
        case 2:
            emit(step,CommandKind::quit_to_title);
            go(state,Screen::title);
            return;
        default:
            emit(step,CommandKind::resume_mission);
            break;
        }
        go(state,Screen::in_mission);
        return;
    }
    if ((pressed&buttons::cancel)!=0U) { // TEMPORARY POLICY: original pause has no cancel
        emit(step,CommandKind::resume_mission);
        go(state,Screen::in_mission);
        return;
    }
    state.cursor=wrap(state.cursor+vertical(pressed),3);
}

bool offered(const FrontendState& state,campaign_flow::FlowResultChoice choice) noexcept {
    return state.result_choices[static_cast<std::size_t>(choice)];
}

// update_match_state state 7: 0x10 = retry (loss) or continue without save;
// 0x20 = quit (loss) or save and continue (unless mission 16); endurance win
// accepts 0x30 as continue.
void update_result(FrontendState& state,std::uint32_t pressed,FrontendStep& step) {
    using campaign_flow::FlowResultChoice;
    const bool confirm=(pressed&buttons::confirm)!=0U;
    const bool cancel=(pressed&buttons::cancel)!=0U;
    if (offered(state,FlowResultChoice::retry) || offered(state,FlowResultChoice::quit)) {
        if (confirm && offered(state,FlowResultChoice::retry)) {
            if (state.record.difficulty==CampaignDifficulty::easy) emit(step,CommandKind::easy_restart_hook);
            emit(step,CommandKind::restart_mission);
            go(state,Screen::in_mission);
        } else if (cancel && offered(state,FlowResultChoice::quit)) {
            emit(step,CommandKind::quit_to_title);
            go(state,Screen::title);
        }
        return;
    }
    if (cancel && offered(state,FlowResultChoice::save_and_continue)) {
        if (state.save_enabled) emit(step,CommandKind::write_campaign_record);
        emit(step,CommandKind::advance_mission,static_cast<std::int32_t>(FlowResultChoice::save_and_continue));
        go(state,Screen::in_mission);
        return;
    }
    if (offered(state,FlowResultChoice::continue_without_save) && (confirm || (cancel && state.result_cancel_continues))) {
        emit(step,CommandKind::advance_mission,static_cast<std::int32_t>(FlowResultChoice::continue_without_save));
        go(state,Screen::in_mission);
    }
}

void add(std::vector<DrawCommand>& out,const FrontendState& state,ItemId item,std::int32_t index,std::int32_t value=0) {
    out.push_back(DrawCommand{.item=item,.value=value,.selected=state.cursor==index,.enabled=item_enabled(state,item)});
}

} // namespace

PakDecision decide_pak_load(PakProbe probe) noexcept {
    switch (probe) {
    case PakProbe::ok_with_save: return PakDecision::load_record;
    case PakProbe::ok_no_save: return PakDecision::use_defaults;
    case PakProbe::damaged: return PakDecision::repair_offer;
    case PakProbe::full: return PakDecision::manage_notes;
    case PakProbe::no_pak:
    case PakProbe::checksum_bad:
    case PakProbe::device_error:
    default: return PakDecision::error_retry_or_continue;
    }
}

FrontendState make_frontend_state() {
    FrontendState state;
    state.record=default_campaign_record();
    state.setup_menu=frontend_setup::make_main_menu(1,false);
    state.gangs=frontend_setup::make_gang_select(1,false);
    state.gangs.gang[0]=unit_from_record(state.record,false);
    state.controls=frontend_setup::make_controls_setup(static_cast<std::int32_t>(state.record.control_scheme));
    (void)frontend_setup::load_controls_from_record(state.controls,state.record);
    return state;
}

FrontendStep apply_pak_probe(FrontendState& state,PakProbe probe,const CampaignRecord* loaded) {
    FrontendStep step;
    state.pak=decide_pak_load(probe);
    if (state.pak==PakDecision::load_record && loaded==nullptr) state.pak=PakDecision::error_retry_or_continue;
    switch (state.pak) {
    case PakDecision::load_record:
        state.record=*loaded;
        state.record_loaded=true;
        state.save_enabled=true;
        emit(step,CommandKind::load_campaign_record);
        break;
    case PakDecision::use_defaults:
        state.record=default_campaign_record();
        state.record_loaded=false;
        state.save_enabled=true; // a fresh note will be allocated on first write
        emit(step,CommandKind::use_default_record);
        break;
    default:
        state.record_loaded=false;
        state.save_enabled=false;
        break;
    }
    if (state.screen==Screen::pak_menu && (state.pak==PakDecision::load_record || state.pak==PakDecision::use_defaults))
        enter_default_setup_menu(state);
    return step;
}

FrontendStep update_frontend(FrontendState& state,MenuInput input) {
    FrontendStep step;
    const std::uint32_t pressed=input.pressed;
    switch (state.screen) {
    case Screen::title: update_title(state,pressed,step); break;
    case Screen::pak_menu: update_pak(state,pressed,step); break;
    case Screen::campaign_menu: update_campaign_menu(state,pressed,step); break;
    case Screen::difficulty: update_difficulty(state,pressed); break;
    case Screen::unit_select: update_unit_select(state,pressed); break;
    case Screen::controller_preset: update_preset(state,input,step); break;
    case Screen::options: update_options(state,pressed,step); break;
    case Screen::pause: update_pause(state,pressed,step); break;
    case Screen::result: update_result(state,pressed,step); break;
    case Screen::in_mission:
    default: break;
    }
    return step;
}

void enter_pause(FrontendState& state) {
    if (state.screen==Screen::in_mission) go(state,Screen::pause);
}

FrontendStep enter_result(FrontendState& state,ResultKind kind) {
    using campaign_flow::FlowResultChoice;
    FrontendStep step;
    state.result=kind;
    state.result_choices={};
    state.result_cancel_continues=false;
    if (kind==ResultKind::defeat) {
        state.result_choices[static_cast<std::size_t>(FlowResultChoice::retry)]=true;
        state.result_choices[static_cast<std::size_t>(FlowResultChoice::quit)]=true;
    } else {
        state.result_choices[static_cast<std::size_t>(FlowResultChoice::continue_without_save)]=true;
        state.result_choices[static_cast<std::size_t>(FlowResultChoice::save_and_continue)]=state.save_enabled;
    }
    emit(step,CommandKind::stop_unit_sounds);
    go(state,Screen::result);
    return step;
}

FrontendStep enter_result(FrontendState& state,const campaign_flow::FlowTransition& result) {
    using campaign_flow::FlowResultChoice;
    FrontendStep step;
    // update_match_state.c:342-344: a won endurance wave never opens the
    // dialog; the flow already advanced with its carried save choice.
    if (result.auto_advanced) {
        state.result_choices={};
        state.result_cancel_continues=false;
        state.result=ResultKind::victory;
        go(state,Screen::in_mission);
        return step;
    }
    state.result_choices=result.choices;
    const bool loss_menu=result.choices[static_cast<std::size_t>(FlowResultChoice::retry)]
        || result.choices[static_cast<std::size_t>(FlowResultChoice::quit)];
    state.result=loss_menu ? ResultKind::defeat : ResultKind::victory;
    const auto waves=campaign_wave_definitions();
    const bool endurance=result.wave_index<waves.size() && waves[result.wave_index].game_kind==7;
    // Defensive only: flow-emitted won endurance results are auto_advanced and
    // return above, so this is reachable only from hand-built transitions.
    state.result_cancel_continues=endurance && result.won;
    emit(step,CommandKind::stop_unit_sounds);
    go(state,Screen::result);
    return step;
}

bool item_enabled(const FrontendState& state,ItemId item) noexcept {
    switch (item) {
    case ItemId::title_attract: return false;
    case ItemId::campaign_continue: return state.record_loaded;
    case ItemId::pak_repair: return state.pak==PakDecision::repair_offer;
    case ItemId::pak_delete_note: return state.pak==PakDecision::manage_notes;
    case ItemId::result_retry: return offered(state,campaign_flow::FlowResultChoice::retry);
    case ItemId::result_quit: return offered(state,campaign_flow::FlowResultChoice::quit);
    case ItemId::result_continue: return offered(state,campaign_flow::FlowResultChoice::continue_without_save);
    case ItemId::result_save_continue: return offered(state,campaign_flow::FlowResultChoice::save_and_continue);
    default: return true;
    }
}

std::vector<DrawCommand> frontend_draw(const FrontendState& state) {
    std::vector<DrawCommand> out;
    const CampaignRecord& record=state.record;
    switch (state.screen) {
    case Screen::title:
        add(out,state,ItemId::title_start,0);
        add(out,state,ItemId::title_attract,1);
        break;
    case Screen::pak_menu:
        if (state.pak==PakDecision::repair_offer) add(out,state,ItemId::pak_repair,0);
        else if (state.pak==PakDecision::manage_notes) add(out,state,ItemId::pak_delete_note,0);
        else add(out,state,ItemId::pak_retry,0);
        add(out,state,ItemId::pak_continue_without_save,1);
        break;
    case Screen::campaign_menu:
        for (const fs::SetupRow row : fs::setup_rows) {
            const auto index=static_cast<std::int32_t>(row);
            out.push_back(DrawCommand{.item=ItemId::setup_row,.value=index,.selected=state.setup_menu.cursor==index,
                                      .enabled=fs::setup_row_enabled(state.setup_menu,row)});
        }
        break;
    case Screen::difficulty:
        add(out,state,ItemId::difficulty_easy,0);
        add(out,state,ItemId::difficulty_normal,1);
        add(out,state,ItemId::difficulty_hard,2);
        break;
    case Screen::unit_select:
        out.push_back(DrawCommand{.item=ItemId::unit_choice,.value=state.gangs.gang[0],.selected=true,.enabled=true});
        break;
    case Screen::controller_preset: {
        const std::int32_t rows=fs::binding_row_count(state.controls.preset_saved);
        for (std::int32_t row=0; row<rows; ++row)
            out.push_back(DrawCommand{.item=ItemId::binding_row,.value=row,.selected=state.controls.row==row,.enabled=true});
        out.push_back(DrawCommand{.item=ItemId::preset_choice,.value=state.controls.preset_selected,
                                  .selected=state.controls.row==fs::preset_row,.enabled=true});
        out.push_back(DrawCommand{.item=ItemId::controls_done,.value=0,.selected=state.controls.row==fs::done_row,.enabled=true});
        break;
    }
    case Screen::options:
        add(out,state,ItemId::option_sfx_volume,0,record.sfx_volume);
        add(out,state,ItemId::option_music_volume,1,record.music_volume);
        add(out,state,ItemId::option_difficulty,2,difficulty_index(record.difficulty));
        add(out,state,ItemId::option_powerups,3,record.powerups.raw);
        add(out,state,ItemId::option_powerup_regen,4,record.powerup_regen.raw);
        add(out,state,ItemId::option_control_scheme,5,record.control_scheme);
        add(out,state,ItemId::option_unlimited_ammo,6,record.unlimited_ammo.raw);
        add(out,state,ItemId::option_exit,7);
        break;
    case Screen::pause:
        add(out,state,ItemId::pause_continue,0);
        add(out,state,ItemId::pause_restart,1);
        add(out,state,ItemId::pause_quit,2);
        break;
    case Screen::result:
        // Only offered choices are drawn; each is mapped to a button, not a cursor.
        for (const ItemId item : {ItemId::result_retry,ItemId::result_quit,ItemId::result_continue,ItemId::result_save_continue}) {
            if (!item_enabled(state,item)) continue;
            const bool continues=item==ItemId::result_continue || item==ItemId::result_save_continue;
            out.push_back(DrawCommand{.item=item,.value=continues ? static_cast<std::int32_t>(record.mission_id) : 0,.selected=false,.enabled=true});
        }
        break;
    case Screen::in_mission:
    default: break;
    }
    if (state.help_visible) out.push_back(DrawCommand{.item=ItemId::help_overlay,.value=0,.selected=false,.enabled=true});
    return out;
}

std::vector<frontend_setup::SetupDrawCommand> setup_draw(const FrontendState& state) {
    switch (state.screen) {
    case Screen::campaign_menu: return fs::draw_main_menu(state.setup_menu);
    case Screen::unit_select: return fs::draw_gang_select(state.gangs);
    case Screen::controller_preset: return fs::draw_controls_setup(state.controls);
    default: return {};
    }
}

void serialize(StateWriter& writer,const FrontendState& state) {
    writer.u8(static_cast<std::uint8_t>(state.screen));
    writer.s32(state.cursor);
    writer.boolean(state.help_visible);
    writer.boolean(state.record_loaded);
    writer.boolean(state.save_enabled);
    writer.boolean(state.locked_unit_available);
    writer.u8(static_cast<std::uint8_t>(state.pak));
    frontend_setup::serialize(writer,state.setup_menu);
    frontend_setup::serialize(writer,state.gangs);
    frontend_setup::serialize(writer,state.controls);
    for (const std::uint32_t draw : state.computer_gang_draws) writer.u32(draw);
    writer.u8(static_cast<std::uint8_t>(state.result));
    for (const bool choice : state.result_choices) writer.boolean(choice);
    writer.boolean(state.result_cancel_continues);
    const ControllerPakRecord bytes=serialize_campaign_record(state.record);
    for (const std::uint8_t byte : bytes.bytes) writer.u8(byte);
}

} // namespace battletanx::frontend
