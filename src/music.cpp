#include "battletanx/audio.hpp"

#include "battletanx/big_endian.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace battletanx {
namespace {
constexpr std::size_t music_control_start=0x298D60U;
constexpr std::size_t music_control_end=0x29A710U;
constexpr std::size_t music_wave_start=0x217EC0U;
constexpr std::size_t music_wave_end=0x298D60U;
constexpr std::size_t sequence_table_offset=0xB5714U;
constexpr std::uint32_t sequence_count=19U;
constexpr std::uint32_t pi_rom_base=0xB0000000U;
constexpr std::size_t max_music_voices=48U;

struct Envelope {
    std::uint32_t attack_us{};
    std::uint32_t decay_us{};
    std::uint32_t release_us{};
    std::uint8_t attack_volume{127U};
    std::uint8_t decay_volume{127U};
};

struct MusicSample {
    std::vector<std::int16_t> pcm;
    Envelope envelope;
    std::uint8_t velocity_min{},velocity_max{};
    std::uint8_t key_min{},key_max{},key_base{};
    std::int8_t detune{};
    std::uint8_t volume{127U},pan{64U};
    std::optional<std::pair<std::uint32_t,std::uint32_t>> loop;
};

struct Instrument {
    std::uint8_t volume{127U},pan{64U};
    std::vector<MusicSample> sounds;
};

struct MusicBank {
    std::uint32_t sample_rate{};
    std::vector<Instrument> instruments;
};

struct MidiEvent {
    std::uint64_t frame{};
    std::uint8_t status{},a{},b{};
};

struct MidiSequence {
    std::vector<MidiEvent> events;
    std::uint64_t frames{};
};

struct Channel {
    std::uint8_t program{},volume{127U},pan{64U};
    std::uint16_t bend{8192U};
};

struct Voice {
    const MusicSample* sample{};
    std::uint8_t channel{},key{},velocity{};
    float instrument_volume{1.0F},instrument_pan{};
    double position{};
    double step{};
    std::uint64_t age{};
    bool released{};
    float release_level{1.0F};
    std::uint64_t release_age{};
};

void require_range(std::span<const std::uint8_t> bytes,std::size_t offset,
                   std::size_t size,const char* description) {
    if (offset>bytes.size() || size>bytes.size()-offset) {
        throw std::runtime_error(std::string("music ")+description+" is out of range");
    }
}

std::uint32_t read_vlq(std::span<const std::uint8_t> bytes,std::size_t& offset,
                       std::size_t end) {
    std::uint32_t value=0U;
    for (int count=0; count<4; ++count) {
        if (offset>=end) throw std::runtime_error("music sequence has a truncated VLQ");
        const std::uint8_t byte=bytes[offset++];
        value=(value<<7U)|(byte&0x7FU);
        if ((byte&0x80U)==0U) return value;
    }
    throw std::runtime_error("music sequence has an oversized VLQ");
}

std::vector<std::int16_t> decode_wave(const BigEndianView& view,
                                      std::span<const std::uint8_t> control,
                                      std::span<const std::uint8_t> waves,
                                      std::size_t wavetable) {
    require_range(control,wavetable,20U,"wavetable");
    const std::size_t base=view.u32(wavetable);
    const std::size_t length=view.u32(wavetable+4U);
    if (view.u8(wavetable+8U)!=0U) throw std::runtime_error("music wave is not VADPCM");
    require_range(waves,base,length,"wave payload");
    const std::size_t book=view.u32(wavetable+16U);
    require_range(control,book,8U,"predictor book");
    const std::uint32_t order=view.u32(book);
    const std::uint32_t predictors=view.u32(book+4U);
    if (order!=2U || predictors==0U || predictors>16U) {
        throw std::runtime_error("music predictor dimensions are invalid");
    }
    const std::size_t coefficient_count=static_cast<std::size_t>(predictors)*16U;
    require_range(control,book+8U,coefficient_count*2U,"predictor coefficients");
    std::vector<std::int16_t> coefficients(coefficient_count);
    for (std::size_t index=0U; index<coefficient_count; ++index) {
        coefficients[index]=view.s16(book+8U+index*2U);
    }
    return decode_vadpcm(waves.subspan(base,length),coefficients,predictors);
}

MusicBank load_music_bank(const Rom& rom) {
    const auto control=rom.bytes().subspan(
        music_control_start,music_control_end-music_control_start);
    const auto waves=rom.bytes().subspan(music_wave_start,music_wave_end-music_wave_start);
    const BigEndianView view(control);
    if (view.u16(0U)!=0x4231U || view.u16(2U)!=1U) {
        throw std::runtime_error("music bank has an invalid B1 header");
    }
    const std::size_t bank=view.u32(4U);
    require_range(control,bank,12U,"bank");
    const std::int16_t instrument_count=view.s16(bank);
    if (instrument_count<=0 || instrument_count>128) {
        throw std::runtime_error("music bank instrument count is invalid");
    }
    require_range(control,bank+12U,static_cast<std::size_t>(instrument_count)*4U,
                  "instrument table");
    MusicBank result;
    result.sample_rate=view.u32(bank+4U);
    if (result.sample_rate<8000U || result.sample_rate>96000U) {
        throw std::runtime_error("music bank sample rate is invalid");
    }
    result.instruments.reserve(static_cast<std::size_t>(instrument_count));
    for (std::int16_t instrument_index=0; instrument_index<instrument_count; ++instrument_index) {
        const std::size_t instrument=view.u32(
            bank+12U+static_cast<std::size_t>(instrument_index)*4U);
        require_range(control,instrument,16U,"instrument");
        const std::int16_t sound_count=view.s16(instrument+14U);
        if (sound_count<=0 || sound_count>128) {
            throw std::runtime_error("music instrument sound count is invalid");
        }
        require_range(control,instrument+16U,static_cast<std::size_t>(sound_count)*4U,
                      "sound table");
        Instrument decoded_instrument;
        decoded_instrument.volume=view.u8(instrument);
        decoded_instrument.pan=view.u8(instrument+1U);
        decoded_instrument.sounds.reserve(static_cast<std::size_t>(sound_count));
        for (std::int16_t sound_index=0; sound_index<sound_count; ++sound_index) {
            const std::size_t sound=view.u32(
                instrument+16U+static_cast<std::size_t>(sound_index)*4U);
            require_range(control,sound,16U,"sound");
            const std::size_t envelope=view.u32(sound);
            const std::size_t keymap=view.u32(sound+4U);
            const std::size_t wavetable=view.u32(sound+8U);
            require_range(control,envelope,16U,"envelope");
            require_range(control,keymap,6U,"keymap");
            MusicSample decoded;
            decoded.pcm=decode_wave(view,control,waves,wavetable);
            decoded.envelope.attack_us=view.u32(envelope);
            decoded.envelope.decay_us=view.u32(envelope+4U);
            decoded.envelope.release_us=view.u32(envelope+8U);
            decoded.envelope.attack_volume=view.u8(envelope+12U);
            decoded.envelope.decay_volume=view.u8(envelope+13U);
            decoded.velocity_min=view.u8(keymap);
            decoded.velocity_max=view.u8(keymap+1U);
            decoded.key_min=view.u8(keymap+2U);
            decoded.key_max=view.u8(keymap+3U);
            decoded.key_base=view.u8(keymap+4U);
            decoded.detune=static_cast<std::int8_t>(view.u8(keymap+5U));
            decoded.pan=view.u8(sound+12U);
            decoded.volume=view.u8(sound+13U);
            const std::size_t loop=view.u32(wavetable+12U);
            if (loop!=0U) {
                require_range(control,loop,44U,"loop");
                const std::uint32_t start=view.u32(loop);
                const std::uint32_t end=view.u32(loop+4U);
                if (start>=end || end>decoded.pcm.size()) {
                    throw std::runtime_error("music loop bounds are invalid");
                }
                decoded.loop=std::pair{start,end};
            }
            decoded_instrument.sounds.push_back(std::move(decoded));
        }
        result.instruments.push_back(std::move(decoded_instrument));
    }
    return result;
}

MidiSequence load_midi_sequence(const Rom& rom,std::uint32_t sequence_index,
                                std::uint32_t output_rate) {
    if (sequence_index>=sequence_count || output_rate<8000U || output_rate>192000U) {
        throw std::runtime_error("music sequence request is invalid");
    }
    const auto table=rom.bytes().subspan(sequence_table_offset,(sequence_count+1U)*4U);
    const BigEndianView table_view(table);
    const std::uint32_t begin_token=table_view.u32(sequence_index*4U);
    const std::uint32_t end_token=table_view.u32((sequence_index+1U)*4U);
    if (begin_token<pi_rom_base || end_token<begin_token) {
        throw std::runtime_error("music sequence ROM range is invalid");
    }
    const std::size_t begin=begin_token-pi_rom_base;
    const std::size_t end=end_token-pi_rom_base;
    require_range(rom.bytes(),begin,end-begin,"sequence");
    const auto bytes=rom.bytes().subspan(begin,end-begin);
    const BigEndianView view(bytes);
    require_range(bytes,0U,22U,"sequence header");
    if (view.u32(0U)!=0x4D546864U || view.u32(4U)!=6U ||
        view.u16(8U)!=0U || view.u16(10U)!=1U || view.u16(14U)!=0x4D54U ||
        view.u16(16U)!=0x726BU) {
        throw std::runtime_error("music sequence header is unsupported");
    }
    const std::uint16_t division=view.u16(12U);
    if (division==0U || (division&0x8000U)!=0U) {
        throw std::runtime_error("music sequence time division is invalid");
    }
    const std::size_t track_end=22U+view.u32(18U);
    require_range(bytes,22U,track_end-22U,"track");
    std::size_t offset=22U;
    std::uint8_t running_status=0U;
    std::uint32_t tempo=500000U;
    long double frame=0.0L;
    MidiSequence result;
    bool ended=false;
    while (offset<track_end) {
        const std::uint32_t delta=read_vlq(bytes,offset,track_end);
        frame+=static_cast<long double>(delta)*tempo*output_rate/
               (static_cast<long double>(division)*1000000.0L);
        if (offset>=track_end) throw std::runtime_error("music event is truncated");
        std::uint8_t status=bytes[offset];
        if (status==0xFFU) {
            require_range(bytes,offset,2U,"meta event");
            const std::uint8_t type=bytes[offset+1U];
            offset+=2U;
            const std::uint32_t length=read_vlq(bytes,offset,track_end);
            require_range(bytes,offset,length,"meta payload");
            if (type==0x51U) {
                if (length!=3U) throw std::runtime_error("music tempo event is invalid");
                tempo=static_cast<std::uint32_t>(bytes[offset])<<16U |
                      static_cast<std::uint32_t>(bytes[offset+1U])<<8U |
                      bytes[offset+2U];
                if (tempo==0U) throw std::runtime_error("music tempo is zero");
            }
            if (type==0x2FU) {
                if (length!=0U) throw std::runtime_error("music end event is invalid");
                ended=true;
                break;
            }
            offset+=length;
            continue;
        }
        if (status==0xF0U || status==0xF7U) {
            ++offset;
            const std::uint32_t length=read_vlq(bytes,offset,track_end);
            require_range(bytes,offset,length,"system event");
            offset+=length;
            continue;
        }
        if ((status&0x80U)!=0U) {
            running_status=status;
            ++offset;
        } else {
            status=running_status;
        }
        if ((status&0x80U)==0U || (status&0xF0U)==0xF0U) {
            throw std::runtime_error("music running status is invalid");
        }
        const std::uint8_t kind=status&0xF0U;
        const std::size_t data_size=(kind==0xC0U || kind==0xD0U)?1U:2U;
        require_range(bytes,offset,data_size,"channel event");
        const std::uint8_t a=bytes[offset];
        const std::uint8_t b=data_size==2U?bytes[offset+1U]:0U;
        if ((a&0x80U)!=0U || (b&0x80U)!=0U) {
            throw std::runtime_error("music channel event data is invalid");
        }
        offset+=data_size;
        result.events.push_back({static_cast<std::uint64_t>(std::llround(frame)),status,a,b});
    }
    if (!ended) throw std::runtime_error("music sequence has no end event");
    result.frames=std::max<std::uint64_t>(1U,static_cast<std::uint64_t>(std::llround(frame)));
    return result;
}

float envelope_level(const Voice& voice,std::uint32_t rate) {
    const auto& env=voice.sample->envelope;
    const float attack_level=static_cast<float>(env.attack_volume)/127.0F;
    const float decay_level=static_cast<float>(env.decay_volume)/127.0F;
    const std::uint64_t attack_frames=static_cast<std::uint64_t>(env.attack_us)*rate/1000000U;
    const std::uint64_t decay_frames=static_cast<std::uint64_t>(env.decay_us)*rate/1000000U;
    if (voice.released) {
        const std::uint64_t release_frames=std::max<std::uint64_t>(
            1U,static_cast<std::uint64_t>(env.release_us)*rate/1000000U);
        return voice.release_age>=release_frames?0.0F:
            voice.release_level*(1.0F-static_cast<float>(voice.release_age)/release_frames);
    }
    if (attack_frames!=0U && voice.age<attack_frames) {
        return attack_level*static_cast<float>(voice.age)/attack_frames;
    }
    const std::uint64_t decay_age=voice.age>attack_frames?voice.age-attack_frames:0U;
    if (decay_frames!=0U && decay_age<decay_frames) {
        const float amount=static_cast<float>(decay_age)/decay_frames;
        return attack_level+(decay_level-attack_level)*amount;
    }
    return decay_level;
}

const MusicSample* select_sample(const Instrument& instrument,std::uint8_t key,
                                 std::uint8_t velocity) {
    const auto found=std::find_if(instrument.sounds.begin(),instrument.sounds.end(),
        [key,velocity](const MusicSample& sound) {
            return key>=sound.key_min && key<=sound.key_max &&
                   velocity>=sound.velocity_min && velocity<=sound.velocity_max;
        });
    return found==instrument.sounds.end()?nullptr:&*found;
}

void release_voices(std::vector<Voice>& voices,std::uint8_t channel,std::uint8_t key,
                    std::uint32_t output_rate) {
    for (Voice& voice:voices) {
        if (!voice.released && voice.channel==channel && voice.key==key) {
            voice.release_level=envelope_level(voice,output_rate);
            voice.released=true;
            voice.release_age=0U;
        }
    }
}

std::int16_t clamp_pcm(float sample) {
    const float clipped=std::clamp(sample,-32768.0F,32767.0F);
    return static_cast<std::int16_t>(std::lrint(clipped));
}

double playback_step(const Voice& voice,const Channel& channel,
                     std::uint32_t bank_rate,std::uint32_t output_rate) {
    const float bend=(static_cast<float>(channel.bend)-8192.0F)/8192.0F*2.0F;
    const float semitones=static_cast<float>(voice.key)-voice.sample->key_base+
                          static_cast<float>(voice.sample->detune)/100.0F+bend;
    return static_cast<double>(bank_rate)/output_rate*
           std::pow(2.0,static_cast<double>(semitones)/12.0);
}

MusicTrack render_track(const MusicBank& bank,const MidiSequence& sequence,
                        std::uint32_t sequence_index,std::uint32_t output_rate) {
    MusicTrack result;
    result.sample_rate=output_rate;
    result.sequence_index=sequence_index;
    if (sequence.frames>std::numeric_limits<std::size_t>::max()/2U) {
        throw std::runtime_error("music render is too large");
    }
    result.stereo_samples.resize(static_cast<std::size_t>(sequence.frames)*2U);
    std::array<Channel,16> channels{};
    std::vector<Voice> voices;
    voices.reserve(max_music_voices);
    std::size_t event_index=0U;
    for (std::uint64_t frame=0U; frame<sequence.frames; ++frame) {
        while (event_index<sequence.events.size() && sequence.events[event_index].frame<=frame) {
            const MidiEvent& event=sequence.events[event_index++];
            const std::uint8_t channel_index=event.status&0x0FU;
            Channel& channel=channels[channel_index];
            switch (event.status&0xF0U) {
            case 0x80U:
                release_voices(voices,channel_index,event.a,output_rate);
                break;
            case 0x90U:
                if (event.b==0U) {
                    release_voices(voices,channel_index,event.a,output_rate);
                } else if (channel.program<bank.instruments.size()) {
                    const Instrument& instrument=bank.instruments[channel.program];
                    if (const MusicSample* sample=select_sample(instrument,event.a,event.b)) {
                        if (voices.size()>=max_music_voices) voices.erase(voices.begin());
                        voices.push_back({sample,channel_index,event.a,event.b,
                            static_cast<float>(instrument.volume)/127.0F,
                            (static_cast<float>(instrument.pan)-64.0F)/63.0F});
                        voices.back().step=playback_step(
                            voices.back(),channel,bank.sample_rate,output_rate);
                    }
                }
                break;
            case 0xB0U:
                if (event.a==7U) channel.volume=event.b;
                if (event.a==10U) channel.pan=event.b;
                break;
            case 0xC0U:
                channel.program=event.a;
                break;
            case 0xE0U:
                channel.bend=static_cast<std::uint16_t>(event.a|(event.b<<7U));
                for (Voice& voice:voices) {
                    if (voice.channel==channel_index) {
                        voice.step=playback_step(
                            voice,channel,bank.sample_rate,output_rate);
                    }
                }
                break;
            default:
                break;
            }
        }
        float left=0.0F;
        float right=0.0F;
        for (Voice& voice:voices) {
            const MusicSample& sample=*voice.sample;
            if (sample.pcm.empty()) continue;
            const std::size_t sample_index=static_cast<std::size_t>(voice.position);
            if (sample_index>=sample.pcm.size()) continue;
            const std::size_t next_index=std::min(sample_index+1U,sample.pcm.size()-1U);
            const float fraction=static_cast<float>(voice.position-sample_index);
            const float pcm=sample.pcm[sample_index]+
                            (sample.pcm[next_index]-sample.pcm[sample_index])*fraction;
            const Channel& channel=channels[voice.channel];
            const float level=envelope_level(voice,output_rate);
            const float volume=0.22F*level*voice.instrument_volume*
                (static_cast<float>(sample.volume)/127.0F)*
                (static_cast<float>(voice.velocity)/127.0F)*
                (static_cast<float>(channel.volume)/127.0F);
            const float sample_pan=(static_cast<float>(sample.pan)-64.0F)/63.0F;
            const float channel_pan=(static_cast<float>(channel.pan)-64.0F)/63.0F;
            const float pan=std::clamp(voice.instrument_pan+sample_pan+channel_pan,-1.0F,1.0F);
            left+=pcm*volume*std::min(1.0F,1.0F-pan);
            right+=pcm*volume*std::min(1.0F,1.0F+pan);
            voice.position+=voice.step;
            if (sample.loop && voice.position>=sample.loop->second) {
                const double loop_length=sample.loop->second-sample.loop->first;
                voice.position=sample.loop->first+
                    std::fmod(voice.position-sample.loop->first,loop_length);
            }
            ++voice.age;
            if (voice.released) ++voice.release_age;
        }
        const std::size_t out=static_cast<std::size_t>(frame)*2U;
        result.stereo_samples[out]=clamp_pcm(left);
        result.stereo_samples[out+1U]=clamp_pcm(right);
        voices.erase(std::remove_if(voices.begin(),voices.end(),[output_rate](const Voice& voice) {
            if (voice.position>=voice.sample->pcm.size()) return true;
            return voice.released && envelope_level(voice,output_rate)<=0.0F;
        }),voices.end());
    }
    return result;
}
} // namespace

MusicTrack render_music_sequence(const Rom& rom,std::uint32_t sequence_index,
                                 std::uint32_t output_rate) {
    return render_track(load_music_bank(rom),
                        load_midi_sequence(rom,sequence_index,output_rate),
                        sequence_index,output_rate);
}

} // namespace battletanx
