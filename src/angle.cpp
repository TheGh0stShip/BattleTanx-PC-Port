#include "battletanx/angle.hpp"
#include "battletanx/big_endian.hpp"

#include <stdexcept>

namespace battletanx {
namespace {
constexpr float kInterpolationScale=1.0F/256.0F;

float interpolate(const std::array<float,AngleTable::value_count>& values,
                  std::uint32_t index,std::uint32_t fraction) noexcept {
    const float base=values[index];
    return base+(values[index+1U]-base)*static_cast<float>(fraction)*kInterpolationScale;
}
} // namespace

AngleTable AngleTable::from_big_endian(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t required=value_count*sizeof(float);
    if (bytes.size()<required) throw std::runtime_error("angle table is truncated");
    const BigEndianView view(bytes.first(required));
    std::array<float,value_count> values{};
    for (std::size_t index=0; index<values.size(); ++index) values[index]=view.f32(index*4U);
    return AngleTable(values);
}

float AngleTable::sine(std::uint32_t angle) const noexcept {
    const std::uint32_t wrapped=angle&0xFFFFU;
    return interpolate(values_,wrapped>>8U,wrapped&0xFFU);
}

float AngleTable::cosine(std::uint32_t angle) const noexcept {
    const std::uint32_t wrapped=angle&0xFFFFU;
    const std::uint32_t index=((wrapped>>8U)+0x40U)&0xFFU;
    return interpolate(values_,index,wrapped&0xFFU);
}

} // namespace battletanx

