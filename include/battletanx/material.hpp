#pragma once

#include "battletanx/texture.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battletanx {

enum class TextureWrap : std::uint8_t { repeat,mirror,clamp };
enum class CullMode : std::uint8_t { none,back };
enum class AlphaMode : std::uint8_t { opaque,edge_test,other };
enum class CombineMode : std::uint8_t {
    texture_times_shade,primitive,texture,primitive_environment_lerp
};

struct MaterialFrame {
    std::uint32_t word0{};
    std::uint32_t word1{};
};

struct NativeMaterial {
    std::optional<RgbaImage> texture;
    float tile_left{};
    float tile_top{};
    std::uint32_t width{1U};
    std::uint32_t height{1U};
    TextureWrap wrap_s{TextureWrap::repeat};
    TextureWrap wrap_t{TextureWrap::repeat};
    CullMode cull{CullMode::none};
    AlphaMode alpha{AlphaMode::opaque};
    CombineMode combine{CombineMode::texture_times_shade};
    bool bilinear{};
    bool depth_test{};
    std::uint32_t primitive_rgba{0xFFFFFFFFU};
    std::uint32_t raw_render_mode{};
    std::vector<MaterialFrame> animation_frames;
};

NativeMaterial decode_material_blob(std::span<const std::uint8_t> blob,
                                    std::uint16_t animation);

} // namespace battletanx
