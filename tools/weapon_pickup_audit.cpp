// Optional diagnostic: weapon and pickup usage in a user supplied ROM.
// Prints only counts, numeric ids and hashes; never writes files.
//
//   weapon_pickup_audit <rom.z64>
//
// Reports
//  - pickup kinds per world stream for skirmish (mode 0) and campaign (mode 3)
//    filtering, through interpret_level_spawns (bank group, type 10);
//  - pickup kinds per campaign wave (campaign filtering of the wave's stream);
//  - weapon ids reachable through pickups (pickup_weapon_grant, the event 22
//    random grant) and unit loadouts (unit_kind_definition);
//  - D_80125EC0 / D_80125ED4 in the ROM against weapon_catalog.hpp and
//    runtime_constants.hpp.

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/level.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/runtime_constants.hpp"
#include "battletanx/sha256.hpp"
#include "battletanx/unit_kinds.hpp"
#include "battletanx/weapon_catalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace battletanx;

constexpr std::size_t kCooldownRom=0xB5EC0U;   // D_80125EC0
constexpr std::size_t kRepeatRom=0xB5ED4U;     // D_80125ED4
constexpr std::size_t kTableBytes=weapon_id_count;
constexpr std::int32_t kSkirmishMode=0;
constexpr std::int32_t kCampaignMode=3;
constexpr std::uint8_t kUnitKinds=3U;
constexpr std::uint8_t kPickupKindLimit=32U;   // pickup parameters are u8; report < 32

using KindCounts=std::map<std::uint32_t,std::size_t>;

struct StreamPickups {
    KindCounts kinds;
    std::size_t random_spots{};
    std::size_t filtered{};
};

StreamPickups collect(const WorldLevel& world,std::int32_t mode) {
    SpawnContext context;
    context.game_mode=mode;
    context.bank_group=world.bank_group;
    const LevelSpawnPlan plan=interpret_level_spawns(world,context);
    StreamPickups out;
    for (const PickupPlacement& p : plan.pickups) ++out.kinds[p.kind];
    out.random_spots=plan.random_spots.size();
    out.filtered=plan.skipped_count(SkipReason::pickup_mode_filtered);
    return out;
}

void print_kinds(const KindCounts& kinds) {
    std::size_t total=0U;
    for (const auto& [kind,count] : kinds) total+=count;
    std::cout << " total " << total << " kinds";
    if (kinds.empty()) std::cout << " -";
    for (const auto& [kind,count] : kinds) std::cout << ' ' << kind << ':' << count;
}

template <std::size_t N>
std::size_t compare_table(const BigEndianView& view,std::size_t rom,const std::array<std::uint8_t,N>& table,
                          std::size_t& compared) {
    std::size_t mismatches=0U;
    compared=N<kTableBytes ? N : kTableBytes;
    for (std::size_t i=0U; i<compared; ++i) if (view.u8(rom+i)!=table[i]) ++mismatches;
    return mismatches;
}

} // namespace

int main(int argc,char** argv) {
    if (argc!=2) {
        std::cerr << "usage: weapon_pickup_audit <rom.z64>\n";
        return 2;
    }
    try {
        const Rom rom=Rom::load_verified(argv[1]);
        const BigEndianView view=rom.view();
        int failures=0;

        // ---- pickups per stream ----
        std::map<int,StreamPickups> campaign_by_stream;
        std::set<std::uint32_t> placed_kinds;
        for (const LevelStreamDescriptor& d : level_stream_descriptors()) {
            const WorldLevel world=load_world_level(rom,d);
            const StreamPickups skirmish=collect(world,kSkirmishMode);
            const StreamPickups campaign=collect(world,kCampaignMode);
            std::int64_t header_total=0;
            for (const std::int32_t c : world.pickup_spawn_counts) header_total+=c;
            std::cout << "stream " << d.index << " bank_group "
                      << (world.bank_group ? static_cast<std::int64_t>(*world.bank_group) : std::int64_t{-1})
                      << " header_spawn_total " << header_total << '\n';
            std::cout << "  skirmish";
            print_kinds(skirmish.kinds);
            std::cout << " random_spots " << skirmish.random_spots << " filtered " << skirmish.filtered << '\n';
            std::cout << "  campaign";
            print_kinds(campaign.kinds);
            std::cout << " random_spots " << campaign.random_spots << " filtered " << campaign.filtered << '\n';
            for (const auto& [kind,count] : skirmish.kinds) placed_kinds.insert(kind);
            for (const auto& [kind,count] : campaign.kinds) placed_kinds.insert(kind);
            for (std::size_t k=0U; k<world.pickup_spawn_counts.size(); ++k)
                if (world.pickup_spawn_counts[k]>0) placed_kinds.insert(static_cast<std::uint32_t>(k));
            campaign_by_stream[d.index]=campaign;
        }

        // ---- pickups per campaign wave (campaign filtering) ----
        std::size_t waves_resolved=0U;
        for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
            const auto found=campaign_by_stream.find(static_cast<int>(wave.world_stream));
            std::cout << "wave " << int{wave.wave_index} << " id " << wave.mission_id << " stream " << int{wave.world_stream};
            if (found==campaign_by_stream.end()) {
                std::cout << " stream_missing\n";
                continue;
            }
            ++waves_resolved;
            print_kinds(found->second.kinds);
            std::cout << '\n';
        }

        // ---- reachable weapon ids ----
        std::set<std::uint32_t> pickup_weapons, random_weapons, loadout_weapons;
        for (const std::uint32_t kind : placed_kinds)
            if (kind<kPickupKindLimit)
                if (const auto grant=pickup_weapon_grant(static_cast<std::uint8_t>(kind))) pickup_weapons.insert(grant->weapon);
        for (std::uint32_t r=0U; r<32U; ++r)
            if (const auto grant=pickup_weapon_grant(random_grant_pickup_kind(r))) random_weapons.insert(grant->weapon);
        for (std::uint8_t k=0U; k<kUnitKinds; ++k)
            if (const auto def=unit_kind_definition(k)) loadout_weapons.insert(def->primary_weapon);
        const auto print_set=[](const char* label,const std::set<std::uint32_t>& ids) {
            std::cout << label << ' ' << ids.size();
            for (const std::uint32_t id : ids) std::cout << ' ' << id;
            std::cout << '\n';
        };
        print_set("placed_pickup_kinds",placed_kinds);
        print_set("weapons_via_placed_pickups",pickup_weapons);
        print_set("weapons_via_random_grant",random_weapons);
        print_set("weapons_via_loadout",loadout_weapons);
        std::set<std::uint32_t> all=pickup_weapons;
        all.insert(random_weapons.begin(),random_weapons.end());
        all.insert(loadout_weapons.begin(),loadout_weapons.end());
        print_set("weapons_reachable",all);
        std::set<std::uint32_t> unreachable;
        for (std::uint32_t id=0U; id<weapon_id_count; ++id) if (!all.count(id)) unreachable.insert(id);
        print_set("weapons_unreachable",unreachable);

        // ---- cooldown / repeat tables ----
        const auto cooldown_bytes=view.subspan(kCooldownRom,kTableBytes);
        const auto repeat_bytes=view.subspan(kRepeatRom,kTableBytes);
        std::size_t n_catalog_cd=0U, n_catalog_rp=0U, n_runtime_cd=0U, n_runtime_rp=0U;
        const std::size_t catalog_cd=compare_table(view,kCooldownRom,weapon_cooldown_table,n_catalog_cd);
        const std::size_t catalog_rp=compare_table(view,kRepeatRom,weapon_repeat_table,n_catalog_rp);
        const std::size_t runtime_cd=compare_table(view,kCooldownRom,retail::weapon_cooldown,n_runtime_cd);
        const std::size_t runtime_rp=compare_table(view,kRepeatRom,retail::weapon_repeats,n_runtime_rp);
        std::size_t definition_mismatch=0U;
        for (std::uint8_t id=0U; id<weapon_id_count; ++id) {
            const auto def=weapon_definition(id);
            if (!def) { ++definition_mismatch; continue; }
            if (def->cooldown!=view.u8(kCooldownRom+id)) ++definition_mismatch;
            if (def->auto_fire!=(view.u8(kRepeatRom+id)!=0U)) ++definition_mismatch;
        }
        std::cout << "cooldown_rom_sha256 " << hex_digest(sha256(cooldown_bytes)) << '\n'
                  << "repeat_rom_sha256 " << hex_digest(sha256(repeat_bytes)) << '\n'
                  << "catalog_cooldown_mismatches " << catalog_cd << '/' << n_catalog_cd << '\n'
                  << "catalog_repeat_mismatches " << catalog_rp << '/' << n_catalog_rp << '\n'
                  << "runtime_cooldown_mismatches " << runtime_cd << '/' << n_runtime_cd
                  << " (header length " << retail::weapon_cooldown.size() << ")\n"
                  << "runtime_repeat_mismatches " << runtime_rp << '/' << n_runtime_rp
                  << " (header length " << retail::weapon_repeats.size() << ")\n"
                  << "definition_mismatches " << definition_mismatch << '\n'
                  << "waves_resolved " << waves_resolved << '/' << campaign_wave_count << '\n';
        if (catalog_cd || catalog_rp || runtime_cd || runtime_rp || definition_mismatch
            || waves_resolved!=campaign_wave_count) failures=1;
        std::cout << (failures ? "FAIL\n" : "OK\n");
        return failures;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
