#include "battletanx/spawner_runtime.hpp"
#include "runtime_test_support.hpp"

#include <limits>
#include <vector>

using namespace battletanx;
using namespace battletanx::spawner_runtime;

namespace {

// RetailRandom fixtures (seed*0x10DCD+1, halves swapped):
//  seed 0 -> 0x00010000 (&0xF=0, %5=1), 0x0DCE0001 (&0xF=1)
//  seed 1 -> 0x0DCE0001 (&0xF=1)
//  seed 2 -> 0x1B9B0002 (%5=4)
constexpr std::uint32_t seed_roll_pass=0U;
constexpr std::uint32_t seed_roll_fail=1U;

SpawnerDefinition definition(std::uint8_t mode,std::uint8_t owner=1U,std::uint8_t count=1U,
                             std::uint8_t parameter=0U,std::uint8_t gate=0U,bool fast=false) {
    const std::uint16_t id=static_cast<std::uint16_t>(owner|(mode<<2U)|(count<<4U)|(gate<<12U));
    SpawnerDefinition d=unpack_spawner(id,parameter,fast);
    d.x=100.0F;
    d.z=200.0F;
    d.heading=0x400U;
    return d;
}

OwnerSpawnRecords owners_with_budget(std::int32_t budget) {
    OwnerSpawnRecords owners{};
    for (std::size_t o=0U; o<owners.size(); ++o) {
        for (std::size_t s=0U; s<member_slots; ++s) {
            owners[o].unit_ids[s]=static_cast<std::uint16_t>(o*5U+s);
        }
        owners[o].budget=budget;
        owners[o].gate_open=true;
    }
    return owners;
}

AreaQuery hits(std::vector<AreaHit> list) {
    return [list](const AreaBox&) { return AreaQueryResult{true,list}; };
}

AreaQuery empty_area() { return hits({}); }

} // namespace

RUNTIME_TEST(mode_maps_to_unit_kind) {
    REQUIRE(unit_kind_for_mode(0U)==UnitKind::tank);
    REQUIRE(unit_kind_for_mode(1U)==UnitKind::bike);
    REQUIRE(unit_kind_for_mode(2U)==UnitKind::goliath);
    REQUIRE(unit_kind_for_mode(3U)==UnitKind::goliath);
    REQUIRE(!unit_kind_for_mode(4U).has_value());
    return true;
}

RUNTIME_TEST(every_mode_spawns_expected_kind) {
    for (std::uint8_t mode=0U; mode<4U; ++mode) {
        OwnerSpawnRecords owners=owners_with_budget(3);
        SpawnerState state=make_spawner_state(definition(mode));
        RetailRandom rng{seed_roll_pass};
        SpawnOrderQueue queue;
        const auto r=tick_spawner(state,std::nullopt,3,1000,owners,rng,empty_area(),queue);
        REQUIRE(r.status==SpawnerTickStatus::spawned);
        REQUIRE(queue.orders().size()==1U);
        REQUIRE(queue.orders()[0].kind==*unit_kind_for_mode(mode));
        // Mode 3 draws one extra bit after the roll.
        REQUIRE(rng.seed()==(mode==3U ? 0x10DCEU : 1U));
    }
    return true;
}

RUNTIME_TEST(interval_step_and_boundary) {
    REQUIRE(definition(0U,1U,2U,0U,0U,true).interval_ticks==60);
    REQUIRE(definition(0U,1U,2U).interval_ticks==90);
    OwnerSpawnRecords owners=owners_with_budget(3);
    SpawnerState state=make_spawner_state(definition(0U,1U,2U));
    state.last_tick=10;
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    // last+interval >= now waits, with no draw.
    REQUIRE(tick_spawner(state,std::nullopt,3,100,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::interval_pending);
    REQUIRE(rng.seed()==seed_roll_pass);
    REQUIRE(tick_spawner(state,std::nullopt,3,101,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::spawned);
    REQUIRE(state.last_tick==101);
    return true;
}

RUNTIME_TEST(one_in_sixteen_roll) {
    OwnerSpawnRecords owners=owners_with_budget(3);
    SpawnerState state=make_spawner_state(definition(0U));
    RetailRandom rng{seed_roll_fail};
    SpawnOrderQueue queue;
    REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::roll_failed);
    REQUIRE(queue.orders().empty());
    REQUIRE(owners[1].budget==3);
    return true;
}

RUNTIME_TEST(gates_block_until_owner_bit) {
    for (int variant=0; variant<2; ++variant) {
        OwnerSpawnRecords owners=owners_with_budget(3);
        owners[1].gate_open=false;
        const SpawnerDefinition d=variant==0 ? definition(0U,1U,1U,gated_parameter)
                                             : definition(0U,1U,1U,0U,1U);
        SpawnerState state=make_spawner_state(d);
        RetailRandom rng{seed_roll_pass};
        SpawnOrderQueue queue;
        REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
                SpawnerTickStatus::gated);
        REQUIRE(rng.seed()==seed_roll_pass);
        owners[1].gate_open=true;
        REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
                SpawnerTickStatus::spawned);
    }
    // Gate bits 2 is not gated.
    OwnerSpawnRecords owners=owners_with_budget(3);
    owners[1].gate_open=false;
    SpawnerState state=make_spawner_state(definition(0U,1U,1U,0U,2U));
    RetailRandom rng{seed_roll_pass};
    SpawnOrderQueue queue;
    REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::spawned);
    return true;
}

RUNTIME_TEST(unit_budget_cap) {
    OwnerSpawnRecords owners=owners_with_budget(1);
    SpawnerState state=make_spawner_state(definition(0U,1U,0U));
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    REQUIRE(tick_spawner(state,std::nullopt,3,10,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::spawned);
    REQUIRE(owners[1].budget==0);
    owners[1].unit_active.fill(false);
    rng.reseed(seed_roll_pass);
    REQUIRE(tick_spawner(state,std::nullopt,3,20,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::no_budget);
    // Negative budget passes and is never decremented.
    owners[1].budget=-1;
    rng.reseed(seed_roll_pass);
    REQUIRE(tick_spawner(state,std::nullopt,3,30,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::spawned);
    REQUIRE(owners[1].budget==-1);
    return true;
}

RUNTIME_TEST(full_squad_keeps_budget_and_tick) {
    OwnerSpawnRecords owners=owners_with_budget(4);
    owners[1].unit_active.fill(true);
    SpawnerState state=make_spawner_state(definition(0U));
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::no_free_member);
    REQUIRE(owners[1].budget==4);
    REQUIRE(state.last_tick==0);
    REQUIRE(rng.seed()==1U);
    return true;
}

RUNTIME_TEST(order_queue_full_is_reported) {
    OwnerSpawnRecords owners=owners_with_budget(4);
    SpawnerState state=make_spawner_state(definition(0U));
    SpawnOrderQueue queue{0U};
    RetailRandom rng{seed_roll_pass};
    REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::order_queue_full);
    REQUIRE(owners[1].budget==4);
    REQUIRE(!owners[1].unit_active[0]);
    return true;
}

RUNTIME_TEST(order_fields_and_flags) {
    OwnerSpawnRecords owners=owners_with_budget(2);
    owners[1].unit_active[0]=true;
    SpawnerState state=make_spawner_state(definition(1U,1U,1U,7U));
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::spawned);
    const SpawnOrder& o=queue.orders()[0];
    REQUIRE(o.sequence==0U && o.owner==1U && o.member_slot==1U && o.unit_id==6U);
    REQUIRE(o.parameter==7U && o.heading==0x400U && o.flags==order_flag_force);
    REQUIRE(order_flags(3,0U,false,true)==(order_flag_force|order_flag_campaign_player));
    REQUIRE(order_flags(0,0U,false,false)==0U);
    REQUIRE(order_flags(9,2U,false,false)==order_flag_force);
    REQUIRE(order_flags(5,0U,true,true)==(order_flag_option|order_flag_force));
    return true;
}

RUNTIME_TEST(linked_spawner_ends_on_serial_change_or_destroy) {
    OwnerSpawnRecords owners=owners_with_budget(2);
    SpawnerDefinition d=definition(0U);
    d.linked=true;
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    SpawnerState a=make_spawner_state(d,7);
    REQUIRE(tick_spawner(a,LinkedEffectView{8,false},3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::finished);
    REQUIRE(a.finished);
    SpawnerState b=make_spawner_state(d,7);
    const auto r=tick_spawner(b,LinkedEffectView{7,true},3,500,owners,rng,empty_area(),queue);
    REQUIRE(r.status==SpawnerTickStatus::finished_destroyed && r.destroyed_event);
    SpawnerState c=make_spawner_state(d,7);
    REQUIRE(tick_spawner(c,std::nullopt,3,500,owners,rng,empty_area(),queue).status==
            SpawnerTickStatus::finished);
    REQUIRE(rng.seed()==seed_roll_pass);
    return true;
}

RUNTIME_TEST(link_creation_errors) {
    SpawnerSet set{2U};
    SpawnerDefinition linked=definition(0U);
    linked.linked=true;
    REQUIRE(set.create(linked,7,5)==SpawnerCreateError::linked_outside_campaign);
    REQUIRE(set.create(definition(0U),3,5)==SpawnerCreateError::unexpected_link);
    REQUIRE(set.spawn_counts()[1]==0U);
    // Failed kind-5 allocation: created unlinked and still counted.
    REQUIRE(!set.create(linked,3,std::nullopt).has_value());
    REQUIRE(set.slots()[0].link_failed && !set.slots()[0].state.definition.linked);
    REQUIRE(set.spawn_counts()[1]==1U);
    REQUIRE(!set.create(linked,3,5).has_value());
    REQUIRE(!set.slots()[1].link_failed && set.slots()[1].state.definition.linked);
    REQUIRE(set.spawn_counts()[1]==2U);
    REQUIRE(set.create(definition(0U),3,std::nullopt)==SpawnerCreateError::pool_full);
    REQUIRE(set.spawn_counts()[1]==2U);
    // The fallback spawner runs unlinked: a kind-5 hit blocks it.
    OwnerSpawnRecords owners=owners_with_budget(2);
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    const auto results=set.tick(3,500,owners,rng,hits({AreaHit{false,5,1U,true}}),
        [](std::size_t) { return std::optional<LinkedEffectView>{LinkedEffectView{5,false}}; },queue);
    REQUIRE(results.size()==2U && results[0].status==SpawnerTickStatus::area_blocked);
    REQUIRE(!set.slots()[0].state.finished);
    return true;
}

RUNTIME_TEST(area_rules) {
    AreaQueryResult r{true,{AreaHit{true,9,1U,false},AreaHit{false,wreck_kind,2U,false},
                            AreaHit{false,5,3U,true},AreaHit{false,wreck_kind,4U,false}}};
    const AreaVerdict linked=evaluate_area(r,AreaRule::spawner,true);
    REQUIRE(linked.clear && linked.wreck_notifications==std::vector<std::uint32_t>({2U,4U}));
    const AreaVerdict unlinked=evaluate_area(r,AreaRule::spawner,false);
    REQUIRE(!unlinked.clear && unlinked.wreck_notifications==std::vector<std::uint32_t>({2U}));
    const AreaVerdict respawn=evaluate_area(r,AreaRule::respawn);
    REQUIRE(!respawn.clear && respawn.wreck_notifications.size()==2U);
    AreaQueryResult none{false,{AreaHit{false,9,1U,false}}};
    REQUIRE(evaluate_area(none,AreaRule::spawner).clear);
    // Only 16 results are read.
    AreaQueryResult many{true,std::vector<AreaHit>(16U,AreaHit{false,wreck_kind,0U,false})};
    many.hits.push_back(AreaHit{false,9,0U,false});
    REQUIRE(evaluate_area(many,AreaRule::spawner).clear);
    return true;
}

RUNTIME_TEST(spawner_area_blocked_and_box_shape) {
    OwnerSpawnRecords owners=owners_with_budget(2);
    SpawnerState state=make_spawner_state(definition(2U));
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    AreaBox seen{};
    AreaQuery query=[&seen](const AreaBox& box) {
        seen=box;
        return AreaQueryResult{true,{AreaHit{false,0,1U,false}}};
    };
    REQUIRE(tick_spawner(state,std::nullopt,3,500,owners,rng,query,queue).status==
            SpawnerTickStatus::area_blocked);
    REQUIRE(seen.x==100 && seen.z==200 && seen.angle==0x600U);
    REQUIRE(seen.min_a==-60 && seen.max_a==60 && seen.min_b==-75 && seen.max_b==81);
    REQUIRE(seen.radius==100U);  // isqrt(60*60+81*81)=100.8
    REQUIRE(state.last_tick==0 && owners[1].budget==2);
    const auto bike=build_area_box(-1.9F,2.9F,0U,1U,1.0F);
    REQUIRE(bike && bike->x==-1 && bike->z==2 && bike->max_b==26 && bike->radius==46U);
    REQUIRE(!build_area_box(0.0F,0.0F,0U,3U,1.0F).has_value());
    return true;
}

RUNTIME_TEST(spawner_set_order_and_sequences) {
    SpawnerSet set;
    REQUIRE(!set.create(definition(0U,1U),3,std::nullopt));
    REQUIRE(!set.create(definition(1U,2U),3,std::nullopt));
    OwnerSpawnRecords owners=owners_with_budget(2);
    SpawnOrderQueue queue;
    RetailRandom rng{seed_roll_pass};
    const auto results=set.tick(3,500,owners,rng,empty_area(),{},queue);
    REQUIRE(results.size()==2U);
    REQUIRE(results[0].status==SpawnerTickStatus::spawned);
    REQUIRE(results[1].status==SpawnerTickStatus::roll_failed);
    REQUIRE(queue.orders().size()==1U && queue.orders()[0].owner==1U);
    StateWriter a;
    StateWriter b;
    set.serialize(a);
    set.serialize(b);
    REQUIRE(a.bytes()==b.bytes());
    return true;
}

RUNTIME_TEST(lineup_cycle_skips_trailing_goliath) {
    LineupCycle lineup{{{0U,1U,2U,0U,0U}},3U,0U};
    LineupError error{};
    REQUIRE(next_lineup_kind(lineup,&error)==UnitKind::tank);
    REQUIRE(next_lineup_kind(lineup,&error)==UnitKind::bike);
    // Index 2 is goliath and wraps to 0: redrawn as entry 0.
    REQUIRE(next_lineup_kind(lineup,&error)==UnitKind::tank);
    REQUIRE(lineup.next_index==1U);
    LineupCycle middle{{{2U,0U,0U,0U,0U}},2U,0U};
    REQUIRE(next_lineup_kind(middle,&error)==UnitKind::goliath);
    LineupCycle only{{{2U,0U,0U,0U,0U}},1U,0U};
    REQUIRE(!next_lineup_kind(only,&error) && error==LineupError::only_goliath);
    LineupCycle empty{{{}},0U,0U};
    REQUIRE(!next_lineup_kind(empty,&error) && error==LineupError::empty);
    LineupCycle stale{{{0U,0U,0U,0U,0U}},2U,3U};
    REQUIRE(!next_lineup_kind(stale,&error) && error==LineupError::index_out_of_range);
    LineupCycle bad{{{7U,0U,0U,0U,0U}},2U,0U};
    REQUIRE(!next_lineup_kind(bad,&error) && error==LineupError::invalid_kind);
    return true;
}

RUNTIME_TEST(non_campaign_respawn_uses_random_start_column) {
    SquadSpawnRecord squad;
    squad.owner=1U;
    squad.reserve=2;
    OwnerSpawnRecord owner=owners_with_budget(0)[1];
    std::array<TeamSpawnPoint,team_spawns_per_row> row{};
    for (std::size_t i=0U; i<row.size(); ++i) row[i]=TeamSpawnPoint{static_cast<float>(i*100U),0.0F,0U};
    RespawnContext context;
    context.game_kind=0;
    context.tick=50;
    context.team_row=row;
    // Block column 4 (x=400): seed 2 starts at 4, then 0.
    AreaQuery query=[](const AreaBox& box) {
        return AreaQueryResult{true,box.x==400 ? std::vector<AreaHit>{AreaHit{false,0,1U,false}}
                                               : std::vector<AreaHit>{}};
    };
    RetailRandom rng{2U};
    SpawnOrderQueue queue;
    const auto r=respawn_squad_unit(squad,owner,context,rng,query,queue);
    REQUIRE(r.status==RespawnStatus::spawned && r.point_index==0U);
    REQUIRE(squad.reserve==1 && queue.orders()[0].flags==0U);
    REQUIRE(rng.seed()==0x21B9BU);          // one draw from seed 2
    return true;
}

RUNTIME_TEST(non_campaign_respawn_fails_when_all_blocked) {
    SquadSpawnRecord squad;
    OwnerSpawnRecord owner=owners_with_budget(0)[0];
    RespawnContext context;
    context.team_row=std::array<TeamSpawnPoint,team_spawns_per_row>{};
    RetailRandom rng{0U};
    SpawnOrderQueue queue;
    const auto r=respawn_squad_unit(squad,owner,context,rng,
                                    hits({AreaHit{false,3,1U,false},AreaHit{false,wreck_kind,9U,false}}),queue);
    REQUIRE(r.status==RespawnStatus::no_clear_point);
    REQUIRE(r.wreck_notifications.size()==5U);
    REQUIRE(queue.orders().empty());
    context.team_row.reset();
    REQUIRE(respawn_squad_unit(squad,owner,context,rng,empty_area(),queue).status==
            RespawnStatus::no_team_row);
    return true;
}

RUNTIME_TEST(campaign_respawn_markers_in_order_with_fallthrough) {
    SquadSpawnRecord squad;
    squad.owner=0U;
    squad.reserve=1;
    squad.human_controlled=true;
    OwnerSpawnRecord owner=owners_with_budget(0)[0];
    std::vector<MemberMarker> markers{MemberMarker{SpawnRespawnPoint{10.0F,0.0F,0U,11U,0U},0},
                                      MemberMarker{SpawnRespawnPoint{20.0F,0.0F,0U,12U,0U},0}};
    RespawnContext context;
    context.game_kind=5;
    context.reserve_lock_player=true;
    context.markers=markers;
    RetailRandom rng{9U};
    SpawnOrderQueue queue;
    // Everything blocked: retail still uses the last marker.
    const auto r=respawn_squad_unit(squad,owner,context,rng,hits({AreaHit{false,3,1U,false}}),queue);
    REQUIRE(r.status==RespawnStatus::spawned && r.point_index==1U);
    REQUIRE(queue.orders()[0].parameter==12U && queue.orders()[0].x==20.0F);
    REQUIRE(queue.orders()[0].flags==order_flag_campaign_player);
    REQUIRE(rng.seed()==9U);                // no draw in campaign
    REQUIRE(squad.reserve==1);              // owner 0 with the lock keeps its reserve
    REQUIRE(squad.respawn_pending);
    context.markers={};
    REQUIRE(respawn_squad_unit(squad,owner,context,rng,empty_area(),queue).status==
            RespawnStatus::no_markers);
    return true;
}

RUNTIME_TEST(campaign_respawn_rejects_non_finite_marker) {
    SquadSpawnRecord squad;
    OwnerSpawnRecord owner=owners_with_budget(0)[0];
    std::vector<MemberMarker> markers(1U);
    markers[0].point.x=std::numeric_limits<float>::quiet_NaN();
    RespawnContext context;
    context.game_kind=3;
    context.markers=markers;
    RetailRandom rng{0U};
    SpawnOrderQueue queue;
    REQUIRE(respawn_squad_unit(squad,owner,context,rng,empty_area(),queue).status==
            RespawnStatus::invalid_point);
    REQUIRE(queue.orders().empty() && !owner.unit_active[0]);
    return true;
}

RUNTIME_TEST(respawn_pool_full) {
    SquadSpawnRecord squad;
    OwnerSpawnRecord owner=owners_with_budget(0)[0];
    owner.unit_active.fill(true);
    std::vector<MemberMarker> markers(1U);
    RespawnContext context;
    context.game_kind=3;
    context.markers=markers;
    RetailRandom rng{0U};
    SpawnOrderQueue queue;
    REQUIRE(respawn_squad_unit(squad,owner,context,rng,empty_area(),queue).status==
            RespawnStatus::no_free_member);
    REQUIRE(squad.lineup.next_index==0U);   // count 1: wraps back to 0
    return true;
}

RUNTIME_TEST(member_respawn_cooldown_and_shape) {
    SquadSpawnRecord squad;
    squad.owner=2U;
    squad.member_respawns={0,3};
    OwnerSpawnRecord owner=owners_with_budget(0)[2];
    std::vector<MemberMarker> markers(3U);
    for (std::size_t i=0U; i<markers.size(); ++i) {
        markers[i].point=SpawnRespawnPoint{static_cast<float>(i),0.0F,0U,static_cast<std::uint8_t>(40U+i),0U};
    }
    markers[1].last_used=641;   // 1000-360=640: still cooling
    markers[2].last_used=640;   // exactly usable
    RespawnContext context;
    context.game_kind=4;
    context.tick=1000;
    context.markers=markers;
    SpawnOrderQueue queue;
    const auto r=respawn_squad_member(squad,owner,context,empty_area(),queue);
    REQUIRE(r.status==RespawnStatus::spawned && r.point_index==2U);
    const SpawnOrder& o=queue.orders()[0];
    REQUIRE(o.kind==UnitKind::bike && o.parameter==42U);
    REQUIRE(o.flags==(order_flag_option|order_flag_force));
    REQUIRE(markers[2].last_used==1000 && squad.member_respawns[1]==2);
    // Marker 0 is never used; markers 1 (641>640) and 2 are cooling.
    REQUIRE(respawn_squad_member(squad,owner,context,empty_area(),queue).status==
            RespawnStatus::no_clear_point);
    squad.member_respawns={1,0};
    markers[1].last_used=0;
    context.tick=1001;
    const auto t=respawn_squad_member(squad,owner,context,empty_area(),queue);
    REQUIRE(t.status==RespawnStatus::spawned && queue.orders().back().kind==UnitKind::tank);
    REQUIRE(squad.member_respawns[0]==0 && markers[1].last_used==1001);
    // last_used is part of the serialized state.
    StateWriter before;
    serialize_member_markers(before,markers);
    markers[1].last_used=5;
    StateWriter after;
    serialize_member_markers(after,markers);
    REQUIRE(before.bytes()!=after.bytes());
    return true;
}

RUNTIME_TEST(one_marker_span_serves_both_paths) {
    SquadSpawnRecord squad;
    squad.owner=1U;
    squad.member_respawns={1,0};
    OwnerSpawnRecord owner=owners_with_budget(0)[1];
    std::vector<MemberMarker> markers(2U);
    markers[1].point=SpawnRespawnPoint{50.0F,60.0F,0U,7U,0U};
    RespawnContext context;
    context.game_kind=3;
    context.tick=2000;
    context.markers=markers;
    SpawnOrderQueue queue;
    REQUIRE(respawn_squad_member(squad,owner,context,empty_area(),queue).status==RespawnStatus::spawned);
    REQUIRE(markers[1].last_used==2000);
    // Marker 0 blocked: the unit path falls through to the same marker 1.
    AreaQuery query=[](const AreaBox& box) {
        return AreaQueryResult{true,box.x==0 ? std::vector<AreaHit>{AreaHit{false,0,1U,false}}
                                             : std::vector<AreaHit>{}};
    };
    RetailRandom rng{0U};
    const auto r=respawn_squad_unit(squad,owner,context,rng,query,queue);
    REQUIRE(r.status==RespawnStatus::spawned && r.point_index==1U);
    REQUIRE(queue.orders().back().x==50.0F && queue.orders().back().parameter==7U);
    return true;
}

RUNTIME_TEST(squad_clock_gates) {
    SquadSpawnRecord squad;
    squad.reserve=1;
    squad.member_respawns={1,0};
    SquadRespawnClock clock{true,2U,4U,false,0,90,100.0F};
    REQUIRE(member_respawn_due(clock,squad,3));
    REQUIRE(!member_respawn_due(clock,squad,0));
    clock.alive=3U;
    REQUIRE(!member_respawn_due(clock,squad,3));
    clock.alive=2U;
    REQUIRE(!unit_respawn_due(clock,squad,0,10));
    REQUIRE(clock.due_tick==100 && clock.delay==90);
    REQUIRE(!unit_respawn_due(clock,squad,0,100));
    REQUIRE(unit_respawn_due(clock,squad,0,101));
    RespawnResult ok;
    ok.status=RespawnStatus::spawned;
    finish_unit_respawn(clock,ok);
    REQUIRE(clock.due_tick==0);
    // Kind 0 with no reserve: delay grows by 30*scale and clamps at 360*scale.
    squad.reserve=0;
    clock.unlimited=true;
    clock.delay=350;
    REQUIRE(!unit_respawn_due(clock,squad,0,10));
    REQUIRE(clock.due_tick==360 && clock.delay==360);
    // Campaign human squad with a pending respawn waits.
    clock.due_tick=5;
    squad.human_controlled=true;
    squad.respawn_pending=true;
    REQUIRE(!unit_respawn_due(clock,squad,3,100));
    return true;
}

RUNTIME_TEST(linked_spawner_ignores_own_effect_in_area) {
    // Seed 0: one draw -> 0x00010000 (&0xF=0), roll passes. Mode 0: no extra draw.
    SpawnerDefinition d=definition(0U);
    d.linked=true;
    const AreaHit own{false,linked_effect_kind,1U,true};
    const AreaHit other{false,linked_effect_kind,2U,false};
    {
        OwnerSpawnRecords owners=owners_with_budget(2);
        SpawnOrderQueue queue;
        RetailRandom rng{seed_roll_pass};
        SpawnerState s=make_spawner_state(d,5);
        const auto r=tick_spawner(s,LinkedEffectView{5,false},3,500,owners,rng,hits({own}),queue);
        REQUIRE(r.status==SpawnerTickStatus::spawned);
        REQUIRE(queue.orders().size()==1U && owners[1].budget==1);
        REQUIRE(rng.seed()==1U);
    }
    {
        // A kind-5 hit that is not this spawner's own effect still blocks.
        OwnerSpawnRecords owners=owners_with_budget(2);
        SpawnOrderQueue queue;
        RetailRandom rng{seed_roll_pass};
        SpawnerState s=make_spawner_state(d,5);
        const auto r=tick_spawner(s,LinkedEffectView{5,false},3,500,owners,rng,hits({own,other}),queue);
        REQUIRE(r.status==SpawnerTickStatus::area_blocked);
        REQUIRE(queue.orders().empty() && owners[1].budget==2);
    }
    return true;
}

RUNTIME_TEST(one_marker_span_unit_path_fallthrough_uses_last_marker) {
    SquadSpawnRecord squad;
    squad.owner=1U;
    squad.member_respawns={1,0};
    OwnerSpawnRecord owner=owners_with_budget(0)[1];
    std::vector<MemberMarker> markers(3U);
    markers[1].point=SpawnRespawnPoint{50.0F,60.0F,0U,7U,0U};
    markers[2].point=SpawnRespawnPoint{80.0F,90.0F,0U,9U,0U};
    RespawnContext context;
    context.game_kind=3;
    context.tick=2000;
    context.markers=markers;
    SpawnOrderQueue queue;
    REQUIRE(respawn_squad_member(squad,owner,context,empty_area(),queue).status==RespawnStatus::spawned);
    REQUIRE(markers[1].last_used==2000);
    // Every box blocked: the unit path ignores cooldowns and ends on the last marker.
    RetailRandom rng{0U};
    const AreaQuery blocked=hits({AreaHit{false,0,1U,false}});
    const auto r=respawn_squad_unit(squad,owner,context,rng,blocked,queue);
    REQUIRE(r.status==RespawnStatus::spawned && r.point_index==2U);
    REQUIRE(queue.orders().back().x==80.0F && queue.orders().back().parameter==9U);
    REQUIRE(rng.seed()==0U);                // campaign: no draw
    REQUIRE(markers[2].last_used==0 && markers[1].last_used==2000);
    // Member path with the same blocked area: marker 1 cooling, marker 2 blocked.
    REQUIRE(respawn_squad_member(squad,owner,context,blocked,queue).status==
            RespawnStatus::no_clear_point);
    return true;
}

RUNTIME_TEST(build_member_markers_keeps_point_order) {
    REQUIRE(build_member_markers({}).empty());
    const std::vector<SpawnRespawnPoint> points{
        SpawnRespawnPoint{.x=10.0F,.z=-20.0F,.heading=0x100U,.parameter=3U,.placement_index=7U},
        SpawnRespawnPoint{.x=30.0F,.z=40.0F,.heading=0x200U,.parameter=9U,.placement_index=12U},
    };
    const std::vector<MemberMarker> markers=build_member_markers(points);
    REQUIRE(markers.size()==2U);
    for (std::size_t i=0; i<points.size(); ++i) {
        REQUIRE(markers[i].point.x==points[i].x && markers[i].point.z==points[i].z);
        REQUIRE(markers[i].point.heading==points[i].heading);
        REQUIRE(markers[i].point.parameter==points[i].parameter);
        REQUIRE(markers[i].point.placement_index==points[i].placement_index);
        REQUIRE(markers[i].last_used==0);
    }
    return true;
}

// Pins RetailRandom draws per tick_spawner outcome to the retail order in
// spawn_available_owner_unit (func_8010D59C): gate, interval, roll (&0xF),
// budget, mode-3 bit, area, free member. Host queue-full adds no draw.
RUNTIME_TEST(spawner_rng_draw_count_per_status) {
    const auto draws_after=[](std::uint32_t seed,std::uint32_t count) {
        RetailRandom r{seed};
        for (std::uint32_t i=0U; i<count; ++i) (void)r.next_u32();
        return r.seed();
    };
    const auto run=[](std::uint8_t mode,std::int32_t budget,std::int32_t tick,std::uint32_t seed,
                      std::size_t queue_capacity,bool squad_full,const AreaQuery& area,
                      SpawnerTickStatus expected) {
        OwnerSpawnRecords owners=owners_with_budget(budget);
        if (squad_full) owners[1].unit_active.fill(true);
        SpawnerState state=make_spawner_state(definition(mode));
        SpawnOrderQueue queue{queue_capacity};
        RetailRandom rng{seed};
        const bool ok=tick_spawner(state,std::nullopt,3,tick,owners,rng,area,queue).status==expected;
        return ok ? rng.seed() : 0xFFFFFFFFU;
    };
    const AreaQuery blocked=hits({AreaHit{false,9,1U,false}});
    REQUIRE(run(0U,4,0,seed_roll_pass,8U,false,empty_area(),SpawnerTickStatus::interval_pending)==
            draws_after(seed_roll_pass,0U));
    REQUIRE(run(0U,4,500,seed_roll_fail,8U,false,empty_area(),SpawnerTickStatus::roll_failed)==
            draws_after(seed_roll_fail,1U));
    REQUIRE(run(3U,0,500,seed_roll_pass,8U,false,empty_area(),SpawnerTickStatus::no_budget)==
            draws_after(seed_roll_pass,1U));
    REQUIRE(run(0U,4,500,seed_roll_pass,8U,false,blocked,SpawnerTickStatus::area_blocked)==
            draws_after(seed_roll_pass,1U));
    REQUIRE(run(3U,4,500,seed_roll_pass,8U,false,blocked,SpawnerTickStatus::area_blocked)==
            draws_after(seed_roll_pass,2U));
    REQUIRE(run(3U,4,500,seed_roll_pass,8U,true,empty_area(),SpawnerTickStatus::no_free_member)==
            draws_after(seed_roll_pass,2U));
    REQUIRE(run(0U,4,500,seed_roll_pass,0U,false,empty_area(),SpawnerTickStatus::order_queue_full)==
            draws_after(seed_roll_pass,1U));
    REQUIRE(run(0U,4,500,seed_roll_pass,8U,false,empty_area(),SpawnerTickStatus::spawned)==
            draws_after(seed_roll_pass,1U));
    REQUIRE(run(3U,4,500,seed_roll_pass,8U,false,empty_area(),SpawnerTickStatus::spawned)==
            draws_after(seed_roll_pass,2U));
    return true;
}

RUNTIME_TEST_MAIN()
