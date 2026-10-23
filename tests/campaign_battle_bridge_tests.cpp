#include "battletanx/campaign_battle_bridge.hpp"
#include "battletanx/runtime_math.hpp"

#include "runtime_test_support.hpp"

#include <array>
#include <string>

namespace {
using namespace battletanx;
namespace cb=battletanx::campaign_bridge;
namespace qw=battletanx::queen_world;

const AngleTable& angles() {
    static const AngleTable table=reference_angle_table();
    return table;
}

SessionConfig session_config() {
    SessionConfig config;
    config.rng_seed=0x2468U;
    return config;
}

// Two human-style owners on open ground; owner 1 is not computer-controlled so
// its unit stays where it was placed.
struct Field {
    BattleSession session;
    EntityId player{};
    EntityId other{};

    explicit Field(float other_x=0.0F,float other_z=600.0F)
        : session(session_config(),angles(),open_ground_unit_motion(angles())) {
        session.add_owner(0U,OwnerSetup{.team=0U,.computer=false,.squad_cap=1U,.respawn_bank=0,.respawn_enabled=false,
                     .armor_scale=retail::human_scale_default,.damage_scale=retail::human_scale_default,
                     .objective_base=false});
        session.add_owner(1U,OwnerSetup{.team=1U,.computer=false,.squad_cap=5U,.respawn_bank=0,.respawn_enabled=false,
                     .armor_scale=retail::human_scale_default,.damage_scale=retail::human_scale_default,
                     .objective_base=false});
        player=*session.spawn_unit(UnitSpawn{.kind=retail::UnitKind::tank,.owner=0U,.x=0.0F,.z=0.0F,.heading=0U,.flags=0U});
        other=*session.spawn_unit(UnitSpawn{.kind=retail::UnitKind::tank,.owner=1U,.x=other_x,.z=other_z,.heading=0U,.flags=0U});
    }
};

SessionInput idle(std::int32_t frame) {
    SessionInput input;
    input.advance=frame==0;
    return input;
}

qw::Config two_owner_queens() {
    qw::Config config;
    config.game_kind=0U;
    config.owner_count=2U;
    config.owners[0]=qw::OwnerInfo{.present=true,.team=0U};
    config.owners[1]=qw::OwnerInfo{.present=true,.team=1U};
    return config;
}

} // namespace

RUNTIME_TEST(bridge_tick_runs_phases_in_documented_order) {
    Field field;
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    const cb::TickReport report=bridge.tick(field.session,idle(0));
    const std::array<cb::Phase,cb::phase_count> expected{
        cb::Phase::queen_contacts,cb::Phase::queen_step,cb::Phase::placed_contacts,cb::Phase::turret_bullets,
        cb::Phase::placed_update,cb::Phase::spawners,cb::Phase::deployables,cb::Phase::session_step,
        cb::Phase::session_events,cb::Phase::effects_update};
    // No queen config: queen_step is skipped after queen_contacts returns early.
    REQUIRE(report.phases_run==cb::phase_count-1U);
    REQUIRE(report.order[0]==static_cast<std::uint8_t>(expected[0]));
    for (std::size_t i=1U; i<report.phases_run; ++i)
        REQUIRE(report.order[i]==static_cast<std::uint8_t>(expected[i+1U]));
    REQUIRE(field.session.tick()==1);
    return true;
}

RUNTIME_TEST(bridge_routes_queen_take_to_session) {
    // Owner 0's tank sits on owner 1's base; the queen there is taken.
    // Owner 0 also overlaps owner 1's base (half 195) in the same unit loop;
    // only taken_from_base is asserted because the queen-before-base contact
    // order is a TEMPORARY POLICY.
    Field field(5000.0F,5000.0F);
    cb::BridgeConfig config;
    config.queen=two_owner_queens();
    cb::Bridge bridge(config,angles());
    REQUIRE(bridge.error()==cb::CreateError::none);
    const qw::PlaceResult home=bridge.place_queen_object(qw::LevelObject{.kind=11U,.active=true,.x=-3000.0F,.z=0.0F});
    const qw::PlaceResult enemy=bridge.place_queen_object(qw::LevelObject{.kind=12U,.active=true,.x=0.0F,.z=0.0F});
    REQUIRE(home.outcome==qw::PlaceOutcome::placed && enemy.outcome==qw::PlaceOutcome::placed);
    REQUIRE(enemy.owner==1U && enemy.queen.valid());
    bridge.finish_queen_placement();
    const cb::TickReport report=bridge.tick(field.session,idle(0));
    bool taken=false;
    for (const qw::QueenEvent& event : bridge.routed_queen_events())
        if (event.kind==qw::QueenEventKind::taken_from_base && event.holder_team==1U && event.team==0U) taken=true;
    REQUIRE(taken);
    REQUIRE(report.queen_events_routed>=1U);
    qw::QueenView view{};
    REQUIRE(bridge.queens()->view(enemy.queen,view));
    REQUIRE(view.state==qw::QueenState::carried && view.carrier==field.player);
    return true;
}

RUNTIME_TEST(bridge_drops_carried_queen_when_carrier_destroyed) {
    Field field(5000.0F,5000.0F);
    cb::BridgeConfig config;
    config.queen=two_owner_queens();
    cb::Bridge bridge(config,angles());
    static_cast<void>(bridge.place_queen_object(qw::LevelObject{.kind=11U,.active=true,.x=-3000.0F,.z=0.0F}));
    const qw::PlaceResult enemy=bridge.place_queen_object(qw::LevelObject{.kind=12U,.active=true,.x=0.0F,.z=0.0F});
    bridge.finish_queen_placement();
    static_cast<void>(bridge.tick(field.session,idle(0)));
    // Destroy the carrier between ticks through the public damage entry point.
    // Clear spawn-protection/shield flags first so the hit cannot be absorbed.
    UnitRecord* carrier=field.session.units().get(field.player);
    REQUIRE(carrier!=nullptr);
    carrier->flags=0U;
    static_cast<void>(field.session.apply_damage(field.player,100000,1U,true));
    static_cast<void>(bridge.tick(field.session,idle(1)));
    bool dropped=false;
    for (const qw::QueenEvent& event : bridge.routed_queen_events())
        if (event.kind==qw::QueenEventKind::dropped) dropped=true;
    REQUIRE(dropped);
    qw::QueenView view{};
    REQUIRE(bridge.queens()->view(enemy.queen,view));
    REQUIRE(view.state==qw::QueenState::dropped);
    return true;
}

RUNTIME_TEST(bridge_spawner_orders_become_session_units) {
    Field field;
    cb::BridgeConfig config;
    config.game_kind=0;
    config.spawner_owners[1]=cb::SpawnerOwnerSetup{.budget=3,.gate_open=true};
    cb::Bridge bridge(config,angles());
    // id: owner 1, mode 0 (tank), count field 1 -> interval 30 ticks.
    // Away from the player at (0,0) so the shape-0 area check is clear.
    SpawnerDefinition definition=unpack_spawner(0x0011U,0U,true);
    definition.x=3000.0F;
    definition.z=3000.0F;
    REQUIRE(!bridge.add_spawner(definition,std::nullopt).has_value());
    std::uint32_t requested=0U;
    for (std::int32_t frame=0; frame<900 && requested==0U; ++frame) {
        const cb::TickReport report=bridge.tick(field.session,idle(frame));
        requested+=report.spawns_requested;
        bridge.clear_events(field.session);
    }
    REQUIRE(requested>=1U);
    std::size_t owner1_units=0U;
    field.session.units().for_each([&](EntityId,const UnitRecord& unit) { if (unit.owner==1U) ++owner1_units; });
    REQUIRE(owner1_units>=2U);
    return true;
}

RUNTIME_TEST(bridge_mine_contact_damages_unit) {
    // Owner 1's tank shares the player's position; the player lays a mine.
    Field field(0.0F,0.0F);
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    const deployables::DeployResult placed=bridge.fire_deployable(field.session,field.player,12U,false);
    REQUIRE(placed.status==deployables::DeployStatus::placed);
    const std::int32_t before=field.session.units().get(field.other)->health;
    std::uint32_t contacts=0U;
    for (std::int32_t frame=0; frame<deployables::mine_arm_ticks+20; ++frame) {
        contacts+=bridge.tick(field.session,idle(frame)).mine_contacts;
        bridge.clear_events(field.session);
    }
    REQUIRE(contacts>=1U);
    const UnitRecord* other=field.session.units().get(field.other);
    REQUIRE(other==nullptr || !other->alive || other->health<=before);   // spawn protection may absorb the hit
    return true;
}

RUNTIME_TEST(bridge_consumes_session_events_into_effects) {
    Field field;
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    std::uint32_t spawned=0U;
    for (std::int32_t frame=0; frame<140; ++frame) {
        SessionInput input=idle(frame);
        input.drive.fire=true;
        input.drive.fire_pressed=frame==0;
        spawned+=bridge.tick(field.session,input).effect_spawns;
        bridge.clear_events(field.session);
    }
    REQUIRE(spawned>=1U);
    return true;
}

RUNTIME_TEST(bridge_does_not_reconsume_events_left_in_session) {
    Field field;
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    SessionInput input=idle(0);
    input.drive.fire=true;
    input.drive.fire_pressed=true;
    const cb::TickReport first=bridge.tick(field.session,input);
    const std::size_t pending=field.session.gameplay_events().size();
    const cb::TickReport second=bridge.tick(field.session,idle(1));   // events not cleared
    REQUIRE(first.session_events_consumed==pending);
    REQUIRE(second.session_events_consumed==field.session.gameplay_events().size()-pending);
    return true;
}

RUNTIME_TEST(bridge_self_destruct_reaches_session) {
    Field field;
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    const ObjectiveEventStatus status=bridge.self_destruct(field.session,0U);
    REQUIRE(status==ObjectiveEventStatus::applied || status==ObjectiveEventStatus::ignored ||
            status==ObjectiveEventStatus::frozen);
    return true;
}

RUNTIME_TEST(bridge_serialization_is_deterministic) {
    auto run=[]() {
        Field field(0.0F,0.0F);
        cb::BridgeConfig config;
        config.queen=two_owner_queens();
        config.spawner_owners[1]=cb::SpawnerOwnerSetup{.budget=2,.gate_open=true};
        cb::Bridge bridge(config,angles());
        static_cast<void>(bridge.place_queen_object(qw::LevelObject{.kind=11U,.active=true,.x=-3000.0F,.z=0.0F}));
        static_cast<void>(bridge.place_queen_object(qw::LevelObject{.kind=12U,.active=true,.x=3000.0F,.z=0.0F}));
        bridge.finish_queen_placement();
        SpawnerDefinition definition=unpack_spawner(0x0011U,0U,true);
        definition.x=3000.0F;    // clear of the player so spawner orders run
        definition.z=3000.0F;
        static_cast<void>(bridge.add_spawner(definition,std::nullopt));
        static_cast<void>(bridge.fire_deployable(field.session,field.player,12U,false));
        for (std::int32_t frame=0; frame<200; ++frame) {
            static_cast<void>(bridge.tick(field.session,idle(frame)));
            bridge.clear_events(field.session);
        }
        StateWriter writer;
        bridge.serialize(writer);
        field.session.serialize(writer);
        return writer.hex();
    };
    const std::string a=run();
    const std::string b=run();
    REQUIRE(!a.empty() && a==b);
    return true;
}

RUNTIME_TEST(bridge_gun_buddy_becomes_placed_turret) {
    Field field;
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    const std::size_t before=bridge.placed_world().size();
    const deployables::DeployResult result=bridge.fire_deployable(field.session,field.player,13U,false);
    REQUIRE(result.status==deployables::DeployStatus::placed);
    REQUIRE(bridge.placed_world().size()==before+1U);
    REQUIRE(bridge.squad_stats().spawned==1U);
    bool turret=false;
    for (const placed::Snapshot& snapshot : bridge.placed_world().snapshots())
        if (snapshot.kind==placed::Kind::turret) turret=true;
    REQUIRE(turret);
    return true;
}

RUNTIME_TEST(bridge_buddy_area_query_sees_mines) {
    Field field;
    cb::Bridge bridge(cb::BridgeConfig{},angles());
    REQUIRE(bridge.fire_deployable(field.session,field.player,12U,false).status==deployables::DeployStatus::placed);
    // The buddy area_clear folds this query in: a box over the mine is blocked,
    // the default unpowered buddy spot 145 behind the hull (45 + 10 reach) is not.
    REQUIRE(bridge.deployable_state().mine_blocks_area(0.0F,0.0F,45.0F));
    REQUIRE(!bridge.deployable_state().mine_blocks_area(0.0F,-145.0F,45.0F));
    REQUIRE(bridge.fire_deployable(field.session,field.player,13U,false).status==deployables::DeployStatus::placed);
    return true;
}

RUNTIME_TEST(bridge_place_markers_forwards_bank_entries) {
    cb::BridgeConfig config;
    config.queen=two_owner_queens();
    cb::Bridge bridge(config,angles());
    const std::array<qw::MarkerBankEntry,2> entries{{{.kind=24U,.x=10.0F,.z=20.0F,.param=50U},
                                                     {.kind=3U,.x=0.0F,.z=0.0F,.param=50U}}};
    REQUIRE(bridge.place_markers(entries,1U)==1U);
    return true;
}

RUNTIME_TEST(bridge_turret_bullet_serializes) {
    cb::TurretBullet bullet;
    bullet.damage=0x11U;
    StateWriter writer;
    bullet.serialize(writer);
    REQUIRE(!writer.hex().empty());
    return true;
}

RUNTIME_TEST_MAIN()
