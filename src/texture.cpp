#include "battletanx/texture.hpp"
#include "battletanx/big_endian.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace battletanx {
namespace {

std::size_t checked_pixel_count(std::uint32_t width,std::uint32_t height) {
    if (width==0U || height==0U || width>1024U || height>1024U ||
        width>std::numeric_limits<std::size_t>::max()/height) {
        throw std::runtime_error("texture dimensions are invalid");
    }
    return static_cast<std::size_t>(width)*height;
}

void require_bytes(std::span<const std::uint8_t> source,std::size_t count) {
    if (source.size()<count) throw std::runtime_error("texture data is truncated");
}

std::uint8_t expand_five(std::uint32_t value) {
    return static_cast<std::uint8_t>((value<<3U)|(value>>2U));
}

std::uint32_t decode_rgba5551(std::uint16_t value) {
    return (value&1U ? 0xFF000000U : 0U) |
           (static_cast<std::uint32_t>(expand_five((value>>11U)&31U))<<16U) |
           (static_cast<std::uint32_t>(expand_five((value>>6U)&31U))<<8U) |
           expand_five((value>>1U)&31U);
}

} // namespace

RgbaImage decode_rgba16_texture(std::span<const std::uint8_t> source,
                                std::uint32_t width,std::uint32_t height) {
    const std::size_t count=checked_pixel_count(width,height);
    require_bytes(source,count*2U);
    const BigEndianView view(source);
    RgbaImage image{width,height,std::vector<std::uint32_t>(count)};
    for (std::size_t index=0U; index<count; ++index) {
        image.pixels[index]=decode_rgba5551(view.u16(index*2U));
    }
    return image;
}

RgbaImage decode_ci4_texture(std::span<const std::uint8_t> source,
                             std::span<const std::uint8_t> palette,
                             std::uint32_t width,std::uint32_t height) {
    const std::size_t count=checked_pixel_count(width,height);
    require_bytes(source,(count+1U)/2U);
    require_bytes(palette,32U);
    const BigEndianView palette_view(palette);
    std::array<std::uint32_t,16> colors{};
    for (std::size_t index=0U; index<colors.size(); ++index) {
        colors[index]=decode_rgba5551(palette_view.u16(index*2U));
    }
    RgbaImage image{width,height,std::vector<std::uint32_t>(count)};
    for (std::size_t index=0U; index<count; ++index) {
        const std::uint8_t packed=source[index/2U];
        image.pixels[index]=colors[(index&1U)!=0U ? packed&15U : packed>>4U];
    }
    return image;
}

RgbaImage decode_rgba32_texture(std::span<const std::uint8_t> source,
                                std::uint32_t width,std::uint32_t height) {
    const std::size_t count=checked_pixel_count(width,height);
    require_bytes(source,count*4U);
    RgbaImage image{width,height,std::vector<std::uint32_t>(count)};
    for (std::size_t index=0U; index<count; ++index) {
        const std::size_t offset=index*4U;
        image.pixels[index]=(static_cast<std::uint32_t>(source[offset+3U])<<24U) |
                            (static_cast<std::uint32_t>(source[offset])<<16U) |
                            (static_cast<std::uint32_t>(source[offset+1U])<<8U) |
                            source[offset+2U];
    }
    return image;
}

RgbaImage decode_i4_texture(std::span<const std::uint8_t> source,
                            std::uint32_t width,std::uint32_t height) {
    const std::size_t count=checked_pixel_count(width,height);
    require_bytes(source,(count+1U)/2U);
    RgbaImage image{width,height,std::vector<std::uint32_t>(count)};
    for (std::size_t index=0U; index<count; ++index) {
        const std::uint8_t packed=source[index/2U];
        const std::uint32_t intensity=((index&1U)!=0U ? packed&15U : packed>>4U)*17U;
        image.pixels[index]=0xFF000000U|intensity*0x010101U;
    }
    return image;
}

RgbaImage decode_i8_texture(std::span<const std::uint8_t> source,
                            std::uint32_t width,std::uint32_t height) {
    const std::size_t count=checked_pixel_count(width,height);
    require_bytes(source,count);
    RgbaImage image{width,height,std::vector<std::uint32_t>(count)};
    for (std::size_t index=0U; index<count; ++index) {
        image.pixels[index]=0xFF000000U|static_cast<std::uint32_t>(source[index])*0x010101U;
    }
    return image;
}

} // namespace battletanx
