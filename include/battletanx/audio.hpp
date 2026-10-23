#pragma once

#include "battletanx/rom.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {

struct PcmSound {
    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate{};
    std::uint32_t loop_start{};
    std::uint32_t loop_end{};
    std::uint8_t volume{127U};
    std::uint8_t pan{64U};
};

[[nodiscard]] double advance_looping_cursor(double cursor,float pitch,
                                            std::size_t sample_count,
                                            std::uint32_t loop_start,
                                            std::uint32_t loop_end) noexcept;

struct SoundBank {
    std::vector<PcmSound> sounds;
    std::uint32_t sample_rate{};
};

struct VehicleSoundMix {
    float speed{};
    float engine_gain{};
    float engine_pitch{};
    float skid_gain{};
};

[[nodiscard]] VehicleSoundMix vehicle_sound_mix(float velocity_x,float velocity_z,
                                                 float master_gain) noexcept;

struct MusicTrack {
    std::vector<std::int16_t> stereo_samples;
    std::uint32_t sample_rate{};
    std::uint32_t sequence_index{};
};

// Decodes the N64's framed mono VADPCM representation. Each predictor owns
// sixteen signed coefficients and every nine encoded bytes produce 16 samples.
[[nodiscard]] std::vector<std::int16_t> decode_vadpcm(
    std::span<const std::uint8_t> encoded,
    std::span<const std::int16_t> coefficients,
    std::uint32_t predictor_count);

// Loads the BattleTanx B1 sound-effect control bank and its paired wave table
// directly from the verified retail ROM.
[[nodiscard]] SoundBank load_sound_effect_bank(const Rom& rom);

// Renders one retail type-0 MIDI sequence through the game's B1 music bank.
// The returned stereo PCM is host-owned and can be looped without retaining
// any N64 address tokens or executing the original audio runtime.
[[nodiscard]] MusicTrack render_music_sequence(const Rom& rom,
                                               std::uint32_t sequence_index,
                                               std::uint32_t output_rate=44100U);

} // namespace battletanx
