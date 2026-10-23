#pragma once

// Setup menus that frontend_model.hpp leaves unmodelled: the main setup menu
// (update_game_setup_menu_input), the per-player button-binding rows
// (update_player_slot_configuration / assign_player_slot_value) and gang
// selection (poll_player_bundle_selection). Value-only: draw commands name
// items by identifier and carry numbers, never retail text or images.
// Evidence classification: CONFIRMED unless a comment says otherwise.

#include "battletanx/campaign_record.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace battletanx::frontend_setup {

inline constexpr std::size_t binding_words=17U;
using BindingWords=std::array<std::uint32_t,binding_words>;

// Menu bits are the same as battletanx::frontend::buttons (func_80096920).
namespace menu_bits {
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
} // namespace menu_bits

// ---------------------------------------------------------------- bindings

inline constexpr std::int32_t preset_count=4;
inline constexpr std::int32_t binding_row_last=10;  // rows clamp 0..10
inline constexpr std::int32_t preset_row=9;
inline constexpr std::int32_t done_row=10;
inline constexpr std::int32_t help_min_row=8;       // help toggles only at row>=8
// Words >= this are analog/system entries, never a binding row.
inline constexpr std::uint32_t non_row_threshold=0x00100000U;
// Raw controller bits accepted as a new binding (pressed & 0xE03F).
inline constexpr std::uint32_t assignable_mask=0x0000E03FU;

// D_80125D50 (ROM 0x0B5D50), four 68-byte records of 17 big-endian words.
inline constexpr std::array<BindingWords,preset_count> preset_bindings{{
    {0x2000U,0x8000U,0x4000U,0x20U,0x8U,0x4U,0x100000U,0x100000U,0x400000U,0x400000U,0x200000U,0x200000U,
     0x1U,0x2U,0x200000U,0x200000U,0x100000U},
    {0x2000U,0x8000U,0x4000U,0x20U,0x8U,0x4U,0x100000U,0x100000U,0x400000U,0x400000U,0x200000U,0x200000U,
     0x1U,0x2U,0x200000U,0x200000U,0x10U},
    {0x2000U,0x8000U,0x4000U,0x100000U,0x10U,0x20U,0x200000U,0x200000U,0x8U,0x4U,0x1U,0x2U,
     0x1U,0x2U,0x200000U,0x200000U,0x100000U},
    {0x2000U,0x8000U,0x4000U,0x100000U,0x8U,0x4U,0x1U,0x2U,0x400000U,0x400000U,0x200000U,0x200000U,
     0x10U,0x20U,0x200000U,0x200000U,0x100000U},
}};
// D_80135034 (ROM 0x0C5034): number of binding rows shown per preset.
inline constexpr std::array<std::int32_t,preset_count> preset_row_counts{8,9,9,9};
// D_80135020 (ROM 0x0C5020): icon id per 1-based lowest-set-bit index (0 = none).
inline constexpr std::array<std::uint8_t,17> binding_icon_by_bit{0U,8U,9U,7U,6U,4U,5U,0U,0U,0U,0U,0U,0U,0U,1U,3U,2U};
// Assignable values in lowest-bit-first order. TEMPORARY POLICY: the retail
// screen has no cycle (a row takes whatever button is pressed); this order is
// used only by cycle_binding for hosts without raw-button capture.
inline constexpr std::array<std::uint32_t,9> assignable_values{0x1U,0x2U,0x4U,0x8U,0x10U,0x20U,0x2000U,0x4000U,0x8000U};

[[nodiscard]] std::int32_t binding_row_count(std::int32_t preset) noexcept; // 0 for an out-of-range preset
// Word index edited by `row` (the row-th word below non_row_threshold), or
// nullopt when the row is not a binding row for this preset.
[[nodiscard]] std::optional<std::uint8_t> binding_word_for_row(const BindingWords& words,std::int32_t preset,
                                                               std::int32_t row) noexcept;
// Lowest set bit of a raw pressed mask & assignable_mask, or 0.
[[nodiscard]] std::uint32_t binding_value_from_raw(std::uint32_t raw_pressed) noexcept;
[[nodiscard]] std::uint8_t binding_icon(std::uint32_t value) noexcept;

enum class BindingEdit : std::uint8_t {
    invalid=0U,  // bad row/preset/value
    assigned,    // value was unused
    swapped,     // another row held it; that row received the old value
    rejected,    // value is held by a non-row word (e.g. a mirror); nothing changes
};
static_assert(sizeof(BindingEdit)==1U);

// assign_player_slot_value (func_800D6274), including the 10->12 and 11->13 mirrors.
BindingEdit assign_binding(BindingWords& words,std::int32_t preset,std::int32_t row,std::uint32_t value) noexcept;
// TEMPORARY POLICY: next/previous assignable value that does not get rejected.
BindingEdit cycle_binding(BindingWords& words,std::int32_t preset,std::int32_t row,std::int32_t direction) noexcept;
// get_entity_record_68 copy; returns false (words untouched) for a bad preset.
bool reset_to_preset(BindingWords& words,std::int32_t preset) noexcept;

struct ControlsSetupState {
    std::int32_t row=0;              // D_80328290[player]
    std::int32_t preset_selected=0;  // D_803282A0[player]
    std::int32_t preset_saved=0;     // D_803283B8[player]; stored as record control_scheme
    bool help_visible=false;         // D_80327614
    bool edited=false;               // D_8012579D (player 0 only)
    BindingWords words=preset_bindings[0]; // D_803282A8[player]
};

struct ControlsInput {
    std::uint32_t menu_pressed=0U; // menu_bits, read with C buttons ignored
    std::uint32_t raw_pressed=0U;  // raw controller pressed bits
};

enum class ControlsResult : std::uint8_t { pending=0U, done };
static_assert(sizeof(ControlsResult)==1U);

[[nodiscard]] ControlsSetupState make_controls_setup(std::int32_t preset);
// Single-player step of update_player_slot_configuration (campaign = player 0).
ControlsResult update_controls_setup(ControlsSetupState& state,ControlsInput input);

// CampaignRecord round trip: bindings <-> 0x24..0x67, preset <-> control_scheme (0x19).
void store_controls_in_record(const ControlsSetupState& state,CampaignRecord& record) noexcept;
// run_game_setup_screen "Load Game": record bindings replace the live ones
// only when the player has not edited them this session. Returns whether loaded.
bool load_controls_from_record(ControlsSetupState& state,const CampaignRecord& record) noexcept;

// ---------------------------------------------------------------- gangs

inline constexpr std::int32_t gang_count=10;           // D_80134010 wraps 0..9
inline constexpr std::int32_t locked_gang_index=1;     // skipped while D_80135773==0
inline constexpr std::int32_t gang_slot_count=4;
inline constexpr std::int32_t absent_team=4;           // D_80328400 value for an unused slot
inline constexpr std::int32_t self_target=-2;          // D_80327F3C "editing own slot"
// D_80134A80 (ROM 0x0C4A80): colour index stored in D_80134020 per gang.
inline constexpr std::array<std::int32_t,gang_count> gang_colour{0,2,3,4,7,1,5,6,8,9};

struct GangSelectState {
    std::int32_t human_count=1;                         // D_80134E58
    std::array<std::int32_t,gang_slot_count> team{0,absent_team,absent_team,absent_team}; // D_80328400
    std::array<std::int32_t,gang_slot_count> gang{};    // D_80134010
    std::array<std::int32_t,gang_slot_count> panel_direction{-1,-1,-1,-1}; // D_80327F10 sign
    std::array<bool,gang_slot_count> ready{};           // D_80327F20
    std::array<std::int32_t,gang_slot_count> colour{};  // D_80134020, set on done
    std::int32_t target=self_target;                    // D_80327F3C
    bool help_visible=false;
    bool locked_available=false;                        // D_80135773
};

enum class GangSelectResult : std::uint8_t { pending=0U, done, back };
static_assert(sizeof(GangSelectResult)==1U);

[[nodiscard]] GangSelectState make_gang_select(std::int32_t human_count,bool locked_available);
// load_resource_bundle_3dc430: computer slots draw a gang from `draws`
// (one RetailRandom u32 per slot index >= human_count, caller supplied).
void assign_computer_gangs(GangSelectState& state,const std::array<std::uint32_t,gang_slot_count>& draws) noexcept;
// Gang step for a delta of -1/0/+1 with the locked-index rule.
[[nodiscard]] std::int32_t step_gang(std::int32_t gang,std::int32_t delta,bool locked_available) noexcept;
// pressed[i] = menu bits for human player i (index 0 drives help/cancel/target).
GangSelectResult update_gang_select(GangSelectState& state,const std::array<std::uint32_t,gang_slot_count>& pressed);

// ---------------------------------------------------------------- main setup menu

// draw_main_menu_screen / update_game_setup_menu_input rows (D_80134E54).
enum class SetupRow : std::uint8_t {
    players=0U, playmode=1U, options=2U, credits=3U, input_code=4U, load_game=5U, start_game=6U,
};
static_assert(sizeof(SetupRow)==1U);
inline constexpr std::int32_t setup_row_count=7;
inline constexpr std::int32_t playmode_count=5;    // D_80134E5C 0..4
inline constexpr std::int32_t campaign_playmode=0; // STRONG INFERENCE: start with mode 0 runs the campaign path
// Visual order (draw y positions) differs from cursor order: credits is drawn between options and input code.
inline constexpr std::array<SetupRow,setup_row_count> setup_rows{
    SetupRow::players,SetupRow::playmode,SetupRow::options,SetupRow::credits,
    SetupRow::input_code,SetupRow::load_game,SetupRow::start_game};

struct MainMenuState {
    std::int32_t cursor=static_cast<std::int32_t>(SetupRow::start_game);
    std::int32_t players=1;          // D_80134E58
    std::int32_t controller_count=1; // D_80126714
    std::int32_t playmode=0;         // D_80134E5C
    bool save_enabled=false;         // D_80134E74
    bool help_visible=false;
};

struct MainMenuStep {
    bool accepted=false;  // menu loop exits; `row` is the accepted row
    SetupRow row=SetupRow::start_game;
    bool cursor_sound=false;
    bool value_sound=false;
    bool accept_sound=false;
};

[[nodiscard]] MainMenuState make_main_menu(std::int32_t controller_count,bool save_enabled);
[[nodiscard]] bool setup_row_enabled(const MainMenuState& state,SetupRow row) noexcept;
MainMenuStep update_main_menu(MainMenuState& state,std::uint32_t pressed);

// ---------------------------------------------------------------- draw

enum class SetupItem : std::uint8_t {
    none=0U,
    menu_row,         // value = SetupRow, aux = players/playmode value for rows 0/1
    binding_row,      // value = row, aux = bound value, icon = binding_icon
    binding_blank,    // row >= preset row count and < 9
    preset_row,       // value = preset_selected
    done_row,
    gang_slot,        // value = slot, aux = gang
    help_overlay,
};
static_assert(sizeof(SetupItem)==1U);

struct SetupDrawCommand {
    SetupItem item=SetupItem::none;
    std::int32_t value=0;
    std::int32_t aux=0;
    std::uint8_t icon=0U;
    bool selected=false;
    bool enabled=true;
    bool ready=false;
};

[[nodiscard]] std::vector<SetupDrawCommand> draw_main_menu(const MainMenuState& state);
[[nodiscard]] std::vector<SetupDrawCommand> draw_controls_setup(const ControlsSetupState& state);
[[nodiscard]] std::vector<SetupDrawCommand> draw_gang_select(const GangSelectState& state);

void serialize(StateWriter& writer,const ControlsSetupState& state);
void serialize(StateWriter& writer,const GangSelectState& state);
void serialize(StateWriter& writer,const MainMenuState& state);

} // namespace battletanx::frontend_setup
