#pragma once

#include "battletanx/mission_state.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/texture.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {

inline constexpr std::size_t retail_hud_image_count=23U;

struct HudAssets { std::vector<RgbaImage> images; };

struct HudState {
    std::int32_t health{};
    std::int32_t max_health{};
    std::int32_t primary_ammo{};
    std::int32_t score{};
    MatchState match_state{MatchState::live};
};

[[nodiscard]] HudAssets decode_hud_assets(std::span<const std::uint8_t> pack);
[[nodiscard]] HudAssets load_hud_assets(const Rom& rom);
[[nodiscard]] std::size_t filled_health_segments(
    std::int32_t health,std::int32_t max_health) noexcept;
void draw_player_hud(std::span<std::uint32_t> pixels,int width,int height,
                     const HudAssets& assets,const HudState& state);

} // namespace battletanx
