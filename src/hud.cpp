#include "battletanx/hud.hpp"

#include "battletanx/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace battletanx {
namespace {
constexpr std::size_t hud_rom_start=0x308290U;
constexpr std::size_t hud_rom_end=0x30BFE0U;
constexpr int coordinate_scale=2;

std::uint32_t be32(std::span<const std::uint8_t> bytes,std::size_t offset) {
    if (offset>bytes.size() || bytes.size()-offset<4U)
        throw std::runtime_error("HUD image table is truncated");
    return static_cast<std::uint32_t>(bytes[offset])<<24U |
           static_cast<std::uint32_t>(bytes[offset+1U])<<16U |
           static_cast<std::uint32_t>(bytes[offset+2U])<<8U | bytes[offset+3U];
}

std::uint32_t tint(std::uint32_t pixel,std::uint32_t color) noexcept {
    const auto mul=[](std::uint32_t a,std::uint32_t b) { return (a*b+127U)/255U; };
    const std::uint32_t alpha=((pixel>>24U)*0x80U+127U)/255U;
    return alpha<<24U | mul((pixel>>16U)&255U,(color>>16U)&255U)<<16U |
           mul((pixel>>8U)&255U,(color>>8U)&255U)<<8U |
           mul(pixel&255U,color&255U);
}

void pixel(std::span<std::uint32_t> pixels,int width,int height,
           int x,int y,std::uint32_t color) {
    if (x<0 || y<0 || x>=width || y>=height) return;
    auto& destination=pixels[static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+
                             static_cast<std::size_t>(x)];
    destination=alpha_blend(color,destination);
}

void rectangle(std::span<std::uint32_t> pixels,int width,int height,
               int x,int y,int w,int h,std::uint32_t color) {
    for (int row=0; row<h; ++row)
        for (int column=0; column<w; ++column)
            pixel(pixels,width,height,x+column,y+row,color);
}

void sprite(std::span<std::uint32_t> pixels,int width,int height,
            const RgbaImage& image,int x,int y,std::uint32_t color) {
    for (std::uint32_t sy=0U; sy<image.height; ++sy) {
        for (std::uint32_t sx=0U; sx<image.width; ++sx) {
            const auto source=tint(image.pixels[static_cast<std::size_t>(sy)*image.width+sx],color);
            if ((source>>24U)==0U) continue;
            rectangle(pixels,width,height,(x+static_cast<int>(sx))*coordinate_scale,
                      (y+static_cast<int>(sy))*coordinate_scale,
                      coordinate_scale,coordinate_scale,source);
        }
    }
}

using Glyph=std::array<std::uint8_t,7>;
Glyph glyph(char c) noexcept {
    switch (c) {
    case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
    case '6': return {6,8,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,2,12};
    case '-': return {0,0,0,31,0,0,0}; case 'A': return {14,17,17,31,17,17,17};
    case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
    case 'I': return {14,4,4,4,4,4,14}; case 'L': return {16,16,16,16,16,16,31};
    case 'P': return {30,17,17,30,16,16,16}; case 'S': return {15,16,16,14,1,1,30};
    case 'U': return {17,17,17,17,17,17,14}; default: return {};
    }
}

void text(std::span<std::uint32_t> pixels,int width,int height,
          std::string_view value,int x,int y,int scale,std::uint32_t color) {
    for (const char c:value) {
        const auto rows=glyph(c);
        for (std::size_t row=0U; row<rows.size(); ++row)
            for (int column=0; column<5; ++column)
                if ((rows[row]&(1U<<(4-column)))!=0U)
                    rectangle(pixels,width,height,x+column*scale,
                              y+static_cast<int>(row)*scale,scale,scale,color);
        x+=6*scale;
    }
}

void number(std::span<std::uint32_t> pixels,int width,int height,
            std::int32_t value,int x,int y,int scale,std::uint32_t color) {
    std::array<char,16> reverse{};
    std::size_t count=0U;
    std::int64_t magnitude=value;
    if (magnitude<0) magnitude=-magnitude;
    do { reverse[count++]=static_cast<char>('0'+magnitude%10); magnitude/=10; }
    while (magnitude!=0 && count<reverse.size()-1U);
    if (value<0) reverse[count++]='-';
    for (std::size_t index=0U; index<count; ++index) {
        const char c=reverse[count-index-1U];
        text(pixels,width,height,std::string_view(&c,1U),x,y,scale,color);
        x+=6*scale;
    }
}
} // namespace

HudAssets decode_hud_assets(std::span<const std::uint8_t> pack) {
    if (be32(pack,0U)!=retail_hud_image_count)
        throw std::runtime_error("HUD image count does not match retail data");
    HudAssets assets;
    assets.images.reserve(retail_hud_image_count);
    for (std::size_t index=0U; index<retail_hud_image_count; ++index) {
        const std::size_t start=be32(pack,4U+index*4U);
        const std::size_t end=index+1U<retail_hud_image_count ?
            be32(pack,4U+(index+1U)*4U) : pack.size();
        if (start>end || end>pack.size() || end-start<0x220U)
            throw std::runtime_error("HUD image range is invalid");
        const std::size_t available_texels=end-start-0x20U;
        const std::uint32_t dimension=available_texels>=0x800U && available_texels<=0x808U ?
                                      64U : available_texels>=0x200U && available_texels<=0x208U ?
                                      32U : 0U;
        if (dimension==0U) throw std::runtime_error("HUD image dimensions are unsupported");
        const std::size_t texel_bytes=static_cast<std::size_t>(dimension)*dimension/2U;
        assets.images.push_back(decode_ci4_texture(
            pack.subspan(start+0x20U,texel_bytes),pack.subspan(start,0x20U),
            dimension,dimension));
    }
    return assets;
}

HudAssets load_hud_assets(const Rom& rom) {
    return decode_hud_assets(rom.bytes().subspan(hud_rom_start,hud_rom_end-hud_rom_start));
}

std::size_t filled_health_segments(std::int32_t health,std::int32_t max_health) noexcept {
    if (health<=0 || max_health<=0) return 0U;
    const auto clamped=std::min(health,max_health);
    return std::min<std::size_t>(16U,static_cast<std::size_t>(
        static_cast<double>(clamped)/static_cast<double>(max_health)*16.0));
}

void draw_player_hud(std::span<std::uint32_t> pixels,int width,int height,
                     const HudAssets& assets,const HudState& state) {
    if (width<=0 || height<=0 || pixels.size()!=
        static_cast<std::size_t>(width)*static_cast<std::size_t>(height))
        throw std::invalid_argument("HUD framebuffer dimensions are invalid");
    if (assets.images.size()!=retail_hud_image_count)
        throw std::invalid_argument("HUD asset set is incomplete");

    const std::size_t filled=filled_health_segments(state.health,state.max_health);
    for (std::size_t segment=0U; segment<16U; ++segment)
        rectangle(pixels,width,height,
                  (0x7B+static_cast<int>(segment)*5)*coordinate_scale,
                  0x10*coordinate_scale,5*coordinate_scale,8*coordinate_scale,
                  segment<filled ? 0x80C83200U : 0x80202020U);
    sprite(pixels,width,height,assets.images[18],270,8,0xFFFFFFFFU);
    number(pixels,width,height,state.primary_ammo,278*coordinate_scale,
           39*coordinate_scale,coordinate_scale,0xFFFFFFFFU);
    number(pixels,width,height,state.score,0x9C*coordinate_scale,
           0x1A*coordinate_scale,coordinate_scale,0xFFFFFFFFU);

    std::string_view banner;
    std::uint32_t color=0xE0FFFFFFU;
    if (state.match_state==MatchState::paused) banner="PAUSED";
    else if (state.match_state==MatchState::result_banner) {
        banner=state.health>0 ? "SUCCESS" : "FAILED";
        color=state.health>0 ? 0xE040FF40U : 0xE0FF4040U;
    }
    if (!banner.empty()) {
        constexpr int scale=4;
        const int text_width=static_cast<int>(banner.size())*6*scale-scale;
        const int x=(width-text_width)/2;
        const int y=(height-7*scale)/2;
        rectangle(pixels,width,height,x-12,y-10,text_width+24,7*scale+20,0xA0000000U);
        text(pixels,width,height,banner,x,y,scale,color);
    }
}

} // namespace battletanx
