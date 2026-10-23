# Frontend model

`include/battletanx/frontend_model.hpp` / `src/frontend_model.cpp` model the
campaign frontend as data: a `FrontendState`, edge-triggered `MenuInput` bits,
`TransitionCommand`s for the host to act on, and `DrawCommand`s that name items
and values by identifier only. No layout, image, or retail text is reproduced.

Status: uncompiled and untested.

## Screens

| Screen | Original source | Behaviour |
|---|---|---|
| title | (policy) | start / attract; attract emits `attract_unsupported` and stays on the title (no attract screen) |
| pak_menu | poll_controller_pak_screen.c | confirm = act and re-poll (result 5); cancel = continue without saving (result 4, clears save flag) |
| campaign_menu | update_game_setup_menu_input / draw_main_menu_screen | `frontend_setup::MainMenuState` (7 rows: players, playmode, options, credits, input code, load game, start game). Start game with playmode 0 opens difficulty; load game opens unit select with the loaded record; options opens options; any other accepted row (or start game in another playmode) emits `setup_row_unsupported` (value = SetupRow). 0x40 accepts start game from any row. Opens on load game when a record was loaded, else on start game |
| difficulty | update_options_menu_input.c row 2 | easy/normal/hard |
| unit_select | poll_player_bundle_selection.c | `frontend_setup::GangSelectState`, one human slot; slot 0 starts from the record unit; computer slots use `computer_gang_draws` (host RetailRandom values); confirm readies (done), cancel returns to the setup menu |
| controller_preset | update_player_slot_configuration.c | `frontend_setup::ControlsSetupState`: rows 0..8 bind the raw button pressed (`MenuInput::raw_pressed`), row 9 steps the preset `(n+1)%4` / `(n+3)%4` and resets the bindings, row 10 exits on confirm or cancel. On exit the bindings and preset are stored in the record (`control_scheme`), `write_campaign_record` is emitted when saving is enabled, then `start_mission`. Bindings edited earlier in the session are kept on re-entry; otherwise the record's preset and bindings load |
| options | update_options_menu_input.c | rows 0..7 wrap; volumes clamp 0..7; difficulty wraps 0..2; flags toggle `&1`; row 7 exits on right/confirm/0x40; cancel exits |
| pause | draw_pause_menu.c | continue(0)/restart(1)/quit(2), wraps; restart at difficulty 0 also emits `easy_restart_hook` (increment_frame_event_counter). Restart maps to campaign_flow `battle_finished{restarted}` (a retry); quit maps to `battle_finished{aborted}` |
| result | update_match_state.c state 7 | driven by campaign_flow `FlowTransition::choices`: loss confirm retry, cancel quit; win confirm continues without saving, cancel saves and continues when offered (not mission 16); endurance end: confirm continue, cancel save; won endurance wave (`auto_advanced`): no screen, back to in_mission; no choices: nothing accepted. `advance_mission.value` is the FlowResultChoice |

Entering the result state emits `stop_unit_sounds` (func_80097B7C).
`frontend_draw` names setup rows (`setup_row`), binding rows, the preset and
done rows by identifier; `setup_draw` returns the full
`frontend_setup::SetupDrawCommand` list (players/playmode values, binding
icons, gang slots) for the three setup screens.

## Classification

- CONFIRMED: options wrap/clamp rules; pause wrap and confirm mapping; setup
  menu, binding, preset and gang rules (frontend_setup_menus); unit wrap and
  locked-index skip; pak confirm/cancel results; func_8010D984 is
  increment_frame_event_counter (D_803C4D70), reset once per wave
  (play_campaign_wave.c:108); a won endurance wave skips the result dialog
  (update_match_state.c:342-344).
- STRONG INFERENCE: options rows 2..6 map to CampaignRecord 0x16, 0x17, 0x18,
  0x19, 0x1B (contiguous RAM block, 0x1A skipped); defeat/victory result input;
  a new campaign keeps option values, base defense and bindings
  (reset_session_state_defaults touches none of them); playmode 0 is the
  campaign.
- TEMPORARY POLICY: title screen layout; setup-menu cancel returns to the
  title; load game is enabled only when a record was read; pause cancel
  resumes; out-of-range saved unit becomes 9; pak probe classification;
  computer gang draws default to zero unless the host supplies them.
- UNRESOLVED: attract/demo playback; players, credits and input-code rows.

## Consumers

`FrontendState` no longer has `unit_choice`, `preset_choice` or
`preset_saved`; use `gangs.gang[0]`, `controls.preset_selected` and
`controls.preset_saved`. The legacy `campaign_new` / `campaign_continue` /
`campaign_options` items are no longer drawn.
