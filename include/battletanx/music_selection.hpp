#pragma once

// Pure audio selection and scheduling. Nothing here touches a platform or
// mixer: every decision is emitted as a typed AudioCommand that the host maps
// onto Platform::set_music_track / play_sound / set_looping_sound /
// stop_looping_sound. Track ids equal the sequence_index accepted by
// render_music_sequence (retail apply_audio_sequence reads the offset table
// one word after the host table base, so retail track t == host index t).

#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace battletanx::music_selection {

inline constexpr std::uint8_t music_track_count=19U;      // music.cpp sequence_count
inline constexpr std::uint8_t music_volume_max=7U;         // options menu clamps 0..7
inline constexpr std::size_t max_local_units=4U;
inline constexpr std::size_t loop_roles_per_unit=2U;       // engine/rail, skid
inline constexpr std::size_t loop_channel_count=max_local_units*loop_roles_per_unit;
inline constexpr std::size_t command_capacity=32U;

// Sound-effect bank indices (allocate_and_play_sound index argument).
namespace sound {
inline constexpr std::uint16_t countdown_tick=1U;      // draw_frame_overlays phase beep; tally icon
inline constexpr std::uint16_t tally_large_icon=3U;    // draw_round_over_screen stage 7
inline constexpr std::uint16_t countdown_start=0xCU;   // first countdown frame of a wave
inline constexpr std::uint16_t countdown_mid=0x14U;    // counter == phase_length*3
inline constexpr std::uint16_t tank_engine=0x10U;      // update_unit_from_pose
inline constexpr std::uint16_t tank_skid=0x12U;        // update_unit_from_pose
inline constexpr std::uint16_t rail_engine=0x18U;      // update_rail_unit_control
inline constexpr std::uint16_t tally_stage=0x26U;      // draw_round_over_screen stages 2/3
} // namespace sound

// Named retail music tracks outside the wave table (frontend sequences).
namespace track {
inline constexpr std::uint8_t story_first=7U;   // update_sequence_c step 2
inline constexpr std::uint8_t story_second=8U;  // update_sequence_c step 4
inline constexpr std::uint8_t title=13U;        // init_sequence_b_screen / update_sequence_d
} // namespace track

enum class AudioCommandKind : std::uint8_t {
    play_music,        // start track looping forever (retail alCSPLoop count -1)
    stop_music,        // immediate stop, no fade (stop_sequence_player sets volume 0)
    set_music_volume,  // music_volume nibble 0..7
    play_sound,        // one-shot
    start_loop,        // begin looping sound on loop_channel
    update_loop,       // adjust pitch/volume of an active loop
    stop_loop,
};
static_assert(std::is_same_v<std::underlying_type_t<AudioCommandKind>,std::uint8_t>);

struct AudioCommand {
    AudioCommandKind kind=AudioCommandKind::stop_music;
    std::uint8_t track=0U;          // play_music
    std::uint8_t music_volume=0U;   // set_music_volume (0..7)
    std::uint8_t loop_channel=0U;   // *_loop
    std::uint8_t priority=0U;       // retail slot priority (argument + 5)
    std::uint16_t sound_id=0U;      // play_sound / start_loop
    bool has_pitch=false;           // update_loop: false keeps the current pitch
    float pitch=1.0F;               // update_loop (retail configure_active_sound value)
    std::int16_t volume=0;          // update_loop, retail 0..32767 scale before master gain
};

class AudioCommandList {
public:
    // Returns false (and drops the command) once full.
    bool push(const AudioCommand& command) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] const AudioCommand& operator[](std::size_t index) const noexcept { return commands_[index]; }
    void clear() noexcept { count_=0U; overflowed_=false; }

private:
    std::array<AudioCommand,command_capacity> commands_{};
    std::size_t count_=0U;
    bool overflowed_=false;
};

// select_screen_transition (func_8007D2A0): level -> track; nullopt = stop.
// Levels 0, 18 and 19 stop the music; unlisted values (e.g. 3) select 0.
[[nodiscard]] std::optional<std::uint8_t> music_track_for_level(std::int32_t level) noexcept;
// Retail music volume value passed to set_audio_sequence_pitch: nibble<<12.
[[nodiscard]] std::int16_t retail_music_volume_value(std::uint8_t music_volume) noexcept;

// Owns "which track is playing" (retail D_80125710). Requesting the current
// track again is coalesced (apply_audio_sequence early-out), but the volume
// command is always re-emitted as in retail.
struct MusicState {
    std::int16_t current_track=-1;

    void serialize(StateWriter& writer) const;
};

void request_track(MusicState& state,std::uint8_t track_id,std::uint8_t music_volume,
                   AudioCommandList& out) noexcept;
void stop_music(MusicState& state,AudioCommandList& out) noexcept;
void select_level_music(MusicState& state,std::int32_t level,std::uint8_t music_volume,
                        AudioCommandList& out) noexcept;

// Frontend/campaign transitions with a music consequence.
enum class MusicTransition : std::uint8_t {
    title_screen,          // stop, play title (13), volume
    story_first_card,      // stop, play 7, volume
    story_second_card,     // stop, play 8, volume
    attract_finished,      // run_sequence_b_loop exit: stop
    wave_loading,          // play_campaign_wave non-demo: stop
    demo_loading,          // play_campaign_wave demo: music untouched
    countdown_finished,    // update_match_state 4->1: level music unless demo playback
    wave_restarted,        // play_campaign_wave restart path: same as wave_loading
    briefing,              // TEMPORARY POLICY: no retail call found; music untouched
    results,               // TEMPORARY POLICY: no retail call found; music untouched
    intermission,          // TEMPORARY POLICY: no retail call found; music untouched
    volume_changed,        // options menu: volume only
};
static_assert(std::is_same_v<std::underlying_type_t<MusicTransition>,std::uint8_t>);

struct TransitionContext {
    std::int32_t level=0;          // wave mission id (game mode +4)
    std::uint8_t music_volume=music_volume_max;
    bool demo_playback=false;      // audio_restarts_disabled (func_800EC644)
};

void apply_music_transition(MusicState& state,MusicTransition transition,
                            const TransitionContext& context,AudioCommandList& out) noexcept;

// Countdown cues (draw_frame_overlays, game mode 4). counter is D_801B4AD4
// after update_match_state's decrement; phase_length is D_801B4AC0.
struct CountdownAudioState {
    bool wave_start_pending=false;  // D_801B4AE8, set by play_campaign_wave
    std::int32_t previous_phase=-1; // D_80362110

    void serialize(StateWriter& writer) const;
};

void begin_wave_countdown(CountdownAudioState& state) noexcept;
// Returns false for a non-positive phase_length (unsupported input; no cues).
bool countdown_audio_frame(CountdownAudioState& state,std::int32_t counter,
                           std::int32_t phase_length,AudioCommandList& out) noexcept;

// Round-over tally sounds (draw_round_over_screen). thresholds defaults to the
// static D_80134FC0 contents; the trailing words are separate symbols that may
// be written at runtime, so callers may supply their own.
using TallyThresholds=std::array<std::int32_t,8>;
inline constexpr TallyThresholds retail_tally_thresholds{0,15,30,45,60,75,0,0};

[[nodiscard]] std::uint8_t tally_stage(std::int32_t score,const TallyThresholds& thresholds) noexcept;
void round_over_tally_sounds(std::int32_t score,std::int32_t small_icons,std::int32_t large_icons,
                             const TallyThresholds& thresholds,AudioCommandList& out) noexcept;

// Per-local-unit engine/skid loops. Each unit owns its own handles (retail
// Unit::engineSound/skidSound); there is no cross-unit coalescing. Within a
// unit, a loop is started only when its handle is idle. Loops use priority 6,
// one-shots priority 5 (allocate_and_play_sound adds 5).
enum class LoopRole : std::uint8_t { engine=0U, skid=1U };
static_assert(std::is_same_v<std::underlying_type_t<LoopRole>,std::uint8_t>);

struct UnitLoopState {
    bool engine_active=false;
    bool skid_active=false;
    std::uint16_t engine_sound=0U;  // tank_engine or rail_engine when active

    void serialize(StateWriter& writer) const;
};

[[nodiscard]] std::optional<std::uint8_t> loop_channel(std::size_t local_slot,LoopRole role) noexcept;

// update_unit_from_pose tail: human-controlled tank. Returns false for an
// out-of-range slot.
bool update_tank_loops(UnitLoopState& state,std::size_t local_slot,float speed,
                       AudioCommandList& out) noexcept;
// update_rail_unit_control: eligible = local && (no restriction || in view).
bool update_rail_loops(UnitLoopState& state,std::size_t local_slot,bool eligible,
                       float velocity,AudioCommandList& out) noexcept;
// Unit destroyed, unit left the local set, or wave teardown.
bool release_unit_loops(UnitLoopState& state,std::size_t local_slot,AudioCommandList& out) noexcept;

// float -> s32 -> s16 store, as the retail cvt/sh sequence does.
[[nodiscard]] std::int16_t retail_s16_from_float(float value) noexcept;

} // namespace battletanx::music_selection
