// Optional diagnostic: which computer-player plays each campaign wave can
// reach. Reads the play script table (D_80137888, ROM 0x0C7888, 29 pointers to
// 16-byte {run, monitor, play, state} steps) from a user supplied ROM, then
// closes the push graph from the root plays of func_800EA2A4:
//  * owner context (kind 1): flag set -> kind 0/1/3/6: 15, 2: 25,
//    4/5/7/9: 2, other: 12; flag clear -> kind 2: 26, other: 10. Which owners
//    carry the flag is UNRESOLVED, so both branches are reported.
//  * member context (kind 2): unit class 0x42/0x46 -> 3; 0x41/0x43/0x45 -> 5;
//    flag 4 -> 1; status 1 -> 11; otherwise 5.
// A step pushes its play field when non-zero (ai_play_stack_update); the root
// script's first step names the play that is pushed.
// TEMPORARY POLICY: a script ends at the first step whose run/monitor pointer
// falls outside [kTextLow, kTextHigh), whose play is out of range, or whose
// state is outside -2..63. These bounds are heuristics, not retail values.
// Prints only counts, play numbers and a table hash; writes nothing.
//
//   campaign_play_audit <rom.z64>

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <set>
#include <span>
#include <vector>

namespace {

using namespace battletanx;

constexpr std::uint32_t kProgramBias=0x80070000U;
constexpr std::uint32_t kScriptTableVram=0x80137888U;
constexpr std::size_t kPlayCount=29U;
constexpr std::size_t kStepSize=16U;
constexpr std::size_t kMaxSteps=64U;
constexpr std::uint32_t kTextLow=0x80070000U, kTextHigh=0x80130000U;

struct PlayStep {
    std::uint32_t run{}, monitor{};
    std::int32_t play{}, state{};
};

using PlayGraph=std::array<std::vector<PlayStep>,kPlayCount>;

bool code_pointer(std::uint32_t value) noexcept {
    return value==0U || (value>=kTextLow && value<kTextHigh);
}

// Reads each script until a record stops looking like a step or the next
// script begins. Script 0 is a null pointer in retail.
PlayGraph read_graph(const BigEndianView& view,std::size_t rom_size,std::array<std::uint32_t,kPlayCount>& heads) {
    PlayGraph graph;
    std::set<std::uint32_t> starts;
    for (std::size_t i=0U; i<kPlayCount; ++i) {
        heads[i]=view.u32(kScriptTableVram-kProgramBias+4U*i);
        if (heads[i]!=0U) starts.insert(heads[i]);
    }
    for (std::size_t i=0U; i<kPlayCount; ++i) {
        if (heads[i]<kProgramBias) continue;
        std::uint32_t address=heads[i];
        for (std::size_t n=0U; n<kMaxSteps; ++n, address+=kStepSize) {
            if (n>0U && starts.count(address)!=0U) break;
            const std::size_t rom=address-kProgramBias;
            if (rom+kStepSize>rom_size) break;
            const PlayStep step{view.u32(rom),view.u32(rom+4U),view.s32(rom+8U),view.s32(rom+12U)};
            if (!code_pointer(step.run) || !code_pointer(step.monitor) || step.play<0
                || step.play>=static_cast<std::int32_t>(kPlayCount) || step.state<-2 || step.state>=64) break;
            graph[i].push_back(step);
        }
    }
    return graph;
}

std::set<std::uint32_t> closure(const PlayGraph& graph,std::initializer_list<std::uint32_t> roots) {
    std::set<std::uint32_t> seen;
    std::vector<std::uint32_t> pending(roots);
    while (!pending.empty()) {
        const std::uint32_t play=pending.back();
        pending.pop_back();
        if (play>=kPlayCount || graph[play].empty() || seen.count(play)!=0U) continue;
        seen.insert(play);
        for (const PlayStep& step : graph[play])
            if (step.play!=0) pending.push_back(static_cast<std::uint32_t>(step.play));
    }
    return seen;
}

std::uint32_t owner_root(std::int32_t kind,bool flag) noexcept {
    if (flag) {
        switch (kind) {
        case 0: case 1: case 3: case 6: return 15U;
        case 2: return 25U;
        case 4: case 5: case 7: case 9: return 2U;
        default: return 12U;
        }
    }
    return kind==2 ? 26U : 10U;
}

void print_set(const char* label,const std::set<std::uint32_t>& plays) {
    std::cout << ' ' << label << ' ' << plays.size() << " [";
    bool first=true;
    for (const std::uint32_t p : plays) {
        std::cout << (first ? "" : ",") << p;
        first=false;
    }
    std::cout << ']';
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: campaign_play_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        const BigEndianView view=rom.view();
        std::array<std::uint32_t,kPlayCount> heads{};
        const PlayGraph graph=read_graph(view,rom.bytes().size(),heads);
        int failures=0;
        std::size_t scripts=0U, steps=0U;
        for (std::size_t i=0U; i<kPlayCount; ++i) {
            if (graph[i].empty()) {
                std::cout << "play " << i << " script none\n";
                continue;
            }
            ++scripts;
            steps+=graph[i].size();
            // Step 0 is a header: {0, 0, own play, initial state}.
            const PlayStep& head=graph[i].front();
            if (head.play!=static_cast<std::int32_t>(i)) failures=1;
            std::set<std::uint32_t> pushes;
            for (std::size_t n=1U; n<graph[i].size(); ++n)
                if (graph[i][n].play!=0) pushes.insert(static_cast<std::uint32_t>(graph[i][n].play));
            std::cout << "play " << i << " steps " << graph[i].size() << " header_play " << head.play
                      << " initial_state " << head.state;
            print_set("pushes",pushes);
            std::cout << '\n';
        }
        const auto table=view.subspan(kScriptTableVram-kProgramBias,kPlayCount*4U);
        std::cout << "scripts " << scripts << " steps " << steps
                  << " table_sha256 " << hex_digest(sha256(table)) << '\n';
        const std::set<std::uint32_t> member=closure(graph,{3U,5U,1U,11U});
        std::cout << "member_roots [1,3,5,11]";
        print_set("member_reachable",member);
        std::cout << '\n';
        std::set<std::uint32_t> all;
        for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
            const std::uint32_t flagged=owner_root(wave.game_kind,true);
            const std::uint32_t plain=owner_root(wave.game_kind,false);
            const std::set<std::uint32_t> owner_flag=closure(graph,{flagged});
            const std::set<std::uint32_t> owner_plain=closure(graph,{plain});
            std::set<std::uint32_t> combined=member;
            combined.insert(owner_flag.begin(),owner_flag.end());
            combined.insert(owner_plain.begin(),owner_plain.end());
            all.insert(combined.begin(),combined.end());
            std::cout << "wave " << int{wave.wave_index} << " id " << wave.mission_id << " kind " << wave.game_kind
                      << " owner_root_flag " << flagged << " owner_root_plain " << plain;
            print_set("owner_flag",owner_flag);
            print_set("owner_plain",owner_plain);
            print_set("all",combined);
            std::cout << '\n';
        }
        print_set("campaign_reachable",all);
        std::cout << '\n' << (failures ? "FAIL\n" : "OK\n");
        return failures;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
