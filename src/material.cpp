#include "battletanx/material.hpp"
#include "battletanx/big_endian.hpp"
#include "battletanx/model.hpp"

#include <stdexcept>

namespace battletanx {
namespace {

struct ImageLoad {
    std::uint32_t offset{};
    std::uint32_t bytes{};
};

TextureWrap decode_wrap(std::uint32_t value) {
    if ((value&2U)!=0U) return TextureWrap::clamp;
    if ((value&1U)!=0U) return TextureWrap::mirror;
    return TextureWrap::repeat;
}

std::uint32_t size_bits(std::uint32_t size) {
    constexpr std::array<std::uint32_t,4> bits{4U,8U,16U,32U};
    return bits[size&3U];
}

std::uint32_t rgba_bytes_to_host(std::uint32_t value) {
    return ((value&0xFFU)<<24U)|(value>>8U);
}

} // namespace

NativeMaterial decode_material_blob(std::span<const std::uint8_t> blob,
                                    std::uint16_t animation) {
    constexpr std::size_t command_limit=512U;
    const BigEndianView view(blob);
    NativeMaterial material;
    std::uint32_t image_offset=0U;
    std::uint32_t image_size=0U;
    bool have_image_command=false;
    std::optional<ImageLoad> image_load;
    std::array<std::optional<ImageLoad>,16> palette_loads{};
    std::array<std::uint32_t,8> tile_tmem{};
    std::array<bool,8> have_tile{};
    std::uint32_t tile_format=0U;
    std::uint32_t tile_size=0U;
    bool have_render_tile=false;
    std::uint32_t render_palette=0U;
    bool have_tile_size=false;
    std::size_t display_list_end=0U;
    for (std::size_t command=0U; command<command_limit; ++command) {
        const std::size_t offset=command*8U;
        const std::uint32_t word0=view.u32(offset);
        const std::uint32_t word1=view.u32(offset+4U);
        switch (word0>>24U) {
        case 0xE6U:
        case 0xE7U:
        case 0xE8U:
            break;
        case 0xFDU:
            image_size=(word0>>19U)&3U;
            image_offset=word1;
            have_image_command=true;
            break;
        case 0xF3U: {
            if (!have_image_command || image_load) {
                throw std::runtime_error("material has an invalid texture load");
            }
            const std::uint32_t texels=((word1>>12U)&0xFFFU)+1U;
            image_load=ImageLoad{image_offset,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(texels)*
                                            size_bits(image_size)+7U)/8U)};
            break;
        }
        case 0xF0U: {
            const std::uint32_t tile=(word1>>24U)&7U;
            if (!have_image_command || !have_tile[tile]) {
                throw std::runtime_error("material has an invalid palette load");
            }
            const std::uint32_t colors=((word1>>14U)&0x3FFU)+1U;
            const std::uint32_t tmem=tile_tmem[tile];
            if (colors!=16U || tmem<256U || ((tmem-256U)&15U)!=0U ||
                (tmem-256U)/16U>=palette_loads.size()) {
                throw std::runtime_error("material palette has invalid TMEM placement");
            }
            palette_loads[(tmem-256U)/16U]=ImageLoad{image_offset,colors*2U};
            break;
        }
        case 0xF5U: {
            const std::uint32_t tile=(word1>>24U)&7U;
            tile_tmem[tile]=word0&0x1FFU;
            have_tile[tile]=true;
            if (tile==0U) {
                tile_format=(word0>>21U)&7U;
                tile_size=(word0>>19U)&3U;
                render_palette=(word1>>20U)&15U;
                material.wrap_t=decode_wrap((word1>>18U)&3U);
                material.wrap_s=decode_wrap((word1>>8U)&3U);
                have_render_tile=true;
            }
            break;
        }
        case 0xF2U:
            if (((word1>>24U)&7U)==0U) {
                const std::uint32_t left=(word0>>12U)&0xFFFU;
                const std::uint32_t top=word0&0xFFFU;
                const std::uint32_t right=(word1>>12U)&0xFFFU;
                const std::uint32_t bottom=word1&0xFFFU;
                if (right<left || bottom<top) {
                    throw std::runtime_error("material tile has inverted bounds");
                }
                material.tile_left=static_cast<float>(left)/4.0F;
                material.tile_top=static_cast<float>(top)/4.0F;
                material.width=((right-left)>>2U)+1U;
                material.height=((bottom-top)>>2U)+1U;
                have_tile_size=true;
            }
            break;
        case 0xBBU:
            if (word0!=0xBB000001U || word1!=0x80008000U) {
                throw std::runtime_error("material has an unsupported texture scale");
            }
            break;
        case 0xB7U:
            if ((word1&0x2000U)!=0U) material.cull=CullMode::back;
            if ((word1&1U)!=0U) material.depth_test=true;
            break;
        case 0xB6U:
            if (word1!=0x3000U) {
                throw std::runtime_error("material clears unsupported geometry state");
            }
            material.cull=CullMode::none;
            break;
        case 0xBAU:
            if (word0==0xBA000C02U) material.bilinear=(word1&0x2000U)!=0U;
            break;
        case 0xB9U:
            material.raw_render_mode=word1;
            material.depth_test=true;
            if (word1==0x552230U) material.alpha=AlphaMode::opaque;
            else if (word1==0x553078U) material.alpha=AlphaMode::edge_test;
            else material.alpha=AlphaMode::other;
            break;
        case 0xFCU:
            if (word0==0xFC127E24U && word1==0xFFFFF3F9U) {
                material.combine=CombineMode::texture_times_shade;
            } else if (word0==0xFCFFFFFFU && word1==0xFFFDF6FBU) {
                material.combine=CombineMode::primitive;
            } else if (word0==0xFCFFFFFFU && word1==0xFFFCF279U) {
                material.combine=CombineMode::texture;
            } else if (word0==0xFC5096A1U && word1==0x332DFEFFU) {
                material.combine=CombineMode::primitive_environment_lerp;
            } else {
                throw std::runtime_error("material has an unsupported combine mode");
            }
            break;
        case 0xFAU:
            material.primitive_rgba=rgba_bytes_to_host(word1);
            break;
        case 0xB8U:
            if (word0!=0xB8000000U || word1!=0U) {
                throw std::runtime_error("material has an invalid end command");
            }
            display_list_end=offset+8U;
            command=command_limit;
            break;
        default:
            throw std::runtime_error("material contains an unsupported command");
        }
        if (display_list_end!=0U) break;
    }
    if (display_list_end==0U) throw std::runtime_error("material has no end command");

    if (image_load) {
        if (!have_render_tile || !have_tile_size) {
            throw std::runtime_error("textured material has no render tile");
        }
        const std::uint64_t expected_bits=static_cast<std::uint64_t>(material.width)*
            material.height*size_bits(tile_size);
        if ((expected_bits+7U)/8U!=image_load->bytes) {
            throw std::runtime_error("material texture size disagrees with its tile");
        }
        if (image_load->offset<display_list_end || (image_load->offset&7U)!=0U) {
            throw std::runtime_error("material texture overlaps its command list");
        }
        const auto pixels=BlobView(blob,{image_load->offset,image_load->bytes}).bytes();
        if (tile_format==0U && tile_size==2U) {
            material.texture=decode_rgba16_texture(pixels,material.width,material.height);
        } else if (tile_format==0U && tile_size==3U) {
            material.texture=decode_rgba32_texture(pixels,material.width,material.height);
        } else if (tile_format==2U && tile_size==0U) {
            const auto& palette_load=palette_loads[render_palette];
            if (!palette_load || palette_load->bytes!=32U ||
                palette_load->offset<display_list_end || (palette_load->offset&7U)!=0U) {
                throw std::runtime_error("CI4 material has an invalid palette");
            }
            const auto palette=BlobView(
                blob,{palette_load->offset,palette_load->bytes}).bytes();
            material.texture=decode_ci4_texture(
                pixels,palette,material.width,material.height);
        } else if (tile_format==4U && tile_size==0U) {
            material.texture=decode_i4_texture(pixels,material.width,material.height);
        } else if (tile_format==4U && tile_size==1U) {
            material.texture=decode_i8_texture(pixels,material.width,material.height);
        } else {
            throw std::runtime_error("material uses an unsupported texture format");
        }
    } else {
        for (const auto& palette_load:palette_loads) {
            if (palette_load) throw std::runtime_error("material has a palette without a texture");
        }
    }

    const std::uint32_t frame_offset=animation&0x7FFU;
    const std::uint32_t frame_count=animation>>11U;
    for (std::uint32_t frame=0U; frame<frame_count; ++frame) {
        const std::uint32_t offset=frame_offset+frame*8U;
        const auto command=BigEndianView(BlobView(blob,{offset,8U}).bytes());
        const std::uint32_t word0=command.u32(0U);
        const std::uint32_t word1=command.u32(4U);
        const std::uint32_t opcode=word0>>24U;
        if (opcode!=0xF2U && opcode!=0xF5U) {
            throw std::runtime_error("material animation uses an unsupported command");
        }
        material.animation_frames.push_back({word0,word1});
    }
    return material;
}

} // namespace battletanx
