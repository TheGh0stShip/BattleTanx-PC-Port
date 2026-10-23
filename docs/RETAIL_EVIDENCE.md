# Retail evidence index

Index of the retail (USA Rev 0) evidence behind the native gameplay and
campaign runtime. Paths are relative to the decompilation root. For the main
program image, ROM offset = VRAM - 0x80070000 (big-endian). Each header
repeats the detailed per-field evidence; this page is the map.

Classification:

- **CONFIRMED**: read directly from matching decomp code or ROM bytes.
- **STRONG INFERENCE**: consistent reading of several call sites, not a single
  direct statement.
- **TEMPORARY POLICY**: native choice standing in for unrecovered behavior;
  named in code and replaceable.
- **UNRESOLVED**: not recovered; native code returns a typed unsupported
  result.

## Frame loop

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Campaign driver walks 17 waves, plays cutscene sequences 15/16/17/18 before waves 0/5/9/13, sets game mode 9 | `src/code/gameplay/run_campaign_waves.c` | `run_campaign_waves` 0x8007B280 | CONFIRMED |
| Driver plays each wave through the endurance wrapper, which repeats the wave body while `params->result` is set | `src/code/gameplay/play_endurance_wave.c` | `play_endurance_wave` 0x8007B9E4 | CONFIRMED |
| Wave body: load level, `setup_players`, support assets, clock reset, then per frame `reset_runtime_pools` -> `update_match_state` -> audio refresh -> overlays -> frame finalize -> frame counter -> `update_game_clock` | `src/code/gameplay/play_campaign_wave.c` | `play_campaign_wave` 0x8007BD24 | CONFIRMED |
| Unit, computer-controlled unit and effect updates run from scheduled tasks inside the frame, not inline in the wave body | `src/code/gameplay/ai_run_update_phase.c` | `ai_run_update_phase` 0x800E6754 | STRONG INFERENCE |
| Native per-tick order today (release dead, shells, units incl. computer control, pickups, match state, objective, respawns, tick); the campaign target order adds owner plays, route service, placed objects/spawners/deployables/effects and queen carry, see CAMPAIGN_GAMEPLAY.md | native `battle_session.cpp` `BattleSession::step` | — | TEMPORARY POLICY |

## Effects pool

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| 1024 effect slots, free list built over 1023 links | `src/code/effects/init_effect_kind_table.c` | `D_802C3A30` | CONFIRMED |
| Allocation pops the free-list head; empty list returns NULL; group 1 vs other lists; serial from `D_80125C40` | `src/code/effects/allocate_effect.c` | `allocate_effect` 0x8008AD10 | CONFIRMED |
| Release returns the node to the free list | `src/code/effects/release_effect_pool_node.c` | 0x8008B030 | CONFIRMED |
| Active list update walks per-kind callbacks | `src/code/effects/update_active_effect_list.c` | 0x8008B2C8 | CONFIRMED |
| Native dedicated pools per effect family instead of one shared pool: one projectile pool (`ShellPool`, kinds 2/12/13/14/15/22/34), pickups, deployables | native `projectile_runtime.hpp` | — | TEMPORARY POLICY |
| Effect clock = low word of the u64 game clock >>16 (u16); 1 tick = 23.84 units at 46.875 MHz | `src/code/runtime/update_game_clock.c`; `src/code/libultra/os_initialize.c:98-105` | func_8007B268, func_800ED8C0 0x800ED950; ROM 0xD5DA0 | CONFIRMED |
| Effect scripts run to a NULL handler with no bound; script 17 continues into script 18 | `src/code/effects/run_effect_script.c` | table 0x801386FC | CONFIRMED |
| Oscillator defs 60..62 use game ticks, duration 600 | `src/code/effects/update_effect_particle_oscillation.c` | 0x80139540/54/68 | CONFIRMED |
| `effect_clock::for_game_tick` phase base 0, per-tick sampling | native `effect_runtime.hpp` | — | TEMPORARY POLICY |

## Units

| Claim | Decomp path | Symbol / VRAM | ROM | Class |
|---|---|---|---|---|
| 20 unit records of 0x508 bytes (4 owners x 5 slots) | `src/code/entities/get_unit.c` | `D_802D8180` | — | CONFIRMED |
| 4 owner records of 0x348 bytes | `src/code/entities/get_entity_record.c` | `D_802DE620` | — | CONFIRMED |
| Movement table grip/mass/accel/drag indexed bike=0, tank=1, goliath=2 | `src/code/gameplay/spawn_unit.c` | `spawn_unit` 0x80097038; `D_80125EF0..D_80125F24` | 0xB5EF0..0xB5F24 | CONFIRMED |
| Unit field +0x3BC use | `src/code/gameplay/spawn_unit.c` | `D_80125EFC` | 0xB5EFC | UNRESOLVED |

## Combat

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Damage application with attacker owner credit | `src/code/gameplay/apply_unit_damage.c` | `apply_unit_damage` 0x800983E0 | CONFIRMED |
| Destruction, kill tally via `add_team_counter` | `src/code/gameplay/destroy_unit.c` | `destroy_unit` 0x80097DD0; 0x800A5B6C | CONFIRMED |
| Shell motion and hit resolution (effect kind 34) | `src/code/effects/update_bullet_effect.c` | `update_bullet_effect` 0x8008E480 | CONFIRMED |

## Weapons

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Weapon ids 0..16 dispatched by `fire_unit_weapon`; id indexes ammo array +0x480 | `src/code/gameplay/fire_unit_weapon.c` | 0x800987D8 | CONFIRMED |
| Per-id effect kinds (34 bullets, 2 spread, 22 missile, 12/14/15, 29 placed blast) | `src/code/gameplay/fire_unit_weapon.c` | 0x800987D8 | CONFIRMED |
| Beam (weapons 5/10): length 4000, message 12 damage 0x26, forks past depth 16 | `src/code/effects/spawn_beam_effect.c`; `src/code/entities/unit_handle_hit_message.c:68-73` | — | CONFIRMED |
| Nuke (weapon 6) flight and script-17 ring: message 3 amount 50+(1-d/4000)*50 once per target | `src/code/effects/update_bouncing_debris.c`; `src/code/entities/apply_proximity_hit_to_target.c` | ROM 0x4E70/0x4E74 | CONFIRMED |
| Grenade (weapon 7): message 7 radius 100 amount 22 | `src/code/effects/update_bouncing_grenade.c` | — | CONFIRMED |
| Missile (weapon 11) guidance is owner steering, not auto-acquisition | `src/code/effects/update_missile_effect.c` | — | CONFIRMED |
| Projectile ignore = firing unit only; world normal = -direction; passed beam units excluded | native `projectile_runtime.cpp` | — | TEMPORARY POLICY |
| Turret bullets: effect 34, speed {70,48,48,48}, kind {1,0,0,3}, damage owner +0x338, life 0x70; type 3 fires a beam | `src/code/effects/update_turret.c:332-340`; `src/code/effects/spawn_effect_kind_22.c` | func_8008E29C | CONFIRMED |
| Owner +0x338/+0x340 difficulty-profile values | asm func_800A31C0 | 0x800A3474-0x800A34A0 | UNRESOLVED |
| Effect-family visuals beyond shells and placed blasts | various `src/code/effects/` | — | UNRESOLVED |

## Deployables

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Weapon 12 lays a mine (sound 0x19), weapon 13 a squad unit; unit flag 0x2000 selects the powered grid | `src/code/gameplay/fire_unit_weapon.c` | 0x800987D8 | CONFIRMED |
| Mine message handler (contact, wake, position) | `src/code/effects/timed_effect_handle_message.c` | 0x8010DDD4 | CONFIRMED |
| Area-checked deploy spawn | `src/code/effects/spawn_effect_kind1d_if_area_clear.c` | 0x8010DAA0 | CONFIRMED |
| Dedicated mine/grid pools; grids update before mines | native `deployables_runtime.hpp` | — | TEMPORARY POLICY |
| Weapons 12/13 routed from `WeaponContext::deployables`; deployables never use the projectile pool | native `weapon_runtime.hpp` | — | TEMPORARY POLICY |

## Placed objects

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Kind 5 messages 3/7 release and remove owned effects; message 3 also sets flag 0x10; no score | `src/code/effects/frame_effect_handle_message.c:56-68` | func_8009345C | CONFIRMED |
| Kind 9 crushables: messages 3/7 fling and release; unit contact replies 4 (scrape -1.2) / 5 (crush -6.0) | `src/code/effects/slot_heading_effect_handle_message.c:44-87`; `pulse_effect_handle_message.c:72-100`; `update_tank_drive.c:618-627` | func_80093A5C, func_80093590 | CONFIRMED |
| Flag-1 collapse: 60 frames, stage++, flags &= 0x6F when health > 0 | `src/code/effects/update_effect_kind_5.c:380-427` | func_80092734 0x80092FA4 | CONFIRMED |
| Flag-1 clear path keeps 0x80 | `src/code/effects/update_effect_kind_5.c:428-442` | — | CONFIRMED (significance UNRESOLVED) |
| Message 37; kind 6 message 7; debris model mapping | — | — | UNRESOLVED |
| Fling heading: arccosine of the X offset (object - source), negated when the Z offset is negative | `src/code/effects/grid_effect_handle_message.c:79-102`; `slot_heading_effect_handle_message.c:56-67` | cosine_to_angle 0x800F3BE4 | CONFIRMED |
| Kind 18 record: x at +0x10, z at +0x0C; unit contact (source 0) replies 4, sources 2/12/22/34 reply 0 | `src/code/effects/grid_effect_handle_message.c:48-90` | func_80090BE0 | CONFIRMED |
| Turret blast falloff applied to turret health (`turret_blast_policy`) | `src/code/effects/update_distance_scaled_debris_emitter.c` (crate emitter, not a turret path) | func_800A2A44 | TEMPORARY POLICY |

## Effect families

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Effect kind table init over the 1024-slot pool | `src/code/effects/init_effect_kind_table.c` | 0x8008B9E8 | CONFIRMED |
| Simulated kinds: 0x26 script runner and particles, 0x2A muzzle flash, 4 + 0x2B impact decals; none plays a sound | native `effect_runtime.hpp` | — | STRONG INFERENCE |
| Status overlays set by the caller from pickup/objective state | native `effect_runtime.hpp` | — | TEMPORARY POLICY |

## Pickups

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| `apply_pickup` cases 0..19; kind 14 scores 900 | `src/code/gameplay/apply_pickup.c` | 0x80099D90 | CONFIRMED |
| Pickup effect kind 3 spawn | `src/code/effects/spawn_effect_kind_03.c` | 0x800911A0 | CONFIRMED |
| Pickup effect kind 3 update | `src/code/effects/update_respawning_pickup_effect.c` | 0x800913AC | CONFIRMED |
| Pickup effect kind 3 contact | `src/code/effects/effect_handle_unit_contact.c` | 0x800916B8 | CONFIRMED |
| Queen effect kind 21 spawn | `src/code/effects/spawn_effect_kind_15.c` | 0x800A6958 | CONFIRMED |
| Owner base effect kind 20 | `src/code/entities/create_entity_point_record.c` | 0x800A6AB4 | CONFIRMED |
| Capture marker kind 31 sets result 9 when unblocked | `src/code/runtime/set_result_nine_if_unblocked.c` | 0x800A7614 | CONFIRMED |
| Queen collection and base storage | `src/code/gameplay/collect_pickup.c`, `src/code/effects/attach_effect_to_owner_group.c` | 0x800A70E0, 0x800A6E30 | CONFIRMED |

## Spawns

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Owner setup and per-owner record initialisation | `src/code/gameplay/setup_players.c` | `setup_players` 0x800A5FF0; `initialize_player_record` 0x800A31C0 | CONFIRMED |
| Entity bank reads the one group named by world header +0x280 | `src/code/gameplay/spawn_bank_record_entries.c` | 0x80088670 | CONFIRMED |
| Placed-object loop reads every other group; spawner types 20/21 | `src/code/assets/load_level_data.c` | 0x80088CF0 | CONFIRMED |
| Spawner effect, fast interval from `D_80134E64` | `src/code/effects/spawn_effect_kind_1c.c` | 0x8010D480 | CONFIRMED |
| Spawner tick: gate and interval checks, then roll (`&0xF`), then the mode-3 bit draw | `src/code/gameplay/spawn_available_owner_unit.c` | 0x8010D59C | CONFIRMED |
| Squad member respawn skips a marker while `lastUsed > tick-360` | `src/code/gameplay/respawn_squad_member.c` | 0x800A5784 | CONFIRMED |
| Squad unit respawn: lineup cycle, then area checks; last campaign marker used when none is clear | `src/code/gameplay/respawn_squad_unit.c` | 0x800A5348 | CONFIRMED |
| Respawn gating from squad state | `src/code/gameplay/update_squad_state.c` | 0x800A3DE0 | CONFIRMED |
| Full spawn-request queue: spawner/squad state unchanged after the retail draws and lineup advance | native `spawner_runtime.hpp` | — | TEMPORARY POLICY |
| Placed types other than spawners | `src/code/assets/load_level_data.c` | 0x80088CF0 | UNRESOLVED |
| Linked spawner without a kind-5 serial is created unlinked (`link_failed`) | `src/code/assets/load_level_data.c:470-476` | — | TEMPORARY POLICY |

## Objectives

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Match state machine in `D_8012578C`, live state 1 selects one mechanism per game kind | `src/code/gameplay/update_match_state.c` | 0x8007F2A0 | CONFIRMED |
| Kinds 0/3/6 goal slot check | `src/code/entities/mark_entities_for_matching_slot.c` | 0x800A6798 | CONFIRMED |
| Kind 9 team counter win | `src/code/gameplay/add_team_counter.c` | 0x800A5B6C | CONFIRMED |
| Writer of the kind 3/6 goal slot result | — | — | UNRESOLVED |
| Base/queen pass runs only for wave kinds 0/3/4/6; flag-0x40 bases skipped in kinds 3..6; 20 queens in the campaign | `src/code/runtime/apply_mode_display_rows.c:16-25`; `src/code/assets/load_level_data.c:400-401` | func_8007C2A0, func_800A4B2C | CONFIRMED |
| Owner +0x1B8 base, unit +0x98 carried queens, parameter 0x2111 | `src/code/gameplay/assign_group_object_points.c`; asm func_800E8934 | 0x800A70E0 | CONFIRMED |

## Campaign flow

| Claim | Decomp path | Symbol / VRAM | ROM | Class |
|---|---|---|---|---|
| `gOpponentWaves`: 17 records of 0x220 bytes | `src/code/gameplay/opponent_wave_data.c` | `D_80142DE8` | 0x0D2DE8 | CONFIRMED |
| Lookup by wave id; wave copy patched with bonus/offset and `D_80134E64` | `src/code/gameplay/run_campaign_waves.c` | 0x8007B280 | — | CONFIRMED |
| Loss ends the run unless wave kind is 7, which advances | `src/code/gameplay/run_campaign_waves.c` | 0x8007B280 | — | CONFIRMED |
| Frontend state 19 after the last wave | `src/code/gameplay/run_campaign_waves.c` | `D_801B4AE0` | — | CONFIRMED |
| Save prompt flag set/cleared in match state 7 | `src/code/gameplay/update_match_state.c` | `D_8012579B` | — | CONFIRMED |

## Computer-controlled plays

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Per-owner play stack update | `src/code/gameplay/ai_update_owner_play_stack.c` | 0x800EA89C | CONFIRMED |
| Stack push/update | `src/code/gameplay/ai_play_stack_push.c`, `ai_play_stack_update.c` | 0x800EA1B8, 0x800EA478 | CONFIRMED |
| Six named plays and parameter provider tables (24/24/28) | `src/code/gameplay/ai_play_tables.c` | — | CONFIRMED |
| Root play selection (kind 1) from owner +0x1A6 and game mode | `src/code/gameplay/ai_play_stack.c` | `ai_select_root_play` 0x800EA2A4 | CONFIRMED |
| Member play assignment in squad-slot order, strict `>` keeps earlier member on ties | `src/code/gameplay/ai_assign_member_play_slots.c` | 0x800E9E5C | CONFIRMED |
| Weighted owner-play policy choice | `src/code/gameplay/choose_weighted_rule.c` | 0x800EA93C | CONFIRMED |
| Owner plays other than 2/10/12/14/15/25/26 | — | — | UNRESOLVED |
| Best-scored unit selection | `src/code/gameplay/ai_select_best_scored_unit.c` | 0x800E3CF0 | CONFIRMED |
| Skill = owner*0.6 + pair average*0.4 | `src/code/gameplay/initialize_unit_ai_state.c` | 0x800E4104 | CONFIRMED |
| Round-robin per-unit update budget | `src/code/gameplay/ai_update_units_round_robin.c` | 0x800E63F4 | CONFIRMED |
| Unit pair table and rotor (link 400, range 1800/2000/0.95) | `src/code/gameplay/build_unit_pair_records.c`; `ai_run_update_phase.c:60-114` | func_800E5328, func_800E6754; ROM 0x4390-0x4398 | CONFIRMED |
| Parameter keys 0x1102/03/08/09/10/11/13 and 0x210E/13/16/17 | `src/code/gameplay/ai_parameter_*.c`, `ai_param_*.c` | ROM 0x4810-0x4868, 0x47D0-0x47F0 | CONFIRMED |
| Parameter fallbacks (zero rail, empty linked slot, objective >= 4) | native `campaign_ai_parameters.hpp` | — | TEMPORARY POLICY |

## Navigation

| Claim | Decomp path | Symbol / VRAM | ROM | Class |
|---|---|---|---|---|
| Route ring 0xF8 entries (decomp name is a misnomer) | `src/code/compression/initialize_bitstream_header.c` | 0x800DDCA4 | — | CONFIRMED |
| Planner clears 0xBB8 nodes (bound read from the disassembly; callers in `find_ai_route_step.c`) | `src/code/gameplay/find_ai_route_step.c` | 0x800DEE8C, 0x800DFA68 | — | CONFIRMED |
| Arrival radius 40.0 | — | `D_800744E0` | 0x44E0 | CONFIRMED |
| Edge cost 1000.0 per occupancy, 200.0 per step | — | `D_80074404..D_80074410` | 0x4404..0x4410 | CONFIRMED |
| Record kind classes 0..4 | `src/code/gameplay/classify_object_record_kind.c` | 0x800DF214 | — | CONFIRMED |
| Route service teammate occupancy radius 3000.0 (inclusive) | — | `D_800743AC` (func_800DEE8C) | 0x43AC | CONFIRMED |
| Occupancy weight divisor 1000.0 (`4 - trunc(d/1000)`) | — | `D_800743B0` (func_800DEE8C) | 0x43B0 | CONFIRMED |
| Modes 6/12 direct-steer radii 300.0 / 500.0 (500 only when unit +0x13C == 1) | — | `D_800746A4`, `D_800746A0` (func_800E5FA8) | 0x46A4, 0x46A0 | CONFIRMED |
| Unit triangle re-traced only when unit +0x35A != 0 | `asm/us/functions/func_800DEE8C.s` 0x800DEF60..0x800DEF7C | func_800DEE8C | — | CONFIRMED (field meaning UNRESOLVED) |
| Goal-side trace start triangle (session +0x11954) | — | func_800DEE8C, func_800DE3EC | — | UNRESOLVED |

## Save record

| Claim | Decomp path | Symbol / VRAM | Class |
|---|---|---|---|
| Progress written after a wave only when `D_8012579B` is set; next wave id, owner 0 field +0x210, session value, bonus | `src/code/gameplay/run_campaign_waves.c` | 0x8007B280 | CONFIRMED |
| Writer stores fields into the live 256-byte record and writes the Controller Pak; result ignored | `src/code/assets/write_controller_pak_progress.c` | 0x800D0828 | CONFIRMED |
| Native commit only after host acknowledges the write | native `campaign_save_transaction.hpp` | — | TEMPORARY POLICY |
| Undecoded record bytes | — | — | UNRESOLVED (carried verbatim) |
