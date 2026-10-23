#include "battletanx/campaign_catalog.hpp"
#include "battletanx/music_selection.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <cstdint>

namespace ms=battletanx::music_selection;

namespace {

// Expected select_screen_transition result for levels 1..17 (index 0 = level 1).
constexpr std::array<std::uint8_t,17> level_tracks{7U,2U,0U,0x11U,6U,3U,1U,0x12U,6U,0x10U,4U,5U,6U,8U,6U,0xAU,6U};

bool is(const ms::AudioCommand& command,ms::AudioCommandKind kind) { return command.kind==kind; }

} // namespace

RUNTIME_TEST(every_level_maps_to_its_track) {
    for (std::int32_t level=1; level<=17; ++level) {
        const auto track=ms::music_track_for_level(level);
        REQUIRE(track.has_value());
        REQUIRE(*track==level_tracks[static_cast<std::size_t>(level-1)]);
        REQUIRE(*track<ms::music_track_count);
    }
    REQUIRE(!ms::music_track_for_level(0).has_value());
    REQUIRE(!ms::music_track_for_level(18).has_value());
    REQUIRE(!ms::music_track_for_level(19).has_value());
    REQUIRE(ms::music_track_for_level(20)==std::uint8_t{0U});
    REQUIRE(ms::music_track_for_level(-1)==std::uint8_t{0U});
    return true;
}

RUNTIME_TEST(every_campaign_wave_selects_level_music_after_countdown) {
    const auto waves=battletanx::campaign_wave_definitions();
    REQUIRE(waves.size()==17U);
    for (const auto& wave:waves) {
        REQUIRE(wave.mission_id>=1 && wave.mission_id<=17);
        ms::MusicState state;
        ms::AudioCommandList out;
        ms::apply_music_transition(state,ms::MusicTransition::wave_loading,
                                   {.level=wave.mission_id,.music_volume=5U,.demo_playback=false},out);
        ms::apply_music_transition(state,ms::MusicTransition::countdown_finished,
                                   {.level=wave.mission_id,.music_volume=5U,.demo_playback=false},out);
        REQUIRE(out.size()==3U);
        REQUIRE(is(out[0],ms::AudioCommandKind::stop_music));
        REQUIRE(is(out[1],ms::AudioCommandKind::play_music));
        REQUIRE(out[1].track==level_tracks[static_cast<std::size_t>(wave.mission_id-1)]);
        REQUIRE(static_cast<std::int8_t>(out[1].track)==wave.music_transition);
        REQUIRE(is(out[2],ms::AudioCommandKind::set_music_volume) && out[2].music_volume==5U);
        REQUIRE(state.current_track==out[1].track);
    }
    return true;
}

RUNTIME_TEST(same_track_is_coalesced_and_stop_resets) {
    ms::MusicState state;
    ms::AudioCommandList out;
    ms::select_level_music(state,5,7U,out);
    ms::select_level_music(state,9,7U,out);  // also track 6
    REQUIRE(out.size()==3U);
    REQUIRE(is(out[0],ms::AudioCommandKind::play_music) && out[0].track==6U);
    REQUIRE(is(out[2],ms::AudioCommandKind::set_music_volume));
    out.clear();
    ms::select_level_music(state,18,7U,out);
    REQUIRE(out.size()==1U && is(out[0],ms::AudioCommandKind::stop_music));
    REQUIRE(state.current_track==-1);
    out.clear();
    ms::request_track(state,ms::music_track_count,7U,out);
    REQUIRE(out.size()==1U && is(out[0],ms::AudioCommandKind::stop_music));
    return true;
}

RUNTIME_TEST(every_transition_has_its_command_shape) {
    struct Expect { ms::MusicTransition transition; std::size_t count; ms::AudioCommandKind first; std::uint8_t track; };
    const std::array<Expect,12> cases{{
        {ms::MusicTransition::title_screen,3U,ms::AudioCommandKind::stop_music,ms::track::title},
        {ms::MusicTransition::story_first_card,3U,ms::AudioCommandKind::stop_music,ms::track::story_first},
        {ms::MusicTransition::story_second_card,3U,ms::AudioCommandKind::stop_music,ms::track::story_second},
        {ms::MusicTransition::attract_finished,1U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::wave_loading,1U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::wave_restarted,1U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::demo_loading,0U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::countdown_finished,2U,ms::AudioCommandKind::play_music,2U},
        {ms::MusicTransition::briefing,0U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::results,0U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::intermission,0U,ms::AudioCommandKind::stop_music,0U},
        {ms::MusicTransition::volume_changed,1U,ms::AudioCommandKind::set_music_volume,0U},
    }};
    for (const auto& expect:cases) {
        ms::MusicState state;
        ms::AudioCommandList out;
        ms::apply_music_transition(state,expect.transition,{.level=2,.music_volume=3U,.demo_playback=false},out);
        REQUIRE(out.size()==expect.count);
        if (expect.count==0U) continue;
        REQUIRE(out[0].kind==expect.first);
        if (expect.count==3U) {
            REQUIRE(is(out[1],ms::AudioCommandKind::play_music) && out[1].track==expect.track);
            REQUIRE(out[2].music_volume==3U);
        }
        if (expect.first==ms::AudioCommandKind::play_music) REQUIRE(out[0].track==expect.track);
    }
    ms::MusicState state;
    ms::AudioCommandList out;
    ms::apply_music_transition(state,ms::MusicTransition::countdown_finished,
                               {.level=2,.music_volume=3U,.demo_playback=true},out);
    REQUIRE(out.size()==0U);
    return true;
}

RUNTIME_TEST(music_volume_value_and_clamp) {
    REQUIRE(ms::retail_music_volume_value(0U)==0);
    REQUIRE(ms::retail_music_volume_value(7U)==0x7000);
    REQUIRE(ms::retail_music_volume_value(9U)==0x7000);
    ms::AudioCommandList out;
    ms::MusicState state;
    ms::apply_music_transition(state,ms::MusicTransition::volume_changed,{.level=0,.music_volume=12U,.demo_playback=false},out);
    REQUIRE(out[0].music_volume==7U);
    return true;
}

RUNTIME_TEST(countdown_emits_start_mid_and_phase_ticks) {
    constexpr std::int32_t phase_length=10;
    ms::CountdownAudioState state;
    ms::begin_wave_countdown(state);
    std::array<std::uint32_t,0x30> counts{};
    for (std::int32_t counter=phase_length*5-1; counter>=1; --counter) {
        ms::AudioCommandList out;
        REQUIRE(ms::countdown_audio_frame(state,counter,phase_length,out));
        for (std::size_t i=0U; i<out.size(); ++i) {
            REQUIRE(is(out[i],ms::AudioCommandKind::play_sound));
            ++counts[out[i].sound_id];
        }
    }
    REQUIRE(counts[ms::sound::countdown_start]==1U);
    REQUIRE(counts[ms::sound::countdown_mid]==1U);
    REQUIRE(counts[ms::sound::countdown_tick]==3U);
    ms::AudioCommandList out;
    REQUIRE(!ms::countdown_audio_frame(state,5,0,out));
    REQUIRE(out.size()==0U);
    return true;
}

RUNTIME_TEST(tally_stage_sounds) {
    const auto& table=ms::retail_tally_thresholds;
    REQUIRE(ms::tally_stage(0,table)==1U);
    REQUIRE(ms::tally_stage(15,table)==2U);
    REQUIRE(ms::tally_stage(80,table)==8U);
    ms::AudioCommandList out;
    ms::round_over_tally_sounds(15,0,0,table,out);
    ms::round_over_tally_sounds(30,0,0,table,out);
    ms::round_over_tally_sounds(16,0,0,table,out);
    REQUIRE(out.size()==2U && out[0].sound_id==ms::sound::tally_stage && out[1].sound_id==ms::sound::tally_stage);
    // Synthetic thresholds reaching stages 6 and 7.
    constexpr ms::TallyThresholds custom{0,10,20,30,40,50,100,500};
    out.clear();
    ms::round_over_tally_sounds(62,5,0,custom,out);   // stage 6, excess 12
    ms::round_over_tally_sounds(140,0,2,custom,out);  // stage 7, excess 40
    ms::round_over_tally_sounds(62,1,0,custom,out);   // too few small icons
    REQUIRE(out.size()==2U);
    REQUIRE(out[0].sound_id==ms::sound::countdown_tick && out[1].sound_id==ms::sound::tally_large_icon);
    return true;
}

RUNTIME_TEST(tank_loops_start_once_and_stop_skid) {
    ms::UnitLoopState state;
    ms::AudioCommandList out;
    REQUIRE(ms::update_tank_loops(state,1U,0.75F,out));
    REQUIRE(out.size()==4U);
    REQUIRE(is(out[0],ms::AudioCommandKind::start_loop) && out[0].sound_id==ms::sound::tank_engine && out[0].loop_channel==2U);
    REQUIRE(out[0].priority==6U);
    REQUIRE(is(out[1],ms::AudioCommandKind::update_loop) && out[1].volume==0x3800 && out[1].has_pitch);
    REQUIRE(is(out[2],ms::AudioCommandKind::start_loop) && out[2].sound_id==ms::sound::tank_skid && out[2].loop_channel==3U);
    REQUIRE(out[3].volume==14336 && !out[3].has_pitch);
    out.clear();
    REQUIRE(ms::update_tank_loops(state,1U,0.9F,out));
    REQUIRE(out.size()==2U);
    out.clear();
    REQUIRE(ms::update_tank_loops(state,1U,0.2F,out));
    REQUIRE(out.size()==2U && is(out[1],ms::AudioCommandKind::stop_loop) && !state.skid_active);
    out.clear();
    REQUIRE(ms::release_unit_loops(state,1U,out));
    REQUIRE(out.size()==1U && !state.engine_active);
    REQUIRE(!ms::update_tank_loops(state,ms::max_local_units,0.0F,out));
    return true;
}

RUNTIME_TEST(rail_loop_and_independent_units) {
    std::array<ms::UnitLoopState,ms::max_local_units> units{};
    ms::AudioCommandList out;
    for (std::size_t slot=0U; slot<units.size(); ++slot) REQUIRE(ms::update_rail_loops(units[slot],slot,true,-21.2F,out));
    REQUIRE(out.size()==8U);
    for (std::size_t slot=0U; slot<units.size(); ++slot) {
        REQUIRE(static_cast<std::size_t>(out[slot*2U].loop_channel)==slot*2U && out[slot*2U].sound_id==ms::sound::rail_engine);
        REQUIRE(out[slot*2U+1U].volume==28672);
    }
    out.clear();
    REQUIRE(ms::update_rail_loops(units[0],0U,true,1.0F,out));  // below 0.1
    REQUIRE(out.size()==1U && is(out[0],ms::AudioCommandKind::stop_loop));
    out.clear();
    REQUIRE(ms::update_rail_loops(units[1],1U,false,21.2F,out));
    REQUIRE(out.size()==1U && out[0].loop_channel==2U);
    battletanx::StateWriter writer;
    units[2].serialize(writer);
    REQUIRE(writer.bytes().size()==4U);
    return true;
}

RUNTIME_TEST(command_list_overflow_and_s16_wrap) {
    ms::AudioCommandList out;
    for (std::size_t i=0U; i<ms::command_capacity; ++i) REQUIRE(out.push({}));
    REQUIRE(!out.push({}) && out.overflowed());
    REQUIRE(ms::retail_s16_from_float(32768.0F)==-32768);
    REQUIRE(ms::retail_s16_from_float(-1.5F)==-1);
    return true;
}

RUNTIME_TEST_MAIN()
