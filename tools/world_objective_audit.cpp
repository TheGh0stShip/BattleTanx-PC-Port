// Optional diagnostic: per campaign wave, lists the queen bases, the queens
// assign_group_object_points (func_800A4B2C) would create, and the type-25
// marker groups of the entity bank. Reads a user supplied ROM; prints only
// counts, numeric metadata and hashes; writes nothing.
//
//   world_objective_audit <rom.z64>
//
// Recovered identification (see the research notes for the evidence table):
//  * a base is a placed world item (any group except header +0x280) whose
//    type is 11..14; type-11 maps to D_801260A4[0] ... type-14 to [3];
//  * each accepted base becomes effect kind 0x14 (motion box +-0xC3) and, unless
//    game kind 6 and owner slot 0, a queen effect kind 0x15 on the same point
//    (spawn_effect_kind_15, func_800A6958: motion box +-0x14, corner offset
//    D_80073050 = 20.0, ROM 0x003050);
//  * an owner record accepts at most one base (+0x1AF capped at 1);
//  * the slot must be below func_8007AAB0 (here: the wave owner count);
//  * the base pass runs only for game kinds 0, 3, 4 and 6 (func_8007C2A0,
//    apply_mode_display_rows.c:16-25); other waves print "gated (kind N)".

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/level.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace battletanx;

constexpr std::size_t kQueenOffsetRom=0x003050U;  // D_80073050 (f32)
constexpr std::int32_t kBaseTypeFirst=11, kBaseTypeLast=14;
constexpr std::uint8_t kFlagCampaignOnly=0x10U, kFlagSkirmishOnly=0x40U, kFlagGated=0x02U;
constexpr std::int32_t kMarkerType=25;
constexpr std::size_t kMarkerGroups=4U;

bool mode_kept(std::uint8_t flags,bool campaign) noexcept {
    return !(((flags&kFlagCampaignOnly)!=0U && !campaign) || ((flags&kFlagSkirmishOnly)!=0U && campaign));
}

const LevelStreamDescriptor* find_stream(int index) noexcept {
    for (const LevelStreamDescriptor& d : level_stream_descriptors()) if (d.index==index) return &d;
    return nullptr;
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: world_objective_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        const BigEndianView view=rom.view();
        const float queen_offset=view.f32(kQueenOffsetRom);
        std::cout << "queen_corner_offset " << queen_offset << " queen_box 20 base_box 195\n";
        int failures=0;
        std::size_t total_bases=0U, total_queens=0U, total_markers=0U;
        for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
            const LevelStreamDescriptor* stream=find_stream(wave.world_stream);
            if (stream==nullptr) {
                std::cout << "wave " << int{wave.wave_index} << " stream_missing\n";
                failures=1;
                continue;
            }
            const std::vector<std::uint8_t> bytes=load_world_stream(rom,*stream);
            const WorldLevel world=parse_world_level(bytes);
            const bool campaign=wave.game_kind>=3 && wave.game_kind<=6;
            std::array<std::size_t,4> bases{}, bases_raw{};
            std::size_t gated=0U;
            for (std::size_t g=0U; g<world.groups.size(); ++g) {
                const WorldGroup& group=world.groups[g];
                const bool bank=world.bank_group && *world.bank_group==g;
                for (std::size_t i=0U; i<group.item_count; ++i) {
                    const std::size_t index=static_cast<std::size_t>(group.first_item)+i;
                    if (index>=world.items.size()) break;
                    const WorldItem& item=world.items[index];
                    if (item.type<kBaseTypeFirst || item.type>kBaseTypeLast) continue;
                    const auto slot=static_cast<std::size_t>(item.type-kBaseTypeFirst);
                    ++bases_raw[slot];
                    if (bank || !mode_kept(item.flags,campaign)) continue;
                    ++bases[slot];
                    if ((item.flags&kFlagGated)!=0U) ++gated;
                }
            }
            // STRONG INFERENCE: owner slot = D_801260A4[type-11]; kinds >= 3 (not 9)
            // are assumed to map by identity.
            const bool base_pass=wave.game_kind==0 || wave.game_kind==3 || wave.game_kind==4 || wave.game_kind==6;
            std::size_t queens=0U, accepted=0U;
            for (std::size_t slot=0U; base_pass && slot<bases.size(); ++slot) {
                if (bases[slot]==0U || static_cast<std::int32_t>(slot)>=wave.owner_count) continue;
                ++accepted;
                if (!(wave.game_kind==6 && slot==0U)) ++queens;
            }
            std::array<std::size_t,kMarkerGroups> markers{};
            std::size_t marker_overflow=0U;
            if (world.bank_group && *world.bank_group<world.groups.size()) {
                const WorldGroup& group=world.groups[*world.bank_group];
                for (std::size_t i=0U; i<group.item_count; ++i) {
                    const std::size_t index=static_cast<std::size_t>(group.first_item)+i;
                    if (index>=world.items.size()) break;
                    const WorldItem& item=world.items[index];
                    if (item.type!=kMarkerType || !mode_kept(item.flags,campaign)) continue;
                    const std::size_t g=item.parameter>>4U;
                    if (g<kMarkerGroups) ++markers[g]; else ++marker_overflow;
                }
            }
            total_bases+=accepted;
            total_queens+=queens;
            for (const std::size_t m : markers) total_markers+=m;
            std::cout << "wave " << int{wave.wave_index} << " id " << wave.mission_id << " kind " << wave.game_kind
                      << " owners " << wave.owner_count
                      << " bases_placed " << bases[0] << ',' << bases[1] << ',' << bases[2] << ',' << bases[3]
                      << " bases_all_groups " << bases_raw[0] << ',' << bases_raw[1] << ',' << bases_raw[2] << ',' << bases_raw[3]
                      << " bases_flag2 " << gated;
            if (base_pass) std::cout << " bases_accepted " << accepted << " queens " << queens;
            else std::cout << " bases_accepted gated (kind " << wave.game_kind << ") queens gated (kind " << wave.game_kind << ')';
            std::cout
                      << " markers " << markers[0] << ',' << markers[1] << ',' << markers[2] << ',' << markers[3]
                      << " marker_overflow " << marker_overflow
                      << " stream_sha256 " << hex_digest(sha256(std::span<const std::uint8_t>(bytes))) << '\n';
        }
        std::cout << "total_bases_accepted " << total_bases << '\n'
                  << "total_queens " << total_queens << '\n'
                  << "total_markers " << total_markers << '\n';
        std::cout << (failures ? "FAIL\n" : "OK\n");
        return failures;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
