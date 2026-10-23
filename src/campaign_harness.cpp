#include "battletanx/campaign_harness.hpp"

#include "battletanx/campaign_save_transaction.hpp"

namespace battletanx::campaign_harness {
namespace {

using campaign_flow::FlowResultChoice;
using music_selection::MusicTransition;

constexpr std::size_t slot(FlowResultChoice choice) noexcept { return static_cast<std::size_t>(choice); }

Error fail(ErrorCode code,std::uint8_t wave) noexcept { return Error{.code=code,.wave_index=wave}; }

const CampaignWaveDefinition* wave_at(std::uint8_t index) noexcept {
    const auto waves=campaign_wave_definitions();
    return index<waves.size() ? &waves[index] : nullptr;
}

std::int16_t result_code(bool won) noexcept {
    return won ? campaign_flow::flow_result_code_won : campaign_flow::flow_result_code_selectable;
}

void write_record(StateWriter& out,const CampaignRecord& record) {
    out.u8(record.mission_id);
    out.s32(record.carry);
    out.s32(record.unit_type);
    out.s16(record.bonus);
    out.u8(static_cast<std::uint8_t>(record.difficulty));
}

} // namespace

DecodedInputs complete_synthetic_inputs() noexcept {
    DecodedInputs inputs;
    const auto waves=campaign_wave_definitions();
    for (std::size_t i=0U; i<waves.size(); ++i) {
        inputs.waves[i]=DecodedWaveInputs{.world_stream=true,.navigation=waves[i].navigation_available,
                                          .briefing_image=true,.spawn_table=true};
    }
    return inputs;
}

void BattleRequest::serialize(StateWriter& out) const {
    out.u32(sequence);
    launch.serialize(out);
    out.u8(static_cast<std::uint8_t>(objective));
    out.s32(par_time_seconds);
}

FlowDecision default_flow_decision(const CampaignWaveDefinition& wave,FlowResultChoice choice) noexcept {
    if (choice==FlowResultChoice::quit) return FlowDecision::quit;
    if (choice==FlowResultChoice::retry) return FlowDecision::retry;
    return next_campaign_wave(wave)==nullptr ? FlowDecision::campaign_complete : FlowDecision::advance;
}

Coordinator::Coordinator(DecodedInputs inputs,Adapters adapters) noexcept
    : inputs_(inputs),adapters_(adapters),record_(default_campaign_record()) {}

Error Coordinator::check_wave_inputs(std::uint8_t wave_index) const noexcept {
    const CampaignWaveDefinition* wave=wave_at(wave_index);
    if (wave==nullptr) return fail(ErrorCode::wave_out_of_catalog,wave_index);
    const DecodedWaveInputs& in=inputs_.waves[wave_index];
    if (!in.world_stream) return fail(ErrorCode::missing_world_stream,wave_index);
    if (wave->navigation_available && !in.navigation) return fail(ErrorCode::missing_navigation,wave_index);
    if (wave->briefing_image>=0 && !in.briefing_image) return fail(ErrorCode::missing_briefing_image,wave_index);
    if (!in.spawn_table) return fail(ErrorCode::missing_spawn_table,wave_index);
    if (wave->objective==CampaignObjectiveKind::unresolved) return fail(ErrorCode::objective_unresolved,wave_index);
    return Error{};
}

void Coordinator::set_phase(Phase phase) {
    phase_=phase;
    if (adapters_.frontend!=nullptr) adapters_.frontend->present(phase_,wave_index_,last_won_);
}

void Coordinator::music(MusicTransition transition) {
    if (adapters_.music==nullptr) return;
    const CampaignWaveDefinition* wave=wave_at(wave_index_);
    adapters_.music->select(transition,wave!=nullptr ? wave->mission_id : 0);
}

Error Coordinator::enter_briefing(std::uint8_t wave_index) {
    const Error error=check_wave_inputs(wave_index);
    if (!error.ok()) return error;
    wave_index_=wave_index;
    request_.reset();
    events_.clear();
    events_overflowed_=false;
    countdown_done_=false;
    music(MusicTransition::briefing);
    set_phase(Phase::briefing);
    return Error{};
}

Error Coordinator::start_new(const Settings& settings) {
    if (phase_!=Phase::menu) return fail(ErrorCode::wrong_phase,wave_index_);
    const Settings previous=settings_;
    const CampaignRecord previous_record=record_;
    settings_=settings;
    retrying_=false;
    last_won_=false;
    last_save_choice_=false;
    record_=default_campaign_record();
    record_.difficulty=settings.difficulty;
    record_.unit_type=settings.selected_unit_kind;
    const Error error=enter_briefing(0U);
    if (!error.ok()) { settings_=previous; record_=previous_record; }
    return error;
}

Error Coordinator::resume(const CampaignRecord& record,std::int32_t selected_unit_kind) {
    if (phase_!=Phase::menu) return fail(ErrorCode::wrong_phase,wave_index_);
    const CampaignResume target=campaign_resume_for_record(record);
    if (target.status==CampaignResumeStatus::unknown_mission || (target.status==CampaignResumeStatus::resume_wave && target.wave==nullptr))
        return fail(ErrorCode::resume_mission_unknown,record.mission_id);
    if (target.status==CampaignResumeStatus::resume_wave) {
        const Error error=check_wave_inputs(target.wave->wave_index);
        if (!error.ok()) return error;
    }
    settings_=Settings{.difficulty=record.difficulty,.selected_unit_kind=selected_unit_kind};
    record_=record;
    saved_=record;
    retrying_=false;
    last_won_=false;
    last_save_choice_=false;
    if (target.status==CampaignResumeStatus::campaign_complete) {
        // Mission id 0 names no wave. Retail never writes it (no save after
        // the final wave); accepted as completed for robustness.
        wave_index_=static_cast<std::uint8_t>(campaign_wave_count);
        set_phase(Phase::completed);
        return Error{};
    }
    return enter_briefing(target.wave->wave_index);
}

Error Coordinator::confirm_briefing() {
    if (phase_!=Phase::briefing) return fail(ErrorCode::wrong_phase,wave_index_);
    const CampaignWaveDefinition* wave=wave_at(wave_index_);
    // TEMPORARY POLICY: skill group = difficulty index (catalog STRONG INFERENCE).
    const auto group=static_cast<std::uint8_t>(settings_.difficulty);
    request_=BattleRequest{
        .sequence=++request_sequence_,
        .launch=make_campaign_wave_launch(*wave,record_.carry,record_.bonus,group,settings_.selected_unit_kind),
        .objective=wave->objective,
        .par_time_seconds=wave->par_time_seconds};
    events_.clear();
    events_overflowed_=false;
    countdown_done_=false;
    // play_campaign_wave: the restart path and the normal load both stop music.
    music(retrying_ ? MusicTransition::wave_restarted : MusicTransition::wave_loading);
    retrying_=false;
    set_phase(Phase::awaiting_battle);
    return Error{};
}

Error Coordinator::countdown_finished() {
    if (phase_!=Phase::awaiting_battle || countdown_done_) return fail(ErrorCode::wrong_phase,wave_index_);
    countdown_done_=true;
    music(MusicTransition::countdown_finished);
    return Error{};
}

Error Coordinator::restart_battle() {
    if (phase_!=Phase::awaiting_battle) return fail(ErrorCode::wrong_phase,wave_index_);
    // play_campaign_wave `goto restart`: same wave, no briefing, no result.
    retrying_=true;
    phase_=Phase::briefing;
    return confirm_briefing();
}

Error Coordinator::post_objective_event(ObjectiveEventKind kind) {
    if (phase_!=Phase::awaiting_battle) return fail(ErrorCode::wrong_phase,wave_index_);
    if (events_.size()>=max_objective_events) { events_overflowed_=true; return Error{}; }
    events_.push_back(kind);
    return Error{};
}

bool Coordinator::resolve_won(const CampaignWaveDefinition& wave,OutcomeKind kind) const noexcept {
    if (kind==OutcomeKind::won) return true;
    if (kind==OutcomeKind::lost) return false;
    // First decisive event wins; no decisive event counts as a loss
    // (TEMPORARY POLICY: retail ends the match only on a decisive state).
    for (const ObjectiveEventKind event : events_) {
        if (event==ObjectiveEventKind::player_squad_lost) return false;
        switch (wave.objective) {
        case CampaignObjectiveKind::last_team_standing:
        case CampaignObjectiveKind::endurance:
            if (event==ObjectiveEventKind::teams_reduced) return true;
            break;
        case CampaignObjectiveKind::goal_slot:
            if (event==ObjectiveEventKind::goal_slot_reached) return true;
            break;
        case CampaignObjectiveKind::player_win_flag:
            if (event==ObjectiveEventKind::player_win_flag) return true;
            break;
        case CampaignObjectiveKind::unresolved:
            break;
        }
    }
    return false;
}

Error Coordinator::submit_outcome(const BattleOutcome& outcome) {
    if (phase_!=Phase::awaiting_battle) return fail(ErrorCode::wrong_phase,wave_index_);
    const CampaignWaveDefinition* wave=wave_at(wave_index_);
    last_won_=resolve_won(*wave,outcome.kind);
    if (last_won_) {
        ++waves_cleared_;
        // Mirrors the fields write_controller_pak_progress stores.
        record_.mission_id=wave->saved_mission_id_after_clear;
        record_.carry=outcome.score;
        record_.bonus=outcome.bonus;
        record_.unit_type=settings_.selected_unit_kind;
        record_.difficulty=settings_.difficulty;
    } else if (wave->advances_on_failure) {
        record_.mission_id=wave->saved_mission_id_after_clear;
    }
    request_.reset();
    music(MusicTransition::results);
    if (last_won_ && campaign_flow::result_auto_advances(*wave,result_code(true))) {
        // update_match_state.c:342-344: a won kind-7 wave skips the dialog and
        // D_8012579B keeps the previous dialog's value.
        phase_=Phase::result;
        const FlowResultChoice carried=last_save_choice_ ? FlowResultChoice::save_and_continue
                                                         : FlowResultChoice::continue_without_save;
        return decide_and_continue(*wave,carried);
    }
    set_phase(Phase::result);
    return Error{};
}

ResultChoices Coordinator::offered_choices() const noexcept {
    const CampaignWaveDefinition* wave=wave_at(wave_index_);
    if (phase_!=Phase::result || wave==nullptr) return ResultChoices{};
    return campaign_flow::offered_result_choices(*wave,result_code(last_won_));
}

void Coordinator::advance_after(const CampaignWaveDefinition& wave,FlowDecision decision) {
    if (decision==FlowDecision::quit) { quit_to_menu(); return; }
    if (decision==FlowDecision::campaign_complete) { set_phase(Phase::completed); return; }
    if (decision==FlowDecision::retry) {
        retrying_=true;
        if (!enter_briefing(wave_index_).ok()) { retrying_=false; set_phase(Phase::menu); }
        return;
    }
    const CampaignWaveDefinition* next=next_campaign_wave(wave);
    if (next==nullptr) { set_phase(Phase::completed); return; }
    // Missing inputs on the next wave leave the harness in the menu.
    if (!enter_briefing(next->wave_index).ok()) set_phase(Phase::menu);
}

Error Coordinator::acknowledge_result(FlowResultChoice choice) {
    if (phase_!=Phase::result) return fail(ErrorCode::wrong_phase,wave_index_);
    if (slot(choice)>=campaign_flow::flow_result_choice_count || !offered_choices()[slot(choice)])
        return fail(ErrorCode::choice_not_offered,wave_index_);
    // Every state-7 button writes D_8012579B (update_match_state.c:383-426).
    last_save_choice_=choice==FlowResultChoice::save_and_continue;
    return decide_and_continue(*wave_at(wave_index_),choice);
}

Error Coordinator::decide_and_continue(const CampaignWaveDefinition& wave,FlowResultChoice choice) {
    pending_decision_=adapters_.flow!=nullptr ? adapters_.flow->decide(wave,last_won_,choice)
                                               : default_flow_decision(wave,choice);
    const bool progresses=pending_decision_==FlowDecision::advance || pending_decision_==FlowDecision::campaign_complete;
    // Never a save after the final wave (index 16): X7 verdict,
    // campaign_save::max_save_next_wave_index.
    const bool saveable=wave_index_<campaign_save::max_save_next_wave_index;
    if (progresses && saveable && choice==FlowResultChoice::save_and_continue) return begin_save();
    advance_after(wave,pending_decision_);
    return Error{};
}

Error Coordinator::save_status(SaveStatus status) {
    if (status==SaveStatus::pending) { set_phase(Phase::save_pending); return Error{}; }
    if (status!=SaveStatus::committed) {
        set_phase(Phase::save_prompt);
        return fail(ErrorCode::save_rejected,wave_index_);
    }
    saved_=record_;
    ++saves_committed_;
    advance_after(*wave_at(wave_index_),pending_decision_);
    return Error{};
}

Error Coordinator::begin_save() {
    if (adapters_.save==nullptr) return save_status(SaveStatus::committed);
    // Only reached with wave_index_ < 16, so +1 <= max_save_next_wave_index.
    const auto next_index=static_cast<std::uint8_t>(wave_index_+1U);
    return save_status(adapters_.save->commit(record_,next_index));
}

Error Coordinator::poll_save() {
    if (phase_!=Phase::save_pending) return fail(ErrorCode::wrong_phase,wave_index_);
    if (adapters_.save==nullptr) return save_status(SaveStatus::committed);
    const SaveStatus status=adapters_.save->poll();
    if (status==SaveStatus::pending) return Error{};
    return save_status(status);
}

Error Coordinator::answer_save(bool save) {
    if (phase_!=Phase::save_prompt) return fail(ErrorCode::wrong_phase,wave_index_);
    if (save) return begin_save();
    advance_after(*wave_at(wave_index_),pending_decision_);
    return Error{};
}

void Coordinator::quit_to_menu() noexcept {
    request_.reset();
    events_.clear();
    events_overflowed_=false;
    retrying_=false;
    countdown_done_=false;
    phase_=Phase::menu;
    if (adapters_.frontend!=nullptr) adapters_.frontend->present(phase_,wave_index_,last_won_);
}

void Coordinator::serialize(StateWriter& out) const {
    out.u32(harness_serial_version);
    out.u8(static_cast<std::uint8_t>(phase_));
    out.u8(wave_index_);
    out.u8(static_cast<std::uint8_t>(settings_.difficulty));
    out.s32(settings_.selected_unit_kind);
    write_record(out,record_);
    out.boolean(saved_.has_value());
    if (saved_) write_record(out,*saved_);
    out.boolean(request_.has_value());
    if (request_) request_->serialize(out);
    out.u32(request_sequence_);
    out.u32(static_cast<std::uint32_t>(events_.size()));
    for (const ObjectiveEventKind event : events_) out.u8(static_cast<std::uint8_t>(event));
    out.boolean(last_won_);
    out.u8(static_cast<std::uint8_t>(pending_decision_));
    out.u32(waves_cleared_);
    out.u32(saves_committed_);
    out.boolean(retrying_);
    out.boolean(countdown_done_);
    out.boolean(events_overflowed_);
    out.boolean(last_save_choice_);
}

std::array<std::uint8_t,32> Coordinator::checksum() const {
    StateWriter out;
    serialize(out);
    return out.digest();
}

} // namespace battletanx::campaign_harness
