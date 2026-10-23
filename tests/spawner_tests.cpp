#include "battletanx/level_spawns.hpp"
#include "battletanx/spawner_runtime.hpp"
#include "runtime_test_support.hpp"

#include <limits>

using namespace battletanx;

namespace {

WorldItem item(std::int32_t type,std::uint8_t flags,std::uint8_t parameter,std::uint16_t id,
               float x=0.0F,float z=0.0F) {
    WorldItem result;
    result.type=type;
    result.flags=flags;
    result.parameter=parameter;
    result.id=id;
    result.x=x;
    result.z=z;
    return result;
}

// Group 0 is the bank, group 1 holds placed objects.
WorldLevel level_with(std::vector<WorldItem> bank,std::vector<WorldItem> placed={}) {
    WorldLevel level;
    WorldGroup g0;
    g0.first_item=0U;
    g0.item_count=static_cast<std::uint16_t>(bank.size());
    WorldGroup g1;
    g1.first_item=g0.item_count;
    g1.item_count=static_cast<std::uint16_t>(placed.size());
    level.groups={g0,g1};
    level.items=std::move(bank);
    level.items.insert(level.items.end(),placed.begin(),placed.end());
    return level;
}

SpawnContext context(std::int32_t mode) {
    SpawnContext ctx;
    ctx.game_mode=mode;
    ctx.view_count=1;
    ctx.bank_group=0U;
    ctx.active_player_count=4;
    return ctx;
}

SpawnerState ready_spawner(std::uint16_t id) {
    return make_spawner_state(unpack_spawner(id,0U,false));
}

SpawnerOwnerView owner_with_budget(std::int32_t budget) {
    SpawnerOwnerView owner;
    owner.budget=budget;
    owner.gate_open=true;
    return owner;
}

const SpawnAreaClear always_clear=[](const SpawnAreaQuery&) { return true; };

// First tick at which the 1-in-16 roll passes, computed with the raw LCG.
std::int32_t expected_first_roll(std::uint32_t seed,std::int32_t first_tick) {
    for (std::int32_t tick=first_tick;; ++tick) {
        seed=seed*0x10DCDU+1U;
        const std::uint32_t value=(seed<<16U)|(seed>>16U);
        if ((value&0xFU)==0U) return tick;
    }
}

} // namespace

RUNTIME_TEST(spawner_id_unpacking) {
    // owner 2, mode 3, count 4, gate bits 1
    const std::uint16_t id=static_cast<std::uint16_t>(2U|(3U<<2U)|(4U<<4U)|(1U<<12U));
    const SpawnerDefinition slow=unpack_spawner(id,0x45U,false);
    REQUIRE(slow.owner==2U);
    REQUIRE(slow.mode==3U);
    REQUIRE(slow.shape==2U);
    REQUIRE(slow.count_field==4U);
    REQUIRE(slow.interval_ticks==180);
    REQUIRE(slow.gate_bits==1U);
    REQUIRE(slow.parameter==0x45U);
    REQUIRE(unpack_spawner(id,0U,true).interval_ticks==120);
    REQUIRE(unpack_spawner(1U<<2U,0U,false).shape==1U);
    REQUIRE(unpack_spawner(0U,0U,false).shape==0U);
    REQUIRE(unpack_spawner(0x0FF0U,0U,false).interval_ticks==45*255);
    return true;
}

RUNTIME_TEST(spawner_interval_strict_compare) {
    SpawnerState spawner=ready_spawner(static_cast<std::uint16_t>(2U<<4U)); // 90 ticks
    spawner.last_tick=100;
    SpawnerOwnerView owner=owner_with_budget(3);
    RetailRandom rng(1U);
    const std::uint32_t before=rng.seed();
    REQUIRE(update_spawner(spawner,std::nullopt,owner,190,rng,always_clear).status==
            SpawnerUpdateStatus::interval_pending);
    REQUIRE(rng.seed()==before);  // no roll consumed while waiting
    const auto status=update_spawner(spawner,std::nullopt,owner,191,rng,always_clear).status;
    REQUIRE(status!=SpawnerUpdateStatus::interval_pending);
    return true;
}

RUNTIME_TEST(spawner_random_gate_first_tick_deterministic) {
    constexpr std::uint32_t seed=0x1234U;
    SpawnerState spawner=ready_spawner(0U);
    SpawnerOwnerView owner=owner_with_budget(5);
    RetailRandom rng(seed);
    std::int32_t spawned_at=-1;
    for (std::int32_t tick=1; tick<2000 && spawned_at<0; ++tick) {
        const auto result=update_spawner(spawner,std::nullopt,owner,tick,rng,always_clear);
        if (result.status==SpawnerUpdateStatus::spawned) {
            spawned_at=tick;
            REQUIRE(result.request.has_value());
            REQUIRE(result.request->squad_slot==0U);
            REQUIRE(result.request->tick==tick);
        } else {
            REQUIRE(result.status==SpawnerUpdateStatus::roll_failed);
        }
    }
    REQUIRE(spawned_at==expected_first_roll(seed,1));
    REQUIRE(spawner.last_tick==spawned_at);
    REQUIRE(owner.budget==4);
    return true;
}

RUNTIME_TEST(spawner_budget_exhausted) {
    SpawnerState spawner=ready_spawner(0U);
    SpawnerOwnerView owner=owner_with_budget(0);
    RetailRandom rng(0x1234U);
    for (std::int32_t tick=1; tick<500; ++tick) {
        const auto result=update_spawner(spawner,std::nullopt,owner,tick,rng,always_clear);
        REQUIRE(result.status!=SpawnerUpdateStatus::spawned);
        REQUIRE(!result.request.has_value());
    }
    return true;
}

RUNTIME_TEST(spawner_blocked_area_and_full_squad) {
    SpawnerState spawner=ready_spawner(static_cast<std::uint16_t>(1U<<2U)); // shape 1
    SpawnerOwnerView owner=owner_with_budget(2);
    RetailRandom rng(0x1234U);
    SpawnAreaQuery seen{};
    const SpawnAreaClear blocked=[&seen](const SpawnAreaQuery& q) { seen=q; return false; };
    bool saw_block=false;
    for (std::int32_t tick=1; tick<500; ++tick) {
        const auto result=update_spawner(spawner,std::nullopt,owner,tick,rng,blocked);
        REQUIRE(result.status!=SpawnerUpdateStatus::spawned);
        saw_block=saw_block || result.status==SpawnerUpdateStatus::area_blocked;
    }
    REQUIRE(saw_block);
    REQUIRE(seen.shape==1U);
    REQUIRE(seen.angle==0x200U);
    REQUIRE(owner.budget==2);
    owner.squad_slot_alive.fill(true);
    bool saw_full=false;
    for (std::int32_t tick=1; tick<500; ++tick) {
        const auto result=update_spawner(spawner,std::nullopt,owner,tick,rng,always_clear);
        REQUIRE(result.status!=SpawnerUpdateStatus::spawned);
        saw_full=saw_full || result.status==SpawnerUpdateStatus::no_free_slot;
    }
    REQUIRE(saw_full);
    return true;
}

RUNTIME_TEST(spawner_owner_gate_and_linked_end) {
    SpawnerState gated=make_spawner_state(unpack_spawner(0U,0x45U,false));
    SpawnerOwnerView owner=owner_with_budget(1);
    owner.gate_open=false;
    RetailRandom rng(7U);
    REQUIRE(update_spawner(gated,std::nullopt,owner,100,rng,always_clear).status==
            SpawnerUpdateStatus::gated);
    SpawnerDefinition def=unpack_spawner(0U,0U,false);
    def.linked=true;
    SpawnerState linked=make_spawner_state(def,9);
    REQUIRE(update_spawner(linked,LinkedEffectView{9,true},owner,100,rng,always_clear).status==
            SpawnerUpdateStatus::finished_destroyed);
    SpawnerState replaced=make_spawner_state(def,9);
    REQUIRE(update_spawner(replaced,LinkedEffectView{10,false},owner,100,rng,always_clear).status==
            SpawnerUpdateStatus::finished);
    return true;
}

RUNTIME_TEST(marker_grouping) {
    std::vector<WorldItem> bank;
    bank.push_back(item(25,0U,0x0AU,0x1046U));         // group 0, low 10, param 0x46, high 1
    for (int i=0; i<6; ++i) bank.push_back(item(25,0U,0x13U,70U)); // group 1, one too many
    bank.push_back(item(25,0U,0x50U,0U));              // group 5: out of range
    const LevelSpawnPlan plan=interpret_level_spawns(level_with(bank),context(0));
    REQUIRE(plan.markers[0].size()==1U);
    REQUIRE(plan.markers[0][0].low==10U);
    REQUIRE(plan.markers[0][0].param==0x46U);
    REQUIRE(plan.markers[0][0].high==1U);
    REQUIRE(plan.markers[1].size()==5U);
    REQUIRE(plan.markers[1][0].low==3U);
    REQUIRE(plan.skipped_count(SkipReason::marker_group_full)==1U);
    REQUIRE(plan.skipped_count(SkipReason::marker_group_out_of_range)==1U);
    return true;
}

RUNTIME_TEST(campaign_mode_filters_flagged_items) {
    const std::vector<WorldItem> bank{
        item(10,0x10U,0U,0U),   // campaign pickup
        item(10,0x00U,5U,0U),   // skirmish pickup
        item(10,0x00U,22U,0U),  // skirmish random spot
        item(22,0x40U,0U,0U),   // skirmish-only placement
        item(22,0x10U,0U,0x0100U)};
    const LevelSpawnPlan campaign=interpret_level_spawns(level_with(bank),context(5));
    REQUIRE(campaign.pickups.size()==1U);
    REQUIRE(campaign.pickups[0].kind==0U);
    REQUIRE(campaign.random_spots.empty());
    REQUIRE(campaign.units.size()==1U);
    REQUIRE(!campaign.units[0].respawn_flag);
    REQUIRE(!campaign.random_distribution_allowed);
    REQUIRE(campaign.skipped_count(SkipReason::pickup_mode_filtered)==2U);
    REQUIRE(campaign.skipped_count(SkipReason::mode_flag_filtered)==1U);
    const LevelSpawnPlan skirmish=interpret_level_spawns(level_with(bank),context(7));
    REQUIRE(skirmish.pickups.size()==1U);
    REQUIRE(skirmish.pickups[0].kind==5U);
    REQUIRE(skirmish.random_spots.size()==1U);
    REQUIRE(skirmish.units.size()==1U);
    REQUIRE(skirmish.skipped_count(SkipReason::mode_flag_filtered)==2U);
    REQUIRE(skirmish.random_distribution_allowed);
    return true;
}

RUNTIME_TEST(spawner_types_mode_and_view_gating) {
    const std::uint16_t id=static_cast<std::uint16_t>(1U|(2U<<4U));
    const std::vector<WorldItem> bank{item(21,0U,3U,id)};
    const std::vector<WorldItem> placed{item(20,0U,0U,id),item(21,0U,0U,id)};
    const LevelSpawnPlan campaign=interpret_level_spawns(level_with(bank,placed),context(4));
    REQUIRE(campaign.spawners.size()==3U);
    REQUIRE(campaign.spawners[0].source==SpawnSource::bank_spawner);
    REQUIRE(campaign.spawners[0].parameter==3U);
    REQUIRE(campaign.spawners[1].linked);
    REQUIRE(!campaign.spawners[2].linked);
    REQUIRE(campaign.spawners[1].owner==1U);
    REQUIRE(campaign.spawners[1].interval_ticks==90);
    SpawnContext split=context(0);
    split.view_count=2;
    const LevelSpawnPlan versus=interpret_level_spawns(level_with(bank,placed),split);
    REQUIRE(versus.spawners.empty());
    REQUIRE(versus.skipped_count(SkipReason::linked_spawner_mode)==1U);
    REQUIRE(versus.skipped_count(SkipReason::view_count_gated)==2U);
    return true;
}

RUNTIME_TEST(unknown_types_are_reported) {
    const std::vector<WorldItem> bank{item(99,0U,0U,0U),item(23,0U,0U,0U),item(24,0U,0U,0U)};
    const std::vector<WorldItem> placed{item(1,0U,0U,0U),item(77,0U,0U,0U)};
    const LevelSpawnPlan plan=interpret_level_spawns(level_with(bank,placed),context(0));
    REQUIRE(plan.skipped.size()==5U);
    REQUIRE(plan.skipped_count(SkipReason::retail_ignored_type)==1U);
    REQUIRE(plan.skipped_count(SkipReason::untraced_type)==2U);
    REQUIRE(plan.skipped_count(SkipReason::placed_object_other)==1U);
    REQUIRE(plan.skipped_count(SkipReason::unknown_placed_type)==1U);
    REQUIRE(plan.skipped[0].placement_index==0U);
    REQUIRE(plan.skipped[0].type==99);
    return true;
}

RUNTIME_TEST(player_placement_selection) {
    // slot index (id>>1)&3, side (id>>3)&3, optional id&1, respawn (id>>8)&1
    const std::vector<WorldItem> bank{
        item(22,0U,0U,static_cast<std::uint16_t>((1U<<1U)|(3U<<3U)|0x100U),10.0F,20.0F),
        item(22,0U,0U,static_cast<std::uint16_t>(3U<<1U)),
        item(22,0U,0U,1U)};
    SpawnContext ctx=context(0);
    ctx.active_player_count=2;
    ctx.player_slot_map={0U,1U,2U,3U};
    ctx.teams={4U,5U,6U,7U};
    const LevelSpawnPlan plan=interpret_level_spawns(level_with(bank),ctx);
    REQUIRE(plan.units.size()==1U);
    const UnitSpawnDefinition& unit=plan.units[0];
    REQUIRE(unit.player);
    REQUIRE(unit.owner==1U);
    REQUIRE(unit.team==5U);
    REQUIRE(unit.side==3U);
    REQUIRE(unit.respawn_flag);
    REQUIRE(unit.x==10.0F);
    REQUIRE(unit.z==20.0F);
    REQUIRE(plan.skipped_count(SkipReason::player_slot_absent)==1U);
    REQUIRE(plan.skipped_count(SkipReason::optional_entry_gated)==1U);
    // Marker group 0 is still reported for consumers that use it.
    const LevelSpawnPlan markers=interpret_level_spawns(
        level_with({item(25,0U,0x0AU,0U,1.0F,2.0F)}),ctx);
    REQUIRE(markers.markers[0].size()==1U);
    REQUIRE(markers.markers[0][0].x==1.0F);
    return true;
}

RUNTIME_TEST(random_pickup_distribution_rejects_bad_counts) {
    std::vector<WorldItem> bank;
    for (int i=0; i<4; ++i) bank.push_back(item(10,0U,22U,0U,static_cast<float>(i)));
    const LevelSpawnPlan plan=interpret_level_spawns(level_with(bank),context(0));
    std::array<std::int32_t,pickup_spawn_count_kinds> counts{};
    // Overflowing sum: must be rejected, not wrap.
    counts[0]=std::numeric_limits<std::int32_t>::max();
    counts[1]=std::numeric_limits<std::int32_t>::max();
    RetailRandom rng(7U);
    REQUIRE(distribute_random_pickups(plan,counts,rng).empty());
    REQUIRE(rng.seed()==7U);
    // A negative count must not hide a later count larger than the free spots.
    counts={};
    counts[0]=-3;
    counts[1]=6;
    RetailRandom rng2(7U);
    REQUIRE(distribute_random_pickups(plan,counts,rng2).empty());
    REQUIRE(rng2.seed()==7U);
    return true;
}

RUNTIME_TEST(random_pickup_distribution) {
    std::vector<WorldItem> bank;
    for (int i=0; i<4; ++i) bank.push_back(item(10,0U,22U,0U,static_cast<float>(i)));
    const LevelSpawnPlan plan=interpret_level_spawns(level_with(bank),context(0));
    std::array<std::int32_t,pickup_spawn_count_kinds> counts{};
    counts[0]=2;
    counts[5]=2;
    RetailRandom rng(42U);
    const auto placed=distribute_random_pickups(plan,counts,rng);
    REQUIRE(placed.size()==4U);
    REQUIRE(placed[0].kind==0U && placed[3].kind==5U);
    for (std::size_t i=0; i<placed.size(); ++i)
        for (std::size_t j=i+1U; j<placed.size(); ++j)
            REQUIRE(placed[i].placement_index!=placed[j].placement_index);
    counts[1]=1;
    RetailRandom rng2(42U);
    REQUIRE(distribute_random_pickups(plan,counts,rng2).empty());
    RetailRandom rng3(3U);
    REQUIRE(pick_effect_kind(0U,true,rng3)==0U);
    REQUIRE(rng3.seed()==3U);
    return true;
}

// Hand-computed from seed 0 (seed=seed*0x10DCD+1, result = halves swapped):
//   draw1 seed 0x00000001 -> 0x00010000 (%4=0)
//   draw2 seed 0x00010DCE -> 0x0DCE0001 (%4=1)
//   draw3 seed 0x1C5983F7 -> 0x83F71C59 (%4=1, %11=4)
//   draw4 seed 0xC35937CC -> 0x37CCC359 (%4=1)
//   draw5 seed 0x2E130A5D -> 0x0A5D2E13 (%4=3)
RUNTIME_TEST(random_pickup_distribution_retail_draw_order) {
    std::vector<WorldItem> bank;
    for (int i=0; i<4; ++i) bank.push_back(item(10,0U,22U,0U,static_cast<float>(i)));
    const LevelSpawnPlan plan=interpret_level_spawns(level_with(bank),context(0));
    REQUIRE(plan.random_spots.size()==4U);
    std::array<std::int32_t,pickup_spawn_count_kinds> counts{};
    counts[0]=1;
    counts[5]=1;
    // Re-roll on: kind 0 spot (draw1), kind 5 spot (draw2), kind 5 re-roll (draw3: 4 -> 3).
    RetailRandom on(0U);
    const auto a=distribute_random_pickups(plan,counts,on,true);
    REQUIRE(a.size()==2U);
    REQUIRE(a[0].placement_index==plan.random_spots[0].placement_index && a[0].kind==0U);
    REQUIRE(a[1].placement_index==plan.random_spots[1].placement_index && a[1].kind==3U);
    REQUIRE(on.seed()==0x1C5983F7U);
    // Re-roll off: no third draw, kind stays 5.
    RetailRandom off(0U);
    const auto b=distribute_random_pickups(plan,counts,off,false);
    REQUIRE(b.size()==2U);
    REQUIRE(b[1].placement_index==plan.random_spots[1].placement_index && b[1].kind==5U);
    REQUIRE(off.seed()==0x00010DCEU);
    // Taken-spot retries: three kind-0 copies draw spots 0,1,1(retry),1(retry),3.
    counts={};
    counts[0]=3;
    RetailRandom retry(0U);
    const auto c=distribute_random_pickups(plan,counts,retry,true);
    REQUIRE(c.size()==3U);
    REQUIRE(c[0].placement_index==plan.random_spots[0].placement_index);
    REQUIRE(c[1].placement_index==plan.random_spots[1].placement_index);
    REQUIRE(c[2].placement_index==plan.random_spots[3].placement_index);
    REQUIRE(retry.seed()==0x2E130A5DU);
    return true;
}

// update_spawner mode 3 draws one extra value (spawn_available_owner_unit.c:61)
// after the passing &0xF roll; both outcomes give shape 2.

// update_spawner is a deprecated adapter over spawner_runtime::tick_spawner:
// same statuses, same draws, same shape.
RUNTIME_TEST(update_spawner_delegates_to_tick_spawner) {
    const std::uint16_t id=static_cast<std::uint16_t>(1U|(3U<<2U));   // owner 1, mode 3
    SpawnerState legacy=ready_spawner(id);
    SpawnerState canonical=ready_spawner(id);
    SpawnerOwnerView owner=owner_with_budget(2);
    spawner_runtime::OwnerSpawnRecords owners{};
    owners[1].budget=2;
    owners[1].gate_open=true;
    RetailRandom a(0U);
    RetailRandom b(0U);
    spawner_runtime::SpawnOrderQueue queue;
    const auto x=update_spawner(legacy,std::nullopt,owner,500,a,always_clear);
    const auto y=spawner_runtime::tick_spawner(canonical,std::nullopt,0,500,owners,b,{},queue);
    REQUIRE(x.status==SpawnerUpdateStatus::spawned && y.status==spawner_runtime::SpawnerTickStatus::spawned);
    REQUIRE(a.seed()==b.seed() && a.seed()==0x10DCEU);   // roll + mode-3 bit
    REQUIRE(x.request.has_value() && x.request->shape==2U && x.request->squad_slot==0U);
    REQUIRE(owner.budget==1 && owners[1].budget==1);
    REQUIRE(legacy.last_tick==500 && canonical.last_tick==500);
    return true;
}

RUNTIME_TEST_MAIN()
