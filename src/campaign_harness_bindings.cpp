#include "battletanx/campaign_harness_bindings.hpp"

namespace battletanx::campaign_harness_bindings {
namespace {

using campaign_harness::Coordinator;
using campaign_harness::Error;
using campaign_harness::ErrorCode;
using campaign_harness::FlowDecision;
using campaign_harness::Phase;
using campaign_harness::SaveStatus;
using campaign_flow::FlowCommand;
using campaign_flow::FlowCommandKind;
using campaign_flow::FlowPhase;
using campaign_flow::FlowResultChoice;
using campaign_flow::FlowTransitionKind;

constexpr std::size_t slot(FlowResultChoice choice) noexcept { return static_cast<std::size_t>(choice); }

const CampaignWaveDefinition* wave_at(std::uint8_t index) noexcept {
    const auto waves=campaign_wave_definitions();
    return index<waves.size() ? &waves[index] : nullptr;
}

bool has_command(const std::vector<frontend::TransitionCommand>& commands,frontend::CommandKind kind) noexcept {
    for (const frontend::TransitionCommand& command : commands) if (command.kind==kind) return true;
    return false;
}

const frontend::TransitionCommand* find_command(const std::vector<frontend::TransitionCommand>& commands,
                                                frontend::CommandKind kind) noexcept {
    for (const frontend::TransitionCommand& command : commands) if (command.kind==kind) return &command;
    return nullptr;
}

} // namespace

// --- flow --------------------------------------------------------------------

FlowDecision FlowBinding::decide(const CampaignWaveDefinition& wave,bool won,FlowResultChoice choice) const {
    const FlowDecision fallback=campaign_harness::default_flow_decision(wave,choice);
    if (wave.mission_id<0 || wave.mission_id>0xFF) return fallback;
    CampaignRecord record=default_campaign_record();
    record.mission_id=static_cast<std::uint8_t>(wave.mission_id);
    campaign_flow::CampaignFlow flow;
    static_cast<void>(flow.apply(FlowCommand{.kind=FlowCommandKind::resume,.record=record,.accepted=true,.outcome={},.choice=FlowResultChoice::continue_without_save,.options={}}));
    if (flow.phase()==FlowPhase::chapter_intro)
        static_cast<void>(flow.apply(FlowCommand{.kind=FlowCommandKind::intro_finished,.record={},.accepted=true,.outcome={},.choice=FlowResultChoice::continue_without_save,.options={}}));
    // A duplicate mission id can resolve to another wave; do not guess.
    if (flow.phase()!=FlowPhase::briefing || flow.wave_index()!=wave.wave_index) return fallback;
    static_cast<void>(flow.apply(FlowCommand{.kind=FlowCommandKind::briefing_finished,.record={},.accepted=true,.outcome={},.choice=FlowResultChoice::continue_without_save,.options={}}));
    auto next=flow.apply(FlowCommand{.kind=FlowCommandKind::battle_finished,.record={},.accepted=true,
                                     .outcome=campaign_flow::FlowBattleOutcome{.won=won,.aborted=false,.player_score=0,.player_bonus=0,.restarted=false,.result_code=campaign_flow::flow_result_code_from_won},.choice=FlowResultChoice::continue_without_save,.options={}});
    const campaign_flow::FlowTransition* shown=nullptr;
    for (const campaign_flow::FlowTransition& t : next) if (t.kind==FlowTransitionKind::result) shown=&t;
    if (shown==nullptr) return fallback;
    // A won endurance wave auto-advances inside battle_finished; the scratch
    // flow's carried save choice is false, so `next` already holds the
    // next_wave/completion transition. Otherwise apply the player's choice.
    if (!shown->auto_advanced) {
        if (!shown->choices[slot(choice)]) return fallback;
        next=flow.apply(FlowCommand{.kind=FlowCommandKind::result_choice,.record={},.accepted=true,.outcome={},.choice=choice,.options={}});
    }
    for (std::uint32_t round=0U; round<2U; ++round) {
        bool saving=false;
        for (const campaign_flow::FlowTransition& t : next) {
            if (t.kind==FlowTransitionKind::retry) return FlowDecision::retry;
            if (t.kind==FlowTransitionKind::return_to_menu) return FlowDecision::quit;
            if (t.kind==FlowTransitionKind::completion) return FlowDecision::campaign_complete;
            if (t.kind==FlowTransitionKind::next_wave) return FlowDecision::advance;
            if (t.kind==FlowTransitionKind::save_request) saving=true;
        }
        if (!saving) break;
        next=flow.apply(FlowCommand{.kind=FlowCommandKind::save_finished,.record={},.accepted=true,.outcome={},.choice=FlowResultChoice::continue_without_save,.options={}});
    }
    return fallback;
}

// --- save --------------------------------------------------------------------

CampaignWaveClear clear_from_record(const CampaignRecord& record) noexcept {
    return CampaignWaveClear{
        .mission_id=record.mission_id,
        .carry=record.carry,
        .unit_type=record.unit_type,
        .bonus=record.bonus,
        .control_scheme=record.control_scheme,
        .difficulty=record.difficulty,
        .powerups=record.powerups.raw,
        .powerup_regen=record.powerup_regen.raw,
        .base_defense=record.base_defense.raw,
        .unlimited_ammo=record.unlimited_ammo.raw,
        .sfx_volume=record.sfx_volume,
        .music_volume=record.music_volume,
        .bindings=record.bindings};
}

bool same_clear(const CampaignWaveClear& a,const CampaignWaveClear& b) noexcept {
    return a.mission_id==b.mission_id && a.carry==b.carry && a.unit_type==b.unit_type && a.bonus==b.bonus &&
           a.control_scheme==b.control_scheme && a.difficulty==b.difficulty && a.powerups==b.powerups &&
           a.powerup_regen==b.powerup_regen && a.base_defense==b.base_defense &&
           a.unlimited_ammo==b.unlimited_ammo && a.sfx_volume==b.sfx_volume &&
           a.music_volume==b.music_volume && a.bindings==b.bindings;
}

SaveBinding::SaveBinding(const CampaignRecord& authoritative,PakWriteSink* sink)
    : transaction_(authoritative),sink_(sink) {}

campaign_save::AckResult SaveBinding::acknowledge(bool stored) {
    const campaign_save::AckResult ack=transaction_.acknowledge(transaction_.pending()->sequence,stored);
    last_=ack==campaign_save::AckResult::committed ? SaveAttempt::committed : SaveAttempt::write_failed;
    return ack;
}

SaveStatus SaveBinding::commit(const CampaignRecord& staged,std::uint8_t next_wave_index) {
    if (sink_==nullptr) { last_=SaveAttempt::no_sink; return SaveStatus::failed; }
    if (last_==SaveAttempt::in_flight) return SaveStatus::pending;
    const CampaignWaveClear clear=clear_from_record(staged);
    const auto& pending=transaction_.pending();
    const bool reuse=pending && transaction_.failed_attempts()>0U &&
                     pending->next_wave_index==next_wave_index && same_clear(pending->clear,clear);
    if (!reuse && transaction_.request(next_wave_index,clear)!=campaign_save::RequestResult::issued) {
        last_=SaveAttempt::request_refused;
        return SaveStatus::failed;
    }
    ++writes_attempted_;
    const SinkWrite wrote=sink_->write(transaction_.pending()->bytes);
    if (wrote==SinkWrite::deferred) {
        last_=SaveAttempt::in_flight;
        deferred_polls_=0U;
        return SaveStatus::pending;
    }
    return acknowledge(wrote==SinkWrite::stored)==campaign_save::AckResult::committed ? SaveStatus::committed
                                                                                        : SaveStatus::failed;
}

SaveStatus SaveBinding::poll() {
    if (last_==SaveAttempt::in_flight) {
        if (deferred_poll_limit_==0U || ++deferred_polls_<deferred_poll_limit_) return SaveStatus::pending;
        abandon_write();
        return SaveStatus::failed;
    }
    return last_==SaveAttempt::committed ? SaveStatus::committed : SaveStatus::failed;
}

campaign_save::AckResult SaveBinding::finish_write(bool stored) {
    if (last_!=SaveAttempt::in_flight || !transaction_.pending()) return campaign_save::AckResult::no_pending;
    return acknowledge(stored);
}

bool SaveBinding::abandon_write() noexcept {
    if (last_!=SaveAttempt::in_flight) return false;
    // SaveTransaction::abandon drops the pending request; a late
    // finish_write then reports no_pending and the authoritative record
    // stays the last committed one.
    transaction_.abandon();
    last_=SaveAttempt::abandoned;
    deferred_polls_=0U;
    return true;
}

void SaveBinding::serialize(StateWriter& out) const {
    transaction_.serialize(out);
    out.u8(static_cast<std::uint8_t>(last_));
    out.u32(writes_attempted_);
    out.u32(deferred_poll_limit_);
    out.u32(deferred_polls_);
}

// --- frontend ----------------------------------------------------------------

FrontendBinding::FrontendBinding(bool save_enabled) : state_(frontend::make_frontend_state()) {
    state_.save_enabled=save_enabled;
}

void FrontendBinding::present(Phase phase,std::uint8_t wave_index,bool won) {
    ++presentations_;
    if (source_!=nullptr) state_.record=source_->record();
    switch (phase) {
    case Phase::result: {
        // Drive the screen from the same result transition campaign_flow
        // emits (kind result, won, offered choices).
        const CampaignWaveDefinition* wave=wave_at(wave_index);
        campaign_flow::FlowTransition result;
        result.kind=FlowTransitionKind::result;
        result.wave_index=wave_index;
        result.mission_id=wave!=nullptr ? wave->mission_id : 0;
        result.won=won;
        if (wave!=nullptr)
            result.choices=campaign_flow::offered_result_choices(*wave,
                won ? campaign_flow::flow_result_code_won : campaign_flow::flow_result_code_selectable);
        result.auto_advanced=won && wave!=nullptr && campaign_flow::result_auto_advances(*wave,campaign_flow::flow_result_code_won);
        commands_=frontend::enter_result(state_,result).commands;
        return;
    }
    case Phase::menu: state_.screen=frontend::Screen::campaign_menu; break;
    case Phase::briefing:
    case Phase::awaiting_battle: state_.screen=frontend::Screen::in_mission; break;
    case Phase::completed: state_.screen=frontend::Screen::title; break;
    case Phase::save_prompt:
    case Phase::save_pending: return;
    }
    state_.cursor=0;
    state_.help_visible=false;
}

void FrontendBinding::serialize(StateWriter& out) const {
    frontend::serialize(out,state_);
    out.u32(presentations_);
}

namespace {

ResultDrive from_error(const Coordinator& harness,Error error,ResultAction success) {
    if (error.code==ErrorCode::save_rejected) return ResultDrive{.action=ResultAction::save_failed,.error=error};
    if (!error.ok()) return ResultDrive{.action=ResultAction::wrong_phase,.error=error};
    if (harness.phase()==Phase::save_pending) return ResultDrive{.action=ResultAction::save_pending,.error={}};
    return ResultDrive{.action=success,.error={}};
}

} // namespace

ResultDrive drive_result(Coordinator& harness,FrontendBinding& binding,frontend::MenuInput input) {
    if (harness.phase()!=Phase::result || binding.state().screen!=frontend::Screen::result)
        return ResultDrive{.action=ResultAction::wrong_phase,.error=Error{.code=ErrorCode::wrong_phase,.wave_index=harness.wave_index()}};
    auto commands=frontend::update_frontend(binding.state(),input).commands;
    const bool quit=has_command(commands,frontend::CommandKind::quit_to_title);
    const bool restart=has_command(commands,frontend::CommandKind::restart_mission);
    // The advance_mission value names the result choice (frontend_model):
    // save button -> save_and_continue, continue button -> continue_without_save.
    const frontend::TransitionCommand* advance=find_command(commands,frontend::CommandKind::advance_mission);
    const bool save=advance!=nullptr &&
        advance->value==static_cast<std::int32_t>(FlowResultChoice::save_and_continue);
    binding.set_last_commands(std::move(commands));
    if (quit) return from_error(harness,harness.acknowledge_result(FlowResultChoice::quit),ResultAction::quit);
    if (restart) return from_error(harness,harness.acknowledge_result(FlowResultChoice::retry),ResultAction::retry);
    if (advance==nullptr) return ResultDrive{};
    if (save && harness.offered_choices()[slot(FlowResultChoice::save_and_continue)])
        return from_error(harness,harness.acknowledge_result(FlowResultChoice::save_and_continue),ResultAction::continue_saved);
    return from_error(harness,harness.acknowledge_result(FlowResultChoice::continue_without_save),ResultAction::continue_unsaved);
}

ResultDrive restart_battle(Coordinator& harness) {
    if (harness.phase()!=Phase::awaiting_battle)
        return ResultDrive{.action=ResultAction::wrong_phase,.error=Error{.code=ErrorCode::wrong_phase,.wave_index=harness.wave_index()}};
    return from_error(harness,harness.restart_battle(),ResultAction::retry);
}

ResultDrive abandon_result_save(Coordinator& harness,SaveBinding& save) {
    if (harness.phase()!=Phase::save_pending || !save.abandon_write())
        return ResultDrive{.action=ResultAction::wrong_phase,.error=Error{.code=ErrorCode::wrong_phase,.wave_index=harness.wave_index()}};
    // poll now reports failed: the coordinator opens the save prompt.
    return poll_result_save(harness);
}

ResultDrive retry_save(Coordinator& harness) {
    return from_error(harness,harness.answer_save(true),ResultAction::continue_saved);
}

ResultDrive decline_save(Coordinator& harness) {
    return from_error(harness,harness.answer_save(false),ResultAction::continue_unsaved);
}

ResultDrive poll_result_save(Coordinator& harness) {
    const Error error=harness.poll_save();
    if (error.ok() && harness.phase()==Phase::save_pending) return ResultDrive{.action=ResultAction::save_pending,.error={}};
    return from_error(harness,error,ResultAction::continue_saved);
}

// --- music -------------------------------------------------------------------

void MusicBinding::select(music_selection::MusicTransition transition,std::int32_t mission_id) {
    commands_.clear();
    ++selections_;
    music_selection::apply_music_transition(state_,transition,
        music_selection::TransitionContext{.level=mission_id,.music_volume=volume_,.demo_playback=false},commands_);
}

void MusicBinding::serialize(StateWriter& out) const {
    state_.serialize(out);
    out.u8(volume_);
    out.u32(selections_);
}

} // namespace battletanx::campaign_harness_bindings
