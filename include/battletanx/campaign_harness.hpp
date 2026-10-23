#pragma once

// Headless campaign coordinator. Composes the value-layer campaign modules
// (catalog, flow, save transaction, frontend model, music selection) and
// walks menu -> briefing -> battle request -> result -> save -> next wave.
// It never simulates a battle: outcomes and objective events are injected.
//
// Flow, save, frontend and music collaborators are reached through the narrow
// adapter interfaces below. Each adapter pointer is optional; when absent the
// harness applies the catalog rule documented next to the default (see
// api.md for the mapping onto the dedicated modules).

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_flow.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/music_selection.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace battletanx::campaign_harness {

enum class Phase : std::uint8_t {
    menu=0U,
    briefing=1U,
    awaiting_battle=2U,
    result=3U,
    save_prompt=4U,
    completed=5U,
    save_pending=6U,     // asynchronous save write in flight; call poll_save
};
static_assert(std::is_same_v<std::underlying_type_t<Phase>,std::uint8_t>);

enum class ErrorCode : std::uint8_t {
    none=0U,
    wrong_phase,
    wave_out_of_catalog,
    missing_world_stream,
    missing_navigation,
    missing_briefing_image,
    missing_spawn_table,
    objective_unresolved,
    resume_mission_unknown,
    save_rejected,
    choice_not_offered,
};
static_assert(std::is_same_v<std::underlying_type_t<ErrorCode>,std::uint8_t>);

struct Error {
    ErrorCode code=ErrorCode::none;
    std::uint8_t wave_index=0U;
    [[nodiscard]] bool ok() const noexcept { return code==ErrorCode::none; }
};

// Synthetic stand-in for what the asset decoders would have produced for a
// wave. The harness only checks presence; it never reads asset content.
struct DecodedWaveInputs {
    bool world_stream=false;
    bool navigation=false;
    bool briefing_image=false;
    bool spawn_table=false;
};
struct DecodedInputs {
    std::array<DecodedWaveInputs,campaign_wave_count> waves{};
};
// Every input present; navigation only where the catalog says it exists.
[[nodiscard]] DecodedInputs complete_synthetic_inputs() noexcept;

enum class ObjectiveEventKind : std::uint8_t {
    goal_slot_reached=0U,   // mark_entities_for_matching_slot found a goal slot
    player_win_flag=1U,     // player 0 result flag became 2
    player_squad_lost=2U,   // player 0 / team 0 has no living squad
    teams_reduced=3U,       // fewer than two teams remain with player alive
};
static_assert(std::is_same_v<std::underlying_type_t<ObjectiveEventKind>,std::uint8_t>);

enum class OutcomeKind : std::uint8_t { won=0U, lost=1U, from_objective_events=2U };
static_assert(std::is_same_v<std::underlying_type_t<OutcomeKind>,std::uint8_t>);

struct BattleOutcome {
    OutcomeKind kind=OutcomeKind::from_objective_events;
    std::int32_t score=0;   // becomes the save carry on a clear
    std::int16_t bonus=0;   // becomes the save bonus on a clear
};

// What the harness hands to whoever runs the battle.
struct BattleRequest {
    std::uint32_t sequence=0U;
    CampaignWaveLaunch launch{};
    CampaignObjectiveKind objective=CampaignObjectiveKind::unresolved;
    std::int32_t par_time_seconds=0;
    void serialize(StateWriter& out) const;
};

// --- adapters ---------------------------------------------------------------

// The flow decision is made when the result is acknowledged, from the choice
// the player made on the result screen (campaign_flow::FlowResultChoice).
enum class FlowDecision : std::uint8_t { advance=0U, retry=1U, campaign_complete=2U, quit=3U };
static_assert(std::is_same_v<std::underlying_type_t<FlowDecision>,std::uint8_t>);

// Choices the result screen accepts, indexed by FlowResultChoice value. The
// harness asks campaign_flow::offered_result_choices (campaign_flow.hpp)
// with flow_result_code_won / flow_result_code_selectable, so there is one
// rule: a won endurance wave (kind 7) offers nothing and auto-advances
// (campaign_flow::result_auto_advances); an endurance loss offers both
// continue buttons; any other win offers continue_without_save, plus
// save_and_continue unless the catalog disables it (mission 16); any other
// loss offers retry and quit.
using ResultChoices=std::array<bool,campaign_flow::flow_result_choice_count>;

// Expected to map onto campaign_flow (W1). Called with a choice the result
// accepted, or, for a won endurance wave that auto-advances, with the carried
// choice (save_and_continue if the previous result dialog chose save, else
// continue_without_save).
struct FlowAdapter {
    virtual ~FlowAdapter()=default;
    [[nodiscard]] virtual FlowDecision decide(const CampaignWaveDefinition& wave,bool won,
                                              campaign_flow::FlowResultChoice choice) const=0;
};

// Save write status. Hosts that write asynchronously return pending from
// commit and later report committed/failed through poll.
enum class SaveStatus : std::uint8_t { committed=0U, pending=1U, failed=2U };
static_assert(std::is_same_v<std::underlying_type_t<SaveStatus>,std::uint8_t>);

// Expected to map onto campaign_save::SaveTransaction
// (campaign_save_transaction.hpp). commit builds a CampaignWaveClear from
// `staged` and calls request(next_wave_index,clear); next_wave_index is the
// cleared wave + 1, at most 16 (campaign_save::max_save_next_wave_index):
// retail never saves after the final wave (mission 16 offers no save button,
// update_match_state.c:397-405), so the harness never asks for index 17.
// Only RequestResult::issued starts a write; committed
// means acknowledge(...) == AckResult::committed. poll is only called while
// the coordinator is in Phase::save_pending.
//
// Authoritative save content (decision): the coordinator's record() is what
// is staged, not a campaign_flow FlowOptions-built clear. The harness record
// carries the cleared mission id, score, bonus, difficulty and the session
// unit (Settings::selected_unit_kind); every option field (volumes, toggles,
// control scheme, bindings) is carried unchanged from the record the session
// started from (default_campaign_record or the resumed record). A host that
// wants retail's new-game unit default (reset_session_state_defaults: 9)
// passes it as selected_unit_kind. The staged record is adopted as
// saved_record() only after committed.
struct SaveAdapter {
    virtual ~SaveAdapter()=default;
    [[nodiscard]] virtual SaveStatus commit(const CampaignRecord& staged,std::uint8_t next_wave_index)=0;
    [[nodiscard]] virtual SaveStatus poll()=0;
};
// Expected to map onto frontend_model (W11): told which screen is current
// and whether the last battle was won.
struct FrontendAdapter {
    virtual ~FrontendAdapter()=default;
    virtual void present(Phase phase,std::uint8_t wave_index,bool won)=0;
};
// Expected to map onto music_selection::apply_music_transition (W10).
// mission_id is the current wave's mission id (TransitionContext::level).
// Retail order: wave_loading (or wave_restarted on retry) stops music when the
// battle loads (play_campaign_wave), countdown_finished starts the level
// track (update_match_state 4->1). briefing/results are emitted as the
// TEMPORARY POLICY no-op transitions music_selection defines.
struct MusicAdapter {
    virtual ~MusicAdapter()=default;
    virtual void select(music_selection::MusicTransition transition,std::int32_t mission_id)=0;
};

struct Adapters {
    const FlowAdapter* flow=nullptr;
    SaveAdapter* save=nullptr;
    FrontendAdapter* frontend=nullptr;
    MusicAdapter* music=nullptr;
};

// Catalog rule used when no FlowAdapter is supplied: quit -> quit, retry ->
// retry, either continue -> next wave (campaign_complete after the last).
// Not delegated to campaign_flow::CampaignFlow: that class is a stateful
// command machine with no pure "decide" entry point. Its advance rule
// (CampaignFlow::advance/finish_advance: index+1, completion at
// campaign_wave_count) is the same as this one; FlowBinding
// (campaign_harness_bindings.hpp) forwards to a real CampaignFlow.
[[nodiscard]] FlowDecision default_flow_decision(const CampaignWaveDefinition& wave,
                                                 campaign_flow::FlowResultChoice choice) noexcept;

struct Settings {
    CampaignDifficulty difficulty=CampaignDifficulty::normal;
    std::int32_t selected_unit_kind=0;
};

inline constexpr std::uint32_t harness_serial_version=3U;
// TEMPORARY POLICY: at most this many objective events are kept per battle.
// The first decisive event decides the outcome, so later events beyond the
// cap are dropped and events_overflowed() is set (serialized).
inline constexpr std::uint32_t max_objective_events=64U;

class Coordinator {
public:
    Coordinator(DecodedInputs inputs,Adapters adapters) noexcept;

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] std::uint8_t wave_index() const noexcept { return wave_index_; }
    // In-memory session values (updated on every clear).
    [[nodiscard]] const CampaignRecord& record() const noexcept { return record_; }
    // Last record a SaveAdapter accepted (or the harness adopted without one).
    [[nodiscard]] const std::optional<CampaignRecord>& saved_record() const noexcept { return saved_; }
    [[nodiscard]] std::uint32_t saves_committed() const noexcept { return saves_committed_; }
    [[nodiscard]] const std::optional<BattleRequest>& pending_request() const noexcept { return request_; }
    [[nodiscard]] std::uint32_t waves_cleared() const noexcept { return waves_cleared_; }
    [[nodiscard]] bool last_result_won() const noexcept { return last_won_; }
    [[nodiscard]] bool events_overflowed() const noexcept { return events_overflowed_; }

    // menu -> briefing of wave 0 (new game) or the record's wave (resume).
    // A record whose mission id is 0 (no wave; retail never writes it, kept
    // for robustness) resumes as Phase::completed (campaign_resume_for_record).
    Error start_new(const Settings& settings);
    Error resume(const CampaignRecord& record,std::int32_t selected_unit_kind);
    // Validates decoded inputs for any catalog wave (does not change phase).
    [[nodiscard]] Error check_wave_inputs(std::uint8_t wave_index) const noexcept;
    // briefing -> awaiting_battle; produces pending_request(); music
    // wave_loading (wave_restarted after a retry).
    Error confirm_briefing();
    // awaiting_battle, once per battle: music countdown_finished.
    Error countdown_finished();
    // Only accepted while awaiting_battle; consulted by from_objective_events.
    Error post_objective_event(ObjectiveEventKind kind);
    // Pause-menu RESTART (campaign_flow FlowBattleOutcome::restarted,
    // draw_pause_menu case 1): awaiting_battle -> awaiting_battle with a new
    // pending_request() for the same wave, no briefing, no result, record
    // unchanged; music wave_restarted; countdown and objective events reset.
    Error restart_battle();
    // awaiting_battle -> result. A won endurance wave skips the result screen
    // and advances with the carried save choice (last_save_choice()); the
    // return value is then that of the save / advance.
    Error submit_outcome(const BattleOutcome& outcome);
    // D_8012579B: true when the last result dialog chose save_and_continue.
    // Cleared by start_new/resume (campaign_flow TEMPORARY POLICY).
    [[nodiscard]] bool last_save_choice() const noexcept { return last_save_choice_; }
    // Choices the current result accepts (all false outside Phase::result).
    [[nodiscard]] ResultChoices offered_choices() const noexcept;
    // result -> briefing (retry) / menu (quit) / save (save_and_continue) /
    // next briefing or completed (continue_without_save). A save that fails
    // leaves Phase::save_prompt and returns save_rejected; an asynchronous
    // write leaves Phase::save_pending.
    Error acknowledge_result(campaign_flow::FlowResultChoice choice);
    // save_pending: asks the SaveAdapter; committed advances, failed ->
    // save_prompt (save_rejected), pending stays.
    Error poll_save();
    // save_prompt (after a failed write): true retries the write, false
    // continues without saving.
    Error answer_save(bool save);
    // Any phase -> menu.
    void quit_to_menu() noexcept;

    void serialize(StateWriter& out) const;
    [[nodiscard]] std::array<std::uint8_t,32> checksum() const;

private:
    Error enter_briefing(std::uint8_t wave_index);
    void set_phase(Phase phase);
    [[nodiscard]] bool resolve_won(const CampaignWaveDefinition& wave,OutcomeKind kind) const noexcept;
    void advance_after(const CampaignWaveDefinition& wave,FlowDecision decision);
    Error decide_and_continue(const CampaignWaveDefinition& wave,campaign_flow::FlowResultChoice choice);
    Error begin_save();
    Error save_status(SaveStatus status);
    void music(music_selection::MusicTransition transition);

    DecodedInputs inputs_{};
    Adapters adapters_{};
    Settings settings_{};
    Phase phase_=Phase::menu;
    std::uint8_t wave_index_=0U;
    CampaignRecord record_{};
    std::optional<CampaignRecord> saved_;
    std::optional<BattleRequest> request_;
    std::uint32_t request_sequence_=0U;
    std::vector<ObjectiveEventKind> events_;
    bool last_won_=false;
    FlowDecision pending_decision_=FlowDecision::advance;
    std::uint32_t waves_cleared_=0U;
    std::uint32_t saves_committed_=0U;
    bool retrying_=false;
    bool countdown_done_=false;
    bool events_overflowed_=false;
    bool last_save_choice_=false;
};

} // namespace battletanx::campaign_harness
