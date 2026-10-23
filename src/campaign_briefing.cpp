#include "battletanx/campaign_briefing.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace battletanx {
namespace {

constexpr std::size_t briefing_page_table_rom=0x0D268CU;
constexpr std::uint32_t data_vram_to_rom=0x80070000U;
constexpr std::size_t briefing_data_end_rom=0x0D2B44U;

// briefing_text_data.c, D_80141768. PI addresses have their 0xB0000000
// cartridge-window prefix removed before indexing the verified ROM.
constexpr std::array<CampaignBriefingImage,campaign_briefing_image_count> images{{
    {0x7D58F0U,0x7D7CD0U,228U,38U}, {0x7B7FB0U,0x7B9280U,113U,38U},
    {0x7BB850U,0x7BD750U,195U,38U}, {0x7BD750U,0x7BF860U,209U,38U},
    {0x7BF860U,0x7C3890U,155U,30U}, {0x7C3890U,0x7C74D0U,184U,81U},
    {0x7C74D0U,0x7C8E50U,158U,38U}, {0x7C8E50U,0x7CD1F0U,168U,30U},
    {0x7B69A0U,0x7B7FB0U,135U,38U}, {0x7CD1F0U,0x7D14B0U,202U,82U},
    {0x7D14B0U,0x7D58F0U,212U,80U}, {0x7D7CD0U,0x7DA500U,257U,38U},
    {0x7DA500U,0x7DB6F0U,107U,38U}, {0x7DB6F0U,0x7DD680U,199U,38U},
    {0x7DD680U,0x7E2040U,224U,82U}, {0x7B9280U,0x7BB850U,229U,40U},
    {0x7E2040U,0x7E3210U,109U,37U},
}};

constexpr std::array<std::string_view,9> objective_strings{{
    "DESTROY 5 TANKS", "CROSS THE TUNNEL", "DESTROY 15 TANKS",
    "CROSS THE BRIDGE", "RESCUE 1 QUEEN LORD", "RESCUE 2 QUEEN LORDS",
    "RESCUE 3 QUEEN LORDS", "CROSS THE HIGHWAY", "RESCUE MADISON",
}};
constexpr std::array<std::int8_t,17> objective_for_wave{{0,1,2,3,-1,4,4,5,-1,7,6,6,-1,3,6,-1,8}};

constexpr std::array<std::string_view,7> result_strings{{
    "GROUND ZERO CLEARED", "MADE IT!!!", "TIMES SQUARE CLEARED",
    "QUEEN LORD RESCUED", "2 QUEEN LORDS RESCUED",
    "3 QUEEN LORDS RESCUED", "TRIUMPH!",
}};
constexpr std::array<std::int8_t,17> result_for_wave{{0,1,2,1,-1,3,3,4,-1,1,5,5,-1,1,5,-1,6}};

template <std::size_t N>
std::optional<std::string_view> indexed_text(
    std::size_t mission_id,const std::array<std::int8_t,17>& map,
    const std::array<std::string_view,N>& strings) noexcept {
    if (mission_id>=map.size() || map[mission_id]<0) return std::nullopt;
    return strings[static_cast<std::size_t>(map[mission_id])];
}

} // namespace

std::span<const CampaignBriefingImage,campaign_briefing_image_count>
campaign_briefing_images() noexcept { return images; }

RgbaImage load_campaign_briefing_image(const Rom& rom,std::size_t index) {
    if (index>=images.size()) throw std::out_of_range("campaign briefing image index");
    const auto& descriptor=images[index];
    const std::size_t pixels=static_cast<std::size_t>(descriptor.width)*descriptor.height;
    const std::size_t required=512U+pixels;
    if (descriptor.rom_start>descriptor.rom_end || descriptor.rom_end>rom.bytes().size() ||
        descriptor.rom_end-descriptor.rom_start<required)
        throw std::runtime_error("campaign briefing image range is invalid");
    const auto bytes=rom.bytes().subspan(descriptor.rom_start,
                                         descriptor.rom_end-descriptor.rom_start);
    return decode_ci8_texture(bytes.subspan(512U,pixels),bytes.first(512U),
                              descriptor.width,descriptor.height);
}

CampaignBriefingPage load_campaign_briefing_page(const Rom& rom,std::size_t wave_index) {
    if (wave_index>=campaign_briefing_image_count)
        throw std::out_of_range("campaign briefing page index");
    const BigEndianView view=rom.view();
    const std::size_t record=briefing_page_table_rom+wave_index*8U;
    CampaignBriefingPage page;
    page.line_count=view.u32(record);
    const std::uint32_t pointer=view.u32(record+4U);
    if (page.line_count==0U) {
        if (pointer!=0U) throw std::runtime_error("empty campaign briefing page has text");
        return page;
    }
    if (page.line_count>32U || pointer<data_vram_to_rom)
        throw std::runtime_error("campaign briefing page record is invalid");
    const std::size_t start=pointer-data_vram_to_rom;
    if (start>=briefing_data_end_rom || briefing_data_end_rom>rom.bytes().size())
        throw std::runtime_error("campaign briefing text pointer is invalid");
    const auto bytes=rom.bytes();
    std::size_t end=start;
    while (end<briefing_data_end_rom && bytes[end]!=0U) ++end;
    if (end==briefing_data_end_rom)
        throw std::runtime_error("campaign briefing text is unterminated");
    page.text.assign(reinterpret_cast<const char*>(bytes.data()+start),end-start);
    const std::size_t lines=page.text.empty() ? 0U :
        1U+static_cast<std::size_t>(std::count(page.text.begin(),page.text.end(),'\n'));
    const std::size_t rendered_lines=!page.text.empty() && page.text.back()=='\n' ? lines-1U : lines;
    if (rendered_lines!=page.line_count)
        throw std::runtime_error("campaign briefing line count does not match text");
    return page;
}

std::optional<std::string_view> campaign_objective_text(std::size_t wave_index) noexcept {
    return indexed_text(wave_index,objective_for_wave,objective_strings);
}

std::optional<std::string_view> campaign_result_text(std::size_t wave_index) noexcept {
    return indexed_text(wave_index,result_for_wave,result_strings);
}

} // namespace battletanx
