#pragma once

#include "battletanx/rom.hpp"
#include "battletanx/texture.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace battletanx {

inline constexpr std::size_t campaign_briefing_image_count=17U;

struct CampaignBriefingImage {
    std::uint32_t rom_start{};
    std::uint32_t rom_end{};
    std::uint16_t width{};
    std::uint16_t height{};
};

[[nodiscard]] std::span<const CampaignBriefingImage,campaign_briefing_image_count>
campaign_briefing_images() noexcept;
[[nodiscard]] RgbaImage load_campaign_briefing_image(const Rom& rom,std::size_t index);
[[nodiscard]] std::optional<std::string_view> campaign_objective_text(std::size_t wave_index) noexcept;
[[nodiscard]] std::optional<std::string_view> campaign_result_text(std::size_t wave_index) noexcept;

} // namespace battletanx
