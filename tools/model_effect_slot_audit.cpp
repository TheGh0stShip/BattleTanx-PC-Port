// Optional diagnostic: model/effect slot references in a user supplied ROM.
// Prints only counts and hashes; never writes files.
//
//   model_effect_slot_audit <rom.z64>
//
// load_level (func_80081AD0, src/code/levels/load_level.c) fills the s16[182]
// slot table D_802D5A30 with -1 and marks wanted slots 0. load_level_data
// (src/code/assets/load_level_data.c) then replaces each wanted slot i with the
// set referenced at header +0x2DC + i (WorldLevel::set_base + i). The audit
// resolves every wanted slot to a set index per stream and checks that the set
// exists and has at least one LOD.

#include "battletanx/level.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace battletanx;

constexpr std::size_t kSlotCount=182U;

// Unconditional wanted slots (load_level.c, after the -1 fill).
constexpr std::array<std::uint8_t,73> kBaseSlots{
    1,2,3,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
    25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
    62,63,64,65,66,67,68,69,70,71,72,73,76,77,78,79,80,
    82,83,84,85,86,87,88,89,90,92,93,94,95,97,98};
// D_80135775 || D_80135776 -> slot 24; level 16 -> slot 179;
// func_800EC644() -> slots 180, 181.
constexpr std::uint8_t kOptionSlot=24U;
constexpr std::uint8_t kLevel16Slot=179U;
constexpr std::array<std::uint8_t,2> kTwoPlayerSlots{180,181};
constexpr int kLevel16=16;
// enable_color_models: base + player color (0..9 when not 0xFF).
constexpr std::array<std::uint8_t,7> kColorBases{129,119,99,109,169,139,149};
constexpr std::uint8_t kColorCount=10U;

struct SlotGroup {
    const char* name;
    std::vector<std::uint8_t> slots;
    // Conditional groups (two-player, per-player colour; the 0..9 colour range
    // is STRONG INFERENCE) are reported but never counted as failures.
    bool conditional=false;
};

std::vector<SlotGroup> slot_groups() {
    std::vector<SlotGroup> groups;
    groups.push_back({"base",{kBaseSlots.begin(),kBaseSlots.end()},false});
    groups.push_back({"option_flag",{kOptionSlot},false});
    groups.push_back({"level16",{kLevel16Slot},false});
    groups.push_back({"two_player",{kTwoPlayerSlots.begin(),kTwoPlayerSlots.end()},true});
    SlotGroup colors{"player_colors",{},true};
    for (const std::uint8_t base : kColorBases)
        for (std::uint8_t c=0U; c<kColorCount; ++c) colors.slots.push_back(static_cast<std::uint8_t>(base+c));
    groups.push_back(colors);
    return groups;
}

void append_be32(std::vector<std::uint8_t>& out,std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value>>24U));
    out.push_back(static_cast<std::uint8_t>(value>>16U));
    out.push_back(static_cast<std::uint8_t>(value>>8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: model_effect_slot_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        const std::vector<SlotGroup> groups=slot_groups();
        std::size_t unique=0U;
        {
            std::array<bool,kSlotCount> seen{};
            for (const SlotGroup& g : groups)
                for (const std::uint8_t s : g.slots)
                    if (s<kSlotCount && !seen[s]) { seen[s]=true; ++unique; }
        }
        std::cout << "slot_table_size " << kSlotCount << " referenced_slots " << unique << '\n';
        for (const SlotGroup& g : groups) std::cout << "group " << g.name << " slots " << g.slots.size() << '\n';

        int failures=0;
        std::size_t streams=0U;
        for (const LevelStreamDescriptor& d : level_stream_descriptors()) {
            const WorldLevel world=load_world_level(rom,d);
            ++streams;
            std::cout << "stream " << d.index << " set_base " << world.set_base << " sets " << world.shapes.size();
            std::vector<std::uint8_t> resolved;
            std::size_t stream_absent=0U,conditional_absent=0U;
            for (const SlotGroup& g : groups) {
                const bool applies=std::string_view(g.name)!="level16" || d.index==kLevel16;
                std::size_t absent=0U;
                for (const std::uint8_t s : g.slots) {
                    const std::uint64_t set=std::uint64_t{world.set_base}+s;
                    const bool present=s<kSlotCount && set<world.shapes.size()
                        && world.shapes[static_cast<std::size_t>(set)].lod_count!=0U;
                    if (!present) ++absent;
                    append_be32(resolved,present ? static_cast<std::uint32_t>(set) : 0xFFFFFFFFU);
                }
                std::cout << ' ' << g.name << (applies ? (g.conditional ? "(cond)" : "") : "(n/a)") << '='
                          << g.slots.size()-absent << '/' << g.slots.size();
                if (applies && !g.conditional) stream_absent+=absent;
                if (applies && g.conditional) conditional_absent+=absent;
            }
            std::cout << " absent " << stream_absent << " conditional_absent " << conditional_absent
                      << " resolved_sha256 " << hex_digest(sha256(std::span<const std::uint8_t>(resolved))) << '\n';
            if (stream_absent!=0U) failures=1;
        }
        std::cout << "streams " << streams << '\n' << (failures ? "FAIL\n" : "OK\n");
        return failures;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
