#include "battletanx/queen_world.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <utility>

using namespace battletanx;
using namespace battletanx::queen_world;

namespace {

constexpr EntityId unit_a{1U,1U};   // owner 0, team 0
constexpr EntityId unit_b{2U,1U};   // owner 1, team 1
constexpr EntityId unit_c{3U,1U};   // owner 2, team 0
constexpr EntityId unit_d{4U,1U};   // owner 3, team 1

Config four_owner_config(std::uint32_t game_kind) {
    Config config;
    config.game_kind=game_kind;
    config.owner_count=4U;
    config.owners[0]=OwnerInfo{.present=true,.team=0U};
    config.owners[1]=OwnerInfo{.present=true,.team=1U};
    config.owners[2]=OwnerInfo{.present=true,.team=0U};
    config.owners[3]=OwnerInfo{.present=true,.team=1U};
    return config;
}

struct Level {
    std::optional<QueenWorld> world;
    std::array<BaseHandle,4> bases{};
    std::array<QueenHandle,4> queens{};
};

// Objects of kind 11..14 at x = 1000*owner, z = 0 (identity slot rank).
Level make_level(std::uint32_t game_kind) {
    Level level;
    CreateResult created=QueenWorld::create(four_owner_config(game_kind));
    level.world=std::move(created.world);
    for (std::uint8_t i=0U; i<4U; ++i) {
        const PlaceResult placed=level.world->place_level_object(
            LevelObject{.kind=static_cast<std::uint8_t>(11U+i),.active=true,
                        .x=1000.0F*static_cast<float>(i),.z=0.0F});
        level.bases[i]=placed.base;
        level.queens[i]=placed.queen;
    }
    level.world->finish_placement();
    return level;
}

UnitContact contact(EntityId unit,std::uint8_t owner) {
    return UnitContact{.unit=unit,.owner=owner,.event_code=0U,.contact_kind=0U,.collector_blocked=false};
}

QueenState state(const QueenWorld& world,QueenHandle queen) {
    QueenView view;
    if (!world.view(queen,view)) return QueenState::rescued;
    return view.state;
}

// Wave fixtures (synthetic shapes of the retail kind-4 waves; see
// docs/OBJECTIVE_ENTITIES.md). Two owners: player 0 (team 0), computer 1 (team 1).
Config rescue_wave_config() {
    Config config;
    config.game_kind=rescue_game_kind;
    config.owner_count=2U;
    config.owners[0]=OwnerInfo{.present=true,.team=0U};
    config.owners[1]=OwnerInfo{.present=true,.team=1U};
    return config;
}

ObjectiveConfig rescue_objective_config(const QueenWorld& world) {
    ObjectiveConfig config;
    config.game_kind=rescue_game_kind;
    config.owners[0]=ObjectiveOwner{.present=true,.computer=false,.team=0U,.bases=0U};
    config.owners[1]=ObjectiveOwner{.present=true,.computer=true,.team=1U,.bases=0U};
    seed_objective_bases(world,config);
    return config;
}

} // namespace

RUNTIME_TEST(placement_maps_object_kinds_through_slot_rank) {
    Config config=four_owner_config(3U);
    config.slot_rank={2U,0U,3U,1U};
    CreateResult created=QueenWorld::create(config);
    REQUIRE(created.world.has_value());
    QueenWorld& world=*created.world;
    const PlaceResult first=world.place_level_object(LevelObject{.kind=11U,.active=true,.x=5.0F,.z=7.0F});
    REQUIRE(first.outcome==PlaceOutcome::placed && first.owner==2U);
    BaseView base;
    REQUIRE(world.view(first.base,base) && base.owner==2U && base.team==0U && base.queens_held==1U);
    QueenView queen;
    REQUIRE(world.view(first.queen,queen));
    REQUIRE(queen.state==QueenState::at_base && queen.home_owner==2U && queen.spawn_owner==2U);
    REQUIRE(queen.x==5.0F && queen.z==7.0F);
    REQUIRE(world.place_level_object(LevelObject{.kind=11U,.active=true,.x=0.0F,.z=0.0F}).outcome==PlaceOutcome::duplicate_base);
    REQUIRE(world.place_level_object(LevelObject{.kind=10U,.active=true,.x=0.0F,.z=0.0F}).outcome==PlaceOutcome::not_base_kind);
    REQUIRE(world.place_level_object(LevelObject{.kind=15U,.active=true,.x=0.0F,.z=0.0F}).outcome==PlaceOutcome::not_base_kind);
    REQUIRE(world.place_level_object(LevelObject{.kind=12U,.active=false,.x=0.0F,.z=0.0F}).outcome==PlaceOutcome::inactive);
    world.finish_placement();
    REQUIRE(world.total_bases()==1U && world.team_count(0U)==1U);
    REQUIRE(world.place_level_object(LevelObject{.kind=12U,.active=true,.x=0.0F,.z=0.0F}).outcome==PlaceOutcome::placement_closed);
    return true;
}

RUNTIME_TEST(placement_respects_owner_count_and_game_kind) {
    Config config=four_owner_config(0U);
    config.owner_count=2U;
    CreateResult created=QueenWorld::create(config);
    REQUIRE(created.world->place_level_object(LevelObject{.kind=13U,.active=true,.x=0.0F,.z=0.0F}).outcome==
            PlaceOutcome::owner_out_of_range);
    CreateResult other=QueenWorld::create(four_owner_config(2U));
    REQUIRE(other.world->place_level_object(LevelObject{.kind=11U,.active=true,.x=0.0F,.z=0.0F}).outcome==
            PlaceOutcome::kind_has_no_bases);
    Config bad=four_owner_config(3U);
    bad.slot_rank[1]=4U;
    REQUIRE(QueenWorld::create(bad).error==ConfigError::slot_rank_invalid);
    bad=four_owner_config(3U);
    bad.owners[2].team=4U;
    REQUIRE(QueenWorld::create(bad).error==ConfigError::team_out_of_range);
    return true;
}

RUNTIME_TEST(kind6_owner0_base_has_no_queen_but_counts) {
    Level level=make_level(6U);
    REQUIRE(!level.queens[0].valid() && level.bases[0].valid());
    REQUIRE(level.world->total_bases()==4U && level.world->team_count(0U)==2U);
    return true;
}

RUNTIME_TEST(contacts_rejected_before_placement_finished) {
    CreateResult created=QueenWorld::create(four_owner_config(3U));
    const PlaceResult placed=created.world->place_level_object(LevelObject{.kind=11U,.active=true,.x=0.0F,.z=0.0F});
    REQUIRE(created.world->touch_queen(placed.queen,contact(unit_b,1U),0)==Status::not_ready);
    REQUIRE(created.world->step(0)==Status::not_ready);
    return true;
}

RUNTIME_TEST(same_team_contact_is_ignored) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_c,2U),1)==Status::ignored);
    REQUIRE(state(world,level.queens[0])==QueenState::at_base && world.pending_events()==0U);
    return true;
}

RUNTIME_TEST(cross_team_theft_then_store) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),10)==Status::applied);
    REQUIRE(state(world,level.queens[0])==QueenState::carried);
    REQUIRE(world.team_count(0U)==1U && world.team_count(1U)==2U);
    REQUIRE(world.pending_events()==1U);
    const QueenEvent& taken=world.event(0U);
    REQUIRE(taken.kind==QueenEventKind::taken_from_base && taken.holder_team==0U && taken.team==1U);
    REQUIRE(taken.progress==take_progress && taken.from==QueenState::at_base);
    // Wrong team base: no effect.
    REQUIRE(world.touch_base(level.bases[0],contact(unit_b,1U),11)==Status::ignored);
    // Own team base (owner 3, team 1) holds one queen: slot 1.
    REQUIRE(world.touch_base(level.bases[3],contact(unit_b,1U),12)==Status::applied);
    QueenView view;
    REQUIRE(world.view(level.queens[0],view));
    REQUIRE(view.state==QueenState::stored && view.holder_owner==3U && view.home_owner==3U && view.slot==1U);
    REQUIRE(view.x==3000.0F+slot_offsets[1].dx && view.z==slot_offsets[1].dz);
    REQUIRE(world.team_count(1U)==3U);
    const QueenEvent& stored=world.event(1U);
    REQUIRE(stored.kind==QueenEventKind::stored && stored.team==1U && stored.progress==store_progress);
    REQUIRE(stored.previous_home==0U && stored.sequence==taken.sequence+1U);
    return true;
}

RUNTIME_TEST(rehome_policy_off_keeps_home) {
    Config config=four_owner_config(3U);
    config.policies.store_rehomes_queen=false;
    CreateResult created=QueenWorld::create(config);
    QueenWorld& world=*created.world;
    const PlaceResult q0=world.place_level_object(LevelObject{.kind=11U,.active=true,.x=0.0F,.z=0.0F});
    const PlaceResult b1=world.place_level_object(LevelObject{.kind=12U,.active=true,.x=0.0F,.z=0.0F});
    world.finish_placement();
    REQUIRE(world.touch_queen(q0.queen,contact(unit_b,1U),0)==Status::applied);
    REQUIRE(world.touch_base(b1.base,contact(unit_b,1U),1)==Status::applied);
    QueenView view;
    REQUIRE(world.view(q0.queen,view) && view.home_owner==0U && view.holder_owner==1U);
    return true;
}

RUNTIME_TEST(death_drop_and_120_tick_return) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),0)==Status::applied);
    REQUIRE(world.touch_queen(level.queens[2],contact(unit_b,1U),1)==Status::applied);
    world.clear_events();
    REQUIRE(world.carrier_destroyed(unit_b,100.0F,200.0F,50)==Status::applied);
    REQUIRE(world.pending_events()==2U);
    // Head first: queen 2 was collected last.
    REQUIRE(world.event(0U).queen==level.queens[2] && world.event(0U).slot==0U);
    REQUIRE(world.event(1U).queen==level.queens[0] && world.event(1U).slot==1U);
    QueenView view;
    REQUIRE(world.view(level.queens[0],view) && view.state==QueenState::dropped);
    REQUIRE(view.expire_tick==50+drop_return_ticks && view.x==100.0F+slot_offsets[1].dx);
    REQUIRE(view.z==200.0F+slot_offsets[1].dz);
    world.clear_events();
    REQUIRE(world.step(50+drop_return_ticks)==Status::ignored);   // strictly greater
    REQUIRE(world.step(51+drop_return_ticks)==Status::applied);
    REQUIRE(world.pending_events()==2U);
    // Slot order on expiry: queen 0 first.
    REQUIRE(world.event(0U).kind==QueenEventKind::returned && world.event(0U).reason==ReturnReason::drop_expired);
    REQUIRE(world.event(0U).queen==level.queens[0] && world.event(0U).team==0U && world.event(0U).progress==0);
    REQUIRE(state(world,level.queens[0])==QueenState::stored);
    REQUIRE(world.team_count(0U)==2U && world.team_count(1U)==2U);
    return true;
}

RUNTIME_TEST(dropped_queen_picked_up_by_any_team) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),0)==Status::applied);
    REQUIRE(world.carrier_destroyed(unit_b,0.0F,0.0F,1)==Status::applied);
    world.clear_events();
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_a,0U),2)==Status::applied);
    REQUIRE(world.event(0U).kind==QueenEventKind::picked_up && world.event(0U).progress==0);
    REQUIRE(world.team_count(0U)==1U);   // no change on pickup
    REQUIRE(world.touch_base(level.bases[2],contact(unit_a,0U),3)==Status::applied);
    REQUIRE(world.team_count(0U)==2U);
    return true;
}

RUNTIME_TEST(carrier_invalidated_returns_home) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),0)==Status::applied);
    world.clear_events();
    REQUIRE(world.carrier_invalidated(unit_b,5)==Status::applied);
    REQUIRE(world.event(0U).reason==ReturnReason::carrier_invalidated && world.event(0U).team==0U);
    REQUIRE(world.team_count(0U)==2U);
    REQUIRE(world.carrier_invalidated(unit_b,6)==Status::ignored);
    return true;
}

RUNTIME_TEST(simultaneous_contacts_first_wins) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),7)==Status::applied);
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_d,3U),7)==Status::ignored);   // state 1
    QueenView view;
    REQUIRE(world.view(level.queens[0],view) && view.carrier==unit_b);
    REQUIRE(world.pending_events()==1U && world.team_count(0U)==1U);
    return true;
}

RUNTIME_TEST(blocked_and_nonzero_contacts_ignored) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    UnitContact blocked=contact(unit_b,1U);
    blocked.collector_blocked=true;
    REQUIRE(world.touch_queen(level.queens[0],blocked,0)==Status::ignored);
    UnitContact event=contact(unit_b,1U);
    event.event_code=4U;
    REQUIRE(world.touch_queen(level.queens[0],event,0)==Status::ignored);
    REQUIRE(world.touch_base(level.bases[1],event,0)==Status::ignored);
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,9U),0)==Status::invalid_owner);
    return true;
}

RUNTIME_TEST(rescue_contact_in_kind4) {
    Level level=make_level(4U);
    QueenWorld& world=*level.world;
    // Own-team queen: collect is a no-op, result 9 still fires.
    REQUIRE(world.touch_queen(level.queens[2],contact(unit_a,0U),0)==Status::applied);
    REQUIRE(world.pending_events()==1U && world.event(0U).kind==QueenEventKind::rescue_contact);
    REQUIRE(state(world,level.queens[2])==QueenState::rescued);
    world.clear_events();
    // Enemy queen: taken first, then rescue contact.
    REQUIRE(world.touch_queen(level.queens[1],contact(unit_a,0U),1)==Status::applied);
    REQUIRE(world.pending_events()==2U);
    REQUIRE(world.event(0U).kind==QueenEventKind::taken_from_base);
    REQUIRE(world.event(1U).kind==QueenEventKind::rescue_contact);
    QueenView view;
    REQUIRE(world.view(level.queens[1],view) && view.retail_state==1U && view.state==QueenState::rescued);
    // Other owners never produce rescue contact.
    world.clear_events();
    REQUIRE(world.touch_queen(level.queens[3],contact(unit_c,2U),2)==Status::applied);
    REQUIRE(world.pending_events()==1U && world.event(0U).kind==QueenEventKind::taken_from_base);
    return true;
}

RUNTIME_TEST(marker_contact_readiness) {
    CreateResult created=QueenWorld::create(four_owner_config(5U));
    QueenWorld& world=*created.world;
    REQUIRE(world.place_marker(0.0F,0.0F,50,2U).outcome==PlaceOutcome::split_screen);
    const MarkerPlaceResult placed=world.place_marker(10.0F,20.0F,50,1U);
    REQUIRE(placed.outcome==PlaceOutcome::placed);
    world.finish_placement();
    REQUIRE(world.touch_marker(placed.marker,contact(unit_b,1U),0)==Status::ignored);
    UnitContact blocked=contact(unit_a,0U);
    blocked.contact_kind=1U;
    REQUIRE(world.touch_marker(placed.marker,blocked,0)==Status::ignored);
    REQUIRE(world.touch_marker(placed.marker,contact(unit_a,0U),3)==Status::applied);
    REQUIRE(world.event(0U).kind==QueenEventKind::marker_reached && world.event(0U).marker==placed.marker);
    // Repeated contact before draining is idempotent: still one event.
    REQUIRE(world.touch_marker(placed.marker,contact(unit_a,0U),4)==Status::applied);
    REQUIRE(world.pending_events()==1U);
    return true;
}

RUNTIME_TEST(marker_pool_exhaustion) {
    CreateResult created=QueenWorld::create(four_owner_config(5U));
    QueenWorld& world=*created.world;
    for (std::size_t i=0U; i<marker_capacity; ++i) {
        REQUIRE(world.place_marker(0.0F,0.0F,10,1U).outcome==PlaceOutcome::placed);
    }
    REQUIRE(world.place_marker(0.0F,0.0F,10,1U).outcome==PlaceOutcome::pool_exhausted);
    return true;
}

RUNTIME_TEST(invalid_and_stale_handles) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    REQUIRE(world.touch_queen(QueenHandle{},contact(unit_b,1U),0)==Status::stale_handle);
    REQUIRE(world.touch_queen(QueenHandle{EntityId{9U,1U}},contact(unit_b,1U),0)==Status::stale_handle);
    QueenHandle wrong=level.queens[0];
    wrong.id.generation=static_cast<std::uint16_t>(wrong.id.generation+1U);
    REQUIRE(world.touch_queen(wrong,contact(unit_b,1U),0)==Status::stale_handle);
    REQUIRE(world.touch_marker(MarkerHandle{EntityId{0U,1U}},contact(unit_a,0U),0)==Status::stale_handle);
    const QueenHandle old=level.queens[0];
    const BaseHandle old_base=level.bases[0];
    world.reset();
    const PlaceResult again=world.place_level_object(LevelObject{.kind=11U,.active=true,.x=0.0F,.z=0.0F});
    world.finish_placement();
    REQUIRE(again.queen.id.slot==old.id.slot && !(again.queen==old));
    REQUIRE(world.touch_queen(old,contact(unit_b,1U),0)==Status::stale_handle);
    REQUIRE(world.touch_base(old_base,contact(unit_a,0U),0)==Status::stale_handle);
    QueenView view;
    REQUIRE(!world.view(old,view));
    return true;
}

RUNTIME_TEST(event_queue_full_changes_nothing) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    // Fill the queue with take/drop/pickup cycles (no counter drift on pickup).
    REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),0)==Status::applied);
    while (world.pending_events()+2U<=event_capacity) {
        REQUIRE(world.carrier_destroyed(unit_b,0.0F,0.0F,0)==Status::applied);
        REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),0)==Status::applied);
    }
    while (world.pending_events()<event_capacity) {
        REQUIRE(world.carrier_destroyed(unit_b,0.0F,0.0F,0)==Status::applied);
        if (world.pending_events()<event_capacity) {
            REQUIRE(world.touch_queen(level.queens[0],contact(unit_b,1U),0)==Status::applied);
        }
    }
    StateWriter before;
    world.serialize(before);
    REQUIRE(world.touch_queen(level.queens[1],contact(unit_a,0U),1)==Status::event_queue_full);
    StateWriter after;
    world.serialize(after);
    REQUIRE(before.bytes()==after.bytes());
    return true;
}

RUNTIME_TEST(forward_events_drive_objective_capture) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    ObjectiveConfig objective_config;
    objective_config.game_kind=3U;
    for (std::size_t i=0U; i<4U; ++i) {
        objective_config.owners[i]=ObjectiveOwner{.present=true,.computer=i!=0U,
                                                  .team=static_cast<std::uint8_t>(i%2U),.bases=0U};
    }
    seed_objective_bases(world,objective_config);
    ObjectiveCreateResult created=ObjectiveRuntime::create(objective_config);
    REQUIRE(created.runtime.has_value());
    ObjectiveRuntime& objective=*created.runtime;
    REQUIRE(objective.total_bases()==4U);
    // Team 0 steals both team-1 queens and stores them.
    REQUIRE(world.touch_queen(level.queens[1],contact(unit_a,0U),0)==Status::applied);
    REQUIRE(world.touch_queen(level.queens[3],contact(unit_a,0U),0)==Status::applied);
    REQUIRE(world.touch_base(level.bases[0],contact(unit_a,0U),1)==Status::applied);
    REQUIRE(world.pending_events()==4U);
    // Store order: head first (queen 3 then queen 1).
    REQUIRE(world.event(2U).queen==level.queens[3] && world.event(3U).queen==level.queens[1]);
    const ForwardSummary summary=forward_events(world,objective);
    REQUIRE(summary.forwarded==4U && summary.applied==4U && summary.mismatched==0U && world.pending_events()==0U);
    REQUIRE(objective.team_queens(0U)==4U && objective.team_queens(1U)==0U);
    REQUIRE(world.team_count(0U)==objective.team_queens(0U) && world.matching_team()==0U);
    return true;
}

RUNTIME_TEST(forward_skips_local_events) {
    Level level=make_level(3U);
    QueenWorld& world=*level.world;
    ObjectiveConfig objective_config;
    objective_config.game_kind=3U;
    for (std::size_t i=0U; i<4U; ++i) {
        objective_config.owners[i]=ObjectiveOwner{.present=true,.computer=false,
                                                  .team=static_cast<std::uint8_t>(i%2U),.bases=0U};
    }
    seed_objective_bases(world,objective_config);
    ObjectiveCreateResult created=ObjectiveRuntime::create(objective_config);
    REQUIRE(world.touch_queen(level.queens[1],contact(unit_a,0U),0)==Status::applied);
    REQUIRE(world.carrier_destroyed(unit_a,0.0F,0.0F,0)==Status::applied);
    const ForwardSummary summary=forward_events(world,*created.runtime);
    REQUIRE(summary.forwarded==1U && summary.local_only==1U);
    REQUIRE(created.runtime->team_queens(1U)==1U);
    return true;
}

RUNTIME_TEST(serialize_is_deterministic) {
    Level first=make_level(3U);
    Level second=make_level(3U);
    REQUIRE(first.world->touch_queen(first.queens[0],contact(unit_b,1U),3)==Status::applied);
    REQUIRE(second.world->touch_queen(second.queens[0],contact(unit_b,1U),3)==Status::applied);
    StateWriter a;
    StateWriter b;
    first.world->serialize(a);
    second.world->serialize(b);
    REQUIRE(a.bytes()==b.bytes());
    REQUIRE(second.world->carrier_destroyed(unit_b,0.0F,0.0F,4)==Status::applied);
    StateWriter c;
    second.world->serialize(c);
    REQUIRE(a.bytes()!=c.bytes());
    return true;
}

RUNTIME_TEST(objective_call_mapping) {
    QueenEvent event;
    event.kind=QueenEventKind::taken_from_base;
    event.holder_team=2U;
    event.team=1U;
    ObjectiveCall call=objective_call(event);
    REQUIRE(call.entry==ObjectiveEntry::queen_taken && call.first==2U && call.second==1U);
    event.kind=QueenEventKind::returned;
    REQUIRE(objective_call(event).entry==ObjectiveEntry::queen_stored && objective_call(event).first==1U);
    event.kind=QueenEventKind::rescue_contact;
    event.owner=0U;
    REQUIRE(objective_call(event).entry==ObjectiveEntry::queen_contact);
    event.kind=QueenEventKind::marker_reached;
    REQUIRE(objective_call(event).entry==ObjectiveEntry::marker_contact);
    event.kind=QueenEventKind::dropped;
    REQUIRE(objective_call(event).entry==ObjectiveEntry::none);
    event.kind=QueenEventKind::picked_up;
    REQUIRE(objective_call(event).entry==ObjectiveEntry::none);
    return true;
}

RUNTIME_TEST(marker_wave_bank_entry_to_marker_contact) {
    // Waves 1/3/9/13 shape: no type 11..14 object, one bank type-24 entry.
    CreateResult created=QueenWorld::create(rescue_wave_config());
    REQUIRE(created.world.has_value());
    QueenWorld& world=*created.world;
    REQUIRE(world.place_bank_entry(MarkerBankEntry{.kind=23U,.x=1.0F,.z=2.0F,.param=600U},1U).outcome==
            PlaceOutcome::not_marker_kind);
    const MarkerPlaceResult placed=
        world.place_bank_entry(MarkerBankEntry{.kind=bank_entry_kind_marker,.x=-3941.0F,.z=-154.0F,.param=600U},1U);
    REQUIRE(placed.outcome==PlaceOutcome::placed);
    world.finish_placement();
    REQUIRE(world.total_bases()==0U && !world.has_base(0U) && !world.has_base(1U));
    MarkerView view;
    REQUIRE(world.view(placed.marker,view) && view.x==-3941.0F && view.z==-154.0F && view.extent==600);
    // Computer-controlled owner 1 touching it does nothing.
    REQUIRE(world.touch_marker(placed.marker,contact(unit_b,1U),5)==Status::ignored);
    REQUIRE(world.touch_marker(placed.marker,contact(unit_a,0U),6)==Status::applied);
    REQUIRE(world.pending_events()==1U && world.event(0U).kind==QueenEventKind::marker_reached);
    const ObjectiveCall call=objective_call(world.event(0U));
    REQUIRE(call.entry==ObjectiveEntry::marker_contact && call.first==0U);
    ObjectiveCreateResult objective=ObjectiveRuntime::create(rescue_objective_config(world));
    REQUIRE(objective.runtime.has_value());
    const ForwardSummary summary=forward_events(world,*objective.runtime);
    REQUIRE(summary.forwarded==1U && summary.applied==1U);
    REQUIRE(objective.runtime->result(0U)==OwnerResult::won);
    // The marker stays live; a second contact reports again.
    REQUIRE(world.touch_marker(placed.marker,contact(unit_a,0U),7)==Status::applied);
    return true;
}

RUNTIME_TEST(marker_wave_multi_view_has_no_marker) {
    // view count != 1: no effect 31. Unreachable in the campaign (view count 1);
    // retail would then only end by a team-0 loss.
    CreateResult created=QueenWorld::create(rescue_wave_config());
    QueenWorld& world=*created.world;
    const MarkerPlaceResult placed=
        world.place_bank_entry(MarkerBankEntry{.kind=bank_entry_kind_marker,.x=-6389.0F,.z=0.0F,.param=600U},2U);
    REQUIRE(placed.outcome==PlaceOutcome::split_screen && !placed.marker.valid());
    return true;
}

RUNTIME_TEST(marker_extent_is_signed_param) {
    REQUIRE(marker_extent(600U)==600);
    REQUIRE(marker_extent(0xFFFFU)==-1);
    return true;
}

RUNTIME_TEST(wave5_single_queen_rescue) {
    // Wave 5 shape: one placed type-12 object -> owner slot_rank[1] = 1.
    CreateResult created=QueenWorld::create(rescue_wave_config());
    QueenWorld& world=*created.world;
    REQUIRE(world.place_level_object(LevelObject{.kind=11U,.active=false,.x=0.0F,.z=0.0F}).outcome==PlaceOutcome::inactive);
    const PlaceResult placed=
        world.place_level_object(LevelObject{.kind=12U,.active=true,.x=-4176.0F,.z=2303.0F});
    REQUIRE(placed.outcome==PlaceOutcome::placed && placed.owner==1U && placed.queen.valid());
    world.finish_placement();
    REQUIRE(world.total_bases()==1U && world.team_count(1U)==1U && world.team_count(0U)==0U);
    ObjectiveCreateResult objective=ObjectiveRuntime::create(rescue_objective_config(world));
    REQUIRE(objective.runtime.has_value() && objective.runtime->total_bases()==1U);
    // Computer-controlled owner 1 on its own team: no event.
    REQUIRE(world.touch_queen(placed.queen,contact(unit_b,1U),1)==Status::ignored);
    REQUIRE(world.touch_queen(placed.queen,contact(unit_a,0U),2)==Status::applied);
    REQUIRE(world.pending_events()==2U);
    REQUIRE(world.event(0U).kind==QueenEventKind::taken_from_base && world.event(0U).holder_team==1U);
    REQUIRE(world.event(1U).kind==QueenEventKind::rescue_contact);
    REQUIRE(objective_call(world.event(1U)).entry==ObjectiveEntry::queen_contact);
    const ForwardSummary summary=forward_events(world,*objective.runtime);
    REQUIRE(summary.forwarded==2U && summary.applied==2U);
    REQUIRE(objective.runtime->result(0U)==OwnerResult::won);
    return true;
}

RUNTIME_TEST_MAIN()
