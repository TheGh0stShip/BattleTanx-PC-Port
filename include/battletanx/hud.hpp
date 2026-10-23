#pragma once

#include "battletanx/mission_state.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/texture.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battletanx {

inline constexpr std::size_t retail_hud_image_count=23U;

struct FontGlyph {
    std::uint8_t x{};
    std::uint8_t y{};
    std::uint8_t width{};
};

struct RetailFont {
    std::uint32_t height{};
    std::uint32_t image_width{};
    std::uint32_t image_height{};
    std::array<std::uint8_t,128> glyph_index{};
    std::vector<FontGlyph> glyphs;
    RgbaImage atlas;
};

struct HudAssets {
    std::vector<RgbaImage> images;
    std::vector<RetailFont> fonts;
    std::optional<RgbaImage> level_map;
};

struct HudMarker {
    float x{},z{};
    float heading_degrees{};
    std::uint8_t team{};
    bool player{};
};

struct HudMapPosition { int x{},y{}; };

struct HudState {
    std::int32_t health{};
    std::int32_t max_health{};
    std::int32_t primary_ammo{};
    std::int32_t score{};
    std::int32_t frames_remaining{};
    MatchState match_state{MatchState::live};
    std::vector<HudMarker> markers;
};

[[nodiscard]] HudAssets decode_hud_assets(std::span<const std::uint8_t> pack);
[[nodiscard]] std::vector<RetailFont> decode_retail_fonts(
    std::span<const std::uint8_t> pack);
[[nodiscard]] HudAssets load_hud_assets(const Rom& rom);
[[nodiscard]] std::size_t filled_health_segments(
    std::int32_t health,std::int32_t max_health) noexcept;
[[nodiscard]] HudMapPosition level_one_hud_map_position(float x,float z) noexcept;
void draw_player_hud(std::span<std::uint32_t> pixels,int width,int height,
                     const HudAssets& assets,const HudState& state);

} // namespace battletanx
