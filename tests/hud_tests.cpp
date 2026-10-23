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
} // namespace

int main() {
    CHECK(battletanx::filled_health_segments(100,100)==16U);
    CHECK(battletanx::filled_health_segments(50,100)==8U);
    CHECK(battletanx::filled_health_segments(1,100)==0U);
    CHECK(battletanx::filled_health_segments(0,100)==0U);
    CHECK(battletanx::filled_health_segments(100,0)==0U);

    auto fixture=hud_fixture();
    const auto assets=battletanx::decode_hud_assets(fixture);
    CHECK(assets.images.size()==battletanx::retail_hud_image_count);
    CHECK(assets.images[0].width==32U && assets.images[0].height==32U);
    CHECK(assets.images[18].pixels[0]==0xFFFFFFFFU);

    std::vector<std::uint32_t> framebuffer(640U*480U,0x00101820U);
    battletanx::HudState state;
    state.health=50;
    state.max_health=100;
    state.primary_ammo=70;
    state.score=1000;
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    CHECK(std::count(framebuffer.begin(),framebuffer.end(),0x00101820U)<
          static_cast<std::ptrdiff_t>(framebuffer.size()));
    const auto health_pixel=framebuffer[32U*640U+246U];
    const auto empty_pixel=framebuffer[32U*640U+326U];
    CHECK(health_pixel!=empty_pixel);

    state.match_state=battletanx::MatchState::paused;
    const auto before=framebuffer;
    battletanx::draw_player_hud(framebuffer,640,480,assets,state);
    CHECK(framebuffer!=before);

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
