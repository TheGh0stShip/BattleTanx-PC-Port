#include "battletanx/frontend_setup_menus.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;
using namespace battletanx::frontend_setup;

namespace {

ControlsResult menu(ControlsSetupState& state,std::uint32_t bits) {
    return update_controls_setup(state,ControlsInput{.menu_pressed=bits,.raw_pressed=0U});
}

GangSelectResult gang_press(GangSelectState& state,std::uint32_t bits) {
    return update_gang_select(state,std::array<std::uint32_t,gang_slot_count>{bits,0U,0U,0U});
}

} // namespace

RUNTIME_TEST(binding_rows_skip_non_row_words) {
    const BindingWords& words=preset_bindings[2];
    REQUIRE(binding_word_for_row(words,2,0)==std::optional<std::uint8_t>{std::uint8_t{0U}});
    REQUIRE(binding_word_for_row(words,2,3)==std::optional<std::uint8_t>{std::uint8_t{4U}}); // word 3 is >= threshold
    REQUIRE(binding_word_for_row(words,2,8)==std::optional<std::uint8_t>{std::uint8_t{11U}});
    REQUIRE(!binding_word_for_row(words,2,9).has_value());
    REQUIRE(!binding_word_for_row(preset_bindings[0],0,8).has_value()); // preset 0 has 8 rows
    REQUIRE(!binding_word_for_row(words,7,0).has_value());
    return true;
}

RUNTIME_TEST(binding_assign_swaps_rejects_and_mirrors) {
    BindingWords words=preset_bindings[2];
    // Row 0 (word 0 = 0x2000) takes 0x8000, held by row 1 -> swap.
    REQUIRE(assign_binding(words,2,0,0x8000U)==BindingEdit::swapped);
    REQUIRE(words[0]==0x8000U && words[1]==0x2000U);
    // Row 7 edits word 10 and mirrors into word 12.
    REQUIRE(assign_binding(words,2,7,0x4000U)==BindingEdit::swapped);
    REQUIRE(words[10]==0x4000U && words[12]==0x4000U && words[2]==0x1U);
    // Non-assignable value.
    REQUIRE(assign_binding(words,2,0,0x1000U)==BindingEdit::invalid);
    // Preset 0 shows 8 rows (words 0..5,12,13); a value held only past the
    // last shown row is rejected and nothing changes.
    BindingWords zero=preset_bindings[0];
    zero[16]=0x10U;
    const BindingWords before=zero;
    REQUIRE(assign_binding(zero,0,0,0x10U)==BindingEdit::rejected);
    REQUIRE(zero==before);
    BindingWords fresh=preset_bindings[0];
    REQUIRE(assign_binding(fresh,0,0,0x10U)==BindingEdit::assigned);
    REQUIRE(fresh[0]==0x10U);
    return true;
}

RUNTIME_TEST(binding_icons_follow_bit_table) {
    REQUIRE(binding_icon(0U)==0U);
    REQUIRE(binding_icon(0x1U)==8U);
    REQUIRE(binding_icon(0x8000U)==2U);
    REQUIRE(binding_value_from_raw(0x8030U)==0x10U);
    REQUIRE(binding_value_from_raw(0x1000U)==0U);
    return true;
}

RUNTIME_TEST(controls_rows_clamp_and_finish) {
    ControlsSetupState state=make_controls_setup(1);
    REQUIRE(menu(state,menu_bits::up)==ControlsResult::pending);
    REQUIRE(state.row==0);
    for (int i=0; i<20; ++i) (void)menu(state,menu_bits::down);
    REQUIRE(state.row==done_row);
    REQUIRE(menu(state,menu_bits::confirm)==ControlsResult::done);
    return true;
}

RUNTIME_TEST(controls_preset_row_wraps_and_resets) {
    ControlsSetupState state=make_controls_setup(0);
    state.row=preset_row;
    (void)menu(state,menu_bits::left);
    REQUIRE(state.preset_saved==3 && state.words==preset_bindings[3]);
    (void)menu(state,menu_bits::right);
    REQUIRE(state.preset_saved==0 && state.edited);
    // Edit row 0 then reset via confirm on the preset row.
    state.row=0;
    (void)update_controls_setup(state,ControlsInput{.menu_pressed=0U,.raw_pressed=0x8000U});
    REQUIRE(state.words[0]==0x8000U);
    state.row=preset_row;
    (void)menu(state,menu_bits::confirm);
    REQUIRE(state.words==preset_bindings[0]);
    return true;
}

RUNTIME_TEST(controls_help_only_from_row_eight) {
    ControlsSetupState state=make_controls_setup(0);
    (void)menu(state,menu_bits::help);
    REQUIRE(!state.help_visible);
    state.row=help_min_row;
    (void)menu(state,menu_bits::help);
    REQUIRE(state.help_visible);
    (void)menu(state,menu_bits::down);
    REQUIRE(state.row==help_min_row); // frozen while help shows
    (void)menu(state,menu_bits::help);
    REQUIRE(!state.help_visible);
    return true;
}

RUNTIME_TEST(controls_round_trip_through_campaign_record) {
    ControlsSetupState state=make_controls_setup(2);
    REQUIRE(cycle_binding(state.words,2,0,1)!=BindingEdit::invalid);
    const BindingWords edited=state.words;
    CampaignRecord record=default_campaign_record();
    store_controls_in_record(state,record);
    REQUIRE(record.control_scheme==2U);
    const CampaignRecord parsed=parse_campaign_record(serialize_campaign_record(record)).value();
    ControlsSetupState loaded=make_controls_setup(0);
    REQUIRE(load_controls_from_record(loaded,parsed));
    REQUIRE(loaded.words==edited && loaded.preset_saved==2);
    loaded.edited=true;
    REQUIRE(!load_controls_from_record(loaded,record)); // session edits win
    return true;
}

RUNTIME_TEST(gang_select_skips_locked_index) {
    REQUIRE(step_gang(0,1,false)==2);
    REQUIRE(step_gang(2,-1,false)==0);
    REQUIRE(step_gang(0,1,true)==1);
    REQUIRE(step_gang(0,-1,false)==9);
    REQUIRE(step_gang(9,1,false)==0);
    GangSelectState state=make_gang_select(1,false);
    (void)gang_press(state,menu_bits::right);
    REQUIRE(state.gang[0]==2);
    return true;
}

RUNTIME_TEST(gang_select_targets_computer_slots_and_finishes) {
    GangSelectState state=make_gang_select(1,false);
    state.team={0,1,absent_team,2};
    (void)gang_press(state,menu_bits::next_target);
    REQUIRE(state.target==1);
    (void)gang_press(state,menu_bits::next_target);
    REQUIRE(state.target==3); // slot 2 absent
    (void)gang_press(state,menu_bits::next_target);
    REQUIRE(state.target==self_target);
    (void)gang_press(state,menu_bits::prev_target);
    REQUIRE(state.target==3);
    REQUIRE(gang_press(state,menu_bits::confirm)==GangSelectResult::done);
    REQUIRE(state.colour[0]==gang_colour[static_cast<std::size_t>(state.gang[0])]);
    REQUIRE(gang_press(state,menu_bits::cancel)==GangSelectResult::back);
    return true;
}

RUNTIME_TEST(computer_gangs_avoid_locked_index) {
    GangSelectState state=make_gang_select(1,false);
    assign_computer_gangs(state,{0U,1U,9U,18U});
    REQUIRE(state.gang[1]==2 && state.gang[2]==0 && state.gang[3]==0);
    for (std::size_t i=1U; i<4U; ++i) REQUIRE(state.gang[i]!=locked_gang_index);
    return true;
}

RUNTIME_TEST(main_menu_wraps_and_skips_load_game) {
    MainMenuState state=make_main_menu(1,false);
    (void)update_main_menu(state,menu_bits::up);
    REQUIRE(state.cursor==4); // load game disabled
    (void)update_main_menu(state,menu_bits::down);
    REQUIRE(state.cursor==6);
    (void)update_main_menu(state,menu_bits::down);
    REQUIRE(state.cursor==0);
    REQUIRE(!update_main_menu(state,menu_bits::confirm).accepted); // rows 0..1 do not accept
    const MainMenuStep step=update_main_menu(state,menu_bits::alt_confirm);
    REQUIRE(step.accepted && step.row==SetupRow::start_game);
    MainMenuState saved=make_main_menu(2,true);
    (void)update_main_menu(saved,menu_bits::up);
    REQUIRE(saved.cursor==5);
    saved.cursor=0;
    (void)update_main_menu(saved,menu_bits::right);
    REQUIRE(saved.players==2 && saved.playmode==1);
    REQUIRE(!setup_row_enabled(saved,SetupRow::load_game));
    return true;
}

RUNTIME_TEST(setup_draw_and_serialize_are_value_only) {
    const ControlsSetupState controls=make_controls_setup(0);
    const std::vector<SetupDrawCommand> rows=draw_controls_setup(controls);
    REQUIRE(rows.size()==11U);
    REQUIRE(rows[8].item==SetupItem::binding_blank && rows[9].item==SetupItem::preset_row);
    REQUIRE(draw_main_menu(make_main_menu(1,false)).size()==7U);
    StateWriter a;
    StateWriter b;
    serialize(a,controls);
    serialize(b,make_controls_setup(0));
    REQUIRE(a.bytes()==b.bytes());
    StateWriter g;
    serialize(g,make_gang_select(1,false));
    REQUIRE(!g.bytes().empty());
    return true;
}

RUNTIME_TEST_MAIN()
