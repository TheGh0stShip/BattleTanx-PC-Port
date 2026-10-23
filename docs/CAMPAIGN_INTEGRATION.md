# Campaign integration guide for `src/main.cpp`

Status: **every module named here is uncompiled and untested.** None of the
v3 campaign modules is registered in `CMakeLists.txt` yet (OR-10), and none of
the sketches below has been built or run. Treat the signatures as the current
header contents, re-read before wiring, not as a verified contract.

This guide tells the owner of `src/main.cpp` how to wire the v3 campaign layers
into the shipped loop. It does not change `main.cpp`. Line numbers refer to the
current 495-line `src/main.cpp`.

Legend for each step:

- **PUBLIC** — possible today through public headers only.
- **OR-n** — needs the owner request with that id from the owner-request ledger
  (BattleSession, MissionState, or CMake change).
- **NEW** — needs an owner change not yet in the ledger (proposed id given).

## 1. What `main.cpp` does today

| Lines | Behaviour | Problem for the campaign |
|---|---|---|
| 340-348 | settings load | fine |
| 368-378 | mounts the pak, writes defaults if absent, parses `CampaignRecord` | no frontend; no pak-probe decision |
| 379-380 | `campaign_wave_for_record` picks one wave; complete campaign throws | no flow: one wave per process |
| 404-414 | bindings from the record, writes the record back immediately | unacknowledged write outside any transaction |
| 422, 440-447 | picks world/navigation stream for that single wave | must be repeatable per wave launch |
| 461-463 | `render_music_sequence(rom, wave->mission_id)` | passes a mission id where a sequence index is expected; should go through `music_selection::music_track_for_level` |
| 78-93 | `CampaignSpawnInput`: owner 0 human, others computer, `lineup.fill(kind)` | fine as a first pass |
| 94-108 | constant seed `0x42545831` | fine (deterministic) |
| 115-132 | `add_owner` per present owner | **line 126**: `objective_base = game_kind==3 \|\| game_kind==6` marks every present owner a base (OR-7). Lines 127-131 use only `points.front()` (OR-8). |
| 134-140 | spawns planned units | fine |
| 141-151 | pickups + weights | fine |
| 161-336 | platform loop: input, fixed 30 Hz catch-up loop (173-218), render (240-331) | no bridge, no flow, no pause/result menus |
| 174 | `session.step(session_input)` | must become `bridge.tick(session,input)` |
| 181-202 | on `mission_succeeded`: builds `CampaignWaveClear` by hand, `apply_campaign_wave_clear`, `write_controller_pak_record` | saves on every win without a prompt; never acknowledges; loss never handled; no next wave |
| 213 | `session.clear_events()` | must become `bridge.clear_events(session)` once the bridge exists (OR-17) |

(The request text refers to "main.cpp:124"; the base flag is the last
`OwnerSetup` argument on line 126, inside the `add_owner` call at 122-126.)

## 2. Module map (current headers)

| Module | Header | Entry points used here |
|---|---|---|
| campaign flow | `campaign_flow.hpp` | `CampaignFlow::apply(const FlowCommand&) -> std::vector<FlowTransition>`, `options_from_record`, `current_wave()` |
| save transaction | `campaign_save_transaction.hpp` | `SaveTransaction(const CampaignRecord&)`, `request(std::uint8_t next_wave_index,const CampaignWaveClear&)`, `pending()`, `acknowledge(std::uint32_t sequence,bool write_succeeded)`, `abandon()` |
| frontend | `frontend_model.hpp` (`battletanx::frontend`) | `make_frontend_state`, `apply_pak_probe`, `update_frontend(FrontendState&,MenuInput)`, `enter_pause`, `enter_result(FrontendState&,const FlowTransition&)`, `frontend_draw` |
| setup menus | `frontend_setup_menus.hpp` (`battletanx::frontend_setup`) | `make_main_menu`, `update_main_menu`, `make_controls_setup`, `update_controls_setup`, `store_controls_in_record`, `draw_*` |
| music | `music_selection.hpp` | `MusicState`, `AudioCommandList`, `apply_music_transition`, `music_track_for_level`, `CountdownAudioState`, `update_tank_loops` |
| bridge | `campaign_battle_bridge.hpp` (`battletanx::campaign_bridge`) | `Bridge(BridgeConfig,const AngleTable&)`, `place_queen_object`, `place_marker`, `finish_queen_placement`, `spawn_turret`, `spawn_placed`, `spawn_breakable`, `add_spawner`, `fire_deployable`, `self_destruct`, `tick`, `clear_events`, `queens()` |
| queens | `queen_world.hpp` | `Config`, `LevelObject`, `QueenWorld::has_base(owner)`, `seed_objective_bases(const QueenWorld&,ObjectiveConfig&)` |
| world runtimes | `spawner_runtime.hpp`, `placed_objects.hpp`, `deployables_runtime.hpp`, `effect_runtime.hpp` | owned by the bridge; main only feeds level data and reads views |
| routes | `route_service.hpp` | `RouteService::bind_unit/release_unit/set_unit_triangle/submit/tick/follow` |
| computer control | `campaign_ai.hpp` | `CampaignAiController::update(units,owners,angles,rng,frame,commands,routes)`, `apply_route_response`, `set_owner_difficulty` |
| owner plays | `campaign_owner_plays.hpp` (`battletanx::owner_plays`) | `update_owner_plays(OwnerPlayState&,const OwnerPlayInput&,RetailRandom&)`, `owner_tuning_from_wave` |
| harness (optional) | `campaign_harness.hpp`, `campaign_harness_bindings.hpp` | `Coordinator` + `FlowBinding/SaveBinding/FrontendBinding/MusicBinding` |
| owner links | `owner_links.hpp` (`battletanx::owner_links`) | `LinkTracker::build/bind_unit/update(const UpdateInput&)`, `owner_world(snapshot,units,owner)`, `apply_link_fields(tracker,owner,members)` |
| spawn-plan adapters | `campaign_spawn.hpp` | `queen_level_objects(plan,level)`, `objective_marker_entries(plan,level)`, `owned_object_turret_spawn(object)`, `PlannedSpawner::definition` |

The harness `Coordinator` with its bindings is an alternative driver for
stage 1. This guide drives `CampaignFlow` directly because it maps onto the
existing loop with the fewest moving parts; either is acceptable, but do not
run both.

## 3. Staged plan

Each stage leaves the game launchable and playable for at least the first wave.

### Stage 0 — baseline fixes (PUBLIC, plus OR-10 for later stages)

1. **Bases** (main.cpp:126). Do not pass `wave.game_kind==3||6`. Until stage 2,
   pass `false` for every owner except a documented interim policy. Note that
   with all bases false, kinds 3/6 capture objectives never resolve; that is
   already the effective behaviour today (OR-7), so this is not a regression.
   Keep the current flag if a wave must stay winnable before stage 2.
2. **Music index** (main.cpp:462-463). Map the level to a track first:
   ```cpp
   const auto track=battletanx::music_selection::music_track_for_level(wave->mission_id);
   if (show_window && track) music=battletanx::render_music_sequence(rom,*track);
   ```
   and pass `music` as optional into `run_platform_diagnostic` (today line 485
   dereferences `*music` unconditionally).
3. **Respawn points** (127-131): keep `points.front()` (OR-8 is fidelity only).
4. **Early binding write** (410-413): leave as is until stage 1 routes it
   through the save transaction.
5. **Build**: OR-10 (append each module's `target_sources`/`add_runtime_test`
   lines) is required before any later stage. OR-11 (`-ffp-contract=off`) is
   recommended before comparing state hashes across hosts.

### Stage 1 — flow, save, menus (PUBLIC + OR-10)

Restructure `run_platform_diagnostic` into a host state machine with three
modes: `Menu`, `Battle`, `Result`. The battle setup in lines 78-157 becomes a
function `start_battle(const CampaignWaveLaunch&)` that loads the wave's world
stream (move lines 430-470 into a per-wave loader) and builds a fresh
`BattleSession`.

```cpp
battletanx::campaign_flow::CampaignFlow flow;
battletanx::campaign_save::SaveTransaction save(*campaign);   // authoritative record
auto front=battletanx::frontend::make_frontend_state();
front.save_enabled=true;
battletanx::frontend::apply_pak_probe(front,probe,&*campaign); // replaces 369-378 decisions
```

Pak probe: map `load_controller_pak_record` results to `frontend::PakProbe`
and follow `decide_pak_load`/the returned `TransitionCommand`s
(`load_campaign_record`, `use_default_record`, `repair_pak`, `repoll_pak`)
instead of the unconditional default write at 370-375.

Menu tick (replaces nothing; runs when mode is `Menu`):

```cpp
const auto step=battletanx::frontend::update_frontend(front,{/* pad pressed bits */});
for (const auto& cmd:step.commands) switch (cmd.kind) {
case CommandKind::start_mission:  handle(flow.apply({.kind=FlowCommandKind::start_new,.record=front.record})); break;
case CommandKind::resume_mission: handle(flow.apply({.kind=FlowCommandKind::resume,.record=front.record})); break;
case CommandKind::apply_music_volume: /* music_selection volume_changed */ break;
...
}
```

The main-menu rows (`frontend_setup::update_main_menu`) and controls setup
(`update_controls_setup`, then `store_controls_in_record` into the record held
by the frontend) feed the same `front.record`; the bindings no longer need the
immediate write at 410-413 — they reach the pak with the next acknowledged save.

Transition handler (one place that reacts to `FlowTransition`):

```cpp
void handle(std::vector<FlowTransition> transitions) {
    for (const auto& t:transitions) switch (t.kind) {
    case FlowTransitionKind::intro:      flow.apply({.kind=FlowCommandKind::intro_finished}); break; // no cut-scenes yet
    case FlowTransitionKind::briefing:   flow.apply({.kind=FlowCommandKind::briefing_finished}); break;
    case FlowTransitionKind::battle_launch:
    case FlowTransitionKind::retry:      start_battle(t.launch); mode=Battle; break;
    case FlowTransitionKind::result:     enter_result(front,t); mode=Result; break;
    case FlowTransitionKind::save_request: issue_save(*t.save); break;
    case FlowTransitionKind::completion:
    case FlowTransitionKind::return_to_menu: mode=Menu; break;
    default: break; // intermission, next_wave: informational; rejected: log
    }
}
```

(Feed the results of the nested `apply` calls back into `handle`; the sketch
elides that.) A `result` transition with `auto_advanced=true` already carries
its intermission and any `save_request` in the same vector: do not show the
result dialog for it.

Battle end, replacing the save block at main.cpp:181-202 entirely:

```cpp
if (event.kind==GameplayEventKind::mission_succeeded || event.kind==GameplayEventKind::mission_failed) {
    FlowBattleOutcome outcome{.won=event.kind==GameplayEventKind::mission_succeeded,
                              .player_score=session.owners()[0].score};
    pending_outcome=outcome;   // apply after the catch-up loop, not inside it
}
```

Both kinds exist in `GameplayEventKind` (runtime_events.hpp:21-22); the
current loop only consumes `mission_succeeded`. Pause QUIT / RESTART from the
frontend set `aborted` / `restarted` on the outcome.

Save requests → write → acknowledge:

```cpp
void issue_save(const FlowSaveRequest& r) {
    const auto next=static_cast<std::uint8_t>(r.cleared_wave_index+1U);
    if (save.request(next,r.clear)!=RequestResult::issued) { /* busy / invalid: report */ }
}
// once per host frame, outside the 30 Hz loop:
if (const auto& p=save.pending()) {
    bool ok=true;
    try { battletanx::write_controller_pak_record(pak_path,p->bytes); } catch (...) { ok=false; }
    const auto ack=save.acknowledge(p->sequence,ok);
    if (ack==AckResult::committed) flow.apply({.kind=FlowCommandKind::save_finished,.accepted=true});
    else if (ack==AckResult::write_failed) { /* offer retry or save.abandon(); then save_finished,.accepted=false */ }
}
```

`SaveRequest::bytes` is already the serialized record; do not call
`serialize_campaign_record` again. Only `save.authoritative()` is the committed
record; use it for the next `resume`.

Stage 1 is all PUBLIC, apart from OR-9 itself (this wiring) and OR-10.

### Stage 2 — objectives and queens (PUBLIC workaround; OR-21 for the clean path)

Replace main.cpp:126's flag with the queen world's placement result. The
session reads `OwnerSetup::objective_base` into `objective_bases_`
(battle_session.cpp:159) and builds its `ObjectiveRuntime` lazily on the first
step or objective call (battle_session.cpp:308-316), mapping the flag to
`ObjectiveOwner::bases` 0/1. `seed_objective_bases` writes exactly
`world.has_base(i)?1:0` (queen_world.cpp:580-584). So the equivalent result is
reachable today, provided the bridge places queens **before** `add_owner`:

```cpp
campaign_bridge::BridgeConfig bc{.game_kind=wave.game_kind,.game_mode=wave.game_kind};
if (needs_queens(wave.game_kind)) bc.queen=queen_world::Config{
    .game_kind=static_cast<std::uint32_t>(wave.game_kind),
    .owner_count=static_cast<std::uint8_t>(wave.owner_count),
    .owners=/* {present,team} from spawn_plan->owners */};
campaign_bridge::Bridge bridge(std::move(bc),angles);
if (bridge.error()!=campaign_bridge::CreateError::none) throw ...;
for (const auto& obj:battletanx::queen_level_objects(*spawn_plan,world)) bridge.place_queen_object(obj); // scan order
// markers: see note below

bridge.finish_queen_placement();
// then, in the existing add_owner loop (122-126):
const bool base=bridge.queens()!=nullptr && bridge.queens()->has_base(owner);
session.add_owner(owner,OwnerSetup{..., /*objective_base=*/base});
```

Constraints: no objective entry point and no `step` may run before every
`add_owner`, or the runtime is built with the wrong bases. The bridge's
constructor does not take the session, so this ordering is free. The spawn
plan must already exist (it does: main.cpp:97). Queens come from
`queen_level_objects(plan,level)`. Markers come from
`objective_marker_entries(plan,level)`, which yields `MarkerBankEntry` for
`QueenWorld::place_bank_entry`; the bridge exposes only
`place_marker(x,z,extent,view_count)`, so either convert each entry
(`extent=queen_world::marker_extent(param)`, and apply the view-count rule that
`place_bank_entry` would apply) or ask the bridge owner for a
`place_bank_entry` forwarder (module-to-module, not an owner request).
Until queens and markers are placed, waves with those objectives cannot
finish (OR-7, COMPLETE).

OR-21 (session accepts a seeded `ObjectiveConfig`) removes the ordering
constraint; it is not required for correctness if the order above is kept.
With OR-21 the path is: `finish_queen_placement()`, then
`seed_objective_bases(*bridge.queens(),objective_config)`, then hand the config
to the session before its first step. Counts must come only from placement
(types 11..14, first base per owner, owner < owner_count, kinds 0/3/4/6);
kind 6 owner 0's queenless base is counted (retail, do not exclude it).

Queen events are routed by `Bridge::tick` (phases A1 and C3) to
`session.queen_taken/queen_stored/queen_contact/marker_contact`; main does not
call `queen_world::forward_events` itself. `Bridge::self_destruct(session,owner)`
replaces any direct `session.self_destruct` call.

### Stage 3 — world runtimes via the bridge (PUBLIC; fidelity gaps OR-12..OR-24)

At level setup, after the units are spawned (134-140):

```cpp
for (const auto& o:spawn_plan->owned_objects)
    bridge.spawn_turret(battletanx::owned_object_turret_spawn(o),session);   // plan order
for (const auto& p:placed)     bridge.spawn_placed(p.record,p.bounds);       // level reader
for (const auto& b:breakables) bridge.spawn_breakable(b.record,b.bounds);    // level reader
for (const auto& s:spawn_plan->spawners) {
    // kind5_serial_for: host-provided lookup of the serial of the kind-5 effect
    // a linked (placed type 20) spawner belongs to.
    const auto serial=s.definition.linked ? std::optional<std::int32_t>{kind5_serial_for(s)}
                                          : std::nullopt;
    bridge.add_spawner(s.definition,serial);
}
```

A linked definition without a serial is created unlinked
(`SpawnerSlot::link_failed`), TEMPORARY POLICY; check
`bridge.spawners().slots().back().link_failed`.

`PlannedSpawner::definition` is the `SpawnerDefinition` the spawner set takes
directly. Placed/breakable records still need a level-reader adapter. Spawner owner budget and
gate go in `BridgeConfig::spawner_owners` (the session has no field: OR-15).

In the 30 Hz loop (main.cpp:173-218):

```cpp
while (game_tick<target_tick) {
    const auto report=bridge.tick(session,session_input);   // replaces session.step at 174
    for (const auto& event:session.gameplay_events()) { ... } // 175-203 unchanged (rumble, outcome capture)
    for (const auto& sound:session.sound_events())   { ... } // 206-212 unchanged
    bridge.clear_events(session);                            // replaces session.clear_events at 213
    ++game_tick; ...
}
```

`Bridge::tick` already reads the session events through its own cursor during
phase C; main must read them **after** `tick` and must never call
`session.clear_events()` directly (OR-17 would make this robust).

Deployables: `Bridge::tick` supplies its deployables context to `BattleSession`,
so player and computer special-weapon input reaches weapons 12 and 13 through
the same cooldown, ammunition, event and sound bookkeeping as other weapons.
`bridge.fire_deployable(...)` remains a direct helper for scripted placement and
focused tests.

Known gaps (fidelity, game stays playable): turret shots are instant hits
(OR-13), placed-object score awards are dropped (OR-16), shells ignore placed
objects (OR-20, RESOLVED: no campaign wave's end rule needs placed objects
destroyed, update_match_state.c:186-272; fidelity only), mine wear dropped (OR-18), squad respawn duplicated (OR-24).

Respawn markers: at wave load the bridge builds one marker vector per owner,
`markers[o]=spawner_runtime::build_member_markers(plan.respawn_points[o].points)`
(entry i == points[i], last_used 0), and keeps it for the whole wave. Each
`respawn_squad_unit`/`respawn_squad_member` call for a squad of owner `o` sets
`context.markers=markers[o]`, so both paths share last_used. Never span a
temporary, and do not rebuild mid-wave (that resets cooldowns). Serialize the
vectors with `serialize_member_markers`.

Rendering: add `bridge.placed_world().snapshots()` and queen/base/marker views
(`bridge.queens()->view(...)`) to the draw list next to pickups (288-298).

### Stage 4 — computer-controlled plays and routes (NEW owner request)

```cpp
RouteService routes;                         // bind_unit on spawn, release_unit on unit_destroyed
CampaignAiController control(CampaignAiConfig{/* difficulty etc. */});
std::array<owner_plays::OwnerPlayState,owner_capacity> plays{};
for (owner...) if (auto t=owner_plays::owner_tuning_from_wave(wave,group,owner)) /* set_owner_difficulty */;
```

Per tick, before `bridge.tick`:

1. `owner_plays::update_owner_plays(plays[o],input_o,session.rng())` for each
   present computer owner; apply `result.assignment` changes with
   `control.force_member_root_play`, objective/anchor with `set_owner_objective` /
   `set_owner_anchor`.
2. `control.update(session.units(),session.owners(),angles,session.rng(),frame,commands,requests)`.
3. `routes.submit(r)` for each emitted `RouteRequest`; `routes.set_unit_triangle`
   / `set_unit_position` from unit positions; `routes.tick(navigation,budget)`;
   `routes.follow(unit,pos)` → `control.apply_route_response`.
4. Deliver `CampaignAiCommand::command` (`EnemyCommand`) to the session.

Step 4 has **no public entry point**: `BattleSession::step` runs its own
`EnemyController` (step order 3) and does not accept external commands.
Proposed **OR-27 (NEW)**: a `SessionInput`/session hook that replaces the
built-in enemy command for owners driven externally. Without it, stage 4 can
run in shadow mode only (compute and hash, do not apply). Using
`session.rng()` from main also changes the random stream the session consumes;
give the external controllers their own `RetailRandom` until OR-27 settles the
draw order, otherwise determinism tests shift. OR-1..OR-3 (selectable flag,
computer pickup mode) are related fidelity fixes.

Owner links run before step 1: build an `owner_links::LinkTracker` per battle
(`build` with the squads' `UnitLinkSet`s, `bind_unit` on each spawn), call
`tracker.update(input)` once per tick, then build each owner's
`owner_plays::OwnerWorld` with `owner_links::owner_world(snapshot,units,owner)`
and fill the `MemberView` array with `apply_link_fields` before
`update_owner_plays`. The `OwnerSnapshot` queen/base samples come from
`bridge.queens()->view(...)`. All PUBLIC; only the final command delivery
needs OR-27.

### Stage 5 — effects and music (PUBLIC; OR-19 for exact effect mapping)

Effects: the bridge already updates its `EffectRuntime` in phase C4. Render
from `bridge.effect_runtime().collect(queue)` (`EffectRenderQueue`) after the
world items. Event-to-effect mapping is a policy until OR-19 exposes the
weapon `EffectEvent` queue.

Music and audio:

```cpp
music_selection::MusicState music_state;
music_selection::AudioCommandList audio;
music_selection::apply_music_transition(music_state,MusicTransition::wave_loading,
    {.level=launch_mission_id,.music_volume=vol},audio);
// after the countdown ends:
music_selection::apply_music_transition(music_state,MusicTransition::countdown_finished,ctx,audio);
```

Drain `audio` once per host frame: `play_music` → render (cached per track)
and `platform->set_music_track`; `stop_music`; `set_music_volume`;
`play_sound`; loop kinds → `set_looping_sound` / `stop_looping_sound`. Replace
the hand-built engine/skid loops at 224-238 with `update_tank_loops(loops,0,speed,...)`
and the countdown cues with `countdown_audio_frame`. Title, result and
intermission transitions come from the stage-1 handler (`title_screen`,
`results`, `intermission` — the last two are TEMPORARY POLICY no-ops).

## 4. Per-tick order (host frame)

```
host frame:
  pump input (161-168)
  mode Menu/Result: update_frontend / update_main_menu -> FlowCommands -> handle()
  mode Battle: while game_tick<target_tick (173):
      [stage 4] owner_links update -> owner plays -> campaign_ai.update -> routes submit/tick/follow -> (OR-27) commands
      bridge.tick(session,input)                     // A1 queens, A2 placed, A3 spawners, A4 deployables,
                                                     // B session.step, C1-C4 events/effects
      read session.gameplay_events()                 // rumble, outcome capture, route release on unit_destroyed
      read session.sound_events()                    // play_sound
      bridge.clear_events(session)
      ++game_tick; clear edge-triggered input (215-217)
  after the catch-up loop:
      pending_outcome -> flow.apply(battle_finished) -> handle()
      save.pending() -> write_controller_pak_record -> save.acknowledge -> flow.apply(save_finished)
      drain music_selection AudioCommandList
  render (240-331): world, units, shells, pickups, placed/queens, effects, HUD or frontend_draw
  present, sleep (331-335)
```

Rules: never call `flow.apply` or perform pak I/O inside the 30 Hz loop; never
call `session.step` or `session.clear_events` directly once the bridge exists;
build a new `BattleSession` and `Bridge` for each `battle_launch`/`retry`
transition (state does not carry between waves except through the flow's
carry/bonus).

## 5. Owner-request cross-reference

| Step | Status |
|---|---|
| Stage 0 base flag / music index | PUBLIC (main.cpp only) |
| Building any v3 module | OR-10 (CMake) |
| Cross-host hash stability | OR-11 (CMake), OR-5/OR-6 (session hash) |
| Stage 1 flow/save/menus | PUBLIC; is OR-9 |
| Stage 2 base seeding | PUBLIC with placement-before-`add_owner`; OR-21 clean path; OR-7 closed by this stage |
| Stage 2 queen/marker data | level-reader adapter (not an owner request) |
| Stage 3 bridge tick + level objects | PUBLIC; fidelity OR-12, OR-13, OR-15, OR-16, OR-18, OR-22, OR-23, OR-24 |
| Stage 3 event cursor | works today; OR-17 for robustness |
| Stage 3 deployables from units | wired through `Bridge::tick` for player and computer input |
| Stage 4 apply computer commands | **OR-27 (NEW, not in ledger)**; OR-1..OR-4 fidelity |
| Stage 5 effects | PUBLIC; OR-19 for exact mapping |
| Stage 5 music | PUBLIC |
| Respawn point variety | OR-8 |
| MissionState index safety | OR-25 |

Remember: these modules are uncompiled and untested; each stage must be
compiled, its runtime tests run, and the first wave played before the next
stage begins.
