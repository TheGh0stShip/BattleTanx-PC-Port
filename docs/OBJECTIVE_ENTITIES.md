# Objective world entities (queens, bases, capture markers)

Module: `include/battletanx/queen_world.hpp`, `src/queen_world.cpp`
(namespace `battletanx::queen_world`). It owns the queen bases, queens and
effect-31 capture markers of a level and emits ordered value events that map
1:1 onto the `ObjectiveRuntime` / `BattleSession` entry points
`queen_taken`, `queen_stored`, `queen_contact` and `marker_contact`.
Units are never owned here; a carrier is referenced only by its `EntityId`.

Status: source only, not yet compiled or run.

## Retail objects

| Native | Retail | Created by | Contact handler |
|---|---|---|---|
| base | effect kind 20, size 0x2C, box +-0xC3 | `create_entity_point_record` (func_800A6AB4) from `assign_group_object_points` (func_800A4B2C) | func_800A74AC (release_matching_owner_effects) |
| queen | effect kind 21, size 0x34, box +-0x14 (D_80073050 = 20.0) | `spawn_effect_kind_15` (func_800A6958), one per base | func_800A739C -> collect_pickup (func_800A70E0); update func_800A6900 |
| capture marker | effect kind 31, size 0x18, box +-extent | `spawn_effect_marker` (func_800A7550) from bank entry kind 24, single view only | func_800A7614 |

### Kind 4 ("rescue") objectives (CONFIRMED)

Effect 21 is allocated only by `spawn_effect_kind_15` (func_800A6958), whose
sole caller is func_800A6AB4, reached only from func_800A4B2C (placed types
11..14). No script, wave or computer-controlled path creates a queen.
`append_entity_point_record` (func_800A4A64) is dead code: no `jal` to it and no
pointer word to it in the ROM.

| Wave | Objective | Source |
|---|---|---|
| 1, 3, 9, 13 | one effect-31 marker, extent 600; no queen, no base | bank item type 24 |
| 5 | one effect-21 queen (+ base) | placed type 12 -> owner `D_801260A4[1]`, flags 0x7 (team-slot gated) |

Queen contact (func_800A739C) sets result 9 only in game kind 4 when the
touching unit's owner is 0. Marker contact (func_800A7614) sets result 9 when
an owner-0 unit touches the marker, with no game-kind check, and the marker
stays live (the handler frees nothing).

Host flow for a marker wave: for each bank record-0 item that passes the
caller's mode filter, call `QueenWorld::place_bank_entry(MarkerBankEntry{kind,
x, z, param}, view_count)`; kinds other than 24 return `not_marker_kind`.
Extent is `(s16)param` (`marker_extent`). Then `finish_placement()`; report
unit overlap of the +-extent box with `touch_marker`, which queues
`marker_reached`; `objective_call` maps it to `marker_contact(owner)`
(`ObjectiveRuntime::on_marker_contact` / `BattleSession::marker_contact`).

Multi-view: `spawn_bank_record_entries` creates the marker only when the view
count is 1, so in a multi-view session a marker wave has no marker at all
(`place_bank_entry` returns `split_screen`). What, if anything, then ends such
a wave is UNRESOLVED; the campaign is single-view, so this does not block it.

### Placed types 11..14 are bases (CONFIRMED)

`assign_group_object_points` scans every active level object; kinds 11, 12,
13, 14 map to owner `D_801260A4[0..3]` (gSlotRank, filled by
`randomize_slot_order` or reset to 0..3 by `clear_four_runtime_pairs`). An
owner index at or above `get_entity_capacity()` is skipped, and an owner
already holding a base (`pointCount == 1`) is skipped. It runs only for game
kinds 0, 3, 4, 6 (`apply_mode_display_rows`), followed by
`recount_team_active_entities` (func_800A66C8), which sets D_802DF340 = bases
and D_802DF344[team] = bases of each team. Only `create_entity_point_record`
allocates effect 20 and only `spawn_effect_kind_15` allocates effect 21;
`spawn_bank_record_entries` creates neither (its kinds 13/14 are pickup kinds
in `pick_effect_kind`, a different namespace). `append_entity_point_record`
(func_800A4A64) has the same body but is dead code (see above).

Kind 6: owner 0's base gets no queen but still counts in both counters.

Counter updates (CONFIRMED, X22): +1 to the base owner's team when a queen is
attached at a base (`attach_effect_to_owner_group`), -1 to the holder's team
when an enemy collects a queen from a base (`collect_pickup`, state 0; a
same-team collector changes nothing). `queen_world` forwards these as
`on_queen_taken(holder_team,collector_team)` and `on_queen_stored(base_team)`.

Seeding (wiring, OR-7/OR-21): `ObjectiveOwner::bases` must come from
`seed_objective_bases(world,config)` after `finish_placement`, never from owner
presence. main.cpp's `objective_base = game_kind==3||game_kind==6` flag is
replaced by `queens->has_base(owner)` (OR-7); OR-21 lets the session take the
seeded `ObjectiveConfig` before the objective initialises.

Placed objects and win conditions (OR-20, RESOLVED by X22): no campaign wave's
end rule reads placed objects (`update_match_state.c:186-272`). Shells ignoring
placed objects is fidelity only.

### Queen record fields

+0xC home owner, +0xD spawn owner, +0x10 state (0 base list, 1 carried,
2 dropped), +0x14 holder owner, +0x18 motion handle, +0x24 expire tick,
+0x28 carrier link, +0x30 next. Base: owner record +0x1B8 = base record,
base +0x10 = queen list head.

### Transitions

| From | Trigger | Guard | To | Counter | Event |
|---|---|---|---|---|---|
| at_base / stored | queen contact | event 0, kind 0, not blocked, team differs | carried | team[holder]-- | taken_from_base |
| dropped | queen contact | event 0, kind 0, not blocked | carried | - | picked_up |
| any | queen contact | kind 4, owner 0 (after collect) | rescued overlay | - | rescue_contact |
| carried | base contact by carrier | event 0, kind 0, same team | stored, slot = list length | team[base]++ | stored |
| carried | carrier destroyed | - | dropped at unit + D_801266D0[i], expire = tick + 0x78 | - | dropped |
| dropped | step | tick > expire | stored at home base | team[home]++ | returned (drop_expired) |
| carried | carrier link invalid | caller-reported | stored at home base | team[home]++ | returned (carrier_invalidated) |
| - | marker contact | event 0, kind 0, touching unit owner 0 (any game kind) | marker stays live | - | marker_reached |

Progress amounts reported in events: 1000 on collect when home team differs
from the collector team, 5000 on attach when home team differs from the base
team. D_801266F0 (= 1) makes attach rehome the queen to the storing base
(`Policies::store_rehomes_queen`).

D_801266D0 slot offsets {dz, dx}: (0,0), (60,0), (0,60), (-60,0).

There is no travelling "returning" state in retail: expiry and carrier-link
loss attach the queen in the same update. The native `returned` event marks
those returns.

### Game-kind gating of bases and queens (v3)

| Behavior | Class | Citation |
|---|---|---|
| func_800A4B2C runs only when wave kind is 0, 3, 4 or 6, so kind-5 waves (waves 0 and 2) place no bases and no queens | CONFIRMED | `runtime/apply_mode_display_rows.c:16-25`; asm func_8007C2A0 0x8007C2B0-0x8007C2EC; `gameplay/play_campaign_wave.c:157-161` |
| Bases whose object carries flag 0x40 in kinds 3..6 are skipped (loader clears the active byte); affects waves 2, 6, 7 | CONFIRMED | `assets/load_level_data.c:400-401`; `assign_group_object_points.c:78`; `runtime/game_mode_ranges.c:6-14` |
| The campaign holds 20 queens in total; an earlier count of 22 (two on wave 0) ran the base pass without the kind gate and is wrong | CONFIRMED | X17 verdict |
| `QueenWorld` decides placement and state only; `ObjectiveRuntime` decides the match. `ForwardSummary::mismatched` counts taken/stored events not applied | TEMPORARY POLICY (native authority split) | `queen_world.hpp` |

### Owner and carrier links (`owner_links`, v3)

| Behavior | Class | Citation |
|---|---|---|
| Owner +0x1AF base point count (0/1), +0x1B8 base record; base +0x10 queen list head, chained through queen +0x2C | CONFIRMED | `gameplay/assign_group_object_points.c:67-144`; `effects/attach_effect_to_owner_group.c:65-76` |
| Unit +0x98 = carried queen list head; queen +0x28 = holder (base record, or unit +0x504 while carried); cleared on delivery | CONFIRMED | asm func_800A70E0; `entities/release_same_class_effects.c:124-132` |
| Parameter 0x2111 = 0.2 per queen whose holder is a kind-0 body of another owner (base records never count) | CONFIRMED (formula); STRONG INFERENCE (kind-0 = carrying body) | asm func_800E8934; `D_800747D0` = 0.2 |
| Base +0x18 state writers | UNRESOLVED (`BaseSample.state` caller-supplied, default 0) | only reader `ai_param_target_class_weight_b.c:114` |
| Dropped queens are not counted by 0x2111 | TEMPORARY POLICY | drop path not traced |

## Classification

- CONFIRMED: everything in the tables above except where noted.
- STRONG INFERENCE: world axis of D_801266D0; the decomp's marker
  "ready" byte is the touching unit's owner byte (+0x4), and "inhibited" /
  "blocked" are the handler event code and contact kind (same layout as
  func_800A739C).
- TEMPORARY POLICY: marker pool of 16; owners absent inside the capacity are
  skipped; expiry and carrier-invalid order use queen slot order and carried
  list head-first; the rescued overlay leaves the retail state unchanged.
- UNRESOLVED: collector_blocked meaning (flags & 4, func_800E3460); carrier
  link test in draw_marker_entity; how result 9 becomes the kind-4 player win
  flag (catalog setter); objective of a marker wave in a multi-view session.
