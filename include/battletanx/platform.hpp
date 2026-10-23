#pragma once

#include "battletanx/audio.hpp"
#include "battletanx/gameplay.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace battletanx {

constexpr int native_width=640;
constexpr int native_height=480;

class Platform {
  public:
    virtual ~Platform()=default;
    virtual bool pump(std::array<ControllerPad,4>& controllers)=0;
    virtual void present(std::span<const std::uint32_t> pixels,int width,int height)=0;
    virtual void set_sound_bank(const SoundBank& sounds)=0;
    virtual void set_music_track(const MusicTrack& music,float gain)=0;
    virtual void play_sound(std::uint16_t sound_id,float pan=0.0F,float gain=1.0F)=0;
    virtual void set_looping_sound(std::size_t channel,std::uint16_t sound_id,
                                   float pan,float gain,float pitch)=0;
    virtual void stop_looping_sound(std::size_t channel)=0;

    static std::unique_ptr<Platform> create(std::string_view title);
    static std::filesystem::path controller_pak_path();
};

} // namespace battletanx
