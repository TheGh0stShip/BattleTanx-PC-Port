#include "battletanx/music_selection.hpp"

#include <cmath>

namespace battletanx::music_selection {
namespace {

constexpr std::uint8_t one_shot_priority=5U;   // allocate_and_play_sound(index,0): 0+5
constexpr std::uint8_t loop_priority=6U;       // allocate_and_play_sound(index,1): 1+5
constexpr std::int16_t tank_engine_volume=0x3800;
constexpr float tank_engine_pitch_base=0.8F;
constexpr float tank_skid_threshold=0.5F;
constexpr float loop_volume_scale=28672.0F;
constexpr float rail_speed_divisor=21.2F;
constexpr float rail_audible_threshold=0.1F;

std::uint8_t clamp_volume(std::uint8_t music_volume) noexcept {
    return music_volume>music_volume_max ? music_volume_max : music_volume;
}

void emit_volume(std::uint8_t music_volume,AudioCommandList& out) noexcept {
    out.push(AudioCommand{.kind=AudioCommandKind::set_music_volume,.track=0U,.music_volume=clamp_volume(music_volume),.loop_channel=0U,.priority=0U,.sound_id=0U,.has_pitch=false,.pitch=1.0F,.volume=0});
}

void emit_sound(std::uint16_t id,AudioCommandList& out) noexcept {
    out.push(AudioCommand{.kind=AudioCommandKind::play_sound,.track=0U,.music_volume=0U,.loop_channel=0U,.priority=one_shot_priority,.sound_id=id,.has_pitch=false,.pitch=1.0F,.volume=0});
}

void start_loop(std::uint8_t channel,std::uint16_t id,AudioCommandList& out) noexcept {
    out.push(AudioCommand{.kind=AudioCommandKind::start_loop,.track=0U,.music_volume=0U,.loop_channel=channel,
                          .priority=loop_priority,.sound_id=id,.has_pitch=false,.pitch=1.0F,.volume=0});
}

void update_loop(std::uint8_t channel,bool has_pitch,float pitch,std::int16_t volume,
                 AudioCommandList& out) noexcept {
    out.push(AudioCommand{.kind=AudioCommandKind::update_loop,.track=0U,.music_volume=0U,.loop_channel=channel,.priority=0U,.sound_id=0U,
                          .has_pitch=has_pitch,.pitch=pitch,.volume=volume});
}

void stop_loop(std::uint8_t channel,AudioCommandList& out) noexcept {
    out.push(AudioCommand{.kind=AudioCommandKind::stop_loop,.track=0U,.music_volume=0U,.loop_channel=channel,.priority=0U,.sound_id=0U,.has_pitch=false,.pitch=1.0F,.volume=0});
}

} // namespace

bool AudioCommandList::push(const AudioCommand& command) noexcept {
    if (count_>=commands_.size()) {
        overflowed_=true;
        return false;
    }
    commands_[count_++]=command;
    return true;
}

std::optional<std::uint8_t> music_track_for_level(std::int32_t level) noexcept {
    switch (level) {
    case 0: return std::nullopt;
    case 1: return std::uint8_t{7U};
    case 2: return std::uint8_t{2U};
    case 4: return std::uint8_t{0x11U};
    case 6: return std::uint8_t{3U};
    case 7: return std::uint8_t{1U};
    case 8: return std::uint8_t{0x12U};
    case 10: return std::uint8_t{0x10U};
    case 11: return std::uint8_t{4U};
    case 12: return std::uint8_t{5U};
    case 14: return std::uint8_t{8U};
    case 16: return std::uint8_t{0xAU};
    case 5: case 9: case 13: case 15: case 17: return std::uint8_t{6U};
    case 18: case 19: return std::nullopt;
    default: return std::uint8_t{0U};
    }
}

std::int16_t retail_music_volume_value(std::uint8_t music_volume) noexcept {
    return static_cast<std::int16_t>(clamp_volume(music_volume)<<12U);
}

void MusicState::serialize(StateWriter& writer) const {
    writer.s16(current_track);
}

void request_track(MusicState& state,std::uint8_t track_id,std::uint8_t music_volume,
                   AudioCommandList& out) noexcept {
    if (track_id>=music_track_count) {
        stop_music(state,out);
        return;
    }
    if (state.current_track!=track_id) {
        out.push(AudioCommand{.kind=AudioCommandKind::play_music,.track=track_id,.music_volume=0U,.loop_channel=0U,.priority=0U,.sound_id=0U,.has_pitch=false,.pitch=1.0F,.volume=0});
        state.current_track=track_id;
    }
    emit_volume(music_volume,out);
}

void stop_music(MusicState& state,AudioCommandList& out) noexcept {
    out.push(AudioCommand{.kind=AudioCommandKind::stop_music,.track=0U,.music_volume=0U,.loop_channel=0U,.priority=0U,.sound_id=0U,.has_pitch=false,.pitch=1.0F,.volume=0});
    state.current_track=-1;
}

void select_level_music(MusicState& state,std::int32_t level,std::uint8_t music_volume,
                        AudioCommandList& out) noexcept {
    const auto track=music_track_for_level(level);
    if (track) request_track(state,*track,music_volume,out);
    else stop_music(state,out);
}

void apply_music_transition(MusicState& state,MusicTransition transition,
                            const TransitionContext& context,AudioCommandList& out) noexcept {
    switch (transition) {
    case MusicTransition::title_screen:
        stop_music(state,out);
        request_track(state,track::title,context.music_volume,out);
        break;
    case MusicTransition::story_first_card:
        stop_music(state,out);
        request_track(state,track::story_first,context.music_volume,out);
        break;
    case MusicTransition::story_second_card:
        stop_music(state,out);
        request_track(state,track::story_second,context.music_volume,out);
        break;
    case MusicTransition::attract_finished:
    case MusicTransition::wave_loading:
    case MusicTransition::wave_restarted:
        stop_music(state,out);
        break;
    case MusicTransition::countdown_finished:
        if (!context.demo_playback) select_level_music(state,context.level,context.music_volume,out);
        break;
    case MusicTransition::volume_changed:
        emit_volume(context.music_volume,out);
        break;
    case MusicTransition::demo_loading:
    case MusicTransition::briefing:
    case MusicTransition::results:
    case MusicTransition::intermission:
        break;
    }
}

void CountdownAudioState::serialize(StateWriter& writer) const {
    writer.boolean(wave_start_pending);
    writer.s32(previous_phase);
}

void begin_wave_countdown(CountdownAudioState& state) noexcept {
    state.wave_start_pending=true;
}

bool countdown_audio_frame(CountdownAudioState& state,std::int32_t counter,
                           std::int32_t phase_length,AudioCommandList& out) noexcept {
    if (phase_length<=0) return false;
    if (state.wave_start_pending) {
        emit_sound(sound::countdown_start,out);
        state.wave_start_pending=false;
    }
    if (static_cast<std::int64_t>(counter)==static_cast<std::int64_t>(phase_length)*3) {
        emit_sound(sound::countdown_mid,out);
    }
    const std::int32_t phase=counter/phase_length;
    if (phase>=0 && phase<3 && state.previous_phase!=phase) emit_sound(sound::countdown_tick,out);
    state.previous_phase=phase;
    return true;
}

std::uint8_t tally_stage(std::int32_t score,const TallyThresholds& thresholds) noexcept {
    for (std::size_t index=0U; index<thresholds.size(); ++index) {
        if (score<thresholds[index]) return static_cast<std::uint8_t>(index);
    }
    return 8U;
}

void round_over_tally_sounds(std::int32_t score,std::int32_t small_icons,std::int32_t large_icons,
                             const TallyThresholds& thresholds,AudioCommandList& out) noexcept {
    const std::uint8_t stage=tally_stage(score,thresholds);
    // Stage 0 would read thresholds[-1] in retail; it never plays a sound.
    if (stage==0U || stage>=8U) return;
    const std::int32_t base=thresholds[stage-1U];
    const bool exact=score==base;
    const std::int64_t excess=static_cast<std::int64_t>(score)-base;
    switch (stage) {
    case 2U:
    case 3U:
        if (exact) emit_sound(sound::tally_stage,out);
        break;
    case 6U: {
        const std::int64_t count=excess/6;
        if (small_icons>=count && excess!=0 && excess==count*6) emit_sound(sound::countdown_tick,out);
        break;
    }
    case 7U: {
        const std::int64_t count=excess/40;
        if (large_icons>=count && excess!=0 && excess==count*40) emit_sound(sound::tally_large_icon,out);
        break;
    }
    default:
        break;
    }
}

void UnitLoopState::serialize(StateWriter& writer) const {
    writer.boolean(engine_active);
    writer.boolean(skid_active);
    writer.u16(engine_sound);
}

std::optional<std::uint8_t> loop_channel(std::size_t local_slot,LoopRole role) noexcept {
    if (local_slot>=max_local_units) return std::nullopt;
    return static_cast<std::uint8_t>(local_slot*loop_roles_per_unit+static_cast<std::uint8_t>(role));
}

std::int16_t retail_s16_from_float(float value) noexcept {
    if (std::isnan(value)) return 0;
    std::int32_t whole=0;
    if (value>=2147483647.0F) whole=2147483647;
    else if (value<=-2147483648.0F) whole=-2147483647-1;
    else whole=static_cast<std::int32_t>(value);
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(whole)&0xFFFFU));
}

bool update_tank_loops(UnitLoopState& state,std::size_t local_slot,float speed,
                       AudioCommandList& out) noexcept {
    const auto engine=loop_channel(local_slot,LoopRole::engine);
    const auto skid=loop_channel(local_slot,LoopRole::skid);
    if (!engine || !skid) return false;
    if (!state.engine_active) {
        start_loop(*engine,sound::tank_engine,out);
        state.engine_active=true;
        state.engine_sound=sound::tank_engine;
    }
    update_loop(*engine,true,speed+tank_engine_pitch_base,tank_engine_volume,out);
    if (tank_skid_threshold<speed) {
        if (!state.skid_active) {
            start_loop(*skid,sound::tank_skid,out);
            state.skid_active=true;
        }
        update_loop(*skid,false,1.0F,retail_s16_from_float(2.0F*(speed-tank_skid_threshold)*loop_volume_scale),out);
        return true;
    }
    if (state.skid_active) {
        stop_loop(*skid,out);
        state.skid_active=false;
    }
    return true;
}

bool update_rail_loops(UnitLoopState& state,std::size_t local_slot,bool eligible,
                       float velocity,AudioCommandList& out) noexcept {
    const auto engine=loop_channel(local_slot,LoopRole::engine);
    if (!engine) return false;
    if (eligible) {
        float level=(velocity<0.0F ? -velocity : velocity)/rail_speed_divisor;
        if (1.0F<level) level=1.0F;
        if (rail_audible_threshold<level) {
            if (!state.engine_active) {
                start_loop(*engine,sound::rail_engine,out);
                state.engine_active=true;
                state.engine_sound=sound::rail_engine;
            }
            update_loop(*engine,true,level+tank_engine_pitch_base,retail_s16_from_float(level*loop_volume_scale),out);
            return true;
        }
    }
    if (state.engine_active) {
        stop_loop(*engine,out);
        state.engine_active=false;
        state.engine_sound=0U;
    }
    return true;
}

bool release_unit_loops(UnitLoopState& state,std::size_t local_slot,AudioCommandList& out) noexcept {
    const auto engine=loop_channel(local_slot,LoopRole::engine);
    const auto skid=loop_channel(local_slot,LoopRole::skid);
    if (!engine || !skid) return false;
    if (state.engine_active) stop_loop(*engine,out);
    if (state.skid_active) stop_loop(*skid,out);
    state=UnitLoopState{};
    return true;
}

} // namespace battletanx::music_selection
