# Campaign flow controller

`battletanx::campaign_flow::CampaignFlow` (include/battletanx/campaign_flow.hpp)
is a value-only state machine for the 17-wave campaign. Hosts send `FlowCommand`
values. Each command returns the `FlowTransition` values it causes, in order.
The controller does no I/O and never writes a `CampaignRecord`.

Status: uncompiled and untested.

## Wave order

The order is catalog order (gOpponentWaves, ROM 0x0D2DE8):
missions 1..15, then 17, then 16. Waves 4, 8, 12 and 15 (missions 5, 9, 13
and 17) are endurance waves (kind 7). The chapter cut-scenes are played
before wave index 0 (sequence 15), 5 (16), 9 (17) and 13 (18). The original
checks the result only for sequence 15; cancelling it returns to the menu.

## Phases and transitions

```
start_new/resume -> intro(seq) | briefing          [phase chapter_intro|briefing]
intro_finished   -> briefing | return_to_menu(intro_cancelled, seq 15 only)
briefing_finished-> battle_launch(CampaignWaveLaunch)        [battle]
battle_finished  -> result(won, choices) | retry (restarted) | return_to_menu(battle_aborted: pause quit only)
                    kind 7 won: result(auto_advanced, save_choice), intermission,
                    then save_request [save_pending] or next_wave, intro|briefing
result_choice:
  retry          -> retry(same launch)                       [battle]
  quit           -> return_to_menu(wave_lost)                [menu]
  continue       -> intermission, next_wave, intro|briefing  (or completion)
  save+continue  -> intermission, save_request               [save_pending]
save_finished    -> next_wave, intro|briefing | completion
set_options      -> (none); replaces the live options
```

Result choices (update_match_state states 6 and 7, one human player). The rule
is public as `offered_result_choices(wave, result_code)` and
`result_auto_advances(wave, result_code)`:

| wave | outcome | choices |
|---|---|---|
| kind != 7 | won | continue; save+continue unless mission 16 |
| kind != 7 | lost | retry, quit |
| kind 7 | lost (normal end) | continue, save+continue |
| kind 7 | won | none: no dialog, auto-advance with the carried save choice |
| any | result code not 0 or 2 | none (UNRESOLVED; update_match_state accepts no button) |

On a non-endurance win, button 0x20 saves (except mission 16) and 0x10
continues without saving (update_match_state.c:397-405). Kind 7 advances on a
loss. `frontend::enter_result` takes the `result` transition so the result
screen offers exactly `choices`; with `auto_advanced` it opens no screen.

### Carried save choice (D_8012579B)

Every state-7 button writes D_8012579B: it is 1 only after save+continue, and
0 after continue, retry or quit (update_match_state.c:383-426). Nothing resets
it at wave start. In state 6, a kind-7 wave whose entity 0 result is 2 jumps
straight to `reset` (update_match_state.c:342-344) and skips the dialog, so
run_campaign_waves.c:122-126 saves after it if and only if the previous dialog
chose save. The flow keeps this as `last_save_choice()`, sets it on every
`result_choice`, leaves it alone on pause restart or quit, and serializes it.

### Final wave (mission 16)

Retail never saves after the final wave. The mission-16 win offers 0x20
without setting the flag (update_match_state.c:397-405: `value04 != 0x10`),
and every exit path clears D_8012579B before the save check at
run_campaign_waves.c:124. The flow offers only continue, so `completion`
follows with no `save_request`, even when the previous wave chose save. The
record keeps mission id 16 from the save after mission 17. After the last
wave, the controller emits `completion` with sequence id 19 (gFrontendState =
19).

## Pause restart, pause quit and retry counters

- Pause RESTART (draw_pause_menu.c:110-115 sets D_80125799 and D_80125798):
  play_campaign_wave.c:215 `goto restart` relaunches the same wave from its
  backup. Report it as `battle_finished` with `outcome.restarted=true`; the flow
  emits `retry`. `restarted` wins over `aborted`.
- Pause QUIT (case 2: D_801B4AE0=2, D_80125798=1) is the only abort:
  `outcome.aborted=true` -> `return_to_menu(battle_aborted)`.
- `easy_restart_count()` mirrors D_803C4D70: reset_frame_event_counter on wave
  entry (play_campaign_wave.c:108); increment_frame_event_counter
  (func_8010D984) on each restart, from the result screen or the pause menu,
  at difficulty easy. CONFIRMED.
- `retries()` counts every retry of the current wave and is also reset on wave
  entry (TEMPORARY POLICY counter; no retail global).

## Carry, bonus and options

- `carry` and `bonus` follow CampaignParams: they start from the record (or
  zero for a new campaign). After a wave is passed they become the outcome's
  `player_score` and `player_bonus`. A retry reuses the pre-battle values,
  because play_campaign_wave restores its backup.
- `FlowOptions` holds unit type, difficulty (also the skill group), powerups,
  powerup regen, control scheme, base defense, unlimited ammo, volumes and
  bindings. It is loaded from the record and replaced by `set_options`.
- An invalid resume returns to the menu without changing the live options,
  carry or bonus.
- A new campaign follows reset_session_state_defaults: mission 1, carry 0,
  bonus 0 and unit type 9. Difficulty and control scheme come from the record
  the host passes (TEMPORARY POLICY: the live D_80134E64/D_803283B8 globals are
  separate from the session record).

## Save requests (mapping for the save transaction module)

The `save` member of a `save_request` transition is a `FlowSaveRequest`; its
`clear` field is a `CampaignWaveClear` holding exactly the
values write_controller_pak_progress stores:

| CampaignWaveClear | source |
|---|---|
| mission_id | catalog `saved_mission_id_after_clear` of the cleared wave (next wave id; 0 after the last wave) |
| carry, bonus | post-battle score and bonus |
| unit_type, difficulty, powerups, powerup_regen, base_defense, unlimited_ammo, control_scheme, sfx/music volume, bindings | `FlowOptions` |

The save transaction module applies it with `apply_campaign_wave_clear` to its
own record, keeping the header and reserved bytes, then serializes the record.
When the write is done, the host sends `save_finished`. The result is
ignored, as in run_campaign_waves.

## Serialization

`serialize(StateWriter&)` writes version 3 big-endian: phase, wave index,
carry, bonus, options, last won flag, last result code (s16), last outcome
(won, aborted, score, bonus, restarted, result code), retry count, easy
restart count and the carried save choice. `checksum()` is the
StateWriter SHA-256 digest of those bytes.

## Classification

- CONFIRMED: wave order and kinds; chapter sequences; result buttons
  (update_match_state.c:383-426); the carried D_8012579B decides the save
  after a skipped dialog (run_campaign_waves.c:122-126); no save after mission 16
  (update_match_state.c:397-405, run_campaign_waves.c:124); pause restart and
  quit; D_803C4D70 restart counter; save field mapping.
- STRONG INFERENCE: the campaign runs with D_80134E5C == 0, so the state-7
  branch above applies (the D_80134E5C != 0 branch is not modelled).
- STRONG INFERENCE: kind-7 won result skips the dialog. State 6
  (update_match_state.c:342-344) tests entity 0 `value10 == 2`, while state 7
  (update_match_state.c:383) tests `func_800A42B0(entity 0) == 2`; the flow
  uses one value (`last_result_code_`, the func_800A42B0 result) for both.
- TEMPORARY POLICY: `retries()` counter; a new start or resume clears the
  carried save choice (retail keeps it for the whole power-on session);
  new-campaign difficulty and control scheme come from the host record.
- UNRESOLVED: game states that produce result codes other than 0 and 2; the
  on-hardware presentation of the skipped kind-7 dialog; whether value10 and
  func_800A42B0 can differ. If value10 != 2 while func_800A42B0 == 2, retail
  shows a kind-7 dialog where both 0x10 and 0x20 continue without saving; the
  flow offers no choices for (kind 7, code 2).
