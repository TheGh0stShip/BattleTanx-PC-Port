#pragma once

#include "battletanx/entity_id.hpp"
#include "battletanx/projectile_runtime.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

// Renderer- and audio-neutral simulation of the retail effect pool
// (D_802C3A30[1024], init_effect_kind_table func_8008B9E8) for the effect
// kinds the campaign needs: the kind-0x26 script runner and the particles it
// spawns, the muzzle flash (0x2A), and impact decals (kind 4 + 0x2B smoke).
// Consumes projectile_runtime's EffectEvent stream; never duplicates it. Output is
// value-only render items. No retail effect kind in this module plays a
// sound, so the audio side stays with combat/weapon SoundEvents.
namespace battletanx::effects {

inline constexpr std::uint16_t pool_capacity=1024U;     // D_802C3A30[1024]
inline constexpr std::int16_t no_link=-1;
inline constexpr std::uint8_t script_count=28U;         // gEffectScriptTable D_80138F3C
inline constexpr std::uint8_t script_length=16U;        // EFFECT_SCRIPT_LENGTH
// run_effect_script (func_800ED994) has no length bound: it reads script[step]
// until a NULL handler. gEffectScripts is one contiguous [28][16] block, so a
// script that fills all 16 commands continues into the next one (CONFIRMED by
// ROM: script 17 runs into script 18 and stops at flat step 24).
inline constexpr std::uint16_t script_table_steps=static_cast<std::uint16_t>(script_count*script_length);
inline constexpr std::uint8_t particle_def_count=63U;   // gEffectParticleDefTable D_80137940
inline constexpr std::uint8_t variant_rotating=0x1CU;   // spawn_effect_kind_26: cycles 0..12
inline constexpr std::uint8_t rotating_variant_count=13U;
inline constexpr std::uint8_t muzzle_flash_def=0x24U;   // spawn_effect_kind_2a_on_owner templateIndex
inline constexpr std::uint8_t decal_queue_capacity=10U; // spawn_queued_effect_kind_2b
inline constexpr std::uint16_t model_slot_count=182U;   // D_802D5A30[182] (load_level.c)
inline constexpr std::uint8_t beam_visual_capacity=64U;

// ---------------------------------------------------------------------------
// Effect clock. Every pool effect reads read_runtime_counter_pair (func_8007B268)
// and keeps the LOW word of the paused-time-excluded counter accumulator
// gClockPreviousTime (D_801257A0, u64; low word D_801257A4) shifted right by 16
// (asm func_800ED8C0 0x800ED950: srl v1,v1,16; sh into +0x0E/+0x10).
// The accumulator advances by osGetTime() deltas (update_game_clock func_8007AD2C)
// at the CPU count rate. osInitialize sets osClockRate = header clock (ROM +0x04
// is 0x0000000F, masked to 0 -> default 62,500,000) * 3 / 4 = 46,875,000 Hz,
// and gGameTicks = elapsed * 1e6 / osClockRate * 30 / 1e6, so one 30 Hz game tick
// is exactly 46,875,000 / 30 = 1,562,500 counts = 1,562,500 / 65,536 =
// 23.841857... effect clock units (715.2557... units per second).
// CONFIRMED: rate and shift. TEMPORARY POLICY: the accumulator phase (retail
// starts at the boot-time count in reset_game_clock) is a caller-supplied base,
// default 0, and the clock is sampled once per game tick (retail samples every
// video frame, which only changes sub-tick granularity).
namespace effect_clock {
inline constexpr std::uint32_t counter_hz=46'875'000U;       // osClockRate after osInitialize
inline constexpr std::uint32_t game_ticks_per_second=30U;    // update_game_clock GAME_TICKS_PER_SECOND
inline constexpr std::uint32_t counts_per_game_tick=counter_hz/game_ticks_per_second; // 1,562,500 (exact)
inline constexpr std::uint32_t counts_per_unit=65'536U;      // >> 16
static_assert(counts_per_game_tick*game_ticks_per_second==counter_hz);

// Effect clock at the start of game tick `tick` given a counter-accumulator base.
// Uses 64-bit arithmetic and keeps the low 32 bits exactly as the retail low-word read.
[[nodiscard]] constexpr std::uint16_t for_game_tick(std::uint32_t tick,std::uint64_t base_count=0U) noexcept {
    const std::uint64_t count=base_count+static_cast<std::uint64_t>(tick)*counts_per_game_tick;
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(count&0xFFFF'FFFFU)>>16U);
}
} // namespace effect_clock

// ---------------------------------------------------------------------------
// Per-group enable table D_80125C44[3][0x2E] (u8; VRAM 0x80125C44, ROM 0x000B5C44).
// update_active_effect_list(group) only updates nodes whose kind is enabled for
// the group; disabled nodes stay linked and untouched. Group callers in
// update_match_state.c: 0 (lines 174/189/292), 1 (line 279, game mode 2 = paused,
// update_game_clock GAME_MODE_PAUSED), 2 (lines 337/377, modes 6/7).
enum class UpdateGroup : std::uint8_t { normal=0U, paused=1U, match_end=2U };
static_assert(std::is_same_v<std::underlying_type_t<UpdateGroup>,std::uint8_t>);
inline constexpr std::uint8_t kind_count=0x2EU;
inline constexpr std::uint8_t update_group_count=3U;
inline constexpr std::array<std::array<std::uint8_t,kind_count>,update_group_count> group_enable{{
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0},
    {0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
}};
[[nodiscard]] constexpr bool kind_enabled(UpdateGroup group,std::uint8_t kind) noexcept {
    const auto g=static_cast<std::uint8_t>(group);
    return g<update_group_count && kind<kind_count && group_enable[g][kind]!=0U;
}

// ---------------------------------------------------------------------------
// Model slot indices into the per-level slot table D_802D5A30[182] (s16).
namespace models {
inline constexpr std::uint16_t beam=0x0AU;              // D_802D5A44 (notify_listeners_along_sweep)
inline constexpr std::uint16_t impact_decal=3U;       // D_802D5A36 (get_impact_model_part, func_8008CED0)
inline constexpr std::uint16_t wreck_type0=0x15U;     // draw_kind_selected_level_model func_800A05A0 (kind 26)
inline constexpr std::uint16_t wreck_type1=0x23U;     //   type 1, variant 0
inline constexpr std::uint16_t wreck_type1_alt=0x48U; //   type 1, variant != 0
inline constexpr std::uint16_t wreck_type2=0x2FU;
inline constexpr std::uint16_t point_marker_base=0x5FU; // draw_marker_entity func_800A6BE0 (kind 21): 0x5F - team
inline constexpr std::uint16_t point_marker_mode16=0xB3U; // game mode 0x10 with owner != 0
inline constexpr std::uint16_t point_marker_paused=0x18U; // D_802D5A60 when D_80135776 != 0
inline constexpr std::uint8_t crate_kind_count=4U;
inline constexpr std::uint16_t crate=73U;             // D_802D5A30[D_80125FB0[k]], D_80125FB0 = {73,73,73,73}
inline constexpr std::uint16_t crate_boost=79U;       // D_80126000 = {79,79,79,79}; draws 79 and 79+1
inline constexpr std::uint8_t pickup_kind_count=18U;  // STRONG INFERENCE length (values after are not slots)
// D_80125CE0[18] (s32, VRAM 0x80125CE0, ROM 0x000B5CE0): pickup kind -> slot (draw_spinning_level_model, kind 3).
inline constexpr std::array<std::uint16_t,pickup_kind_count> pickup{
    1,22,16,2,23,63,64,66,68,76,78,90,85,86,87,88,89,90};

[[nodiscard]] constexpr std::optional<std::uint16_t> for_pickup(std::uint8_t kind) noexcept {
    if (kind>=pickup_kind_count) return std::nullopt;
    return pickup[kind];
}
[[nodiscard]] constexpr std::optional<std::uint16_t> for_wreck(std::int32_t unit_type,std::uint8_t variant) noexcept {
    switch (unit_type) {
    case 0: return wreck_type0;
    case 1: return variant!=0U ? wreck_type1_alt : wreck_type1;
    case 2: return wreck_type2;
    default: return std::nullopt; // retail draws model 0
    }
}
[[nodiscard]] constexpr std::optional<std::uint16_t> for_point_marker(std::uint8_t team,bool mode16_non_host) noexcept {
    if (mode16_non_host) return point_marker_mode16;
    if (team>point_marker_base) return std::nullopt;
    return static_cast<std::uint16_t>(point_marker_base-team);
}
} // namespace models

// Retail effect kind numbers (D_802C3800 index) for the kinds simulated here.
enum class EffectKind : std::uint8_t {
    decal=0x04U,        // func_80093D50 queue record, group 0
    script=0x26U,       // spawn_effect_kind_26, update run_effect_script func_800ED994
    particle=0x27U,     // spawn_scripted_particle func_800EDB20, update func_800EDC64
    ramp=0x28U,         // spawn_effect_kind_28 func_800EE000, update func_800EE164
    oscillator=0x29U,   // func_800EE4D4, update func_800EE75C (never retires; restarts its sweep
                        // after the def's u32 duration, measured in 30 Hz game ticks)
    muzzle_flash=0x2AU, // func_800EED5C, update func_800EDC64
    smoke=0x2BU,        // spawn_effect_kind_2b, update func_8010BEFC (lifetime countdown)
};
static_assert(std::is_same_v<std::underlying_type_t<EffectKind>,std::uint8_t>);

// Named kind-0x26 variants used by the campaign paths (caller files cited).
namespace variant {
inline constexpr std::uint8_t area_marker_small=0x07U;  // spawn_area_marker_effects / breakable object
inline constexpr std::uint8_t debris_landed=0x08U;      // update_bouncing_debris
inline constexpr std::uint8_t tank_destroyed=0x0EU;     // destroy_unit, unit type != 1
inline constexpr std::uint8_t light_unit_destroyed=0x05U; // destroy_unit, unit type 1; destroy_placed_object
inline constexpr std::uint8_t impact_small=0x09U;       // update_shell/bullet_effect
inline constexpr std::uint8_t impact_cannon=0x0BU;      // update_shell/bullet/missile_effect
inline constexpr std::uint8_t blast_armed=0x0DU;        // delayed_blast_effect_update
inline constexpr std::uint8_t killed_by_hit=0x13U;      // unit_handle_hit_message case 12
inline constexpr std::uint8_t object_collapse=0x14U;    // destroy_placed_object debris 0x40000
inline constexpr std::uint8_t object_blast=0x16U;       // destroy_placed_object flags 0x10
inline constexpr std::uint8_t impact_object=0x17U;      // update_shell/bullet_effect
inline constexpr std::uint8_t grenade=0x1AU;            // update_bouncing_grenade
inline constexpr std::uint8_t area_marker=0x1BU;        // spawn_area_marker_effects / trigger_slot_area_burst
} // namespace variant

enum class ScriptOp : std::uint8_t { end, spawn_particle, spawn_ramp, spawn_oscillator, wait };
static_assert(std::is_same_v<std::underlying_type_t<ScriptOp>,std::uint8_t>);

// One gEffectScripts command: handler + four s16 args. For spawns arg0 is the
// particle def index and arg1..arg3 the x/y/z offset; for wait arg0 is the
// elapsed threshold (sound_duration_elapsed: elapsed >= arg0).
struct ScriptStep {
    ScriptOp op{};
    std::int16_t arg0{}, arg1{}, arg2{}, arg3{};
};

struct ParticleDef {
    std::uint16_t duration{}; // def +0x04 (u16)
    std::uint16_t flags{};    // def +0x0C: 0x100 chain to next def, 0x80 anchored, 0x10 light
};

[[nodiscard]] const std::array<ScriptStep,script_length>* script_for_variant(std::uint8_t variant) noexcept;
// Command `step` of the variant's script read through the flat table the way
// retail does (may run into the following script). nullptr past the table end.
[[nodiscard]] const ScriptStep* script_step(std::uint8_t variant,std::uint16_t step) noexcept;
// Oscillator (kind 0x29) sweep duration in game ticks: ParticleSweepDef +0x04 u32
// (update_effect_particle_oscillation). ROM: defs 60/61/62 (0x80139540/54/68) = 600.
// nullopt for defs that are not oscillator defs (spawn reports unsupported_def).
inline constexpr std::uint8_t oscillator_def_first=60U;
inline constexpr std::array<std::uint32_t,3> oscillator_duration{600U,600U,600U};
[[nodiscard]] constexpr std::optional<std::uint32_t> oscillator_duration_for(std::uint8_t def) noexcept {
    if (def<oscillator_def_first || def-oscillator_def_first>=static_cast<int>(oscillator_duration.size())) return std::nullopt;
    return oscillator_duration[static_cast<std::size_t>(def-oscillator_def_first)];
}
[[nodiscard]] const ParticleDef* particle_def(std::uint8_t index) noexcept;

enum class SpawnStatus : std::uint8_t { spawned, pool_exhausted, unsupported_variant, unsupported_def };
static_assert(std::is_same_v<std::underlying_type_t<SpawnStatus>,std::uint8_t>);

struct SpawnResult {
    SpawnStatus status{};
    std::int16_t slot{no_link};
};

struct EffectNode {
    std::int16_t next{no_link};
    bool in_use{};
    bool persistent_group{};    // allocate_effect group 0 (true) or 1 (false)
    std::uint32_t serial{};     // D_80125C40 at allocation (the first allocation is also 0; use in_use, not serial, for liveness)
    EffectKind kind{};
    std::uint8_t index{};       // script variant or particle def index
    std::uint8_t step{};        // script step (+0x14), flat from the script start (can exceed 15)
    std::uint8_t cycle{};       // particle chain count (+0x0D)
    std::uint16_t start{};      // clock at spawn / chain restart (oscillator: game tick)
    std::uint16_t now{};
    std::uint16_t elapsed{};
    std::int32_t lifetime{};    // smoke countdown, -1 = until stopped
    std::int16_t child{no_link};// decal -> its smoke node
    std::uint32_t child_serial{};// serial of that smoke node; guards against slot reuse
    float normal_x{}, normal_z{};// decal +0x0C/+0x10 (negated hit normal)
    EntityId surface{};         // decal +0x20 hit object
    std::uint16_t model_slot{}; // decal part slot
    float x{}, y{}, z{};
    EntityId anchor{};          // muzzle flash owner unit
    std::uint8_t owner{no_owner};
};

// Status overlays drawn from unit/objective state rather than the pool.
// TEMPORARY POLICY: the retail draw paths for these are not ported here; the
// caller sets them from pickup_effects / objective_runtime state.
enum class StatusVisualKind : std::uint8_t {
    shield, cloak, radar, pickup, queen_marker, base_marker, objective_cue, result_cue,
};
static_assert(std::is_same_v<std::underlying_type_t<StatusVisualKind>,std::uint8_t>);
inline constexpr std::uint8_t status_visual_capacity=32U;

struct StatusVisual {
    bool active{};
    StatusVisualKind kind{};
    EntityId subject{};
    std::uint8_t owner{no_owner};
    std::uint16_t model_slot{};  // D_802D5A30 index, bounds-checked against model_slot_count
    std::int32_t remaining{};    // ticks; -1 = until cleared
    float x{}, y{}, z{};
};

enum class RenderItemKind : std::uint8_t { pool_effect, status, beam };
static_assert(std::is_same_v<std::underlying_type_t<RenderItemKind>,std::uint8_t>);

struct EffectRenderItem {
    RenderItemKind item{};
    EffectKind kind{};
    StatusVisualKind status{};
    std::int16_t slot{no_link};
    std::uint32_t serial{};
    std::uint8_t index{};
    std::uint16_t elapsed{};      // oscillator: game ticks
    std::uint16_t duration{};     // oscillator: game ticks
    std::uint16_t flags{};
    std::uint16_t model_slot{};
    EntityId anchor{};
    std::uint8_t owner{no_owner};
    float x{}, y{}, z{};
    float normal_x{}, normal_z{}; // decal only
    std::uint16_t angle{};         // beam only
    float length{};                // beam only
};
using EffectRenderQueue=EventQueue<EffectRenderItem>;

enum class EventMapping : std::uint8_t { spawned, pool_exhausted, unsupported, no_visual };
static_assert(std::is_same_v<std::underlying_type_t<EventMapping>,std::uint8_t>);

// func_80093D50(pos, normalX, normalZ, hitObject, part). Every caller
// (update_shell/bullet/missile_effect, spawn_beam_effect) passes the segment
// hit normal x/z, the hit object (0 for terrain/static hits) and the model
// part of slot models::impact_decal. The record stores the normals negated.
struct DecalParams {
    float x{}, y{}, z{};
    float normal_x{}, normal_z{};
    EntityId surface{};      // hit object; default (none) = terrain
    std::uint16_t model_slot{models::impact_decal};
};
// spawn_effect_kind_2b parameter block constants set by func_80093D50.
inline constexpr std::uint16_t decal_smoke_value20=100U, decal_smoke_value22=100U, decal_smoke_value24=30U,
                               decal_smoke_value26=150U, decal_smoke_value28=60U;

// Crate debris emitter (kind-27 handler func_800A2EA4: event 3 ->
// update_distance_scaled_debris_emitter func_800A2A44, event 7 ->
// update_debris_emitter func_800A2C38). The emitter state lives in the
// caller's crate record; these functions return the spawn_group_part_debris
// (func_80090428) request instead of spawning debris physics.
struct DebrisEmitterState {
    std::uint8_t crate_kind{};  // +0x14 index into D_80125FB0
    std::uint16_t part_count{}; // +0x18
    std::uint8_t team{};        // entity record +0x1BF of +0x12
    float x{}, z{};             // +0x1C / +0x20
    std::int16_t timer{};       // +0x2C
};
struct DebrisRequest {
    std::uint16_t model_slot{};
    float x{}, z{};
    std::uint16_t part_count{};
    std::uint16_t angle{};
    std::uint8_t team{};
};
inline constexpr float debris_distance_limit=4000.0F;
inline constexpr std::int32_t debris_rate_base=50;
// event 7: timer -= elapsed.
[[nodiscard]] std::optional<DebrisRequest> step_debris_emitter(DebrisEmitterState& emitter,float target_x,
                                                               float target_z,std::int32_t elapsed) noexcept;
// event 3: timer -= 50 + (1 - min(dist,4000)/4000) * 50.
[[nodiscard]] std::optional<DebrisRequest> step_distance_scaled_debris_emitter(DebrisEmitterState& emitter,
                                                                               float target_x,float target_z) noexcept;
void serialize(StateWriter& writer,const DebrisEmitterState& emitter);

class EffectRuntime {
public:
    EffectRuntime() noexcept { reset(); }
    void reset() noexcept; // init_effect_kind_table pool/list reset

    // clock: retail time units (read_runtime_counter_pair()>>16), 16-bit wrap.
    SpawnResult spawn_script(std::uint8_t variant,float x,float y,float z,std::uint16_t clock) noexcept;
    SpawnResult spawn_particle(EffectKind kind,std::uint8_t def,float x,float y,float z,
                               std::uint16_t clock) noexcept;
    // Oscillator kind stamps the game tick from the latest update(clock,game_tick,...), not `clock`.
    SpawnResult spawn_muzzle_flash(EntityId unit,std::uint8_t owner,float x,float y,float z,
                                   std::uint16_t clock) noexcept;
    SpawnResult spawn_decal(const DecalParams& params) noexcept;
    SpawnResult spawn_unit_destruction(std::uint8_t unit_type,float x,float y,float z,std::uint16_t clock) noexcept {
        return spawn_script(unit_type==1U ? variant::light_unit_destroyed : variant::tank_destroyed,x,y,z,clock);
    }
    EventMapping consume(const EffectEvent& event,std::uint16_t clock) noexcept;
    // Same, with the decal arguments EffectEvent does not carry (impact_decal only).
    EventMapping consume(const EffectEvent& event,const DecalParams& decal,std::uint16_t clock) noexcept;
    void consume_all(const EffectEventQueue& queue,std::uint16_t clock) noexcept;
    // Host-frame transient visuals are replaced before each simulation tick.
    // A beam is an allocated retail effect with a short countdown; retaining
    // the latest tick makes it visible without adding it to the script pool.
    void clear_transient_visuals() noexcept { beam_count_=0U; }

    // update_active_effect_list: merge pending, walk the walked active list,
    // merge again. Nodes spawned during the walk are first updated next call.
    // game_tick: get_elapsed_game_ticks (D_801B4AA8, 30 Hz) low 16 bits; the
    // oscillator kind runs on it instead of the effect clock.
    void update(std::uint16_t clock,std::uint16_t game_tick,UpdateGroup group=UpdateGroup::normal) noexcept;
    // TEMPORARY POLICY: legacy form reuses the last game tick (0 after reset); oscillators do not
    // advance through it. Kept for tests without oscillators.
    void update(std::uint16_t clock,UpdateGroup group=UpdateGroup::normal) noexcept {
        update(clock,game_tick_,group);
    }
    void release(std::int16_t slot) noexcept; // release_effect func_8008B89C

    bool set_status(const StatusVisual& visual) noexcept;
    void clear_status(EntityId subject,StatusVisualKind kind) noexcept;
    void tick_status() noexcept;

    void collect(EffectRenderQueue& out) const;
    void serialize(StateWriter& writer) const;

    [[nodiscard]] const EffectNode& node(std::int16_t slot) const noexcept {
        static const EffectNode empty{};
        return slot>=0 && static_cast<std::uint16_t>(slot)<pool_capacity ? nodes_[static_cast<std::uint16_t>(slot)] : empty;
    }
    [[nodiscard]] std::uint16_t live_count() const noexcept;
    [[nodiscard]] std::uint32_t failed_spawns() const noexcept { return failed_spawns_; }
    [[nodiscard]] std::int16_t walked_head() const noexcept { return walked_active_; }
    [[nodiscard]] std::int16_t persistent_head() const noexcept { return persistent_active_; }
    [[nodiscard]] std::uint8_t decal_count() const noexcept { return decal_count_; }

private:
    std::int16_t allocate(bool persistent,EffectKind kind) noexcept;
    void merge_pending() noexcept; // retire_effect_group_lists func_8008B1F0
    std::uint8_t update_node(EffectNode& e,std::uint16_t clock) noexcept;
    std::uint8_t update_oscillator(EffectNode& e) noexcept;
    EventMapping map(SpawnResult result) const noexcept;
    std::uint8_t run_script(EffectNode& e,std::uint16_t clock) noexcept;
    std::uint8_t update_particle(EffectNode& e,std::uint16_t clock) noexcept;

    std::array<EffectNode,pool_capacity> nodes_{};
    std::int16_t free_head_{0};          // D_802C13E0
    std::int16_t walked_pending_{no_link};    // D_802C13E2
    std::int16_t walked_active_{no_link};     // D_802C13F4
    std::int16_t persistent_pending_{no_link};// D_802C13F6
    std::int16_t persistent_active_{no_link}; // D_802C3A28
    std::uint32_t serial_{};             // D_80125C40
    std::uint8_t rotating_variant_{};    // D_80138FBC
    std::uint16_t game_tick_{};          // last game tick given to update (oscillator clock)
    std::array<std::int16_t,decal_queue_capacity+1U> decals_{}; // FIFO of decal slots
    std::uint8_t decal_count_{};
    std::uint32_t failed_spawns_{};
    std::array<StatusVisual,status_visual_capacity> status_{};
    std::array<EffectRenderItem,beam_visual_capacity> beams_{};
    std::uint8_t beam_count_{};
};

} // namespace battletanx::effects
