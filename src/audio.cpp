#include "battletanx/audio.hpp"

#include "battletanx/big_endian.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace battletanx {
namespace {
constexpr std::size_t control_start=0x215620U;
constexpr std::size_t control_end=0x217EC0U;
constexpr std::size_t wave_start=0x100000U;
constexpr std::size_t wave_end=0x215620U;
constexpr std::uint16_t bank_revision=0x4231U;
constexpr std::size_t frame_bytes=9U;

std::int16_t clamp_sample(std::int32_t value) noexcept {
    return static_cast<std::int16_t>(std::clamp<std::int32_t>(
        value,std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max()));
}

void add_wrapped(std::uint32_t& accumulator,std::int32_t left,
                 std::int32_t right) noexcept {
    accumulator+=static_cast<std::uint32_t>(
        static_cast<std::int64_t>(left)*static_cast<std::int64_t>(right));
}

void require_range(std::span<const std::uint8_t> bytes,std::size_t offset,
                   std::size_t size,const char* description) {
    if (offset>bytes.size() || size>bytes.size()-offset) {
        throw std::runtime_error(std::string("sound bank ")+description+" is out of range");
    }
}
} // namespace

double advance_looping_cursor(double cursor,float pitch,std::size_t sample_count,
                              std::uint32_t loop_start,std::uint32_t loop_end) noexcept {
    if (!std::isfinite(cursor) || !std::isfinite(pitch) || pitch<=0.0F || sample_count==0U) {
        return 0.0;
    }
    const bool valid_loop=loop_end>loop_start && loop_end<=sample_count;
    const double begin=valid_loop ? static_cast<double>(loop_start) : 0.0;
    const double end=valid_loop ? static_cast<double>(loop_end) :
                                 static_cast<double>(sample_count);
    cursor+=pitch;
    if (cursor>=end) cursor=begin+std::fmod(cursor-begin,end-begin);
    return cursor;
}

VehicleSoundMix vehicle_sound_mix(float velocity_x,float velocity_z,
                                  float master_gain) noexcept {
    if (!std::isfinite(velocity_x) || !std::isfinite(velocity_z) ||
        !std::isfinite(master_gain)) return {};
    const float x=std::abs(velocity_x);
    const float z=std::abs(velocity_z);
    const float speed=std::min(1.0F,(std::max(x,z)+0.75F*std::min(x,z))/100.0F);
    master_gain=std::clamp(master_gain,0.0F,1.0F);
    return {speed,0.5F*master_gain,speed+0.8F,
            speed>0.5F ? 2.0F*(speed-0.5F)*master_gain : 0.0F};
}

std::vector<std::int16_t> decode_vadpcm(
    std::span<const std::uint8_t> encoded,
    std::span<const std::int16_t> coefficients,
    std::uint32_t predictor_count) {
    if (predictor_count==0U || predictor_count>16U ||
        coefficients.size()!=static_cast<std::size_t>(predictor_count)*16U) {
        throw std::runtime_error("invalid VADPCM predictor book");
    }
    const std::size_t remainder=encoded.size()%frame_bytes;
    if (remainder!=0U && (remainder!=1U || encoded.back()!=0U)) {
        throw std::runtime_error("VADPCM payload is not frame aligned");
    }
    const std::size_t encoded_size=encoded.size()-remainder;
    std::vector<std::int16_t> output;
    output.reserve(encoded_size/frame_bytes*16U);
    std::array<std::int16_t,8> state{};
    for (std::size_t frame=0U; frame<encoded_size; frame+=frame_bytes) {
        const std::uint8_t header=encoded[frame];
        const std::uint32_t scale=header>>4U;
        const std::uint32_t predictor=header&0xFU;
        if (scale>12U || predictor>=predictor_count) {
            throw std::runtime_error("VADPCM frame header is invalid");
        }
        const std::size_t coefficient_base=static_cast<std::size_t>(predictor)*16U;
        for (std::size_t half=0U; half<2U; ++half) {
            std::array<std::int32_t,8> residuals{};
            for (std::size_t index=0U; index<8U; ++index) {
                const std::uint8_t packed=encoded[frame+1U+half*4U+index/2U];
                const std::uint8_t raw=(index&1U)==0U ? packed>>4U : packed&0xFU;
                const std::int32_t nibble=raw<8U ? raw : static_cast<std::int32_t>(raw)-16;
                residuals[index]=nibble*static_cast<std::int32_t>(1U<<scale);
            }
            const std::int32_t older=state[6];
            const std::int32_t newer=state[7];
            for (std::size_t index=0U; index<8U; ++index) {
                std::uint32_t accumulator{};
                add_wrapped(accumulator,residuals[index],2048);
                add_wrapped(accumulator,coefficients[coefficient_base+index],older);
                add_wrapped(accumulator,coefficients[coefficient_base+8U+index],newer);
                for (std::size_t previous=0U; previous<index; ++previous) {
                    add_wrapped(accumulator,coefficients[coefficient_base+8U+previous],
                                residuals[index-1U-previous]);
                }
                const std::int32_t signed_accumulator=std::bit_cast<std::int32_t>(accumulator);
                const std::int16_t sample=clamp_sample(signed_accumulator>>11U);
                state[index]=sample;
                output.push_back(sample);
            }
        }
    }
    return output;
}

SoundBank load_sound_effect_bank(const Rom& rom) {
    const auto control=rom.bytes().subspan(control_start,control_end-control_start);
    const auto waves=rom.bytes().subspan(wave_start,wave_end-wave_start);
    const BigEndianView view(control);
    if (view.u16(0U)!=bank_revision || view.u16(2U)!=1U) {
        throw std::runtime_error("sound bank has an invalid B1 header");
    }
    const std::size_t bank=view.u32(4U);
    require_range(control,bank,16U,"record");
    const std::int16_t instrument_count=view.s16(bank);
    if (instrument_count!=1) throw std::runtime_error("sound bank instrument count is invalid");
    SoundBank result;
    result.sample_rate=view.u32(bank+4U);
    if (result.sample_rate<8000U || result.sample_rate>96000U) {
        throw std::runtime_error("sound bank sample rate is invalid");
    }
    const std::size_t instrument=view.u32(bank+12U);
    require_range(control,instrument,16U,"instrument");
    const std::uint8_t instrument_volume=view.u8(instrument);
    const std::int16_t sound_count=view.s16(instrument+14U);
    if (sound_count<=0 || sound_count>256) {
        throw std::runtime_error("sound bank sound count is invalid");
    }
    require_range(control,instrument+16U,static_cast<std::size_t>(sound_count)*4U,
                  "sound pointer table");
    result.sounds.reserve(static_cast<std::size_t>(sound_count));
    for (std::int16_t index=0; index<sound_count; ++index) {
        const std::size_t sound=view.u32(instrument+16U+static_cast<std::size_t>(index)*4U);
        require_range(control,sound,16U,"sound");
        const std::size_t wave=view.u32(sound+8U);
        require_range(control,wave,20U,"wave table");
        const std::size_t sample_offset=view.u32(wave);
        const std::size_t sample_size=view.u32(wave+4U);
        if (view.u8(wave+8U)!=0U) throw std::runtime_error("sound bank contains a non-ADPCM wave");
        require_range(waves,sample_offset,sample_size,"sample data");
        const std::size_t book=view.u32(wave+16U);
        require_range(control,book,8U,"predictor book");
        const std::uint32_t order=view.u32(book);
        const std::uint32_t predictors=view.u32(book+4U);
        if (order!=2U || predictors==0U || predictors>16U) {
            throw std::runtime_error("sound bank predictor dimensions are invalid");
        }
        const std::size_t coefficient_count=static_cast<std::size_t>(predictors)*16U;
        require_range(control,book+8U,coefficient_count*2U,"predictor coefficients");
        std::vector<std::int16_t> coefficients(coefficient_count);
        for (std::size_t coefficient=0U; coefficient<coefficient_count; ++coefficient) {
            coefficients[coefficient]=view.s16(book+8U+coefficient*2U);
        }
        PcmSound decoded;
        decoded.samples=decode_vadpcm(
            waves.subspan(sample_offset,sample_size),coefficients,predictors);
        const std::size_t loop=view.u32(wave+12U);
        if (loop!=0U) {
            require_range(control,loop,12U,"loop record");
            const std::uint32_t loop_start=view.u32(loop);
            const std::uint32_t loop_end=view.u32(loop+4U);
            const std::uint32_t loop_count=view.u32(loop+8U);
            if (loop_count!=0U) {
                if (loop_end<=loop_start || loop_end>decoded.samples.size()) {
                    throw std::runtime_error("sound bank loop range is invalid");
                }
                decoded.loop_start=loop_start;
                decoded.loop_end=loop_end;
            }
        }
        decoded.sample_rate=result.sample_rate;
        decoded.volume=static_cast<std::uint8_t>(
            static_cast<std::uint32_t>(instrument_volume)*view.u8(sound+13U)/127U);
        decoded.pan=view.u8(sound+12U);
        result.sounds.push_back(std::move(decoded));
    }
    return result;
}

} // namespace battletanx
