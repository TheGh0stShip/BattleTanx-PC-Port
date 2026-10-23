#pragma once

// Platform-neutral model of the BattleTanx campaign frontend. It recovers the
// decisions the original menus make (cursor wrap/clamp, confirm/cancel, the
// Controller Pak load/default/error choices, pause and result choices) without
// reproducing any screen layout, image, or retail text. Hosts receive draw
// commands naming items and values by identifier and render them however
// they like. See docs/FRONTEND_MODEL.md for the evidence classification.

#include "battletanx/campaign_flow.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/frontend_setup_menus.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace battletanx::frontend {

// Menu button bits as returned by the original menu reader (func_80096920).
namespace buttons {
inline constexpr std::uint32_t up=0x0001U;
inline constexpr std::uint32_t left=0x0002U;
inline constexpr std::uint32_t right=0x0004U;
inline constexpr std::uint32_t down=0x0008U;
inline constexpr std::uint32_t confirm=0x0010U;
inline constexpr std::uint32_t cancel=0x0020U;
inline constexpr std::uint32_t alt_confirm=0x0040U;
inline constexpr std::uint32_t help=0x0080U;
inline constexpr std::uint32_t prev_target=0x0100U;
inline constexpr std::uint32_t next_target=0x0200U;
} // namespace buttons

struct MenuInput {
    std::uint32_t pressed=0U;     // edge-triggered bits from `buttons`
    // Raw controller pressed bits; only the binding rows of controller_preset
    // read them (a row takes whichever raw button is pressed).
    std::uint32_t raw_pressed=0U;
};

enum class Screen : std::uint8_t {
    title=0U,
    pak_menu,          // poll_controller_pak_screen sub-states
    campaign_menu,     // 7-row setup menu (frontend_setup::MainMenuState)
    difficulty,
    unit_select,       // gang selection (frontend_setup::GangSelectState)
    controller_preset, // binding rows, preset and done (frontend_setup::ControlsSetupState)
    options,
    in_mission,
    pause,
    result,
    // Attract/demo playback is not a screen: selecting it at the title emits
    // CommandKind::attract_unsupported and stays on the title.
};
static_assert(sizeof(Screen)==1U);

enum class PakProbe : std::uint8_t {
    no_pak=0U,       // find_first_controller_pak returned 0
    ok_with_save,    // init status 0/2, a valid BattleTanx note was read
    ok_no_save,      // pak present, no BattleTanx note
    damaged,         // init/read returned a damaged-filesystem status
    full,            // allocate_controller_pak_save failed for space
    checksum_bad,    // read_controller_pak_save checksum mismatch
    device_error,    // any other non-zero status
};
static_assert(sizeof(PakProbe)==1U);

enum class PakDecision : std::uint8_t {
    load_record=0U,
    use_defaults,
    error_retry_or_continue, // shown as an error page: confirm retries, cancel continues without saving
    repair_offer,            // damaged pak: confirm repairs (repair_controller_pak), cancel skips
    manage_notes,            // full pak: note list, delete or continue without saving
};
static_assert(sizeof(PakDecision)==1U);

[[nodiscard]] PakDecision decide_pak_load(PakProbe probe) noexcept;

enum class ItemId : std::uint8_t {
    none=0U,
    title_start,
    title_attract,
    pak_retry,
    pak_continue_without_save,
    pak_repair,
    pak_delete_note,
    campaign_new,      // legacy, no longer drawn (see setup_row)
    campaign_continue, // legacy, no longer drawn (see setup_row)
    campaign_options,  // legacy, no longer drawn (see setup_row)
    difficulty_easy,
    difficulty_normal,
    difficulty_hard,
    unit_choice,       // value = gang of slot 0
    preset_choice,     // value = preset_selected
    option_sfx_volume,
    option_music_volume,
    option_difficulty,
    option_powerups,
    option_powerup_regen,
    option_control_scheme,
    option_unlimited_ammo,
    option_exit,
    pause_continue,
    pause_restart,
    pause_quit,
    result_retry,
    result_continue,
    result_quit,
    help_overlay,
    unsupported_notice,
    result_save_continue, // 0x20 on a result offering save_and_continue
    setup_row,            // campaign_menu: value = frontend_setup::SetupRow
    binding_row,          // controller_preset: value = row
    controls_done,        // controller_preset: done row (10)
};
static_assert(sizeof(ItemId)==1U);

struct DrawCommand {
    ItemId item=ItemId::none;
    std::int32_t value=0;  // option value, unit index, preset index; 0 otherwise
    bool selected=false;
    bool enabled=true;
};

enum class CommandKind : std::uint8_t {
    none=0U,
    load_campaign_record,
    use_default_record,
    repair_pak,
    repoll_pak,            // poll result 5: probe the pak again
    write_campaign_record,
    start_mission,
    resume_mission,
    restart_mission,       // pause: FlowBattleOutcome::restarted; result: FlowResultChoice::retry
    quit_to_title,         // pause: FlowBattleOutcome::aborted; result: FlowResultChoice::quit
    advance_mission,       // value = FlowResultChoice (continue_without_save or save_and_continue)
    stop_unit_sounds,      // func_80097B7C on entering the result state
    apply_sfx_volume,
    apply_music_volume,
    play_cursor_sound,     // func_800ABF40
    play_value_sound,      // func_800ABF70
    easy_restart_hook,     // increment_frame_event_counter (func_8010D984, D_803C4D70) on restart at difficulty 0
    attract_unsupported,
    setup_row_unsupported, // accepted setup row with no native screen; value = SetupRow
};
static_assert(sizeof(CommandKind)==1U);

struct TransitionCommand {
    CommandKind kind=CommandKind::none;
    std::int32_t value=0;
};

inline constexpr std::int32_t unit_choice_count=10;   // D_80134010 wraps 0..9
inline constexpr std::int32_t preset_count=4;         // (sel+1)%4 / (sel+3)%4
inline constexpr std::int32_t options_row_count=8;    // D_80134E60 wraps 0..7
inline constexpr std::int32_t volume_max=7;
inline constexpr std::int32_t locked_unit_index=1;    // skipped while D_80135773==0

enum class ResultKind : std::uint8_t { victory=0U, defeat=1U };
static_assert(sizeof(ResultKind)==1U);

struct FrontendState {
    Screen screen=Screen::title;
    std::int32_t cursor=0;
    bool help_visible=false;          // D_80327614 toggle
    bool record_loaded=false;         // a pak record was accepted
    bool save_enabled=false;          // D_80134E74
    bool locked_unit_available=false; // D_80135773
    PakDecision pak=PakDecision::use_defaults;
    // Recovered setup menus (frontend_setup_menus.hpp). `cursor` mirrors
    // setup_menu.cursor on campaign_menu and controls.row on controller_preset.
    frontend_setup::MainMenuState setup_menu{};
    frontend_setup::GangSelectState gangs{};
    frontend_setup::ControlsSetupState controls{};
    // One RetailRandom draw per gang slot, supplied by the host before
    // unit_select is entered (assign_computer_gangs); zero by default.
    std::array<std::uint32_t,frontend_setup::gang_slot_count> computer_gang_draws{};
    ResultKind result=ResultKind::defeat;
    // Result buttons by campaign_flow::FlowResultChoice value.
    std::array<bool,campaign_flow::flow_result_choice_count> result_choices{};
    bool result_cancel_continues=false; // endurance win: pressed&0x30 both continue
    CampaignRecord record{};
};

struct FrontendStep {
    std::vector<TransitionCommand> commands;
};

[[nodiscard]] FrontendState make_frontend_state();
// Applies a pak probe at the title: chooses record/defaults/error page.
FrontendStep apply_pak_probe(FrontendState& state,PakProbe probe,const CampaignRecord* loaded);
FrontendStep update_frontend(FrontendState& state,MenuInput input);
// Host notifications from gameplay.
void enter_pause(FrontendState& state);
// Legacy entry: victory offers continue and save (save only when save_enabled),
// defeat offers retry and quit. Prefer the FlowTransition overload.
FrontendStep enter_result(FrontendState& state,ResultKind kind);
// Drives the result screen from campaign_flow's `result` transition so both
// modules agree on the offered choices.
FrontendStep enter_result(FrontendState& state,const campaign_flow::FlowTransition& result);
[[nodiscard]] std::vector<DrawCommand> frontend_draw(const FrontendState& state);
// Full setup-menu draw (players/playmode values, binding icons, gang slots)
// for campaign_menu, unit_select and controller_preset; empty elsewhere.
[[nodiscard]] std::vector<frontend_setup::SetupDrawCommand> setup_draw(const FrontendState& state);
[[nodiscard]] bool item_enabled(const FrontendState& state,ItemId item) noexcept;
void serialize(StateWriter& writer,const FrontendState& state);

} // namespace battletanx::frontend
