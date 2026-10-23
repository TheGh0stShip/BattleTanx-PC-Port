#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {

struct RgbaImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint32_t> pixels;
};

RgbaImage decode_rgba16_texture(std::span<const std::uint8_t> source,
                                std::uint32_t width,std::uint32_t height);
RgbaImage decode_ci4_texture(std::span<const std::uint8_t> source,
                             std::span<const std::uint8_t> palette,
                             std::uint32_t width,std::uint32_t height);
RgbaImage decode_ci8_texture(std::span<const std::uint8_t> source,
                             std::span<const std::uint8_t> palette,
                             std::uint32_t width,std::uint32_t height);
RgbaImage decode_rgba32_texture(std::span<const std::uint8_t> source,
                                std::uint32_t width,std::uint32_t height);
RgbaImage decode_i4_texture(std::span<const std::uint8_t> source,
                            std::uint32_t width,std::uint32_t height);
RgbaImage decode_i8_texture(std::span<const std::uint8_t> source,
                            std::uint32_t width,std::uint32_t height);

} // namespace battletanx
