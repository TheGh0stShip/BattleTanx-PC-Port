// Optional diagnostic: per campaign wave, lists the type-20/21 spawners
// (mode, shape, kind, interval, gate), the type-22 owned-object sub-kinds,
// the type-25 markers per group and the world-header team spawn table.
// Reads a user supplied ROM; prints only counts and numeric metadata; writes
// nothing.
//
//   spawner_audit <rom.z64>

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_spawn.hpp"
#include "battletanx/level.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/rom.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace battletanx;

constexpr std::uint8_t kFlagCampaignOnly=0x10U, kFlagSkirmishOnly=0x40U;

bool mode_kept(std::uint8_t flags,bool campaign) noexcept {
    return !(((flags&kFlagCampaignOnly)!=0U && !campaign) || ((flags&kFlagSkirmishOnly)!=0U && campaign));
}

const LevelStreamDescriptor* find_stream(int index) noexcept {
    for (const LevelStreamDescriptor& d : level_stream_descriptors()) if (d.index==index) return &d;
    return nullptr;
}

void print_spawner(const char* origin,const WorldItem& item,std::size_t index) {
    const SpawnerDefinition def=unpack_spawner(item.id,item.parameter,false);
    const SpawnerDefinition fast=unpack_spawner(item.id,item.parameter,true);
    std::cout << "  spawner " << origin << " item " << index << " owner " << int{def.owner}
              << " mode " << int{def.mode} << " shape " << int{def.shape}
              << " count " << int{def.count_field} << " interval " << def.interval_ticks
              << " interval_fast " << fast.interval_ticks << " param " << int{def.parameter}
              << " gate " << int{def.gate_bits} << '\n';
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: spawner_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        int failures=0;
        std::size_t total_spawners=0U, total_owned=0U, total_markers=0U;
        for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
            const LevelStreamDescriptor* stream=find_stream(wave.world_stream);
            if (stream==nullptr) {
                std::cout << "wave " << int{wave.wave_index} << " stream_missing\n";
                failures=1;
                continue;
            }
            const std::vector<std::uint8_t> bytes=load_world_stream(rom,*stream);
            const WorldLevel world=parse_world_level(bytes);
            const bool campaign=campaign_mode(wave.game_kind);
            std::cout << "wave " << int{wave.wave_index} << " id " << wave.mission_id << " kind " << wave.game_kind << '\n';
            std::size_t placed20=0U, placed21=0U, bank21=0U, owned_optional=0U;
            std::array<std::size_t,4> owned{}, markers{};
            for (std::size_t g=0U; g<world.groups.size(); ++g) {
                const WorldGroup& group=world.groups[g];
                const bool bank=world.bank_group && *world.bank_group==g;
                for (std::size_t i=0U; i<group.item_count; ++i) {
                    const std::size_t index=static_cast<std::size_t>(group.first_item)+i;
                    if (index>=world.items.size()) break;
                    const WorldItem& item=world.items[index];
                    if (!mode_kept(item.flags,campaign)) continue;
                    if (bank) {
                        if (item.type==21) { ++bank21; print_spawner("bank21",item,index); }
                        else if (item.type==22) {
                            ++owned[(item.id>>3U)&3U];
                            if ((item.id&1U)!=0U) ++owned_optional;
                        } else if (item.type==25) {
                            const auto group_index=static_cast<std::size_t>(item.parameter>>4U);
                            if (group_index<markers.size()) ++markers[group_index];
                        }
                        continue;
                    }
                    if (item.type==20) { ++placed20; print_spawner("placed20",item,index); }
                    else if (item.type==21) { ++placed21; print_spawner("placed21",item,index); }
                }
            }
            const auto table=parse_team_spawn_table(std::span<const std::uint8_t>(bytes));
            std::size_t table_nonzero=0U;
            if (table) {
                for (const auto& row : *table)
                    for (const TeamSpawnPoint& p : row)
                        if (p.x!=0.0F || p.z!=0.0F || p.heading!=0U) ++table_nonzero;
            }
            // Cross-check against the shared script runner in campaign context.
            SpawnContext context;
            context.game_mode=wave.game_kind;
            context.bank_group=world.bank_group;
            context.active_player_count=wave.owner_count;
            const LevelSpawnPlan plan=interpret_level_spawns(world,context);
            std::size_t plan_markers=0U;
            for (const auto& group : plan.markers) plan_markers+=group.size();
            const std::size_t spawners=placed20+placed21+bank21;
            std::size_t marker_sum=0U;
            for (const std::size_t m : markers) marker_sum+=m;
            total_spawners+=spawners;
            total_markers+=marker_sum;
            for (const std::size_t o : owned) total_owned+=o;
            std::cout << "  placed20 " << placed20 << " placed21 " << placed21 << " bank21 " << bank21
                      << " owned_sub " << owned[0] << ',' << owned[1] << ',' << owned[2] << ',' << owned[3]
                      << " owned_optional " << owned_optional
                      << " markers " << markers[0] << ',' << markers[1] << ',' << markers[2] << ',' << markers[3]
                      << " team_table " << (table ? "yes" : "no") << " team_table_nonzero " << table_nonzero
                      << " plan_spawners " << plan.spawners.size() << " plan_markers " << plan_markers
                      << " plan_skipped " << plan.skipped.size() << '\n';
            // interpret_level_spawns caps each group at 5 markers and counts the rest
            // as marker_group_full; the raw count here is uncapped.
            const std::size_t plan_full=plan.skipped_count(SkipReason::marker_group_full);
            if (plan_markers+plan_full!=marker_sum) failures=1;
        }
        std::cout << "total_spawners " << total_spawners << '\n'
                  << "total_owned_objects " << total_owned << '\n'
                  << "total_markers " << total_markers << '\n';
        std::cout << (failures ? "FAIL\n" : "OK\n");
        return failures;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
