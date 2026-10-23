#pragma once

// Concrete adapters connecting the headless campaign coordinator
// (campaign_harness) to the value-layer modules it was written against:
//   FlowAdapter     -> campaign_flow::CampaignFlow
//   SaveAdapter     -> campaign_save::SaveTransaction (+ a host write sink)
//   FrontendAdapter -> frontend::FrontendState / update_frontend
//   MusicAdapter    -> music_selection::MusicState
// No I/O happens here: the save binding hands complete 256-byte records to a
// PakWriteSink and the music binding only collects AudioCommands.

#include "battletanx/campaign_flow.hpp"
#include "battletanx/campaign_harness.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/campaign_save_transaction.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/frontend_model.hpp"
#include "battletanx/music_selection.hpp"
#include "battletanx/state_hash.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace battletanx::campaign_harness_bindings {

// --- flow --------------------------------------------------------------------

// Answers FlowAdapter::decide by replaying the wave through a scratch
// CampaignFlow (resume at the wave's mission id, finish intro/briefing,
// report the outcome, apply `choice`) and reading its transitions:
// retry -> retry, return_to_menu -> quit, next_wave -> advance,
// completion -> campaign_complete (a save_request is answered with
// save_finished first; the scratch flow's save is discarded, the harness
// record is the authoritative save, see SaveAdapter). Any wave the flow
// cannot reach by mission id, or a choice it rejects, falls back to
// default_flow_decision.
class FlowBinding final : public campaign_harness::FlowAdapter {
public:
    [[nodiscard]] campaign_harness::FlowDecision decide(const CampaignWaveDefinition& wave,bool won,
                                                        campaign_flow::FlowResultChoice choice) const override;
};

// --- save --------------------------------------------------------------------

enum class SinkWrite : std::uint8_t {
    stored=0U,
    failed=1U,
    deferred=2U,   // host finishes later and calls SaveBinding::finish_write
};
static_assert(std::is_same_v<std::underlying_type_t<SinkWrite>,std::uint8_t>);

// Host side of a Controller Pak write. The binding never touches files itself.
struct PakWriteSink {
    virtual ~PakWriteSink()=default;
    [[nodiscard]] virtual SinkWrite write(const ControllerPakRecord& bytes)=0;
};

// Every field write_controller_pak_progress stores, taken from a record.
[[nodiscard]] CampaignWaveClear clear_from_record(const CampaignRecord& record) noexcept;
[[nodiscard]] bool same_clear(const CampaignWaveClear& a,const CampaignWaveClear& b) noexcept;

enum class SaveAttempt : std::uint8_t {
    none=0U,
    committed=1U,
    write_failed=2U,     // request kept pending; the next commit retries or replaces it
    no_sink=3U,
    request_refused=4U,  // busy (write in flight) or invalid_wave_index
    in_flight=5U,        // deferred write not yet finished
    abandoned=6U,        // deferred write given up (abandon_write or poll limit)
};
static_assert(std::is_same_v<std::underlying_type_t<SaveAttempt>,std::uint8_t>);

class SaveBinding final : public campaign_harness::SaveAdapter {
public:
    // sink may be null (every commit then reports no_sink and fails).
    SaveBinding(const CampaignRecord& authoritative,PakWriteSink* sink);

    // Stages `staged` as SaveTransaction::request(next_wave_index,clear) and
    // hands the bytes to the sink. A failed pending request for the same clear
    // and index is rewritten with the same sequence; any other failed pending
    // request is replaced by request() (W12b replace-on-failure policy).
    // next_wave_index is 0..16; 17+ is refused (request_refused,
    // invalid_wave_index) because retail never saves after the final wave.
    [[nodiscard]] campaign_harness::SaveStatus commit(const CampaignRecord& staged,std::uint8_t next_wave_index) override;
    // While a deferred write is in flight: pending, until deferred_poll_limit
    // polls have been made (when non-zero); the write is then abandoned and
    // failed is returned, which opens the coordinator's save prompt.
    [[nodiscard]] campaign_harness::SaveStatus poll() override;
    // Completes a deferred write. Returns no_pending when none is in flight
    // (including after the write was abandoned).
    campaign_save::AckResult finish_write(bool stored);
    // Gives up a deferred write: SaveTransaction::abandon() drops the pending
    // request, last_attempt() becomes abandoned and poll() reports failed.
    // Returns false when no write is in flight. A host quitting to the menu
    // during Phase::save_pending calls this first.
    bool abandon_write() noexcept;
    // TEMPORARY POLICY: 0 (default) = never time out; the host owns the clock.
    // Counted in poll() calls, not time, to stay deterministic.
    void set_deferred_poll_limit(std::uint32_t polls) noexcept { deferred_poll_limit_=polls; }

    [[nodiscard]] const campaign_save::SaveTransaction& transaction() const noexcept { return transaction_; }
    [[nodiscard]] SaveAttempt last_attempt() const noexcept { return last_; }
    [[nodiscard]] std::uint32_t writes_attempted() const noexcept { return writes_attempted_; }
    void serialize(StateWriter& out) const;

private:
    campaign_save::AckResult acknowledge(bool stored);

    campaign_save::SaveTransaction transaction_;
    PakWriteSink* sink_=nullptr;
    SaveAttempt last_=SaveAttempt::none;
    std::uint32_t writes_attempted_=0U;
    std::uint32_t deferred_poll_limit_=0U;
    std::uint32_t deferred_polls_=0U;
};

// --- frontend ----------------------------------------------------------------

// Keeps a frontend::FrontendState in step with the coordinator phase.
// Phase::result enters the result screen through
// enter_result(state,FlowTransition) built from
// campaign_flow::offered_result_choices; menu -> campaign_menu;
// briefing/awaiting_battle -> in_mission; completed -> title. save_prompt and
// save_pending keep the current screen (the frontend model has no separate
// save screen).
// Record sync: after attach(coordinator), every present() first copies
// coordinator.record() into state().record (frontend_model reads
// record.difficulty for the easy-restart hook). Without attach, the host
// calls sync_record() whenever it wants the frontend to see a record.
class FrontendBinding final : public campaign_harness::FrontendAdapter {
public:
    explicit FrontendBinding(bool save_enabled);

    void present(campaign_harness::Phase phase,std::uint8_t wave_index,bool won) override;
    // The coordinator must outlive the binding (or detach with nullptr).
    void attach(const campaign_harness::Coordinator* source) noexcept { source_=source; }
    void sync_record(const CampaignRecord& record) noexcept { state_.record=record; }

    [[nodiscard]] frontend::FrontendState& state() noexcept { return state_; }
    [[nodiscard]] const frontend::FrontendState& state() const noexcept { return state_; }
    [[nodiscard]] const std::vector<frontend::TransitionCommand>& last_commands() const noexcept { return commands_; }
    void set_last_commands(std::vector<frontend::TransitionCommand> commands) { commands_=std::move(commands); }
    [[nodiscard]] std::uint32_t presentations() const noexcept { return presentations_; }
    void serialize(StateWriter& out) const;

private:
    frontend::FrontendState state_{};
    const campaign_harness::Coordinator* source_=nullptr;
    std::vector<frontend::TransitionCommand> commands_;
    std::uint32_t presentations_=0U;
};

enum class ResultAction : std::uint8_t {
    none=0U,            // input did not choose anything
    retry=1U,           // restart_mission -> same wave briefing
    continue_saved=2U,  // continue with a committed save
    continue_unsaved=3U,// continue without a save
    quit=4U,            // quit_to_title -> coordinator menu
    save_failed=5U,     // save prompt open; call retry_save or decline_save
    wrong_phase=6U,
    save_pending=7U,    // deferred write; call poll_result_save
};
static_assert(std::is_same_v<std::underlying_type_t<ResultAction>,std::uint8_t>);

struct ResultDrive {
    ResultAction action=ResultAction::none;
    campaign_harness::Error error{};
};

// Feeds one menu input to the result screen, maps the frontend commands to a
// FlowResultChoice and calls acknowledge_result(choice):
// quit_to_title -> quit; restart_mission -> retry; advance_mission's value
// (a FlowResultChoice: save button 0x20 -> save_and_continue, continue button
// 0x10 -> continue_without_save). write_campaign_record is not consulted. A
// save_and_continue the coordinator does not offer is re-sent as
// continue_without_save. Pause-screen restart is restart_battle below; pause
// quit is Coordinator::quit_to_menu.
ResultDrive drive_result(campaign_harness::Coordinator& harness,FrontendBinding& binding,frontend::MenuInput input);
// Pause-menu RESTART during awaiting_battle: Coordinator::restart_battle
// (flow FlowBattleOutcome::restarted). retry on success.
ResultDrive restart_battle(campaign_harness::Coordinator& harness);
// Gives up a stuck deferred write (SaveBinding::abandon_write) and polls, so
// the coordinator moves to the save prompt (action save_failed); then
// retry_save or decline_save.
ResultDrive abandon_result_save(campaign_harness::Coordinator& harness,SaveBinding& save);
// Save prompt after a failed write: retry it / continue without saving.
ResultDrive retry_save(campaign_harness::Coordinator& harness);
ResultDrive decline_save(campaign_harness::Coordinator& harness);
// Save in flight: Coordinator::poll_save.
ResultDrive poll_result_save(campaign_harness::Coordinator& harness);

// --- music -------------------------------------------------------------------

// Forwards to music_selection::apply_music_transition with
// TransitionContext{level=mission_id, music_volume, demo_playback=false}.
class MusicBinding final : public campaign_harness::MusicAdapter {
public:
    explicit MusicBinding(std::uint8_t music_volume=music_selection::music_volume_max) noexcept : volume_(music_volume) {}

    void select(music_selection::MusicTransition transition,std::int32_t mission_id) override;

    [[nodiscard]] const music_selection::MusicState& state() const noexcept { return state_; }
    [[nodiscard]] const music_selection::AudioCommandList& last_commands() const noexcept { return commands_; }
    [[nodiscard]] std::uint32_t selections() const noexcept { return selections_; }
    void serialize(StateWriter& out) const;

private:
    music_selection::MusicState state_{};
    music_selection::AudioCommandList commands_{};
    std::uint8_t volume_=music_selection::music_volume_max;
    std::uint32_t selections_=0U;
};

} // namespace battletanx::campaign_harness_bindings
