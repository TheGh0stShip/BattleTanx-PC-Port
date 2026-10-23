// Optional diagnostic: checks the compiled campaign catalog against a user
// supplied ROM. Prints only counts, numeric metadata and hashes.
//
//   campaign_audit <rom.z64>

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/level.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace battletanx;

// func_800865E0 fills gLevelRomRanges (D_80125A68, stride 0x10) at boot with
// lui/addiu/sw triples; the table is zero in the ROM image.
constexpr std::size_t kLevelRangeInitRom=0x0165E0U;
constexpr std::uint32_t kLevelRangeVram=0x80125A68U;
constexpr std::uint32_t kPiBase=0xB0000000U;

std::map<std::uint32_t,std::uint32_t> decode_level_range_init(const BigEndianView& rom) {
    std::map<std::uint32_t,std::uint32_t> stores;
    std::array<std::uint32_t,32> regs{};
    for (std::size_t pc=kLevelRangeInitRom; pc<kLevelRangeInitRom+0x1000U; pc+=4U) {
        const std::uint32_t op=rom.u32(pc);
        const std::uint32_t code=op>>26U, rs=(op>>21U)&31U, rt=(op>>16U)&31U;
        const auto imm=static_cast<std::int16_t>(op&0xFFFFU);
        if (op==0x03E00008U) break; // jr $ra
        if (code==0x0FU) regs[rt]=(op&0xFFFFU)<<16U;
        else if (code==0x09U) regs[rt]=regs[rs]+static_cast<std::uint32_t>(static_cast<std::int32_t>(imm));
        else if (code==0x2BU) stores[regs[rs]+static_cast<std::uint32_t>(static_cast<std::int32_t>(imm))]=regs[rt];
    }
    return stores;
}

std::string hash_of(std::span<const std::uint8_t> bytes) { return hex_digest(sha256(bytes)); }

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: campaign_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        const BigEndianView view=rom.view();
        int failures=0;

        std::size_t word_mismatch=0U;
        const auto raw=campaign_wave_raw_words();
        for (std::size_t w=0U; w<raw.size(); ++w)
            for (std::size_t i=0U; i<campaign_wave_record_words; ++i)
                if (view.u32(campaign_wave_table_rom+w*campaign_wave_record_size+4U*i)!=raw[w][i]) ++word_mismatch;
        std::size_t gun_mismatch=0U;
        const auto guns=campaign_gun_tables();
        for (std::size_t t=0U; t<guns.size(); ++t)
            for (std::size_t i=0U; i<4U; ++i)
                if (view.s16(campaign_gun_table_rom+t*8U+2U*i)!=guns[t][i]) ++gun_mismatch;
        std::size_t briefing_mismatch=0U;
        const auto briefing=campaign_briefing_indices();
        for (std::size_t i=0U; i<briefing.size(); ++i)
            if (view.s32(campaign_briefing_index_rom+4U*i)!=briefing[i]) ++briefing_mismatch;
        const auto post=view.u32(campaign_wave_table_rom+campaign_wave_count*campaign_wave_record_size+4U);
        const bool post_ok=static_cast<std::uint8_t>(post)==campaign_wave_definitions()[16].saved_mission_id_after_clear;

        const auto stores=decode_level_range_init(view);
        std::size_t stream_ok=0U, nav_ok=0U, range_ok=0U;
        for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
            const LevelStreamDescriptor* stream=nullptr;
            for (const LevelStreamDescriptor& d : level_stream_descriptors()) if (d.index==wave.world_stream) stream=&d;
            if (stream==nullptr) continue;
            const auto slot=stores.find(kLevelRangeVram+0x10U*wave.world_stream);
            if (slot!=stores.end() && slot->second-kPiBase==stream->world.start) ++range_ok;
            const WorldLevel world=load_world_level(rom,*stream);
            if (!world.items.empty()) ++stream_ok;
            std::size_t triangles=0U;
            if (stream->navigation) {
                const NavigationLevel nav=load_navigation_level(rom,*stream);
                triangles=nav.triangles.size();
                if (triangles>0U) ++nav_ok;
            }
            std::cout << "wave " << int{wave.wave_index} << " id " << wave.mission_id << " kind " << wave.game_kind
                      << " items " << world.items.size() << " groups " << world.groups.size()
                      << " bank_group " << (world.bank_group ? static_cast<std::int64_t>(*world.bank_group) : std::int64_t{-1})
                      << " nav_triangles " << triangles << '\n';
        }
        const auto table=rom.bytes().subspan(campaign_wave_table_rom,campaign_wave_count*campaign_wave_record_size);
        std::cout << "wave_table_sha256 " << hash_of(table) << '\n'
                  << "gun_table_sha256 " << hash_of(rom.bytes().subspan(campaign_gun_table_rom,campaign_gun_table_count*8U)) << '\n'
                  << "word_mismatches " << word_mismatch << '\n'
                  << "gun_mismatches " << gun_mismatch << '\n'
                  << "briefing_mismatches " << briefing_mismatch << '\n'
                  << "post_table_id_ok " << post_ok << '\n'
                  << "level_range_stores " << stores.size() << '\n'
                  << "world_range_matches " << range_ok << '/' << campaign_wave_count << '\n'
                  << "world_streams_decoded " << stream_ok << '/' << campaign_wave_count << '\n'
                  << "navigation_decoded " << nav_ok << '/' << campaign_wave_count << '\n';
        const CampaignCatalogAudit audit=audit_campaign_catalog();
        std::cout << "catalog_issues " << audit.issues.size() << '\n';
        if (word_mismatch || gun_mismatch || briefing_mismatch || !post_ok || !audit.ok()
            || range_ok!=campaign_wave_count || stream_ok!=campaign_wave_count || nav_ok!=campaign_wave_count) failures=1;
        std::cout << (failures ? "FAIL\n" : "OK\n");
        return failures;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
