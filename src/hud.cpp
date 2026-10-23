#include "battletanx/hud.hpp"

#include "battletanx/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace battletanx {
namespace {
constexpr std::size_t hud_rom_start=0x308290U;
constexpr std::size_t hud_rom_end=0x30BFE0U;
constexpr std::size_t gameplay_font_rom_start=0x2C3E50U;
constexpr std::size_t gameplay_font_rom_end=0x2CB230U;
constexpr std::size_t level_one_map_rom_start=0x7E6160U;
constexpr std::size_t level_one_map_rom_end=0x7E6890U;
constexpr int coordinate_scale=2;
constexpr int level_map_width=60;
constexpr int level_map_height=60;
constexpr int level_map_left=0x19;
constexpr int level_map_top=0xDB-level_map_height;
constexpr float level_map_world_width=4375.0F;
constexpr float level_map_world_depth=4375.0F;

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

void sprite_region(std::span<std::uint32_t> pixels,int width,int height,
                   const RgbaImage& image,int x,int y,std::uint32_t color,
                   std::uint32_t region_width,std::uint32_t region_height) {
    if (region_width>image.width || region_height>image.height)
        throw std::invalid_argument("HUD sprite region is outside the image");
    for (std::uint32_t sy=0U; sy<region_height; ++sy) {
        for (std::uint32_t sx=0U; sx<region_width; ++sx) {
            const auto source=tint(image.pixels[static_cast<std::size_t>(sy)*image.width+sx],color);
            if ((source>>24U)==0U) continue;
            rectangle(pixels,width,height,(x+static_cast<int>(sx))*coordinate_scale,
                      (y+static_cast<int>(sy))*coordinate_scale,
                      coordinate_scale,coordinate_scale,source);
        }
    }
}

void level_map_sprite(std::span<std::uint32_t> pixels,int width,int height,
                      const RgbaImage& image) {
    for (std::uint32_t sy=0U; sy<image.height; ++sy) {
        for (std::uint32_t sx=0U; sx<image.width; ++sx) {
            const std::uint32_t source=image.pixels[static_cast<std::size_t>(sy)*image.width+sx];
            const std::uint32_t alpha=(source>>24U)*0xBEU/255U;
            if (alpha==0U) continue;
            const std::uint32_t color=alpha<<24U|(source&0xFFFFFFU);
            rectangle(pixels,width,height,
                      (level_map_left+static_cast<int>(sx))*coordinate_scale,
                      (level_map_top+static_cast<int>(sy))*coordinate_scale,
                      coordinate_scale,coordinate_scale,color);
        }
    }
}

constexpr std::array<std::uint32_t,4> marker_colors{
    0xFF2828F0U,0xFF00CC00U,0xFFCC0000U,0xFFCCCC00U};

void marker(std::span<std::uint32_t> pixels,int width,int height,
            const HudMarker& unit) {
    const auto position=level_one_hud_map_position(unit.x,unit.z);
    const int center_x=(level_map_left+position.x)*coordinate_scale;
    const int center_y=(level_map_top+position.y)*coordinate_scale;
    const std::uint32_t color=marker_colors[unit.team%marker_colors.size()];
    if (!unit.player) {
        rectangle(pixels,width,height,center_x-2,center_y-2,4,4,color);
        return;
    }
    const float radians=unit.heading_degrees*3.14159265358979323846F/180.0F;
    const float forward_x=std::sin(radians);
    const float forward_y=-std::cos(radians);
    const float side_x=-forward_y;
    const float side_y=forward_x;
    for (int distance=-3; distance<=4; ++distance) {
        const float half_width=distance<0 ? 2.5F : (4-distance)*0.75F;
        for (int side=static_cast<int>(-half_width); side<=static_cast<int>(half_width); ++side) {
            const int x=center_x+static_cast<int>(std::lround(forward_x*distance+side_x*side));
            const int y=center_y+static_cast<int>(std::lround(forward_y*distance+side_y*side));
            pixel(pixels,width,height,x,y,color);
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

void retail_text(std::span<std::uint32_t> pixels,int width,int height,
                 const RetailFont& font,std::string_view value,int x,int y,
                 int scale,std::uint32_t color) {
    for (const unsigned char character:value) {
        if (character==' ') { x+=3*scale; continue; }
        if (character>=font.glyph_index.size()) continue;
        const std::uint8_t glyph_id=font.glyph_index[character];
        if (glyph_id==0xFFU || glyph_id>=font.glyphs.size()) continue;
        const auto& character_glyph=font.glyphs[glyph_id];
        for (std::uint32_t row=0U; row<font.height; ++row) {
            for (std::uint32_t column=0U; column<character_glyph.width; ++column) {
                const std::size_t source=(static_cast<std::size_t>(character_glyph.y)+row)*
                    font.image_width+character_glyph.x+column;
                const auto tinted=tint(font.atlas.pixels[source],color);
                if ((tinted>>24U)==0U) continue;
                rectangle(pixels,width,height,x+static_cast<int>(column)*scale,
                          y+static_cast<int>(row)*scale,scale,scale,tinted);
            }
        }
        x+=(static_cast<int>(character_glyph.width)+1)*scale;
    }
}

int retail_text_width(const RetailFont& font,std::string_view value,int scale) {
    int result{};
    for (const unsigned char character:value) {
        if (character==' ') { result+=3*scale; continue; }
        if (character>=font.glyph_index.size()) continue;
        const std::uint8_t glyph_id=font.glyph_index[character];
        if (glyph_id!=0xFFU && glyph_id<font.glyphs.size())
            result+=(static_cast<int>(font.glyphs[glyph_id].width)+1)*scale;
    }
    return result>0 ? result-scale : 0;
}

void number(std::span<std::uint32_t> pixels,int width,int height,
            std::int32_t value,int x,int y,int scale,std::uint32_t color);

void hud_number(std::span<std::uint32_t> pixels,int width,int height,
                const HudAssets& assets,std::int32_t value,int x,int y,
                int scale,std::uint32_t color) {
    if (assets.fonts.empty()) {
        number(pixels,width,height,value,x,y,scale,color);
        return;
    }
    std::array<char,16> buffer{};
    const auto converted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value);
    if (converted.ec!=std::errc{}) return;
    retail_text(pixels,width,height,assets.fonts[0],
                std::string_view(buffer.data(),converted.ptr),x,y,scale,color);
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

std::vector<RetailFont> decode_retail_fonts(std::span<const std::uint8_t> pack) {
    const std::size_t count=be32(pack,0U);
    if (count==0U || count>32U || 4U+count*4U>pack.size())
        throw std::runtime_error("font table header is invalid");
    std::vector<RetailFont> fonts;
    fonts.reserve(count);
    for (std::size_t entry=0U; entry<count; ++entry) {
        const std::size_t start=be32(pack,4U+entry*4U);
        const std::size_t end=entry+1U<count ? be32(pack,4U+(entry+1U)*4U) : pack.size();
        if (start>end || end>pack.size() || end-start<152U)
            throw std::runtime_error("font entry range is invalid");
        RetailFont font;
        font.height=be32(pack,start+4U);
        font.image_width=be32(pack,start+12U);
        const std::size_t image_offset=be32(pack,start+20U);
        if (font.height==0U || font.height>64U || font.image_width==0U ||
            font.image_width>1024U || image_offset<152U || image_offset>end-start)
            throw std::runtime_error("font metrics are invalid");
        std::copy_n(pack.begin()+static_cast<std::ptrdiff_t>(start+24U),
                    font.glyph_index.size(),font.glyph_index.begin());
        // Every retail font maps LF to one index past the drawable glyphs.
        // Text layout consumes the newline before the character renderer, so
        // that slot is not a TextGlyph record (one font contains atlas bytes
        // there). Treat it as the same non-drawing sentinel used by 0xFF.
        font.glyph_index['\n']=0xFFU;
        std::size_t glyph_count{};
        for (const auto index:font.glyph_index)
            if (index!=0xFFU) glyph_count=std::max(glyph_count,static_cast<std::size_t>(index)+1U);
        if (glyph_count==0U || 152U+glyph_count*4U>image_offset)
            throw std::runtime_error("font glyph table is invalid");
        font.glyphs.reserve(glyph_count);
        std::uint32_t image_height{};
        for (std::size_t glyph_index=0U; glyph_index<glyph_count; ++glyph_index) {
            const std::size_t offset=start+152U+glyph_index*4U;
            const FontGlyph glyph_data{pack[offset],pack[offset+1U],pack[offset+2U]};
            if (glyph_data.width==0U ||
                static_cast<std::uint32_t>(glyph_data.x)+glyph_data.width>font.image_width)
                throw std::runtime_error("font glyph metrics are invalid");
            image_height=std::max(image_height,
                static_cast<std::uint32_t>(glyph_data.y)+font.height);
            font.glyphs.push_back(glyph_data);
        }
        const std::size_t pixel_count=static_cast<std::size_t>(font.image_width)*image_height;
        if (start+image_offset>end || pixel_count>end-(start+image_offset))
            throw std::runtime_error("font atlas is truncated");
        font.image_height=image_height;
        font.atlas={font.image_width,image_height,std::vector<std::uint32_t>(pixel_count)};
        for (std::size_t pixel_index=0U; pixel_index<pixel_count; ++pixel_index) {
            const std::uint8_t encoded=pack[start+image_offset+pixel_index];
            const std::uint32_t intensity=(encoded>>4U)*17U;
            const std::uint32_t alpha=(encoded&15U)*17U;
            font.atlas.pixels[pixel_index]=alpha<<24U | intensity*0x010101U;
        }
        fonts.push_back(std::move(font));
    }
    return fonts;
}

HudAssets load_hud_assets(const Rom& rom) {
    auto assets=decode_hud_assets(rom.bytes().subspan(hud_rom_start,hud_rom_end-hud_rom_start));
    assets.fonts=decode_retail_fonts(rom.bytes().subspan(
        gameplay_font_rom_start,gameplay_font_rom_end-gameplay_font_rom_start));
    const auto map=rom.bytes().subspan(
        level_one_map_rom_start,level_one_map_rom_end-level_one_map_rom_start);
    constexpr std::size_t map_pixels=level_map_width*level_map_height/2U;
    if (map.size()!=32U+map_pixels+8U)
        throw std::runtime_error("level-one HUD map range is invalid");
    assets.level_map=decode_ci4_texture(
        map.subspan(32U,map_pixels),map.first(32U),level_map_width,level_map_height);
    return assets;
}

std::size_t filled_health_segments(std::int32_t health,std::int32_t max_health) noexcept {
    if (health<=0 || max_health<=0) return 0U;
    const auto clamped=std::min(health,max_health);
    return std::min<std::size_t>(16U,static_cast<std::size_t>(
        static_cast<double>(clamped)/static_cast<double>(max_health)*16.0));
}

HudMapPosition level_one_hud_map_position(float x,float z) noexcept {
    if (!std::isfinite(x) || !std::isfinite(z)) return {level_map_width/2,level_map_height/2};
    const int map_x=level_map_width/2+static_cast<int>(
        static_cast<float>(level_map_width/2)*x/level_map_world_width);
    const int map_y=level_map_height/2+static_cast<int>(
        static_cast<float>(level_map_height/2)*z/level_map_world_depth);
    return {std::clamp(map_x,0,level_map_width-1),
            std::clamp(map_y,0,level_map_height-1)};
}

void draw_player_hud(std::span<std::uint32_t> pixels,int width,int height,
                     const HudAssets& assets,const HudState& state) {
    if (width<=0 || height<=0 || pixels.size()!=
        static_cast<std::size_t>(width)*static_cast<std::size_t>(height))
        throw std::invalid_argument("HUD framebuffer dimensions are invalid");
    if (assets.images.size()!=retail_hud_image_count)
        throw std::invalid_argument("HUD asset set is incomplete");

    if (state.match_state==MatchState::title && state.briefing_image!=nullptr) {
        const auto& briefing=*state.briefing_image;
        const int logical_width=width/coordinate_scale;
        const int image_x=(logical_width-static_cast<int>(briefing.width))/2;
        rectangle(pixels,width,height,0,0,width,height,0xE0000000U);
        sprite(pixels,width,height,briefing,image_x,18,0xFFFFFFFFU);
        if (!state.objective.empty() && !assets.fonts.empty()) {
            constexpr int scale=2;
            const int objective_width=retail_text_width(assets.fonts[0],state.objective,scale);
            retail_text(pixels,width,height,assets.fonts[0],state.objective,
                        (width-objective_width)/2,220,scale,0xFFFFFFFFU);
        }
    }

    const std::size_t filled=filled_health_segments(state.health,state.max_health);
    for (std::size_t segment=0U; segment<16U; ++segment)
        sprite_region(pixels,width,height,assets.images[7],
                      0x7B+static_cast<int>(segment)*5,0x10,
                      segment<filled ? 0xFFC83200U : 0xFF202020U,5U,8U);
    sprite(pixels,width,height,assets.images[18],270,8,0xFFFFFFFFU);
    hud_number(pixels,width,height,assets,state.primary_ammo,278*coordinate_scale,
               39*coordinate_scale,coordinate_scale,0xFFFFFFFFU);
    hud_number(pixels,width,height,assets,state.score,0x9C*coordinate_scale,
               0x1A*coordinate_scale,coordinate_scale,0xFFFFFFFFU);
    if (assets.level_map) {
        level_map_sprite(pixels,width,height,*assets.level_map);
        for (const auto& unit:state.markers) marker(pixels,width,height,unit);
    }

    std::string_view banner;
    std::array<char,2> countdown_text{};
    std::uint32_t color=0xE0FFFFFFU;
    if (state.match_state==MatchState::title) banner="PRESS START";
    else if (state.match_state==MatchState::countdown) {
        const auto count=std::clamp((state.frames_remaining+23)/24,1,5);
        countdown_text[0]=static_cast<char>('0'+count);
        banner=std::string_view(countdown_text.data(),1U);
    } else if (state.match_state==MatchState::paused) banner="PAUSED";
    else if (state.match_state==MatchState::result_banner) {
        banner=state.health>0 && !state.result.empty() ? state.result :
            state.health>0 ? "SUCCESS" : "FAILED";
        color=state.health>0 ? 0xE040FF40U : 0xE0FF4040U;
    }
    if (!banner.empty()) {
        constexpr int scale=4;
        const int text_width=!assets.fonts.empty() ?
            retail_text_width(assets.fonts[0],banner,scale) :
            static_cast<int>(banner.size())*6*scale-scale;
        const int x=(width-text_width)/2;
        const int text_height=!assets.fonts.empty() ?
            static_cast<int>(assets.fonts[0].height)*scale : 7*scale;
        const int y=state.match_state==MatchState::title && state.briefing_image!=nullptr ?
            height-text_height-32 : (height-text_height)/2;
        rectangle(pixels,width,height,x-12,y-10,text_width+24,text_height+20,0xA0000000U);
        if (!assets.fonts.empty())
            retail_text(pixels,width,height,assets.fonts[0],banner,x,y,scale,color);
        else text(pixels,width,height,banner,x,y,scale,color);
    }
}

} // namespace battletanx
