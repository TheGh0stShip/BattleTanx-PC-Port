#include "battletanx/campaign_flow.hpp"

namespace battletanx::campaign_flow {
namespace {

constexpr std::size_t choice_slot(FlowResultChoice choice) noexcept { return static_cast<std::size_t>(choice); }

// reset_session_state_defaults (func_800D05DC) stores unit type 9.
constexpr std::int32_t new_campaign_unit_type=9;
constexpr std::int32_t first_mission_id=1;

FlowTransition make(FlowTransitionKind kind,std::uint8_t wave_index) {
    FlowTransition t;
    t.kind=kind;
    t.wave_index=wave_index;
    if (wave_index<campaign_wave_count)
        t.mission_id=campaign_wave_definitions()[wave_index].mission_id;
    return t;
}

FlowTransition rejected(FlowRejectReason reason,std::uint8_t wave_index) {
    FlowTransition t=make(FlowTransitionKind::rejected,wave_index);
    t.reject=reason;
    return t;
}

} // namespace

void FlowOptions::serialize(StateWriter& out) const {
    out.s32(unit_type);
    out.u8(static_cast<std::uint8_t>(difficulty));
    out.u8(powerups);
    out.u8(powerup_regen);
    out.u8(control_scheme);
    out.u8(base_defense);
    out.u8(unlimited_ammo);
    out.s32(sfx_volume);
    out.s32(music_volume);
    for (const std::uint32_t word : bindings) out.u32(word);
}

FlowOptions options_from_record(const CampaignRecord& record) noexcept {
    FlowOptions o;
    o.unit_type=record.unit_type;
    o.difficulty=record.difficulty;
    o.powerups=record.powerups.raw;
    o.powerup_regen=record.powerup_regen.raw;
    o.control_scheme=record.control_scheme;
    o.base_defense=record.base_defense.raw;
    o.unlimited_ammo=record.unlimited_ammo.raw;
    o.sfx_volume=record.sfx_volume;
    o.music_volume=record.music_volume;
    o.bindings=record.bindings;
    return o;
}

const CampaignWaveDefinition* CampaignFlow::current_wave() const noexcept {
    if (wave_index_>=campaign_wave_count) return nullptr;
    return &campaign_wave_definitions()[wave_index_];
}

std::vector<FlowTransition> CampaignFlow::apply(const FlowCommand& command) {
    std::vector<FlowTransition> out;
    switch (command.kind) {
    case FlowCommandKind::start_new:
        return start(command.record,true);
    case FlowCommandKind::resume:
        return start(command.record,false);
    case FlowCommandKind::set_options:
        options_=command.options;
        return out;
    case FlowCommandKind::intro_finished: {
        if (phase_!=FlowPhase::chapter_intro) break;
        const CampaignWaveDefinition* wave=current_wave();
        // Only sequence 15 (wave 0) checks the cut-scene result.
        if (!command.accepted && wave && wave->chapter_sequence && *wave->chapter_sequence==15U) {
            phase_=FlowPhase::menu;
            FlowTransition t=make(FlowTransitionKind::return_to_menu,wave_index_);
            t.menu=FlowMenuReason::intro_cancelled;
            out.push_back(t);
            return out;
        }
        phase_=FlowPhase::briefing;
        FlowTransition t=make(FlowTransitionKind::briefing,wave_index_);
        if (wave) t.briefing_image=wave->briefing_image;
        out.push_back(t);
        return out;
    }
    case FlowCommandKind::briefing_finished:
        if (phase_!=FlowPhase::briefing) break;
        launch(out,FlowTransitionKind::battle_launch);
        return out;
    case FlowCommandKind::battle_finished: {
        if (phase_!=FlowPhase::battle) break;
        if (command.outcome.restarted) {
            retry(out);
            return out;
        }
        if (command.outcome.aborted) {
            phase_=FlowPhase::menu;
            FlowTransition t=make(FlowTransitionKind::return_to_menu,wave_index_);
            t.menu=FlowMenuReason::battle_aborted;
            out.push_back(t);
            return out;
        }
        last_outcome_=command.outcome;
        if (command.outcome.result_code==flow_result_code_from_won)
            last_result_code_=command.outcome.won ? flow_result_code_won : flow_result_code_selectable;
        else
            last_result_code_=command.outcome.result_code;
        last_won_=last_result_code_==flow_result_code_won;
        phase_=FlowPhase::result;
        FlowTransition t=make(FlowTransitionKind::result,wave_index_);
        t.won=last_won_;
        const CampaignWaveDefinition* wave=current_wave();
        if (wave && result_auto_advances(*wave,last_result_code_)) {
            // update_match_state.c:342-344: state 6 goes straight to `reset`;
            // D_8012579B keeps the previous dialog's value.
            t.auto_advanced=true;
            t.save_choice=last_save_choice_;
            out.push_back(t);
            advance(out,last_save_choice_);
            return out;
        }
        t.choices=offered_choices();
        out.push_back(t);
        return out;
    }
    case FlowCommandKind::result_choice: {
        if (phase_!=FlowPhase::result) break;
        if (!offered_choices()[choice_slot(command.choice)]) {
            out.push_back(rejected(FlowRejectReason::choice_not_offered,wave_index_));
            return out;
        }
        // Every state-7 button writes D_8012579B: only 0x20 on a win (not
        // mission 16) or on an endurance end sets it (update_match_state.c:383-426).
        last_save_choice_=command.choice==FlowResultChoice::save_and_continue;
        switch (command.choice) {
        case FlowResultChoice::retry:
            retry(out);
            return out;
        case FlowResultChoice::quit: {
            phase_=FlowPhase::menu;
            FlowTransition t=make(FlowTransitionKind::return_to_menu,wave_index_);
            t.menu=FlowMenuReason::wave_lost;
            out.push_back(t);
            return out;
        }
        case FlowResultChoice::save_and_continue:
            advance(out,true);
            return out;
        case FlowResultChoice::continue_without_save:
            advance(out,false);
            return out;
        }
        break;
    }
    case FlowCommandKind::save_finished:
        // run_campaign_waves ignores the write result.
        if (phase_!=FlowPhase::save_pending) break;
        finish_advance(out);
        return out;
    }
    out.push_back(rejected(FlowRejectReason::wrong_phase,wave_index_));
    return out;
}

std::vector<FlowTransition> CampaignFlow::start(const CampaignRecord& record,bool fresh) {
    std::vector<FlowTransition> out;
    if (phase_!=FlowPhase::idle && phase_!=FlowPhase::completed && phase_!=FlowPhase::menu) {
        out.push_back(rejected(FlowRejectReason::already_started,wave_index_));
        return out;
    }
    // Resolve the wave first so an invalid resume leaves the live state intact.
    const CampaignWaveDefinition* wave=fresh ? find_campaign_wave(first_mission_id) : campaign_wave_for_record(record);
    if (!wave) {
        phase_=FlowPhase::menu;
        wave_index_=0U;
        FlowTransition t;
        t.kind=FlowTransitionKind::return_to_menu;
        t.mission_id=static_cast<std::int32_t>(record.mission_id);
        t.reject=FlowRejectReason::invalid_mission_id;
        t.menu=FlowMenuReason::invalid_resume;
        out.push_back(t);
        return out;
    }
    last_won_=false;
    last_result_code_=flow_result_code_selectable;
    last_outcome_=FlowBattleOutcome{};
    // TEMPORARY POLICY: D_8012579B is zero at boot and never reset, so retail
    // carries it across campaigns in one power-on; each start clears it here.
    last_save_choice_=false;
    options_=options_from_record(record);
    if (fresh) {
        // reset_session_state_defaults: mission 1, carry 0, unit 9, bonus 0.
        // Difficulty and control scheme come from the record the host built
        // from its option screen (TEMPORARY POLICY, see docs).
        options_.unit_type=new_campaign_unit_type;
        carry_=0;
        bonus_=0;
    } else {
        carry_=record.carry;
        bonus_=record.bonus;
    }
    wave_index_=wave->wave_index;
    enter_wave(out);
    return out;
}

void CampaignFlow::enter_wave(std::vector<FlowTransition>& out) {
    const CampaignWaveDefinition* wave=current_wave();
    if (!wave) return;
    // play_campaign_wave: reset_frame_event_counter once per wave entry.
    retries_=0U;
    easy_restart_count_=0U;
    if (wave->chapter_sequence) {
        phase_=FlowPhase::chapter_intro;
        FlowTransition t=make(FlowTransitionKind::intro,wave_index_);
        t.sequence_id=*wave->chapter_sequence;
        out.push_back(t);
        return;
    }
    phase_=FlowPhase::briefing;
    FlowTransition t=make(FlowTransitionKind::briefing,wave_index_);
    t.briefing_image=wave->briefing_image;
    out.push_back(t);
}

void CampaignFlow::launch(std::vector<FlowTransition>& out,FlowTransitionKind kind) {
    const CampaignWaveDefinition* wave=current_wave();
    if (!wave) return;
    phase_=FlowPhase::battle;
    FlowTransition t=make(kind,wave_index_);
    // wave.value217 = D_80134E64 (difficulty); wave.value0C = session unit.
    t.launch=make_campaign_wave_launch(*wave,carry_,bonus_,
        static_cast<std::uint8_t>(options_.difficulty),options_.unit_type);
    out.push_back(t);
}

void CampaignFlow::retry(std::vector<FlowTransition>& out) {
    ++retries_;
    if (options_.difficulty==CampaignDifficulty::easy) ++easy_restart_count_;
    launch(out,FlowTransitionKind::retry);
}

std::array<bool,flow_result_choice_count> offered_result_choices(const CampaignWaveDefinition& wave,
                                                                  std::int16_t result_code) noexcept {
    std::array<bool,flow_result_choice_count> c{};
    // Any other entity result: update_match_state accepts no button.
    if (result_code!=flow_result_code_won && result_code!=flow_result_code_selectable) return c;
    const bool won=result_code==flow_result_code_won;
    if (wave.game_kind==7) {
        // Only the non-won end reaches the dialog; a won endurance wave
        // auto-advances (result_auto_advances).
        if (won) return c;
        c[choice_slot(FlowResultChoice::continue_without_save)]=true;
        c[choice_slot(FlowResultChoice::save_and_continue)]=true;
    } else if (won) {
        c[choice_slot(FlowResultChoice::continue_without_save)]=true;
        // false for mission 16: update_match_state.c:397-405 (value04 != 0x10).
        if (wave.save_prompt_after_win) c[choice_slot(FlowResultChoice::save_and_continue)]=true;
    } else {
        c[choice_slot(FlowResultChoice::retry)]=true;
        c[choice_slot(FlowResultChoice::quit)]=true;
    }
    return c;
}

bool result_auto_advances(const CampaignWaveDefinition& wave,std::int16_t result_code) noexcept {
    return wave.game_kind==7 && result_code==flow_result_code_won;
}

std::array<bool,flow_result_choice_count> CampaignFlow::offered_choices() const noexcept {
    const CampaignWaveDefinition* wave=current_wave();
    if (!wave) return {};
    return offered_result_choices(*wave,last_result_code_);
}

void CampaignFlow::advance(std::vector<FlowTransition>& out,bool save) {
    const CampaignWaveDefinition* wave=current_wave();
    if (!wave) return;
    const std::uint8_t cleared=wave_index_;
    carry_=last_outcome_.player_score;
    bonus_=last_outcome_.player_bonus;
    wave_index_=static_cast<std::uint8_t>(wave_index_+1U);
    FlowTransition inter=make(FlowTransitionKind::intermission,wave_index_);
    inter.carry=carry_;
    inter.bonus=bonus_;
    out.push_back(inter);
    if (!save) {
        finish_advance(out);
        return;
    }
    FlowSaveRequest request;
    request.cleared_wave_index=cleared;
    request.clear.mission_id=wave->saved_mission_id_after_clear;
    request.clear.carry=carry_;
    request.clear.unit_type=options_.unit_type;
    request.clear.bonus=bonus_;
    request.clear.control_scheme=options_.control_scheme;
    request.clear.difficulty=options_.difficulty;
    request.clear.powerups=options_.powerups;
    request.clear.powerup_regen=options_.powerup_regen;
    request.clear.base_defense=options_.base_defense;
    request.clear.unlimited_ammo=options_.unlimited_ammo;
    request.clear.sfx_volume=options_.sfx_volume;
    request.clear.music_volume=options_.music_volume;
    request.clear.bindings=options_.bindings;
    FlowTransition t=make(FlowTransitionKind::save_request,wave_index_);
    t.mission_id=static_cast<std::int32_t>(request.clear.mission_id);
    t.save=request;
    out.push_back(t);
    phase_=FlowPhase::save_pending;
}

void CampaignFlow::finish_advance(std::vector<FlowTransition>& out) {
    if (wave_index_>=campaign_wave_count) {
        phase_=FlowPhase::completed;
        FlowTransition t;
        t.kind=FlowTransitionKind::completion;
        t.wave_index=wave_index_;
        t.sequence_id=completion_frontend_state;
        t.carry=carry_;
        t.bonus=bonus_;
        out.push_back(t);
        return;
    }
    out.push_back(make(FlowTransitionKind::next_wave,wave_index_));
    enter_wave(out);
}

void CampaignFlow::serialize(StateWriter& out) const {
    out.u32(flow_serial_version);
    out.u8(static_cast<std::uint8_t>(phase_));
    out.u8(wave_index_);
    out.s32(carry_);
    out.s16(bonus_);
    options_.serialize(out);
    out.boolean(last_won_);
    out.s16(last_result_code_);
    out.boolean(last_outcome_.won);
    out.boolean(last_outcome_.aborted);
    out.s32(last_outcome_.player_score);
    out.s16(last_outcome_.player_bonus);
    out.boolean(last_outcome_.restarted);
    out.s16(last_outcome_.result_code);
    out.u32(retries_);
    out.u32(easy_restart_count_);
    out.boolean(last_save_choice_);
}

std::array<std::uint8_t,32> CampaignFlow::checksum() const {
    StateWriter writer;
    serialize(writer);
    return writer.digest();
}

} // namespace battletanx::campaign_flow
