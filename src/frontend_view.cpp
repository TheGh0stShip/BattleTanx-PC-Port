#include "battletanx/frontend_view.hpp"

#include "battletanx/frontend_setup_menus.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace battletanx::frontend {
namespace {

std::string number(std::int32_t value) { return std::to_string(value); }

std::string_view on_off(std::int32_t value) noexcept { return value==0 ? "OFF" : "ON"; }

std::string_view difficulty(std::int32_t value) noexcept {
    constexpr std::array<std::string_view,3> names{"EASY","NORMAL","HARD"};
    return value>=0 && value<static_cast<std::int32_t>(names.size()) ?
        names[static_cast<std::size_t>(value)] : names[0];
}

std::string_view setup_label(std::int32_t value) noexcept {
    using frontend_setup::SetupRow;
    switch (static_cast<SetupRow>(value)) {
    case SetupRow::players: return "PLAYERS";
    case SetupRow::playmode: return "PLAY MODE";
    case SetupRow::options: return "OPTIONS";
    case SetupRow::credits: return "CREDITS";
    case SetupRow::input_code: return "INPUT CODE";
    case SetupRow::load_game: return "LOAD GAME";
    case SetupRow::start_game: return "START GAME";
    default: return "";
    }
}

std::string setup_value(const FrontendState& state,std::int32_t value) {
    using frontend_setup::SetupRow;
    switch (static_cast<SetupRow>(value)) {
    case SetupRow::players: return number(state.setup_menu.players);
    case SetupRow::playmode:
        return state.setup_menu.playmode==frontend_setup::campaign_playmode ?
            "CAMPAIGN" : "UNAVAILABLE";
    default: return {};
    }
}

std::string binding_name(std::uint32_t value) {
    switch (value) {
    case 0x0001U: return "C RIGHT";
    case 0x0002U: return "C LEFT";
    case 0x0004U: return "C DOWN";
    case 0x0008U: return "C UP";
    case 0x0010U: return "R";
    case 0x0020U: return "L";
    case 0x2000U: return "Z";
    case 0x4000U: return "B";
    case 0x8000U: return "A";
    default: return "UNBOUND";
    }
}

std::string binding_value(const FrontendState& state,std::int32_t row) {
    const auto word=frontend_setup::binding_word_for_row(
        state.controls.words,state.controls.preset_saved,row);
    return word ? binding_name(state.controls.words[*word]) : std::string{};
}

std::string_view title(Screen screen) noexcept {
    switch (screen) {
    case Screen::title: return "BATTLETANX";
    case Screen::pak_menu: return "CONTROLLER PAK";
    case Screen::campaign_menu: return "GAME SETUP";
    case Screen::difficulty: return "DIFFICULTY";
    case Screen::unit_select: return "SELECT LINEUP";
    case Screen::controller_preset: return "CONTROLLER SETUP";
    case Screen::options: return "OPTIONS";
    case Screen::pause: return "PAUSED";
    case Screen::result: return "RESULT";
    case Screen::in_mission:
    default: return "";
    }
}

MenuRowView row_for(const FrontendState& state,const DrawCommand& command) {
    MenuRowView row{.label={},.value={},
                    .selected=command.selected,.enabled=command.enabled};
    switch (command.item) {
    case ItemId::title_start: row.label="START"; break;
    case ItemId::title_attract: row.label="ATTRACT MODE"; break;
    case ItemId::pak_retry: row.label="RETRY"; break;
    case ItemId::pak_continue_without_save: row.label="CONTINUE WITHOUT SAVING"; break;
    case ItemId::pak_repair: row.label="REPAIR PAK"; break;
    case ItemId::pak_delete_note: row.label="DELETE NOTE"; break;
    case ItemId::setup_row:
        row.label=setup_label(command.value);
        row.value=setup_value(state,command.value);
        break;
    case ItemId::difficulty_easy: row.label="EASY"; break;
    case ItemId::difficulty_normal: row.label="NORMAL"; break;
    case ItemId::difficulty_hard: row.label="HARD"; break;
    case ItemId::unit_choice:
        row.label="LINEUP";
        row.value=number(command.value+1);
        break;
    case ItemId::binding_row:
        row.label="CONTROL "+number(command.value+1);
        row.value=binding_value(state,command.value);
        break;
    case ItemId::preset_choice:
        row.label="PRESET";
        row.value=number(command.value+1);
        break;
    case ItemId::controls_done: row.label="DONE"; break;
    case ItemId::option_sfx_volume:
        row.label="SOUND VOLUME"; row.value=number(command.value); break;
    case ItemId::option_music_volume:
        row.label="MUSIC VOLUME"; row.value=number(command.value); break;
    case ItemId::option_difficulty:
        row.label="DIFFICULTY"; row.value=difficulty(command.value); break;
    case ItemId::option_powerups:
        row.label="POWERUPS"; row.value=on_off(command.value); break;
    case ItemId::option_powerup_regen:
        row.label="POWERUP REGEN"; row.value=on_off(command.value); break;
    case ItemId::option_control_scheme:
        row.label="CONTROL SCHEME"; row.value=number(command.value+1); break;
    case ItemId::option_unlimited_ammo:
        row.label="UNLIMITED AMMO"; row.value=on_off(command.value); break;
    case ItemId::option_exit: row.label="EXIT"; break;
    case ItemId::pause_continue: row.label="CONTINUE"; break;
    case ItemId::pause_restart: row.label="RESTART"; break;
    case ItemId::pause_quit: row.label="QUIT"; break;
    case ItemId::result_retry: row.label="RETRY"; break;
    case ItemId::result_continue: row.label="CONTINUE"; break;
    case ItemId::result_quit: row.label="QUIT"; break;
    case ItemId::result_save_continue: row.label="SAVE AND CONTINUE"; break;
    case ItemId::help_overlay:
    case ItemId::unsupported_notice:
    case ItemId::campaign_new:
    case ItemId::campaign_continue:
    case ItemId::campaign_options:
    case ItemId::none:
    default: break;
    }
    return row;
}

} // namespace

MenuView make_menu_view(const FrontendState& state) {
    MenuView view;
    view.title=title(state.screen);
    for (const DrawCommand& command:frontend_draw(state)) {
        if (command.item==ItemId::help_overlay) {
            view.footer="USE THE D-PAD TO CHOOSE. A SELECTS. B GOES BACK.";
            continue;
        }
        MenuRowView row=row_for(state,command);
        if (!row.label.empty()) view.rows.push_back(std::move(row));
    }
    if (state.screen==Screen::controller_preset && view.footer.empty())
        view.footer="PRESS A CONTROLLER BUTTON TO REBIND THE SELECTED CONTROL.";
    return view;
}

} // namespace battletanx::frontend
