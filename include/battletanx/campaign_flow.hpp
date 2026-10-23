#pragma once

// Platform-neutral campaign flow controller.
//
// Models the wave loop of run_campaign_waves (func_8007B280), the retry
// restart inside play_campaign_wave (func_8007BD24), the state-7 result
// buttons of update_match_state (func_8007F2A0) and the progress write
// requested through write_controller_pak_progress (func_800D0828).
//
// The controller is value-only: hosts feed FlowCommand values and receive
// FlowTransition values. It performs no I/O and never mutates a
// CampaignRecord; saves are emitted as FlowSaveRequest values for the save
// transaction module to apply (see docs/CAMPAIGN_FLOW.md).

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace battletanx::campaign_flow {

// run_state_loop_after_sequence_reset (func_8007C58C) cut-scene ids.
inline constexpr std::uint8_t final_sequence_id=19U;
// gFrontendState (D_801B4AE0) value set once every wave has been passed.
inline constexpr std::uint8_t completion_frontend_state=19U;
inline constexpr std::uint32_t flow_serial_version=3U;
// func_800A42B0 entity-0 result values read by update_match_state state 7.
inline constexpr std::int16_t flow_result_code_from_won=-1; // derive from FlowBattleOutcome::won
inline constexpr std::int16_t flow_result_code_selectable=0; // loss / endurance normal end
inline constexpr std::int16_t flow_result_code_won=2;

enum class FlowPhase : std::uint8_t {
    idle=0U,           // not started
    chapter_intro=1U,  // waiting for the chapter cut-scene to end
    briefing=2U,       // waiting for the briefing screen to end
    battle=3U,         // battle launched; waiting for its outcome
    result=4U,         // result screen; waiting for a result choice
    save_pending=5U,   // save requested; waiting for the host to finish it
    completed=6U,      // every wave passed
    menu=7U,           // flow ended and control returned to the main menu
};
static_assert(sizeof(FlowPhase)==1U);

enum class FlowTransitionKind : std::uint8_t {
    intro=0U,          // play chapter cut-scene `sequence_id`
    briefing=1U,       // show briefing for `wave_index`
    battle_launch=2U,  // start battle with `launch`
    result=3U,         // show result; `choices` lists the accepted choices
                       // (none and `auto_advanced` set for a won endurance wave)
    intermission=4U,   // wave cleared/passed; carry and bonus updated
    save_request=5U,   // apply `save` and report FlowCommandKind::save_finished
    next_wave=6U,      // current wave index advanced to `wave_index`
    retry=7U,          // same wave relaunched from its pre-battle snapshot
    completion=8U,     // all 17 waves passed
    return_to_menu=9U, // flow ended without completion
    rejected=10U,      // command not valid in this phase; state unchanged
};
static_assert(sizeof(FlowTransitionKind)==1U);

enum class FlowRejectReason : std::uint8_t {
    none=0U,
    invalid_mission_id=1U,   // saved mission id names no catalog wave
    wrong_phase=2U,
    choice_not_offered=3U,
    already_started=4U,
};
static_assert(sizeof(FlowRejectReason)==1U);

enum class FlowMenuReason : std::uint8_t {
    none=0U,
    intro_cancelled=1U,   // sequence 15 returned < 0
    battle_aborted=2U,    // pause-menu QUIT only (case 2: D_801B4AE0=2, D_80125798=1)
    wave_lost=3U,         // loss on a non-endurance wave, quit chosen
    invalid_resume=4U,    // saved mission id not in the catalog
};
static_assert(sizeof(FlowMenuReason)==1U);

// Result-screen buttons (update_match_state state 7, single human player).
enum class FlowResultChoice : std::uint8_t {
    continue_without_save=0U, // win: button 0x10; endurance: 0x10
    save_and_continue=1U,     // win (mission != 16) or endurance: 0x20
    retry=2U,                 // non-endurance loss: 0x10
    quit=3U,                  // non-endurance loss: 0x20
};
static_assert(sizeof(FlowResultChoice)==1U);
inline constexpr std::size_t flow_result_choice_count=4U;

// Live session options. These are the globals write_controller_pak_progress
// reads (D_80134E64..D_80134E69, volumes, D_803283B8, D_803282A8) plus the
// session unit selection (D_80134E78 +0x10).
struct FlowOptions {
    std::int32_t unit_type=0;
    CampaignDifficulty difficulty=CampaignDifficulty::easy;
    std::uint8_t powerups=0U;
    std::uint8_t powerup_regen=0U;
    std::uint8_t control_scheme=0U;
    std::uint8_t base_defense=0U;
    std::uint8_t unlimited_ammo=0U;
    std::int32_t sfx_volume=0;
    std::int32_t music_volume=0;
    std::array<std::uint32_t,campaign_binding_words> bindings{};
    void serialize(StateWriter& out) const;
};

[[nodiscard]] FlowOptions options_from_record(const CampaignRecord& record) noexcept;

// Battle outcome as returned through CampaignParams[0].
struct FlowBattleOutcome {
    bool won=false;               // params.result (entity 0 result flag == 2)
    bool aborted=false;           // pause QUIT (D_80125798 set, D_80125799 clear)
    std::int32_t player_score=0;  // params.value00 = entity 0 +0x210 -> next carry
    std::int16_t player_bonus=0;  // params.value08 (+0x3A, or endurance kill total)
    // Pause RESTART (draw_pause_menu case 1 sets D_80125799 and D_80125798):
    // play_campaign_wave `goto restart` relaunches the same wave from its
    // backup. Takes precedence over `aborted`.
    bool restarted=false;
    // Raw entity-0 result (func_800A42B0). flow_result_code_from_won derives it
    // from `won`. Values other than 0 and 2 accept no result button in
    // update_match_state (`continue`); the flow offers no choices for them
    // (UNRESOLVED: which game states produce them).
    std::int16_t result_code=flow_result_code_from_won;
};

enum class FlowCommandKind : std::uint8_t {
    start_new=0U,         // record defaults then wave 0
    resume=1U,            // resume at record.mission_id
    intro_finished=2U,    // accepted=false means the cut-scene was cancelled
    briefing_finished=3U,
    battle_finished=4U,
    result_choice=5U,
    save_finished=6U,     // success is reported but never changes the flow
    set_options=7U,
};
static_assert(sizeof(FlowCommandKind)==1U);

struct FlowCommand {
    FlowCommandKind kind=FlowCommandKind::start_new;
    CampaignRecord record{};            // start_new / resume
    bool accepted=true;                 // intro_finished / save_finished
    FlowBattleOutcome outcome{};        // battle_finished
    FlowResultChoice choice=FlowResultChoice::continue_without_save; // result_choice
    FlowOptions options{};              // set_options
};

struct FlowSaveRequest {
    CampaignWaveClear clear{};           // fields write_controller_pak_progress stores
    std::uint8_t cleared_wave_index=0U;  // wave whose result triggered the save
};

struct FlowTransition {
    FlowTransitionKind kind=FlowTransitionKind::rejected;
    std::uint8_t wave_index=0U;
    std::int32_t mission_id=0;
    std::uint8_t sequence_id=0U;                 // intro, completion
    std::int32_t briefing_image=-1;              // briefing
    CampaignWaveLaunch launch{};                 // battle_launch, retry
    bool won=false;                              // result
    std::array<bool,flow_result_choice_count> choices{}; // result, by FlowResultChoice value
    // result: update_match_state state 6 skipped the state-7 dialog (kind 7,
    // entity 0 result 2). The intermission and any save_request follow in the
    // same apply() call; `save_choice` is the carried D_8012579B value used.
    bool auto_advanced=false;
    bool save_choice=false;
    std::int32_t carry=0;                        // intermission
    std::int16_t bonus=0;                        // intermission
    std::optional<FlowSaveRequest> save;         // save_request
    FlowRejectReason reject=FlowRejectReason::none;
    FlowMenuReason menu=FlowMenuReason::none;
};

// update_match_state state 6/7 result-button rule for one wave and entity-0
// result code (flow_result_code_won / flow_result_code_selectable; any other
// value accepts no button). A won endurance wave (game_kind 7) returns no
// choices because retail skips the dialog; see result_auto_advances.
[[nodiscard]] std::array<bool,flow_result_choice_count> offered_result_choices(const CampaignWaveDefinition& wave,
                                                                                std::int16_t result_code) noexcept;
// True when the result skips the dialog (update_match_state.c:342-344) and the
// wave advances with the carried save choice (CampaignFlow::last_save_choice).
[[nodiscard]] bool result_auto_advances(const CampaignWaveDefinition& wave,std::int16_t result_code) noexcept;

class CampaignFlow {
public:
    // Applies one command and returns the transitions it causes, in order.
    [[nodiscard]] std::vector<FlowTransition> apply(const FlowCommand& command);

    [[nodiscard]] FlowPhase phase() const noexcept { return phase_; }
    [[nodiscard]] std::uint8_t wave_index() const noexcept { return wave_index_; }
    [[nodiscard]] std::int32_t carry() const noexcept { return carry_; }
    [[nodiscard]] std::int16_t bonus() const noexcept { return bonus_; }
    [[nodiscard]] const FlowOptions& options() const noexcept { return options_; }
    // Retries (result retry or pause restart) of the current wave; reset on
    // every wave entry (TEMPORARY POLICY counter, not a retail global).
    [[nodiscard]] std::uint32_t retries() const noexcept { return retries_; }
    // Mirror of D_803C4D70: reset_frame_event_counter on wave entry
    // (play_campaign_wave.c:108), increment_frame_event_counter (func_8010D984)
    // on a restart at difficulty easy (update_match_state.c, draw_pause_menu.c).
    [[nodiscard]] std::uint32_t easy_restart_count() const noexcept { return easy_restart_count_; }
    // Mirror of D_8012579B: the save choice of the last result dialog. Retail
    // never clears it at wave start, so a won endurance wave (no dialog) saves
    // iff the previous dialog chose save (update_match_state.c:342-344,
    // run_campaign_waves.c:122-126).
    [[nodiscard]] bool last_save_choice() const noexcept { return last_save_choice_; }
    [[nodiscard]] const CampaignWaveDefinition* current_wave() const noexcept;

    void serialize(StateWriter& out) const;
    [[nodiscard]] std::array<std::uint8_t,32> checksum() const;

private:
    std::vector<FlowTransition> start(const CampaignRecord& record,bool fresh);
    void enter_wave(std::vector<FlowTransition>& out);
    void launch(std::vector<FlowTransition>& out,FlowTransitionKind kind);
    void retry(std::vector<FlowTransition>& out);
    void advance(std::vector<FlowTransition>& out,bool save);
    void finish_advance(std::vector<FlowTransition>& out);
    [[nodiscard]] std::array<bool,flow_result_choice_count> offered_choices() const noexcept;

    FlowPhase phase_=FlowPhase::idle;
    std::uint8_t wave_index_=0U;
    std::int32_t carry_=0;   // params.value00
    std::int16_t bonus_=0;   // params.value08
    FlowOptions options_{};
    bool last_won_=false;
    std::int16_t last_result_code_=flow_result_code_selectable;
    FlowBattleOutcome last_outcome_{};
    std::uint32_t retries_=0U;
    std::uint32_t easy_restart_count_=0U;
    bool last_save_choice_=false;
};

} // namespace battletanx::campaign_flow
