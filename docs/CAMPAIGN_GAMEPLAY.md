# Campaign gameplay runtime

How the native gameplay modules fit together, and how the campaign layer sits
above them. Retail evidence for each rule is indexed in
[RETAIL_EVIDENCE.md](RETAIL_EVIDENCE.md); campaign flow, objective entities and
the frontend model have their own documents ([CAMPAIGN_FLOW.md](CAMPAIGN_FLOW.md),
[OBJECTIVE_ENTITIES.md](OBJECTIVE_ENTITIES.md), [FRONTEND_MODEL.md](FRONTEND_MODEL.md)).

The campaign-layer modules listed under "Campaign layer" are new source that
has not yet been compiled or tested.

## Layers

```text
frontend_model ──► campaign_flow ──► campaign_save_transaction ──► controller_pak
      ▲                 │  (wave request)            (pending → committed record)
      │                 ▼
 music_selection   campaign_harness / host
                        │  (battle request: wave definition, carry, bonus)
                        ▼
                 BattleSession (one wave)
   unit_runtime · combat · weapon_runtime · projectile_runtime · effect_runtime
   pickup_runtime · pickup_effects · placed_objects · spawner_runtime
   deployables_runtime · queen_world · objective_runtime · campaign_spawn
   campaign_ai · campaign_owner_plays · navigation_route · route_service
```

Value flows down as requests, results flow up as typed outcomes and events. No
lower layer calls a higher one.

## Gameplay modules (current main)

| Module | Responsibility |
|---|---|
| `unit_kinds`, `unit_runtime` | Unit and owner records (20 units, 4 owners), movement tables, turret and placed-object record rules |
| `combat` | Damage, destruction, kill/score tallies, hit messages |
| `weapon_catalog`, `weapon_runtime` | Weapon ids 0..16, ammo, fire paths, typed visual/sound side effects |
| `projectile_runtime` | One projectile pool (`ShellPool`) and one updater (`update_shells`) for shells, bullets, beams, nuke, grenade and missile |
| `pickup_runtime`, `pickup_effects` | Pickup entities, respawn timers, `apply_pickup` for all 20 kinds |
| `level_spawns`, `campaign_spawn` | World-stream interpretation; level-start placement per owner |
| `objective_runtime` | One objective mechanism per game kind; no fallback between kinds |
| `campaign_ai`, `navigation_route` | Computer-controlled targeting, skill, route ring and planner |
| `campaign_catalog`, `campaign_record` | 17 typed wave records; 256-byte save record |
| `runtime_events`, `state_hash` | ID-only event queues; deterministic serialization |

## Campaign layer (v3)

| Module | Namespace | Responsibility |
|---|---|---|
| `campaign_flow` | `battletanx::campaign_flow` | Value-only wave loop: commands in, transitions and save requests out |
| `campaign_save_transaction` | `battletanx::campaign_save` | Builds a pending record; commits only on host acknowledgement |
| `frontend_model` | `battletanx::frontend` | Menu decisions and draw commands by identifier; no layout or retail text |
| `music_selection` | `battletanx::music_selection` | Pure audio selection; emits typed audio commands the host maps to platform calls |
| `campaign_harness` | `battletanx::campaign_harness` | Headless menu → briefing → battle → result → save → next wave; injects outcomes, never simulates |
| `placed_objects` | `battletanx::placed` | Pooled turrets, bonus crates and destructibles over the `unit_kinds` record rules |
| `spawner_runtime` | `battletanx::spawner_runtime` | Kind-0x1C spawners and the two squad respawn paths, emitted as ordered spawn requests over `level_spawns`/`campaign_spawn` (tick steps 5 and 7–8) |
| `deployables_runtime` | `battletanx::deployables` | Weapon 12/13 mines, powered 5x5 deploy grids and gun-buddy squad-unit requests (tick step 5) |
| `effect_runtime` | `battletanx::effects` | Effect-script runner, particles, muzzle flash and impact decals as value-only render items, fed by `weapon_runtime` effect events (tick step 5) |
| `queen_world` | `battletanx::queen_world` | Queens, owner bases and the capture marker for game kinds 0/3/4/6, feeding `objective_runtime`; see [OBJECTIVE_ENTITIES.md](OBJECTIVE_ENTITIES.md) (tick step 6) |
| `campaign_owner_plays` | `battletanx::owner_plays` | Owner play stack (plays 2, 10, 12, 14, 15, 25, 26): sets owner role and objective, returns member play assignments that `campaign_ai` applies (tick step 3, before per-unit decisions) |
| `route_service` | `battletanx` (`RouteService`) | One route request slot per unit; round-robin replan cursor over the 20 slots, above `navigation_route` |
| `owner_links` | `battletanx::owner_links` | Owner base samples, carried-queen counts and the unit pair-link table with its update rotor (feeds `campaign_ai` and `campaign_ai_parameters`) |
| `campaign_ai_parameters` | `battletanx` (header `campaign_ai_parameters.hpp`) | Owner/member parameter keys (0x11xx member, 0x21xx owner) for play scoring |
| `campaign_battle_bridge` | `battletanx` (header `campaign_battle_bridge.hpp`) | Session-side adapter: turret bullets, squad/deploy owner mapping, contact replies on bridge shadows |


## Target tick order (campaign integration; TEMPORARY POLICY)

This is the order the campaign modules are meant to run in once they are wired
into `BattleSession::step`. It is not current behavior. Today
`BattleSession::step` (`src/battle_session.cpp`) runs eight steps: release
destroyed units, update shells, update units (including computer controls),
update pickup state, match state step, objective update, due respawns, and the
tick counter. Owner plays, the route service, step 5 below and queen
carry/store are not wired yet.

Target order for one 30 Hz tick:

1. release units destroyed on an earlier tick;
2. shells spawned earlier move and resolve hits;
3. computer control: owner plays, then per-unit decisions, then the route
   service within its replan budget;
4. units in slot order: drive, then weapon fire (new shells wait one tick);
5. placed objects, spawners, deployables and other effects, each in ascending
   slot order;
6. pickups: contact collection, then respawn timers; queen carry/store;
7. match state, squad liveness, respawn scheduling;
8. due respawns spawn;
9. the tick counter advances unless paused.

Retail runs these updates from scheduled tasks inside one frame; the exact
interleaving is not recovered, so the target order is a TEMPORARY POLICY. The
campaign layer runs outside the tick: it reacts only to the session's final
outcome and objective events.

## Recovered behavior by area (v3 update)

Classes as in [RETAIL_EVIDENCE.md](RETAIL_EVIDENCE.md). Decomp paths are under
`src/code/`; ROM offset = VRAM - 0x80070000. All modules below are uncompiled
and untested source.

### Weapons and projectiles (`weapon_runtime`, `projectile_runtime`)

| Behavior | Class | Citation |
|---|---|---|
| One projectile pool and one updater for every flying weapon effect (kinds 2, 12, 13, 14, 15, 22, 34); `WeaponRuntimeState` holds no pool | TEMPORARY POLICY (retail shares the 1024-slot effect pool) | `effects/init_effect_kind_table.c`; native `projectile_runtime.hpp` |
| Cooldown and repeat tables, 17 entries each (id 16 cooldown 1) | CONFIRMED | `D_80125EC0` ROM 0xB5EC0; `D_80125ED4` ROM 0xB5ED4 |
| Weapons 5/10 fire a beam: length 4000, damage 0x26 via hit message 12, depth 0x12 powered / 8, sound 9; powered sets ammo[5]=1 for both ids | CONFIRMED | `gameplay/fire_unit_weapon.c:349-372`; `effects/spawn_beam_effect.c:95-170`; `entities/unit_handle_hit_message.c:68-73` |
| Beam forks past depth 16: two delayed kind-13 spawns, normal +-0x1000/0x800 jitter, delay rand%3 | CONFIRMED (function); delay field STRONG INFERENCE | `spawn_beam_effect.c:175-190`; `effects/effect_delayed_spawn_tick.c` |
| Beam passes through a unit once it answers response 1; passed units are excluded for the rest of that beam | TEMPORARY POLICY | W5a ledger item 1 |
| Weapon 6 nuke: speed 50, vy 10, timer 0xC8, gravity 2, ground/air damping 0.05/0.01, detonates as variant 8 | CONFIRMED | `effects/spawn_effect_kind_0e.c`; `effects/update_bouncing_debris.c`; ROM 0x4E70/0x4E74 |
| Nuke damage: script 17 waits 256 clock units, ramp 50->8000 over 0xC00, message 3 once per target, amount 50+(1-d/4000)*50 | CONFIRMED | `effects/effect_script_data.c`; `update_effect_value_ramp.c`; `entities/apply_proximity_hit_to_target.c` |
| Nuke ring targets re-scanned each tick, nearest first | STRONG INFERENCE (order); TEMPORARY POLICY (re-scan) | `notify_targets_in_radius.c` |
| Weapon 7 grenade: randomized launch, timer 0x50, bounce factors, detonation message 7 radius 100 amount 22, sound 5 | CONFIRMED | `effects/spawn_effect_kind_0f.c`; `effects/update_bouncing_grenade.c` |
| Weapon 11 missile guidance is owner steering (heading -= steer*10*frame), not auto-acquisition; burst mask fires a beam (depth 5) | CONFIRMED | `effects/update_missile_effect.c`; `effects/spawn_effect_kind_16.c` |
| Missile/nuke/grenade/fork ignore only the firing unit (retail passes team or record) | TEMPORARY POLICY | W5a evidence (find-segment ignore) |
| World hits carry normal = -direction (level collision has no normals) | TEMPORARY POLICY | W5a evidence |
| Weapons 12/13 (mines, deploy grid, gun buddy) route through `deployables::DeployablesContext` from `WeaponContext::deployables`; deployables never use `ShellPool` | CONFIRMED (retail paths, `fire_unit_weapon.c:399-445`); routing TEMPORARY POLICY | `weapon_runtime.hpp:49` |
| Weapon 14 decoy: untargetable while now-0x5A < lastMineTime; spawn sets -1000 | CONFIRMED | `entities/entity_is_targetable.c:19` |

### Placed objects (`placed_objects`)

| Behavior | Class | Citation |
|---|---|---|
| Kind 5 message 3: if frame < frameCount-1, release, remove owned effects, flags \|= 0x10; message 7 the same without 0x10; neither scores | CONFIRMED | `effects/frame_effect_handle_message.c:56-68` (func_8009345C) |
| Kind 9 (crushable) messages 3/7: if !(flags&4) flags \|= 3, fling heading when offset nonzero, release; update spawns oriented debris and frees | CONFIRMED | `slot_heading_effect_handle_message.c:44-87`; `update_morphing_effect.c` |
| Unit contact with kind 9: shake counter 10, unit +0x3D0 -= 6.0; replies 4 (scrape -1.2 if speed > 0.1) / 5 (crush -6.0) | CONFIRMED | `pulse_effect_handle_message.c:72-100`; `update_tank_drive.c:618-627` |
| Flag-1 collapse: 60 frames, then stage++; when health > 0, flags &= 0x6F (clears 0x10 and 0x80); health <= 0 repeats the collapse | CONFIRMED | `update_effect_kind_5.c:380-427`; asm func_80092734 0x80092F94-0x80092FA4 |
| Flag-1 clear path advances a stage without touching flags, so 0x80 sticks | CONFIRMED (control flow); gameplay significance UNRESOLVED | `update_effect_kind_5.c:428-442` |
| Native `destroyed` event on reaching the last stage | TEMPORARY POLICY | W3c evidence |
| Message 37 meaning; kind 6 message 7; debris group-to-model mapping | UNRESOLVED | W3b evidence |

### Turret bullets (`campaign_battle_bridge`)

| Behavior | Class | Citation |
|---|---|---|
| Turret fire spawns effect 34: speed {70,48,48,48}[type], bullet kind {1,0,0,3}, damage = owner +0x338 byte, life 0x70, range 10000 | CONFIRMED | `update_turret.c:332-340`; `spawn_effect_kind_22.c:57-85` |
| Type 3 turret fires a beam (depth 5, damage effectKind[3]) | CONFIRMED | `update_turret.c:336` |
| Owner +0x338/+0x340 values (difficulty profile) | UNRESOLVED (copy CONFIRMED) | asm func_800A31C0 0x800A3474-0x800A34A0 |
| Bridge bullets move like `update_ballistic`; no ricochet, sounds or trails; no beam forks | STRONG INFERENCE (motion); TEMPORARY POLICY (omissions) | X12 evidence |

### Owner links and parameter keys (`owner_links`, `campaign_ai_parameters`)

| Behavior | Class | Citation |
|---|---|---|
| Owner +0x1B8 = base record (kind 20), +0x10 queen list head, +0x2C next | CONFIRMED | `gameplay/assign_group_object_points.c:67-144`; `effects/attach_effect_to_owner_group.c:65-76` |
| Unit +0x98 = carried queen list head | CONFIRMED | asm func_800A70E0 |
| Key 0x2111 = 0.2 x queens held by another owner's carrying unit | CONFIRMED (formula); kind-0 holder = carrying body STRONG INFERENCE | asm func_800E8934; `D_800747D0` |
| Pair table 12-byte records, index [20][20], link distance 400, range 1800/2000/0.95, distance max+min*3/8 | CONFIRMED | `gameplay/build_unit_pair_records.c:48-108`; `update_unit_pair_link.c`; ROM 0x4390-0x4398, 0x443C-0x4448 |
| Pair array capacity: port 190 vs retail 200 | STRONG INFERENCE | symbol spacing |
| Keys 0x1102/0x1103 rail progress, 0x1108/0x1109 linked unit behind/ahead, 0x1110/0x1111 carried queens, 0x1113 route reachable | CONFIRMED | `ai_parameter_member_progress_ratio.c`; `ai_parameter_linked_unit_behind.c`; `ai_parameter_member_chain.c`; `ai_parameter_member_route_reachable.c` |
| Keys 0x210E range progress, 0x2113 base state weight, 0x2116/0x2117 base chain | CONFIRMED | `ai_parameter_owner_objective.c:61-64`; `ai_param_target_class_weight_b.c:13-43`; `ai_param_owner_chain_length_weight.c` |
| Base +0x18 state writers; unit +0x18 mode producer; D_80125858 meaning; kind 0x1B identity | UNRESOLVED (caller-supplied) | X8 / W7c evidence |
| Dropped queens not counted; zero-length rail ratio 0; empty linked slot 0/0; 0x2113 objective >= 4 weight 0 | TEMPORARY POLICY | W7c evidence |

### Spawners (`spawner_runtime`)

| Behavior | Class | Citation |
|---|---|---|
| A linked spawner with no kind-5 serial is created unlinked with `SpawnerSlot::link_failed = true` and still counted | TEMPORARY POLICY (retail writes through a null pointer) | `assets/load_level_data.c:470-476` |
| One `MemberMarker` span serves both respawn paths | CONFIRMED (session +0x370) | W4b ledger H3 |
| Spawner mode > 3 maps to tank | STRONG INFERENCE | retail switch fallthrough |
| Writer of squad +0x22 (alive) | UNRESOLVED | `spawn_unit.c` has none |

### Effect clock and script chaining (`effect_runtime`)

| Behavior | Class | Citation |
|---|---|---|
| Effect clock = low word of the u64 game clock, >>16, stored u16; paused time excluded | CONFIRMED | asm func_8007B268; func_800ED8C0 0x800ED950; `runtime/update_game_clock.c` |
| 46.875 MHz counter: 1 tick = 23.84 clock units, 1 s = 715.26 units | CONFIRMED (derived) | `libultra/os_initialize.c:98-105`; ROM 0xD5DA0 |
| `effect_clock::for_game_tick` phase base 0 and per-tick sampling | TEMPORARY POLICY | `effect_runtime.hpp:54-67` |
| Scripts run until a NULL handler with no length bound; script 17 runs into script 18 (16 commands, step 24 ends) | CONFIRMED | `run_effect_script.c`; ROM table at 0x801386FC |
| Oscillator defs 60..62 run on game ticks with u32 duration 600 | CONFIRMED | `update_effect_particle_oscillation.c`; ROM 0x80139540/54/68 |
| Legacy `update(clock)` overload reuses the stored game tick (oscillators freeze for callers not passing it) | TEMPORARY POLICY | F4 ledger |
| Particle bounce RNG for defs 32..35 | UNRESOLVED | F4 ledger finding 8 |

## Data flow

- `campaign_flow` selects a wave from `campaign_catalog`, applies carry and
  bonus, and emits a battle request.
- The host (or `campaign_harness`) builds a session from the decoded level,
  `campaign_spawn` placement and the wave definition.
- Gameplay modules exchange entity IDs and typed events only
  (`runtime_events`); no event carries a host pointer.
- The session result returns to `campaign_flow`, which emits a
  `FlowSaveRequest` when retail would prompt to save; the save transaction
  turns it into a pending record and commits it after a successful write.
- `frontend_model` and `music_selection` read flow state and emit identifiers
  for the host to render or play.

## Determinism rules

- All state serializes through `StateWriter` in explicit big-endian; checksums
  are SHA-256 over that stream.
- Iteration is ascending slot order; pools allocate the lowest free slot and
  use generation-checked handles. No unordered containers.
- Randomness only from `RetailRandom`; angles from `AngleTable` and the
  `runtime_math` helpers. No wall clock, `std::rand` or libm trigonometry.
- Retail-derived indices are bounds-checked; unknown behavior returns a typed
  unsupported result rather than guessing.
- Pool exhaustion fails the spawn without changing state, as a NULL retail
  allocation does.

## Portability

- Serialized and retail-derived fields use `std::uint8_t`..`std::int32_t`;
  never `long`, `size_t` or pointer values, so LP64 (Linux), LLP64 (Windows)
  and ILP32 targets produce identical streams.
- Enum underlying types are fixed and checked with `static_assert`.
- No struct packing, no `reinterpret_cast`, no pointer/integer conversion;
  ROM tables are decoded by value from big-endian bytes.
- Public type names must be unique in `battletanx`. Most module-specific
  types live in sub-namespaces; `route_service` keeps its `Route*`-prefixed
  types in `battletanx`.
