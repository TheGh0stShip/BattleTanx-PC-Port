// Cross-module objective/world scenarios driven only through public headers:
// queen_world -> ObjectiveRuntime, spawner_runtime, deployables -> effects,
// deployables -> squad adapter -> placed::World, and the pickup distributors.
// Every input is synthetic; no retail data is read.

#include "battletanx/deployables_runtime.hpp"
#include "battletanx/effect_runtime.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/objective_runtime.hpp"
#include "battletanx/pickup_runtime.hpp"
#include "battletanx/placed_objects.hpp"
#include "battletanx/powerup_state.hpp"
#include "battletanx/queen_world.hpp"
#include "battletanx/runtime_math.hpp"
#include "battletanx/spawner_runtime.hpp"
#include "battletanx/squad_units.hpp"
#include "battletanx/state_hash.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace battletanx;
namespace qw=battletanx::queen_world;
namespace sr=battletanx::spawner_runtime;
namespace dep=battletanx::deployables;
namespace fx=battletanx::effects;

namespace {

constexpr EntityId player_unit{1U,1U};     // owner 0, team 0
constexpr EntityId computer_unit{2U,1U};   // owner 1, team 1

qw::UnitContact touch(EntityId unit,std::uint8_t owner) {
    return qw::UnitContact{.unit=unit,.owner=owner,.event_code=0U,.contact_kind=0U,.collector_blocked=false};
}

ObjectiveInput live_input(std::int32_t tick) {
    ObjectiveInput input;
    input.match=MatchState::live;
    input.tick=tick;
    input.squad_alive={true,true,false,false};
    return input;
}

qw::Config two_owner_config(std::uint32_t game_kind) {
    qw::Config config;
    config.game_kind=game_kind;
    config.owner_count=2U;
    config.owners[0]=qw::OwnerInfo{.present=true,.team=0U};
    config.owners[1]=qw::OwnerInfo{.present=true,.team=1U};
    return config;
}

ObjectiveConfig two_owner_objective(std::uint32_t game_kind,const qw::QueenWorld& world) {
    ObjectiveConfig config;
    config.game_kind=game_kind;
    config.owners[0]=ObjectiveOwner{.present=true,.computer=false,.team=0U,.bases=0U};
    config.owners[1]=ObjectiveOwner{.present=true,.computer=true,.team=1U,.bases=0U};
    qw::seed_objective_bases(world,config);
    return config;
}

// Queen events seen by the scenario, in forwarding order.
struct EventLog {
    std::vector<qw::QueenEventKind> kinds;
    std::vector<std::uint32_t> sequences;
    std::vector<ObjectiveEventStatus> statuses;
    std::uint32_t local_only{};
};

// Records the pending queen events, forwards them, and checks the summary.
bool drain(qw::QueenWorld& world,ObjectiveRuntime& objective,EventLog& log) {
    const std::size_t pending=world.pending_events();
    for (std::size_t i=0U; i<pending; ++i) {
        log.kinds.push_back(world.event(i).kind);
        log.sequences.push_back(world.event(i).sequence);
    }
    const qw::ForwardSummary summary=qw::forward_events(world,objective);
    REQUIRE(static_cast<std::size_t>(summary.forwarded+summary.local_only)==pending);
    REQUIRE(world.pending_events()==0U);
    for (std::uint32_t i=0U; i<summary.forwarded; ++i) log.statuses.push_back(summary.statuses[i]);
    log.local_only+=summary.local_only;
    return true;
}

bool counters_agree(const qw::QueenWorld& world,const ObjectiveRuntime& objective) {
    for (std::size_t team=0U; team<owner_capacity; ++team) {
        REQUIRE(world.team_count(team)==objective.team_queens(team));
    }
    REQUIRE(world.total_bases()==objective.total_bases());
    return true;
}

// ---- 1. Kind 3 capture ------------------------------------------------------
bool kind3_capture(StateWriter& out) {
    qw::CreateResult created=qw::QueenWorld::create(two_owner_config(3U));
    REQUIRE(created.world.has_value() && created.error==qw::ConfigError::none);
    qw::QueenWorld& world=*created.world;
    const qw::PlaceResult home0=world.place_level_object(qw::LevelObject{.kind=11U,.active=true,.x=0.0F,.z=0.0F});
    const qw::PlaceResult home1=world.place_level_object(qw::LevelObject{.kind=12U,.active=true,.x=1000.0F,.z=0.0F});
    REQUIRE(home0.outcome==qw::PlaceOutcome::placed && home0.owner==0U && home0.queen.valid());
    REQUIRE(home1.outcome==qw::PlaceOutcome::placed && home1.owner==1U && home1.queen.valid());
    world.finish_placement();
    REQUIRE(world.total_bases()==2U && world.team_count(0U)==1U && world.team_count(1U)==1U);

    ObjectiveCreateResult made=ObjectiveRuntime::create(two_owner_objective(3U,world));
    REQUIRE(made.runtime.has_value() && made.error==ObjectiveConfigError::none);
    ObjectiveRuntime& objective=*made.runtime;
    REQUIRE(objective.mechanism()==ObjectiveMechanism::queen_capture);
    REQUIRE(counters_agree(world,objective));
    EventLog log;

    // Tick 10: the computer-controlled owner steals owner 0's queen.
    REQUIRE(world.touch_queen(home0.queen,touch(computer_unit,1U),10)==qw::Status::applied);
    REQUIRE(drain(world,objective,log));
    REQUIRE(counters_agree(world,objective));
    REQUIRE(objective.team_queens(0U)==0U && objective.team_queens(1U)==1U);
    ObjectiveResult result=objective.step(live_input(10));
    REQUIRE(result.status==ObjectiveStatus::ongoing && !result.decided_this_step);

    // Tick 20: the carrier dies; the queen lies on the ground for 0x78 ticks.
    REQUIRE(world.carrier_destroyed(computer_unit,500.0F,40.0F,20)==qw::Status::applied);
    qw::QueenView view;
    REQUIRE(world.view(home0.queen,view) && view.state==qw::QueenState::dropped);
    REQUIRE(view.expire_tick==20+qw::drop_return_ticks && view.slot==0U);
    REQUIRE(view.x==500.0F+qw::slot_offsets[0].dx && view.z==40.0F+qw::slot_offsets[0].dz);
    REQUIRE(drain(world,objective,log));
    REQUIRE(log.local_only==1U && counters_agree(world,objective));

    // Return: strictly after the expire tick.
    REQUIRE(world.step(20+qw::drop_return_ticks)==qw::Status::ignored);
    REQUIRE(world.pending_events()==0U);
    REQUIRE(world.step(21+qw::drop_return_ticks)==qw::Status::applied);
    REQUIRE(world.pending_events()==1U);
    REQUIRE(world.event(0U).kind==qw::QueenEventKind::returned);
    REQUIRE(world.event(0U).reason==qw::ReturnReason::drop_expired);
    REQUIRE(world.event(0U).team==0U && world.event(0U).progress==0);
    REQUIRE(world.view(home0.queen,view) && view.state==qw::QueenState::stored && view.holder_owner==0U);
    REQUIRE(drain(world,objective,log));
    REQUIRE(counters_agree(world,objective));
    REQUIRE(objective.team_queens(0U)==1U && objective.team_queens(1U)==1U);
    result=objective.step(live_input(141));
    REQUIRE(result.status==ObjectiveStatus::ongoing);

    // Tick 200: re-steal; tick 210: store at owner 1's base (slot 1).
    REQUIRE(world.touch_queen(home0.queen,touch(computer_unit,1U),200)==qw::Status::applied);
    REQUIRE(world.touch_base(home1.base,touch(computer_unit,1U),210)==qw::Status::applied);
    REQUIRE(world.pending_events()==2U);
    REQUIRE(world.event(1U).kind==qw::QueenEventKind::stored);
    REQUIRE(world.event(1U).progress==qw::store_progress && world.event(1U).previous_home==0U);
    REQUIRE(world.event(1U).slot==1U);
    REQUIRE(world.view(home0.queen,view) && view.holder_owner==1U && view.home_owner==1U);
    REQUIRE(view.x==1000.0F+qw::slot_offsets[1].dx && view.z==qw::slot_offsets[1].dz);
    REQUIRE(world.matching_team()==1U);
    REQUIRE(drain(world,objective,log));
    REQUIRE(counters_agree(world,objective));
    REQUIRE(objective.team_queens(1U)==2U && objective.team_queens(0U)==0U);

    result=objective.step(live_input(210));
    REQUIRE(result.decided_this_step && result.next_state==MatchState::result_banner);
    REQUIRE(result.status==ObjectiveStatus::failed && objective.status()==ObjectiveStatus::failed);
    REQUIRE(objective.phase()==ObjectivePhase::finished && objective.capture_reached());
    REQUIRE(objective.result(0U)==OwnerResult::eliminated && objective.result(1U)==OwnerResult::won);
    REQUIRE(objective.decided_tick()==210);
    // Finished: later events are frozen.
    REQUIRE(objective.on_queen_stored(0U)==ObjectiveEventStatus::frozen);

    const std::vector<qw::QueenEventKind> expected{
        qw::QueenEventKind::taken_from_base,qw::QueenEventKind::dropped,qw::QueenEventKind::returned,
        qw::QueenEventKind::taken_from_base,qw::QueenEventKind::stored};
    REQUIRE(log.kinds==expected);
    for (std::size_t i=1U; i<log.sequences.size(); ++i) REQUIRE(log.sequences[i]==log.sequences[i-1U]+1U);
    REQUIRE(log.statuses.size()==4U);
    for (ObjectiveEventStatus status : log.statuses) REQUIRE(status==ObjectiveEventStatus::applied);

    world.serialize(out);
    objective.serialize(out);
    return true;
}

// ---- 2. Kind 4 marker wave --------------------------------------------------
bool kind4_marker_wave(StateWriter& out) {
    qw::CreateResult created=qw::QueenWorld::create(two_owner_config(qw::rescue_game_kind));
    REQUIRE(created.world.has_value());
    qw::QueenWorld& world=*created.world;
    const qw::MarkerBankEntry entry{.kind=qw::bank_entry_kind_marker,.x=-1200.0F,.z=300.0F,.param=600U};
    REQUIRE(qw::marker_extent(entry.param)==600);
    const qw::MarkerPlaceResult placed=world.place_bank_entry(entry,1U);
    REQUIRE(placed.outcome==qw::PlaceOutcome::placed && placed.marker.valid());
    world.finish_placement();
    REQUIRE(world.total_bases()==0U);
    qw::MarkerView marker;
    REQUIRE(world.view(placed.marker,marker) && marker.extent==600);

    ObjectiveCreateResult made=ObjectiveRuntime::create(two_owner_objective(qw::rescue_game_kind,world));
    REQUIRE(made.runtime.has_value());
    ObjectiveRuntime& objective=*made.runtime;
    REQUIRE(objective.mechanism()==ObjectiveMechanism::queen_rescue);
    EventLog log;

    // Owner 1 touching the marker is ignored and produces nothing.
    REQUIRE(world.touch_marker(placed.marker,touch(computer_unit,1U),5)==qw::Status::ignored);
    REQUIRE(world.pending_events()==0U);
    REQUIRE(drain(world,objective,log));
    ObjectiveResult result=objective.step(live_input(5));
    REQUIRE(result.status==ObjectiveStatus::ongoing && objective.result(0U)==OwnerResult::playing);

    // Owner 0 wins on contact.
    REQUIRE(world.touch_marker(placed.marker,touch(player_unit,0U),6)==qw::Status::applied);
    REQUIRE(world.pending_events()==1U && world.event(0U).kind==qw::QueenEventKind::marker_reached);
    REQUIRE(qw::objective_call(world.event(0U)).entry==qw::ObjectiveEntry::marker_contact);
    REQUIRE(drain(world,objective,log));
    REQUIRE(log.statuses.size()==1U && log.statuses[0]==ObjectiveEventStatus::applied);
    REQUIRE(objective.result(0U)==OwnerResult::won);
    result=objective.step(live_input(6));
    REQUIRE(result.decided_this_step && result.next_state==MatchState::result_banner);
    REQUIRE(result.status==ObjectiveStatus::succeeded && objective.phase()==ObjectivePhase::finished);

    world.serialize(out);
    objective.serialize(out);
    return true;
}

// ---- 3. Kind 4 wave-5 shape -------------------------------------------------
bool kind4_wave5_rescue(StateWriter& out) {
    qw::CreateResult created=qw::QueenWorld::create(two_owner_config(qw::rescue_game_kind));
    REQUIRE(created.world.has_value());
    qw::QueenWorld& world=*created.world;
    const qw::PlaceResult placed=world.place_level_object(qw::LevelObject{.kind=12U,.active=true,.x=-400.0F,.z=230.0F});
    REQUIRE(placed.outcome==qw::PlaceOutcome::placed && placed.owner==1U && placed.queen.valid());
    world.finish_placement();
    REQUIRE(world.total_bases()==1U && world.team_count(1U)==1U && world.base_of(1U)==placed.base);

    ObjectiveCreateResult made=ObjectiveRuntime::create(two_owner_objective(qw::rescue_game_kind,world));
    REQUIRE(made.runtime.has_value() && made.runtime->total_bases()==1U);
    ObjectiveRuntime& objective=*made.runtime;
    EventLog log;

    REQUIRE(world.touch_queen(placed.queen,touch(computer_unit,1U),1)==qw::Status::ignored);
    REQUIRE(objective.step(live_input(1)).status==ObjectiveStatus::ongoing);
    REQUIRE(world.touch_queen(placed.queen,touch(player_unit,0U),2)==qw::Status::applied);
    qw::QueenView view;
    REQUIRE(world.view(placed.queen,view) && view.state==qw::QueenState::rescued && view.carrier==player_unit);
    REQUIRE(drain(world,objective,log));
    const std::vector<qw::QueenEventKind> expected{qw::QueenEventKind::taken_from_base,
                                                   qw::QueenEventKind::rescue_contact};
    REQUIRE(log.kinds==expected);
    REQUIRE(log.statuses.size()==2U);
    REQUIRE(log.statuses[0]==ObjectiveEventStatus::applied && log.statuses[1]==ObjectiveEventStatus::applied);
    REQUIRE(objective.team_queens(1U)==0U && objective.result(0U)==OwnerResult::won);
    const ObjectiveResult result=objective.step(live_input(2));
    REQUIRE(result.decided_this_step && result.status==ObjectiveStatus::succeeded);

    world.serialize(out);
    objective.serialize(out);
    return true;
}

// ---- 4. Spawner: mode 1 bike, interval 30 -----------------------------------
bool spawner_bike(StateWriter& out) {
    // owner 1 | mode 1 << 2 | count 1 << 4; fast interval step 30.
    const std::uint16_t id=static_cast<std::uint16_t>(1U|(1U<<2U)|(1U<<4U));
    SpawnerDefinition definition=unpack_spawner(id,0U,true);
    REQUIRE(definition.owner==1U && definition.mode==1U && definition.shape==1U);
    REQUIRE(definition.interval_ticks==sr::fast_interval_step);
    definition.x=-250.0F;
    definition.z=640.0F;
    definition.heading=0x2000U;

    sr::SpawnerSet set;
    REQUIRE(!set.create(definition,3,std::nullopt).has_value());
    REQUIRE(set.spawn_counts()[1]==1U && set.slots().size()==1U);

    sr::OwnerSpawnRecords owners{};
    for (std::size_t o=0U; o<owners.size(); ++o) {
        for (std::size_t s=0U; s<sr::member_slots; ++s) {
            owners[o].unit_ids[s]=static_cast<std::uint16_t>(100U+o*10U+s);
        }
        owners[o].gate_open=true;
    }
    owners[1].budget=2;

    RetailRandom rng{0U};
    sr::SpawnOrderQueue queue;
    const sr::AreaQuery clear=[](const sr::AreaBox&) { return sr::AreaQueryResult{true,{}}; };
    const std::function<std::optional<LinkedEffectView>(std::size_t)> no_links=
        [](std::size_t) { return std::optional<LinkedEffectView>{}; };

    std::uint32_t draws=0U;
    std::vector<std::int32_t> spawn_ticks;
    std::int32_t capped_tick=-1;
    std::int32_t full_tick=-1;
    constexpr std::int32_t tick_limit=200000;
    for (std::int32_t tick=1; tick<=tick_limit && full_tick<0; ++tick) {
        const std::vector<sr::SpawnerTickResult> results=set.tick(3,tick,owners,rng,clear,no_links,queue);
        REQUIRE(results.size()==1U);
        switch (results[0].status) {
        case sr::SpawnerTickStatus::interval_pending:
            break;
        case sr::SpawnerTickStatus::roll_failed:
            ++draws;
            break;
        case sr::SpawnerTickStatus::spawned:
            ++draws;
            spawn_ticks.push_back(tick);
            break;
        case sr::SpawnerTickStatus::no_budget:
            ++draws;
            if (capped_tick<0) {
                capped_tick=tick;
                REQUIRE(owners[1].budget==0 && queue.orders().size()==2U);
                owners[1].budget=-1;   // negative: unlimited, never decremented
            }
            break;
        case sr::SpawnerTickStatus::no_free_member:
            ++draws;
            full_tick=tick;
            break;
        default:
            REQUIRE(false);
        }
    }
    // Interval: last_tick 0, 0 + 30 < tick first holds at 31; seed 0 rolls &0xF == 0 there.
    REQUIRE(!spawn_ticks.empty() && spawn_ticks[0]==31);
    REQUIRE(capped_tick>0 && full_tick>0);
    REQUIRE(spawn_ticks.size()==sr::member_slots);
    REQUIRE(spawn_ticks[1]>spawn_ticks[0]+sr::fast_interval_step);
    REQUIRE(capped_tick>spawn_ticks[1]+sr::fast_interval_step && spawn_ticks[2]>capped_tick);
    REQUIRE(owners[1].budget==-1);
    for (bool alive : owners[1].unit_active) REQUIRE(alive);
    // no_free_member leaves the timer untouched.
    REQUIRE(set.slots()[0].state.last_tick==spawn_ticks.back());

    // Exactly one draw per tick that passed the gate and interval (mode 1: no shape bit).
    RetailRandom mirror{0U};
    for (std::uint32_t i=0U; i<draws; ++i) static_cast<void>(mirror.next_u32());
    REQUIRE(mirror.seed()==rng.seed());

    const std::vector<sr::SpawnOrder>& orders=queue.orders();
    REQUIRE(orders.size()==sr::member_slots);
    for (std::size_t i=0U; i<orders.size(); ++i) {
        const sr::SpawnOrder& order=orders[i];
        REQUIRE(order.sequence==i);
        REQUIRE(order.source==sr::OrderSource::spawner);
        REQUIRE(order.owner==1U && static_cast<std::size_t>(order.member_slot)==i);
        REQUIRE(static_cast<std::size_t>(order.unit_id)==110U+i);
        REQUIRE(order.kind==sr::UnitKind::bike);
        REQUIRE(order.x==-250.0F && order.z==640.0F && order.heading==0x2000U);
        REQUIRE(order.parameter==0U);
        REQUIRE(order.flags==sr::order_flags(3,1U,false,true) && order.flags==sr::order_flag_force);
        REQUIRE(order.tick==spawn_ticks[i]);
    }

    set.serialize(out);
    queue.serialize(out);
    for (const sr::OwnerSpawnRecord& owner : owners) owner.serialize(out);
    out.u32(rng.seed());
    return true;
}

// ---- 5. Powered weapon-12 mine grid -> effect runtime -----------------------
struct DeployFixture {
    dep::DeployablesState state;
    dep::DeployableEventQueue events{4096U};
    RetailRandom random{0x1234U};
    dep::DeployablesWorld world;
    std::uint32_t splash_calls{};

    DeployFixture() {
        world.splash_units=[this](float,float,float radius,std::int32_t amount,std::uint8_t,EntityId) {
            if (radius==dep::mine_splash_radius && amount==dep::mine_splash_amount) ++splash_calls;
            return std::size_t{0U};
        };
        world.unit_team=[](std::int16_t) { return std::uint8_t{3U}; };
    }
    dep::DeployablesContext context(std::int32_t tick) {
        return dep::DeployablesContext{.state=state,.events=events,.random=random,.world=world,.tick=tick};
    }
};

dep::DeployerUnit powered_tank() {
    return dep::DeployerUnit{.id=4,.unit_kind=0U,.team=2U,.powered=true,.x=1000.0F,.z=2000.0F,
                             .hull_forward_x=0.0F,.hull_forward_z=1.0F,.heading=0x1000U,
                             .heading_degrees=std::nullopt};
}

bool in_walked_list(const fx::EffectRuntime& effects,std::int16_t slot) {
    std::int16_t i=effects.walked_head();
    for (std::uint16_t guard=0U; i!=fx::no_link && guard<fx::pool_capacity; ++guard) {
        if (i==slot) return true;
        i=effects.node(i).next;
    }
    return false;
}

std::uint16_t effect_clock_at(std::int32_t tick) {
    return fx::effect_clock::for_game_tick(static_cast<std::uint32_t>(tick));
}

bool mine_grid_chain(StateWriter& out) {
    DeployFixture f;
    auto effects=std::make_unique<fx::EffectRuntime>();

    std::int32_t tick=1;
    {
        auto ctx=f.context(tick);
        const dep::DeployResult fired=dep::fire_mine_weapon(ctx,powered_tank());
        REQUIRE(fired.status==dep::DeployStatus::placed && fired.consume_whole_stock);
    }
    // One cell per pass, 25 passes.
    for (int pass=0; pass<25; ++pass) {
        auto ctx=f.context(++tick);
        dep::update_deployables(ctx);
    }
    REQUIRE(f.state.grids.size()==0U);
    REQUIRE(f.state.mines.size()==25U);

    // The first cell (slot 0) was placed on the first grid pass.
    const EntityId first=f.state.mines.slot_id(0U);
    REQUIRE(first.valid());
    const dep::MineRecord& first_mine=*f.state.mines.get(first);
    REQUIRE(first_mine.placed_tick==2 && first_mine.tag==4);
    const float first_x=first_mine.x;
    const float first_z=first_mine.z;
    REQUIRE(!dep::mine_armed(first_mine,2+dep::mine_arm_ticks));
    REQUIRE(dep::mine_armed(first_mine,3+dep::mine_arm_ticks));

    // A fresh untagged mine 50 units away guarantees one splash-chain link
    // (tag -1 skips the placement area check; unarmed, but message 7 wakes it).
    {
        auto ctx=f.context(61);
        REQUIRE(dep::place_mine(ctx,first_x+50.0F,first_z,dep::no_tag).has_value());
    }

    // Arming boundary: placed 2, armed when 2 + 60 < tick.
    {
        auto ctx=f.context(62);
        REQUIRE(dep::contact_mine(ctx,first,dep::ContactSender::unit).report==dep::ContactReport::ordinary);
        REQUIRE(!f.state.mines.get(first)->awake);
    }
    {
        auto ctx=f.context(63);
        const dep::ContactResult hit=dep::contact_mine(ctx,first,dep::ContactSender::unit);
        REQUIRE(hit.report==dep::ContactReport::damage);
        REQUIRE(hit.amount==dep::mine_contact_amount && hit.attacker==4);
        REQUIRE(f.state.mines.get(first)->awake);
    }

    std::size_t seen=f.events.events().size();
    std::vector<EntityId> detonated;
    std::vector<std::int32_t> detonate_ticks;
    std::vector<std::pair<float,float>> detonate_positions;
    std::vector<std::pair<float,float>> splashes;
    std::vector<std::int32_t> splash_ticks;
    std::vector<std::uint32_t> serials;
    for (tick=64; tick<1000; ++tick) {
        auto ctx=f.context(tick);
        dep::update_deployables(ctx);
        const std::span<const dep::DeployableEvent> events=f.events.events();
        std::vector<std::int16_t> spawned_now;
        std::int32_t last_slot=-1;
        for (std::size_t i=seen; i<events.size(); ++i) {
            const dep::DeployableEvent& event=events[i];
            REQUIRE(event.tick==tick);
            if (event.kind==dep::DeployableEventKind::mine_detonating) {
                // Mines update in slot order within a pass.
                REQUIRE(static_cast<std::int32_t>(event.subject.slot)>last_slot);
                last_slot=static_cast<std::int32_t>(event.subject.slot);
                REQUIRE(event.variant==dep::mine_detonate_variant && event.sound==dep::sound_mine_detonate);
                detonated.push_back(event.subject);
                detonate_ticks.push_back(tick);
                detonate_positions.emplace_back(event.x,event.z);
                // Effect runtime: the kind-0x26 explosion is queued, not walked yet.
                const fx::SpawnResult spawned=effects->spawn_script(event.variant,event.x,0.0F,event.z,
                                                                    effect_clock_at(tick));
                REQUIRE(spawned.status==fx::SpawnStatus::spawned);
                const fx::EffectNode& node=effects->node(spawned.slot);
                REQUIRE(node.in_use && node.kind==fx::EffectKind::script && node.index==dep::mine_detonate_variant);
                REQUIRE(node.x==event.x && node.z==event.z);
                REQUIRE(!in_walked_list(*effects,spawned.slot));
                if (!serials.empty()) REQUIRE(node.serial>serials.back());
                serials.push_back(node.serial);
                spawned_now.push_back(spawned.slot);
            } else if (event.kind==dep::DeployableEventKind::mine_splash) {
                splashes.emplace_back(event.x,event.z);
                splash_ticks.push_back(tick);
            }
        }
        seen=events.size();
        effects->update(effect_clock_at(tick),static_cast<std::uint16_t>(tick&0xFFFF));
        // Merged and walked this update: still on the walked list or already retired.
        for (std::int16_t slot : spawned_now) {
            REQUIRE(in_walked_list(*effects,slot) || !effects->node(slot).in_use);
        }
    }
    REQUIRE(f.events.dropped()==0U);
    REQUIRE(effects->failed_spawns()==0U);

    // The contacted mine detonates on the next pass and splashes 8 ticks later.
    REQUIRE(detonated.size()>=2U && detonated[0]==first && detonate_ticks[0]==64);
    REQUIRE(!splashes.empty() && splash_ticks[0]==64+dep::mine_detonate_ticks+1);
    REQUIRE(splashes[0].first==first_x && splashes[0].second==first_z);
    REQUIRE(splashes.size()==detonated.size() && f.splash_calls==splashes.size());

    // Chain closure: each chained mine was woken by an earlier splash within
    // 108 units; every survivor lies outside every splash.
    const float radius_sq=dep::mine_splash_radius*dep::mine_splash_radius;
    std::size_t chained=0U;
    for (std::size_t i=1U; i<detonated.size(); ++i) {
        bool woken=false;
        for (std::size_t s=0U; s<splashes.size(); ++s) {
            if (splash_ticks[s]>=detonate_ticks[i]) continue;
            // Detonated mines are gone; positions come from their events.
            const float dx=detonate_positions[i].first-splashes[s].first;
            const float dz=detonate_positions[i].second-splashes[s].second;
            if (dx*dx+dz*dz<=radius_sq) woken=true;
        }
        REQUIRE(woken);
        ++chained;
    }
    REQUIRE(chained>=1U);
    bool closed=true;
    f.state.mines.for_each([&](EntityId,const dep::MineRecord& mine) {
        if (mine.awake || mine.wake_pending || mine.detonate_tick!=0) closed=false;
        for (const auto& splash : splashes) {
            const float dx=mine.x-splash.first;
            const float dz=mine.z-splash.second;
            if (dx*dx+dz*dz<=radius_sq) closed=false;
        }
    });
    REQUIRE(closed);
    REQUIRE(f.state.mines.size()==26U-detonated.size());

    f.state.serialize(out);
    effects->serialize(out);
    out.u32(static_cast<std::uint32_t>(serials.size()));
    for (std::uint32_t serial : serials) out.u32(serial);
    return true;
}

// ---- 6. Powered weapon-13 grid -> squad adapter -> placed::World ------------
struct SquadFixture {
    AngleTable angles{reference_angle_table()};
    RetailRandom turret_rng{11U};
    placed::World placed_world;
    placed::UpdateContext update{};
    squad::SquadAdapter adapter{placed_world,update};
    dep::DeployablesState state;
    dep::DeployableEventQueue events{1024U};
    RetailRandom deploy_rng{5U};
    dep::DeployablesWorld hooks;

    SquadFixture() {
        update.angles=&angles;
        update.rng=&turret_rng;
        update.area_clear=[](float,float,float) { return false; };   // grid requests skip it
        hooks.spawn_squad_unit=adapter.hook();
        hooks.unit_team=[](std::int16_t) { return std::uint8_t{3U}; };
    }
    dep::DeployablesContext context(std::int32_t tick) {
        return dep::DeployablesContext{.state=state,.events=events,.random=deploy_rng,.world=hooks,.tick=tick};
    }
};

bool buddy_grid(StateWriter& out) {
    SquadFixture f;
    std::int32_t tick=100;
    {
        auto ctx=f.context(tick);
        const dep::DeployResult fired=dep::fire_gun_buddy_weapon(ctx,powered_tank());
        REQUIRE(fired.status==dep::DeployStatus::placed && fired.consume_whole_stock);
    }
    std::vector<std::pair<float,float>> requested;
    std::size_t seen=f.events.events().size();
    for (int pass=0; pass<25; ++pass) {
        auto ctx=f.context(++tick);
        dep::update_deployables(ctx);
        const auto events=f.events.events();
        for (std::size_t i=seen; i<events.size(); ++i) {
            REQUIRE(events[i].kind!=dep::DeployableEventKind::buddy_failed);
            if (events[i].kind==dep::DeployableEventKind::buddy_requested) {
                REQUIRE(events[i].variant==dep::grid_buddy_squad_kind && events[i].tag==4);
                requested.emplace_back(events[i].x,events[i].z);
            }
        }
        seen=events.size();
    }
    REQUIRE(f.state.grids.size()==0U);
    REQUIRE(requested.size()==8U);
    const squad::SquadStats& stats=f.adapter.stats();
    REQUIRE(stats.requested==8U && stats.spawned==8U && stats.blocked==0U && stats.failed==0U);
    REQUIRE(f.placed_world.size()==8U);

    // Spawn events in request order, matching the request positions.
    std::size_t spawned=0U;
    for (const placed::Event& event : f.placed_world.events()) {
        if (event.kind!=placed::EventKind::spawned) continue;
        REQUIRE(spawned<requested.size());
        REQUIRE(event.owner==4U && event.value==static_cast<std::int32_t>(dep::grid_buddy_squad_kind));
        REQUIRE(event.x==requested[spawned].first && event.z==requested[spawned].second);
        const placed::Entry* entry=f.placed_world.get(event.entity);
        REQUIRE(entry!=nullptr && entry->kind==placed::Kind::turret);
        REQUIRE(entry->turret.type==dep::grid_buddy_squad_kind && entry->turret.team==3U);
        REQUIRE(entry->turret.owner==4U && entry->turret.heading==0x9000U);
        ++spawned;
    }
    REQUIRE(spawned==8U);

    f.adapter.serialize(out);
    f.placed_world.serialize(out);
    f.state.serialize(out);
    return true;
}

// ---- 7. Pickup distribution, cloak option off and on ------------------------
void write_placements(StateWriter& out,const std::vector<PickupPlacement>& placed) {
    out.u32(static_cast<std::uint32_t>(placed.size()));
    for (const PickupPlacement& p : placed) {
        out.u32(p.placement_index);
        out.f32(p.x);
        out.f32(p.z);
        out.u8(p.kind);
    }
}

bool pickup_distribution(StateWriter& out) {
    constexpr std::uint32_t spot_count=8U;
    std::vector<PickupPlacement> spots;
    for (std::uint32_t i=0U; i<spot_count; ++i) {
        spots.push_back(PickupPlacement{.placement_index=i,.x=100.0F*static_cast<float>(i),.z=-50.0F,
                                        .kind=random_pickup_spot_parameter});
    }
    LevelSpawnPlan plan;
    plan.random_distribution_allowed=true;
    plan.random_spots=spots;
    std::array<std::int32_t,pickup_spawn_count_kinds> counts{};
    counts[0]=1;    // never re-rolled
    counts[5]=2;    // re-roll kind
    counts[9]=1;    // re-roll kind
    const std::vector<std::uint8_t> raw_kinds{0U,5U,5U,9U};

    std::array<std::uint32_t,2> end_seeds{};
    for (int option=0; option<2; ++option) {
        const bool cloak_pickups=option!=0;   // D_80134E68 (AmmoRules::cloak_pickups)
        RetailRandom level_rng{0x5EEDU};
        RetailRandom powerup_rng{0x5EEDU};
        RetailRandom mirror{0x5EEDU};
        const std::vector<PickupPlacement> canonical=distribute_random_pickups(plan,counts,level_rng,cloak_pickups);
        const powerup::DistributionResult interleaved=powerup::distribute_pickups_interleaved(
            std::span<const PickupPlacement>(spots),counts,0,cloak_pickups,powerup_rng);
        REQUIRE(interleaved.status==powerup::DistributionStatus::placed && interleaved.total==4);
        REQUIRE(canonical.size()==raw_kinds.size() && interleaved.placed.size()==raw_kinds.size());

        // Retail order: per kind ascending, per copy: spot draws (re-drawn
        // while taken), then pick_effect_kind.
        std::array<bool,spot_count> used{};
        for (std::size_t i=0U; i<raw_kinds.size(); ++i) {
            std::uint32_t spot=0U;
            do { spot=mirror.next_u32()%spot_count; } while (used[spot]);
            used[spot]=true;
            const std::uint8_t kind=pick_effect_kind(raw_kinds[i],cloak_pickups,mirror);
            const PickupPlacement& a=canonical[i];
            const PickupPlacement& b=interleaved.placed[i];
            REQUIRE(a.placement_index==spot && b.placement_index==spot);
            REQUIRE(a.x==spots[spot].x && a.z==spots[spot].z && b.x==a.x && b.z==a.z);
            REQUIRE(a.kind==kind && b.kind==kind);
            if (!cloak_pickups || raw_kinds[i]==0U) REQUIRE(kind==raw_kinds[i]);
        }
        REQUIRE(level_rng.seed()==mirror.seed() && powerup_rng.seed()==mirror.seed());
        end_seeds[static_cast<std::size_t>(option)]=mirror.seed();
        write_placements(out,canonical);
    }
    // Campaign modes skip the distribution entirely.
    RetailRandom campaign_rng{0x5EEDU};
    REQUIRE(powerup::distribute_pickups_interleaved(std::span<const PickupPlacement>(spots),counts,4,true,
                                                    campaign_rng).status==powerup::DistributionStatus::campaign_mode);
    REQUIRE(campaign_rng.seed()==0x5EEDU);
    out.u32(end_seeds[0]);
    out.u32(end_seeds[1]);
    return true;
}

// ---- 8. Combined digest -----------------------------------------------------
bool run_all_scenarios(StateWriter& out) {
    REQUIRE(kind3_capture(out));
    REQUIRE(kind4_marker_wave(out));
    REQUIRE(kind4_wave5_rescue(out));
    REQUIRE(spawner_bike(out));
    REQUIRE(mine_grid_chain(out));
    REQUIRE(buddy_grid(out));
    REQUIRE(pickup_distribution(out));
    return true;
}

} // namespace

RUNTIME_TEST(kind3_capture_steal_drop_return_restore) {
    StateWriter out;
    return kind3_capture(out);
}

RUNTIME_TEST(kind4_marker_wave_owner0_wins_owner1_ignored) {
    StateWriter out;
    return kind4_marker_wave(out);
}

RUNTIME_TEST(kind4_wave5_single_queen_rescue) {
    StateWriter out;
    return kind4_wave5_rescue(out);
}

RUNTIME_TEST(spawner_bike_interval_budget_and_full_squad) {
    StateWriter out;
    return spawner_bike(out);
}

RUNTIME_TEST(powered_mine_grid_chain_feeds_effect_runtime) {
    StateWriter out;
    return mine_grid_chain(out);
}

RUNTIME_TEST(powered_buddy_grid_spawns_eight_squad_units) {
    StateWriter out;
    return buddy_grid(out);
}

RUNTIME_TEST(pickup_distribution_matches_level_spawns_with_and_without_cloak_option) {
    StateWriter out;
    return pickup_distribution(out);
}

RUNTIME_TEST(combined_scenario_digest_is_reproducible) {
    StateWriter first;
    StateWriter second;
    REQUIRE(run_all_scenarios(first));
    REQUIRE(run_all_scenarios(second));
    REQUIRE(!first.bytes().empty());
    REQUIRE(first.bytes()==second.bytes());
    REQUIRE(first.digest()==second.digest());
    REQUIRE(first.hex()==second.hex());
    return true;
}

RUNTIME_TEST_MAIN()
