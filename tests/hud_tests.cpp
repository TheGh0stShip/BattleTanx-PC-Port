#include "battletanx/hud.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int failures{};
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition "\n"; \
    ++failures; } } while (false)

void write_be32(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value) {
    bytes[offset]=static_cast<std::uint8_t>(value>>24U);
    bytes[offset+1U]=static_cast<std::uint8_t>(value>>16U);
    bytes[offset+2U]=static_cast<std::uint8_t>(value>>8U);
    bytes[offset+3U]=static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> hud_fixture() {
    constexpr std::size_t header_size=0x68U;
    constexpr std::size_t image_size=0x220U;
    std::vector<std::uint8_t> bytes(
        header_size+battletanx::retail_hud_image_count*image_size);
    write_be32(bytes,0U,battletanx::retail_hud_image_count);
    for (std::size_t index=0U; index<battletanx::retail_hud_image_count; ++index) {
        const std::size_t start=header_size+index*image_size;
        write_be32(bytes,4U+index*4U,static_cast<std::uint32_t>(start));
        bytes[start+2U]=0xFFU;
        bytes[start+3U]=0xFFU; // palette entry 1: opaque white
        std::fill(bytes.begin()+static_cast<std::ptrdiff_t>(start+0x20U),
                  bytes.begin()+static_cast<std::ptrdiff_t>(start+image_size),
                  std::uint8_t{0x11U});
    }
    return bytes;
}

std::vector<std::uint8_t> font_fixture() {
    constexpr std::size_t entry=8U;
    constexpr std::size_t image_offset=160U;
    std::vector<std::uint8_t> bytes(entry+image_offset+4U,0U);
    write_be32(bytes,0U,1U);
    write_be32(bytes,4U,static_cast<std::uint32_t>(entry));
    write_be32(bytes,entry+4U,2U);
    write_be32(bytes,entry+12U,2U);
    write_be32(bytes,entry+20U,static_cast<std::uint32_t>(image_offset));
    std::fill(bytes.begin()+static_cast<std::ptrdiff_t>(entry+24U),
              bytes.begin()+static_cast<std::ptrdiff_t>(entry+152U),
              std::uint8_t{0xFFU});
    bytes[entry+24U+'A']=0U;
    bytes[entry+24U+'\n']=1U; // retail line-break sentinel, not a glyph record
    bytes[entry+152U]=0U;
    bytes[entry+153U]=0U;
    bytes[entry+154U]=2U;
    bytes[entry+image_offset]=0xFFU;
    bytes[entry+image_offset+1U]=0xF8U;
    bytes[entry+image_offset+2U]=0x84U;
    bytes[entry+image_offset+3U]=0x00U;
    return bytes;
}
} // namespace

int main() {
    CHECK(battletanx::filled_health_segments(100,100)==16U);
    CHECK(battletanx::filled_health_segments(50,100)==8U);
    CHECK(battletanx::filled_health_segments(1,100)==0U);
    CHECK(battletanx::filled_health_segments(0,100)==0U);
    CHECK(battletanx::filled_health_segments(100,0)==0U);
    CHECK(battletanx::secondary_weapon_icon(4U)==1U);
    CHECK(battletanx::secondary_weapon_icon(12U)==11U);
    CHECK(!battletanx::secondary_weapon_icon(13U));
    CHECK(!battletanx::secondary_weapon_icon(3U));
    CHECK(battletanx::level_one_hud_map_position(0.0F,0.0F).x==30);
    CHECK(battletanx::level_one_hud_map_position(0.0F,0.0F).y==30);
    CHECK(battletanx::level_one_hud_map_position(4375.0F,4375.0F).x==59);
    CHECK(battletanx::level_one_hud_map_position(-4375.0F,-4375.0F).x==0);

    auto fixture=hud_fixture();
    auto assets=battletanx::decode_hud_assets(fixture);
    CHECK(assets.images.size()==battletanx::retail_hud_image_count);
    CHECK(assets.images[0].width==32U && assets.images[0].height==32U);
    CHECK(assets.images[18].pixels[0]==0xFFFFFFFFU);

    const auto fonts=battletanx::decode_retail_fonts(font_fixture());
    CHECK(fonts.size()==1U);
    CHECK(fonts[0].height==2U && fonts[0].image_width==2U &&
          fonts[0].image_height==2U);
    CHECK(fonts[0].glyph_index['A']==0U && fonts[0].glyphs.size()==1U);
    CHECK(fonts[0].atlas.pixels[0]==0xFFFFFFFFU);
    CHECK(fonts[0].atlas.pixels[3]==0U);
    assets.fonts=fonts;

    std::vector<std::uint32_t> framebuffer(640U*480U,0x00101820U);
    battletanx::HudState state;
    state.health=50;
    state.max_health=100;
    state.primary_ammo=70;
    state.secondary_weapon=12U;
    state.secondary_ammo=15;
    state.score=1000;
    assets.level_map=battletanx::RgbaImage{60U,60U,
        std::vector<std::uint32_t>(60U*60U,0x80404040U)};
    state.markers.push_back({0.0F,0.0F,0.0F,0U,true});
    state.markers.push_back({100.0F,100.0F,0.0F,2U,false});
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    CHECK(std::count(framebuffer.begin(),framebuffer.end(),0x00101820U)<
          static_cast<std::ptrdiff_t>(framebuffer.size()));
    const auto health_pixel=framebuffer[32U*640U+246U];
    const auto empty_pixel=framebuffer[32U*640U+326U];
    CHECK(health_pixel!=empty_pixel);
    CHECK(framebuffer[318U*640U+50U]!=0x00101820U);
    CHECK(framebuffer[82U*640U+500U]!=0x00101820U);

    state.match_state=battletanx::MatchState::paused;
    const auto before=framebuffer;
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    CHECK(framebuffer!=before);

    state.match_state=battletanx::MatchState::title;
    const battletanx::RgbaImage briefing{2U,2U,
        std::vector<std::uint32_t>(4U,0xFFFFFFFFU)};
    state.briefing_image=&briefing;
    state.objective="A";
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    CHECK(framebuffer[36U*640U+318U]!=0x00101820U);
    state.match_state=battletanx::MatchState::countdown;
    state.frames_remaining=120;
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);

    state.match_state=battletanx::MatchState::result_banner;
    state.health=1;
    state.result="A";
    state.result_prompt={};
    std::fill(framebuffer.begin(),framebuffer.end(),0x00101820U);
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    const auto result_without_prompt=framebuffer;
    state.result_prompt="A";
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    CHECK(framebuffer!=result_without_prompt);

    battletanx::frontend::MenuView menu;
    menu.title="A";
    menu.rows.push_back({.label="A",.value="A",.selected=true,.enabled=true});
    std::fill(framebuffer.begin(),framebuffer.end(),0x00101820U);
    battletanx::draw_frontend_menu(framebuffer,640,480,assets,menu);
    CHECK(std::count(framebuffer.begin(),framebuffer.end(),0x00101820U)<
          static_cast<std::ptrdiff_t>(framebuffer.size()));

    bool rejected=false;
    try {
        fixture[3U]=22U;
        static_cast<void>(battletanx::decode_hud_assets(fixture));
    } catch (const std::runtime_error&) { rejected=true; }
    CHECK(rejected);

    if (failures!=0) return 1;
    std::cout << "hud tests passed\n";
    return 0;
}
