// Optional diagnostic: campaign wave -> music track mapping in a user supplied
// ROM. Prints only numeric ids, counts and a hash; never writes files.
//
//   music_mapping_audit <rom.z64>
//
// select_screen_transition (func_8007D2A0, src/code/frontend/select_screen_transition.c)
// is a 20-entry switch compiled to jump table jtbl_80071378. Each case is
// "j func_8007D33C; addiu a0,zero,TRACK". Two targets are shared:
//   0x8007D338  addu a0,zero,zero (default and id 3)        -> track 0
//   0x8007D33C  a0 still holds the -1 preset (ids 18, 19)    -> stop
// Track -1 calls func_80079BD4 (stop); any other value func_80079BB4(track).
// The decoded mapping is compared with music_selection::music_track_for_level,
// CampaignWaveDefinition::music_transition and an expected table embedded here.

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/music_selection.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>

namespace {

using namespace battletanx;

constexpr std::uint32_t kProgramBias=0x80070000U;   // ROM offset = VRAM - bias
constexpr std::uint32_t kFunctionVram=0x8007D2A0U;
constexpr std::uint32_t kFunctionEndVram=0x8007D384U;
constexpr std::uint32_t kJumpTableVram=0x80071378U;
constexpr std::uint32_t kCaseCount=20U;            // sltiu v0,v1,0x14
constexpr std::uint32_t kZeroTargetVram=0x8007D338U;
constexpr std::uint32_t kJoinVram=0x8007D33CU;
constexpr std::uint32_t kPresetVram=0x8007D2B4U;   // delay slot: addiu a0,zero,-1
constexpr std::uint32_t kAddiuA0Zero=0x24040000U;  // addiu a0,zero,imm (upper half)
constexpr std::uint32_t kAddiuMask=0xFFFF0000U;
constexpr std::uint32_t kJumpOpcode=2U;
constexpr std::int32_t kStop=-1;
constexpr std::int32_t kUndecoded=-2;
constexpr std::int32_t kOutOfRangeProbe=20;

// Expected decode (CONFIRMED against the decomp source and the ROM, see
// evidence.md); -1 = stop.
constexpr std::array<std::int32_t,kCaseCount> kExpected{
    -1,7,2,0,17,6,3,1,18,6,16,4,5,6,8,6,10,6,-1,-1};

std::size_t rom_of(std::uint32_t vram) { return static_cast<std::size_t>(vram-kProgramBias); }

std::int32_t imm_a0(const BigEndianView& rom,std::uint32_t vram) {
    const std::uint32_t op=rom.u32(rom_of(vram));
    if ((op&kAddiuMask)!=kAddiuA0Zero) return kUndecoded;
    return static_cast<std::int32_t>(static_cast<std::int16_t>(op&0xFFFFU));
}

std::int32_t decode_target(const BigEndianView& rom,std::uint32_t target) {
    if (target==kZeroTargetVram) return 0;
    if (target==kJoinVram) return imm_a0(rom,kPresetVram);
    if (target<kFunctionVram || target>=kFunctionEndVram) return kUndecoded;
    const std::uint32_t jump=rom.u32(rom_of(target));
    if ((jump>>26U)!=kJumpOpcode || (((target&0xF0000000U)|((jump&0x03FFFFFFU)<<2U))!=kJoinVram)) return kUndecoded;
    return imm_a0(rom,target+4U);
}

std::int32_t from_header(std::int32_t level) {
    const std::optional<std::uint8_t> track=music_selection::music_track_for_level(level);
    return track ? std::int32_t{*track} : kStop;
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: music_mapping_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        const BigEndianView view=rom.view();
        std::array<std::int32_t,kCaseCount> decoded{};
        std::size_t undecoded=0U, expected_mismatch=0U, header_mismatch=0U, out_of_range_tracks=0U;
        for (std::uint32_t id=0U; id<kCaseCount; ++id) {
            const std::uint32_t target=view.u32(rom_of(kJumpTableVram)+4U*id);
            decoded[id]=decode_target(view,target);
            if (decoded[id]==kUndecoded) ++undecoded;
            if (decoded[id]!=kExpected[id]) ++expected_mismatch;
            const std::int32_t header=from_header(static_cast<std::int32_t>(id));
            if (decoded[id]!=header) ++header_mismatch;
            if (decoded[id]>=music_selection::music_track_count) ++out_of_range_tracks;
            std::cout << "level " << id << " track " << decoded[id] << " header " << header << '\n';
        }
        // ids >= 20 skip the table (beqz to the zero target).
        const std::int32_t default_header=from_header(kOutOfRangeProbe);
        if (default_header!=0) ++header_mismatch;
        std::cout << "level_default track 0 header " << default_header << '\n';

        std::size_t wave_mismatch=0U;
        for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
            const std::int32_t id=wave.mission_id;
            const std::int32_t rom_track=id>=0 && static_cast<std::uint32_t>(id)<kCaseCount
                ? decoded[static_cast<std::size_t>(id)] : 0;
            const std::int32_t catalog=wave.music_transition;
            const std::int32_t header=from_header(id);
            if (rom_track!=catalog || rom_track!=header) ++wave_mismatch;
            std::cout << "wave " << int{wave.wave_index} << " id " << id << " track " << rom_track
                      << " catalog " << catalog << " header " << header << '\n';
        }
        const auto table=view.subspan(rom_of(kJumpTableVram),kCaseCount*4U);
        const auto code=view.subspan(rom_of(kFunctionVram),kFunctionEndVram-kFunctionVram);
        std::cout << "jump_table_sha256 " << hex_digest(sha256(table)) << '\n'
                  << "function_sha256 " << hex_digest(sha256(code)) << '\n'
                  << "undecoded " << undecoded << '\n'
                  << "expected_mismatches " << expected_mismatch << '\n'
                  << "header_mismatches " << header_mismatch << '\n'
                  << "tracks_beyond_count " << out_of_range_tracks << '\n'
                  << "wave_mismatches " << wave_mismatch << '\n';
        const bool ok=undecoded==0U && expected_mismatch==0U && header_mismatch==0U
            && out_of_range_tracks==0U && wave_mismatch==0U;
        std::cout << (ok ? "OK\n" : "FAIL\n");
        return ok ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
