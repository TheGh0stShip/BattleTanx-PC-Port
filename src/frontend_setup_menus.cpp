#include "battletanx/frontend_setup_menus.hpp"

namespace battletanx::frontend_setup {
namespace {

bool valid_preset(std::int32_t preset) noexcept { return preset>=0 && preset<preset_count; }

bool assignable(std::uint32_t value) noexcept {
    for (std::uint32_t candidate : assignable_values) if (candidate==value) return true;
    return false;
}

// find_slot in assign_player_slot_value: index of the n-th row word, or 17.
std::size_t find_slot(const BindingWords& words,std::int32_t n) noexcept {
    std::size_t index=0U;
    for (; index<binding_words; ++index) {
        if (words[index]<non_row_threshold) {
            if (--n==-1) break;
        }
    }
    return index;
}

void write_words(StateWriter& writer,const BindingWords& words) {
    for (std::uint32_t word : words) writer.u32(word);
}

} // namespace

std::int32_t binding_row_count(std::int32_t preset) noexcept {
    return valid_preset(preset) ? preset_row_counts[static_cast<std::size_t>(preset)] : 0;
}

std::optional<std::uint8_t> binding_word_for_row(const BindingWords& words,std::int32_t preset,
                                                 std::int32_t row) noexcept {
    if (row<0 || row>=binding_row_count(preset)) return std::nullopt;
    const std::size_t slot=find_slot(words,row);
    if (slot>=binding_words) return std::nullopt;
    return static_cast<std::uint8_t>(slot);
}

std::uint32_t binding_value_from_raw(std::uint32_t raw_pressed) noexcept {
    const std::uint32_t bits=raw_pressed&assignable_mask;
    return bits & (~bits+1U);
}

std::uint8_t binding_icon(std::uint32_t value) noexcept {
    if (value==0U) return binding_icon_by_bit[0];
    std::size_t bit=1U;
    while ((value&1U)==0U) {
        value>>=1U;
        ++bit;
    }
    return bit<binding_icon_by_bit.size() ? binding_icon_by_bit[bit] : std::uint8_t{0U};
}

BindingEdit assign_binding(BindingWords& words,std::int32_t preset,std::int32_t row,std::uint32_t value) noexcept {
    if (!assignable(value)) return BindingEdit::invalid;
    const std::optional<std::uint8_t> slot=binding_word_for_row(words,preset,row);
    if (!slot) return BindingEdit::invalid;
    const std::uint32_t old=words[*slot];
    std::int32_t other=-1;
    const std::int32_t rows=binding_row_count(preset);
    for (std::int32_t i=0; i<rows; ++i) {
        const std::size_t index=find_slot(words,i);
        if (index<binding_words && words[index]==value) {
            other=static_cast<std::int32_t>(index);
            break;
        }
    }
    if (other==-1) {
        for (std::uint32_t word : words) if (word==value) return BindingEdit::rejected;
    }
    words[*slot]=value;
    if (other>=0) words[static_cast<std::size_t>(other)]=old;
    if (*slot==10U) words[12]=value;
    if (*slot==11U) words[13]=value;
    return other>=0 && other!=static_cast<std::int32_t>(*slot) ? BindingEdit::swapped : BindingEdit::assigned;
}

BindingEdit cycle_binding(BindingWords& words,std::int32_t preset,std::int32_t row,std::int32_t direction) noexcept {
    const std::optional<std::uint8_t> slot=binding_word_for_row(words,preset,row);
    if (!slot || direction==0) return BindingEdit::invalid;
    const std::int32_t count=static_cast<std::int32_t>(assignable_values.size());
    std::int32_t position=-1;
    for (std::int32_t i=0; i<count; ++i) if (assignable_values[static_cast<std::size_t>(i)]==words[*slot]) position=i;
    if (position<0) position=direction>0 ? -1 : count;
    const std::int32_t step=direction>0 ? 1 : -1;
    for (std::int32_t tries=0; tries<count; ++tries) {
        position=(position+step+count)%count;
        const std::uint32_t value=assignable_values[static_cast<std::size_t>(position)];
        if (value==words[*slot]) continue;
        BindingWords trial=words;
        const BindingEdit edit=assign_binding(trial,preset,row,value);
        if (edit==BindingEdit::assigned || edit==BindingEdit::swapped) {
            words=trial;
            return edit;
        }
    }
    return BindingEdit::rejected;
}

bool reset_to_preset(BindingWords& words,std::int32_t preset) noexcept {
    if (!valid_preset(preset)) return false;
    words=preset_bindings[static_cast<std::size_t>(preset)];
    return true;
}

ControlsSetupState make_controls_setup(std::int32_t preset) {
    ControlsSetupState state;
    if (!valid_preset(preset)) preset=0;
    state.preset_selected=preset;
    state.preset_saved=preset;
    (void)reset_to_preset(state.words,preset);
    return state;
}

ControlsResult update_controls_setup(ControlsSetupState& state,ControlsInput input) {
    const std::uint32_t menu=input.menu_pressed;
    if (state.help_visible) {
        if ((menu&menu_bits::help)!=0U) state.help_visible=false;
        return ControlsResult::pending;
    }
    if (state.row<binding_row_count(state.preset_saved)) {
        const std::uint32_t value=binding_value_from_raw(input.raw_pressed);
        if (value!=0U) {
            (void)assign_binding(state.words,state.preset_saved,state.row,value);
            state.edited=true;
        }
    }
    if (state.row>=help_min_row && (menu&menu_bits::help)!=0U) state.help_visible=!state.help_visible;
    if (state.row==preset_row) {
        if ((menu&menu_bits::right)!=0U) {
            state.edited=true;
            state.preset_selected=(state.preset_selected+1)%preset_count;
            state.preset_saved=state.preset_selected;
            (void)reset_to_preset(state.words,state.preset_saved);
        }
        if ((menu&menu_bits::left)!=0U) {
            state.edited=true;
            state.preset_selected=(state.preset_selected+3)%preset_count;
            state.preset_saved=state.preset_selected;
            (void)reset_to_preset(state.words,state.preset_saved);
        }
        if ((menu&menu_bits::confirm)!=0U) {
            state.edited=true;
            state.preset_saved=state.preset_selected;
            (void)reset_to_preset(state.words,state.preset_saved);
        }
    }
    std::int32_t delta=0;
    if ((menu&menu_bits::up)!=0U) delta=-1;
    if ((menu&menu_bits::down)!=0U) ++delta;
    state.row+=delta;
    if (state.row<0) state.row=0;
    if (state.row>binding_row_last) state.row=binding_row_last;
    if (state.row!=preset_row) state.preset_selected=state.preset_saved;
    const bool ready=state.row==done_row;
    return ready && (menu&(menu_bits::confirm|menu_bits::cancel))!=0U ? ControlsResult::done : ControlsResult::pending;
}

void store_controls_in_record(const ControlsSetupState& state,CampaignRecord& record) noexcept {
    for (std::size_t i=0; i<binding_words; ++i) record.bindings[i]=state.words[i];
    record.control_scheme=static_cast<std::uint8_t>(state.preset_saved);
}

bool load_controls_from_record(ControlsSetupState& state,const CampaignRecord& record) noexcept {
    if (state.edited) return false;
    // UNRESOLVED: retail does not range-check value19; out-of-range presets are refused here.
    if (!valid_preset(static_cast<std::int32_t>(record.control_scheme))) return false;
    state.preset_saved=static_cast<std::int32_t>(record.control_scheme);
    // TEMPORARY POLICY: retail (run_game_setup_screen.c:126-127) writes only the
    // saved preset and bindings; selected is set too, which update would copy anyway.
    state.preset_selected=state.preset_saved;
    for (std::size_t i=0; i<binding_words; ++i) state.words[i]=record.bindings[i];
    return true;
}

GangSelectState make_gang_select(std::int32_t human_count,bool locked_available) {
    GangSelectState state;
    if (human_count<1) human_count=1;
    if (human_count>gang_slot_count) human_count=gang_slot_count;
    state.human_count=human_count;
    state.locked_available=locked_available;
    for (std::int32_t i=0; i<human_count; ++i) state.team[static_cast<std::size_t>(i)]=0;
    return state;
}

void assign_computer_gangs(GangSelectState& state,const std::array<std::uint32_t,gang_slot_count>& draws) noexcept {
    const std::uint32_t range=state.locked_available ? 10U : 9U;
    for (std::int32_t i=state.human_count; i<gang_slot_count; ++i) {
        const std::size_t slot=static_cast<std::size_t>(i);
        std::int32_t pick=static_cast<std::int32_t>(draws[slot]%range);
        if (!state.locked_available && pick>0) pick+=1;
        state.gang[slot]=pick;
    }
    state.ready={};
}

std::int32_t step_gang(std::int32_t gang,std::int32_t delta,bool locked_available) noexcept {
    std::int32_t value=gang+delta;
    if (!locked_available && value==locked_gang_index) value=delta+1;
    if (value<0) value=gang_count-1;
    if (value>=gang_count) value=0;
    return value;
}

GangSelectResult update_gang_select(GangSelectState& state,const std::array<std::uint32_t,gang_slot_count>& pressed) {
    if ((pressed[0]&menu_bits::help)!=0U) state.help_visible=!state.help_visible;
    if (state.help_visible) return GangSelectResult::pending;
    for (std::int32_t player=0; player<state.human_count && player<gang_slot_count; ++player) {
        const std::uint32_t bits=pressed[static_cast<std::size_t>(player)];
        if (!state.ready[static_cast<std::size_t>(player)]) {
            if (player==0) {
                // Bounded form of the retail do/while; it always terminates at self_target.
                for (std::int32_t guard=0; guard<16; ++guard) {
                    if ((bits&menu_bits::prev_target)!=0U) --state.target;
                    if ((bits&menu_bits::next_target)!=0U) ++state.target;
                    if (state.target<self_target) state.target=gang_slot_count-1;
                    if (state.target==-1) state.target=state.human_count;
                    if (state.target<state.human_count || state.target==gang_slot_count) state.target=self_target;
                    if (state.target==self_target || state.team[static_cast<std::size_t>(state.target)]!=absent_team) break;
                    if (guard==15) state.target=self_target;
                }
            }
            std::int32_t target=player;
            if (player==0 && state.target!=self_target) target=state.target;
            const std::size_t slot=static_cast<std::size_t>(target);
            if ((bits&menu_bits::up)!=0U) state.panel_direction[slot]=-state.panel_direction[slot];
            if ((bits&menu_bits::down)!=0U) state.panel_direction[slot]=-state.panel_direction[slot];
            std::int32_t delta=0;
            if ((bits&menu_bits::left)!=0U) --delta;
            if ((bits&menu_bits::right)!=0U) ++delta;
            state.gang[slot]=step_gang(state.gang[slot],delta,state.locked_available);
        }
        if ((bits&menu_bits::confirm)!=0U) state.ready[static_cast<std::size_t>(player)]=!state.ready[static_cast<std::size_t>(player)];
        if (player==0 && (bits&menu_bits::cancel)!=0U) return GangSelectResult::back;
    }
    for (std::int32_t player=0; player<state.human_count; ++player) {
        if (!state.ready[static_cast<std::size_t>(player)]) return GangSelectResult::pending;
    }
    // run_bundle_3DC430_screen_loop: colour per slot from the chosen gang.
    for (std::size_t i=0; i<state.colour.size(); ++i) {
        const std::int32_t gang=state.gang[i];
        state.colour[i]=gang>=0 && gang<gang_count ? gang_colour[static_cast<std::size_t>(gang)] : 0;
    }
    return GangSelectResult::done;
}

MainMenuState make_main_menu(std::int32_t controller_count,bool save_enabled) {
    MainMenuState state;
    state.controller_count=controller_count<1 ? 1 : (controller_count>4 ? 4 : controller_count);
    state.save_enabled=save_enabled;
    return state;
}

bool setup_row_enabled(const MainMenuState& state,SetupRow row) noexcept {
    if (row==SetupRow::load_game) return state.save_enabled && state.playmode==0;
    return true;
}

MainMenuStep update_main_menu(MainMenuState& state,std::uint32_t pressed) {
    MainMenuStep step;
    if ((pressed&menu_bits::help)!=0U) state.help_visible=!state.help_visible;
    if (state.help_visible) return step;
    std::int32_t delta=0;
    if ((pressed&menu_bits::up)!=0U) {
        delta=-1;
        step.cursor_sound=true;
    }
    if ((pressed&menu_bits::down)!=0U) {
        ++delta;
        step.cursor_sound=true;
    }
    state.cursor+=delta;
    if (!setup_row_enabled(state,SetupRow::load_game) && state.cursor==5) state.cursor=delta+5;
    if (state.cursor<0) state.cursor=setup_row_count-1;
    if (state.cursor>=setup_row_count) state.cursor=0;
    delta=0;
    if ((pressed&menu_bits::left)!=0U) --delta;
    if ((pressed&menu_bits::right)!=0U) ++delta;
    if (state.cursor==0 && delta!=0) {
        step.value_sound=true;
        state.players+=delta;
        if (state.players<=0) state.players=state.controller_count;
        if (state.players>state.controller_count) state.players=1;
        if (state.players>=2 && state.playmode<=0) state.playmode=1;
    } else if (state.cursor==1) {
        if (delta!=0) step.value_sound=true;
        state.playmode+=delta;
        if (state.players>=2) {
            if (state.playmode>=playmode_count) state.playmode=1;
            if (state.playmode<=0) state.playmode=playmode_count-1;
        } else {
            if (state.playmode>=playmode_count) state.playmode=0;
            if (state.playmode<0) state.playmode=playmode_count-1;
        }
    }
    if ((pressed&menu_bits::alt_confirm)!=0U) {
        state.cursor=static_cast<std::int32_t>(SetupRow::start_game);
        step.accepted=true;
        step.accept_sound=true;
    } else if (state.cursor>=2 && (pressed&menu_bits::confirm)!=0U) {
        step.accepted=true;
        step.accept_sound=true;
    }
    step.row=static_cast<SetupRow>(state.cursor);
    return step;
}

std::vector<SetupDrawCommand> draw_main_menu(const MainMenuState& state) {
    std::vector<SetupDrawCommand> commands;
    for (SetupRow row : setup_rows) {
        const std::int32_t index=static_cast<std::int32_t>(row);
        std::int32_t aux=0;
        if (row==SetupRow::players) aux=state.players;
        if (row==SetupRow::playmode) aux=state.playmode;
        commands.push_back(SetupDrawCommand{.item=SetupItem::menu_row,.value=index,.aux=aux,.icon=0U,
                                            .selected=state.cursor==index,.enabled=setup_row_enabled(state,row),.ready=false});
    }
    if (state.help_visible) commands.push_back(SetupDrawCommand{.item=SetupItem::help_overlay,.value=0,.aux=0,.icon=0U,.selected=false,.enabled=true,.ready=false});
    return commands;
}

std::vector<SetupDrawCommand> draw_controls_setup(const ControlsSetupState& state) {
    std::vector<SetupDrawCommand> commands;
    const std::int32_t rows=binding_row_count(state.preset_saved);
    for (std::int32_t row=0; row<preset_row; ++row) {
        const std::optional<std::uint8_t> slot=binding_word_for_row(state.words,state.preset_saved,row);
        if (slot) {
            const std::uint32_t value=state.words[*slot];
            commands.push_back(SetupDrawCommand{.item=SetupItem::binding_row,.value=row,
                                                .aux=static_cast<std::int32_t>(value),.icon=binding_icon(value),
                                                .selected=state.row==row,.enabled=true,.ready=false});
        } else {
            commands.push_back(SetupDrawCommand{.item=SetupItem::binding_blank,.value=row,.aux=0,.icon=0U,
                                                .selected=state.row==row,.enabled=row<rows,.ready=false});
        }
    }
    commands.push_back(SetupDrawCommand{.item=SetupItem::preset_row,.value=state.preset_selected,
                                        .aux=state.preset_saved,.icon=0U,.selected=state.row==preset_row,.enabled=true,.ready=false});
    commands.push_back(SetupDrawCommand{.item=SetupItem::done_row,.value=done_row,.aux=0,.icon=0U,.selected=state.row==done_row,.enabled=true,.ready=false});
    if (state.help_visible) commands.push_back(SetupDrawCommand{.item=SetupItem::help_overlay,.value=0,.aux=0,.icon=0U,.selected=false,.enabled=true,.ready=false});
    return commands;
}

std::vector<SetupDrawCommand> draw_gang_select(const GangSelectState& state) {
    std::vector<SetupDrawCommand> commands;
    for (std::int32_t i=0; i<gang_slot_count; ++i) {
        const std::size_t slot=static_cast<std::size_t>(i);
        if (state.team[slot]==absent_team) continue;
        const bool human=i<state.human_count;
        const bool ready=human ? state.ready[slot] : state.ready[0];
        // draw_gang_select_screen grey test: slot not currently editable.
        const bool dim=(i==0 && state.target!=self_target) || (human ? state.ready[slot] : (state.ready[0] || i!=state.target));
        commands.push_back(SetupDrawCommand{.item=SetupItem::gang_slot,.value=i,.aux=state.gang[slot],.icon=0U,
                                            .selected=!dim,.enabled=true,.ready=ready});
    }
    if (state.help_visible) commands.push_back(SetupDrawCommand{.item=SetupItem::help_overlay,.value=0,.aux=0,.icon=0U,.selected=false,.enabled=true,.ready=false});
    return commands;
}

void serialize(StateWriter& writer,const ControlsSetupState& state) {
    writer.s32(state.row);
    writer.s32(state.preset_selected);
    writer.s32(state.preset_saved);
    writer.boolean(state.help_visible);
    writer.boolean(state.edited);
    write_words(writer,state.words);
}

void serialize(StateWriter& writer,const GangSelectState& state) {
    writer.s32(state.human_count);
    for (std::size_t i=0; i<state.team.size(); ++i) {
        writer.s32(state.team[i]);
        writer.s32(state.gang[i]);
        writer.s32(state.panel_direction[i]);
        writer.boolean(state.ready[i]);
        writer.s32(state.colour[i]);
    }
    writer.s32(state.target);
    writer.boolean(state.help_visible);
    writer.boolean(state.locked_available);
}

void serialize(StateWriter& writer,const MainMenuState& state) {
    writer.s32(state.cursor);
    writer.s32(state.players);
    writer.s32(state.controller_count);
    writer.s32(state.playmode);
    writer.boolean(state.save_enabled);
    writer.boolean(state.help_visible);
}

} // namespace battletanx::frontend_setup
