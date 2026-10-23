#include "battletanx/deployables_runtime.hpp"

#include "runtime_test_support.hpp"

#include <vector>

using namespace battletanx;
using namespace battletanx::deployables;

namespace {

struct SplashCall {
    float x{}, z{}, radius{};
    std::int32_t amount{};
    std::uint8_t flag{};
};

struct Fixture {
    DeployablesState state;
    DeployableEventQueue events{1024U};
    RetailRandom random{0x1234U};
    DeployablesWorld world;
    std::vector<SplashCall> splashes;
    std::vector<SquadSpawnRequest> squads;
    bool squad_ok{true};

    Fixture() {
        world.splash_units=[this](float x,float z,float radius,std::int32_t amount,std::uint8_t flag,EntityId) {
            splashes.push_back(SplashCall{x,z,radius,amount,flag});
            return std::size_t{1U};
        };
        world.spawn_squad_unit=[this](const SquadSpawnRequest& request) {
            squads.push_back(request);
            return squad_ok;
        };
        world.unit_team=[](std::int16_t) { return std::uint8_t{2U}; };
    }
    DeployablesContext context(std::int32_t tick) {
        return DeployablesContext{.state=state,.events=events,.random=random,.world=world,.tick=tick};
    }
};

DeployerUnit tank_at(float x,float z) {
    DeployerUnit unit;
    unit.id=3;
    unit.unit_kind=0U;
    unit.team=1U;
    unit.x=x;
    unit.z=z;
    unit.hull_forward_x=0.0F;
    unit.hull_forward_z=1.0F;
    unit.heading=0U;
    return unit;
}

std::size_t count_events(const DeployableEventQueue& events,DeployableEventKind kind) {
    std::size_t n=0U;
    for (const DeployableEvent& event : events.events()) if (event.kind==kind) ++n;
    return n;
}

} // namespace

RUNTIME_TEST(mine_arms_after_sixty_ticks) {
    Fixture f;
    auto ctx=f.context(100);
    const DeployResult placed=fire_mine_weapon(ctx,tank_at(0.0F,0.0F));
    REQUIRE(placed.status==DeployStatus::placed);
    REQUIRE(!placed.return_before_bookkeeping());
    REQUIRE(count_events(f.events,DeployableEventKind::mine_placed)==1U);
    REQUIRE(f.events.events()[0].sound==sound_mine_placed);
    auto at160=f.context(160);
    REQUIRE(contact_mine(at160,placed.spawned,ContactSender::unit).report==ContactReport::ordinary);
    REQUIRE(!f.state.mines.get(placed.spawned)->awake);
    auto at161=f.context(161);
    const ContactResult hit=contact_mine(at161,placed.spawned,ContactSender::unit);
    REQUIRE(hit.report==ContactReport::damage);
    REQUIRE(hit.amount==30);
    REQUIRE(hit.attacker==3);
    REQUIRE(runtime_test::near(hit.wear,6.0));
    REQUIRE(f.state.mines.get(placed.spawned)->awake);
    return true;
}

RUNTIME_TEST(mine_triggers_on_owner_and_friendly_units) {
    // The handler has no owner or team test: the placing unit itself triggers.
    Fixture f;
    auto ctx=f.context(10);
    const auto id=place_mine(ctx,0.0F,0.0F,3);
    REQUIRE(id.has_value());
    auto later=f.context(200);
    REQUIRE(contact_mine(later,*id,ContactSender::unit).attacker==3);
    REQUIRE(f.state.mines.get(*id)->awake);
    return true;
}

RUNTIME_TEST(mine_contact_rules_by_sender) {
    Fixture f;
    auto ctx=f.context(1);
    const auto a=place_mine(ctx,0.0F,0.0F,1);
    REQUIRE(a.has_value());
    auto later=f.context(500);
    REQUIRE(contact_mine(later,*a,ContactSender::other).report==ContactReport::ordinary);
    REQUIRE(!f.state.mines.get(*a)->awake);
    // Another mine contact wakes it with the report untouched.
    REQUIRE(contact_mine(later,*a,ContactSender::mine).report==ContactReport::unchanged);
    REQUIRE(f.state.mines.get(*a)->awake);
    return true;
}

RUNTIME_TEST(mine_detonates_after_eight_ticks_and_splashes) {
    Fixture f;
    auto ctx=f.context(1);
    const auto id=place_mine(ctx,10.0F,20.0F,0x0105);
    REQUIRE(id.has_value());
    REQUIRE(wake_mine(ctx,*id));
    auto t=f.context(50);
    update_deployables(t);
    REQUIRE(f.state.mines.get(*id)->detonate_tick==50);
    REQUIRE(count_events(f.events,DeployableEventKind::mine_detonating)==1U);
    // Contacts are ignored once detonation started.
    REQUIRE(contact_mine(t,*id,ContactSender::unit).report==ContactReport::unchanged);
    auto t58=f.context(58);
    update_deployables(t58);
    REQUIRE(f.state.mines.contains(*id));
    REQUIRE(f.splashes.empty());
    auto t59=f.context(59);
    update_deployables(t59);
    REQUIRE(!f.state.mines.contains(*id));
    REQUIRE(f.splashes.size()==1U);
    REQUIRE(runtime_test::near(f.splashes[0].radius,108.0));
    REQUIRE(f.splashes[0].amount==20);
    REQUIRE(f.splashes[0].flag==0x05U);
    REQUIRE(count_events(f.events,DeployableEventKind::mine_removed)==1U);
    return true;
}

RUNTIME_TEST(idle_mines_never_expire) {
    Fixture f;
    auto ctx=f.context(1);
    const auto id=place_mine(ctx,0.0F,0.0F,2);
    REQUIRE(id.has_value());
    for (std::int32_t tick=2; tick<5000; tick+=97) {
        auto t=f.context(tick);
        update_deployables(t);
    }
    REQUIRE(f.state.mines.contains(*id));
    REQUIRE(f.state.mines.get(*id)->detonate_tick==0);
    return true;
}

RUNTIME_TEST(splash_chains_unarmed_mines_next_pass) {
    Fixture f;
    auto ctx=f.context(1);
    const auto a=place_mine(ctx,0.0F,0.0F,1);
    const auto b=place_mine(ctx,100.0F,0.0F,1);     // inside 108
    const auto c=place_mine(ctx,300.0F,0.0F,1);     // outside
    REQUIRE(a && b && c);
    REQUIRE(wake_mine(ctx,*a));
    auto t1=f.context(2);
    update_deployables(t1);
    auto t2=f.context(11);
    update_deployables(t2);                          // a splashes (unit adapter first), wakes b
    REQUIRE(!f.state.mines.contains(*a));
    REQUIRE(f.splashes.size()==1U);
    const MineRecord& mb=*f.state.mines.get(*b);
    REQUIRE(mb.awake);
    REQUIRE(mb.detonate_tick==0);                    // starts on the following pass
    REQUIRE(!f.state.mines.get(*c)->awake);
    auto t3=f.context(12);
    update_deployables(t3);
    REQUIRE(f.state.mines.get(*b)->detonate_tick==12);
    return true;
}

RUNTIME_TEST(placement_rejects_overlapping_mine_and_blockers) {
    Fixture f;
    auto ctx=f.context(1);
    REQUIRE(place_mine(ctx,0.0F,0.0F,1).has_value());
    REQUIRE(!place_mine(ctx,5.0F,5.0F,1).has_value());
    REQUIRE(place_mine(ctx,5.0F,5.0F,no_tag).has_value());   // tag -1 skips the area check
    f.world.placement_blocked=[](float,float,float,std::int16_t) { return true; };
    const DeployResult r=fire_mine_weapon(ctx,tank_at(500.0F,500.0F));
    REQUIRE(r.status==DeployStatus::blocked);
    REQUIRE(r.return_before_bookkeeping());
    return true;
}

RUNTIME_TEST(mine_pool_exhaustion_fails_spawn) {
    Fixture f;
    auto ctx=f.context(1);
    for (std::size_t i=0U; i<mine_capacity; ++i)
        REQUIRE(place_mine(ctx,static_cast<float>(i)*50.0F,0.0F,1).has_value());
    REQUIRE(f.state.mines.full());
    const DeployResult r=fire_mine_weapon(ctx,tank_at(0.0F,9000.0F));
    REQUIRE(r.status==DeployStatus::pool_full);
    REQUIRE(f.state.mines.size()==mine_capacity);
    return true;
}

RUNTIME_TEST(powered_mine_grid_places_twenty_five_behind_hull) {
    Fixture f;
    DeployerUnit unit=tank_at(1000.0F,1000.0F);
    unit.powered=true;
    auto ctx=f.context(1);
    const DeployResult r=fire_mine_weapon(ctx,unit);
    REQUIRE(r.status==DeployStatus::placed);
    REQUIRE(r.consume_whole_stock);
    const GridRecord& g=*f.state.grids.get(r.spawned);
    REQUIRE(runtime_test::near(g.step_z,-100.0));
    REQUIRE(runtime_test::near(g.side_x,-100.0));
    for (std::int32_t tick=2; tick<27; ++tick) {
        auto t=f.context(tick);
        update_deployables(t);
    }
    REQUIRE(f.state.grids.size()==0U);
    REQUIRE(f.state.mines.size()==25U);
    f.state.mines.for_each([&](EntityId,const MineRecord& m) {
        if (m.z>1000.0F-70.0F) f.squad_ok=false;     // every row lies behind the hull
    });
    REQUIRE(f.squad_ok);
    return true;
}

RUNTIME_TEST(gun_buddy_requests_reverse_facing_squad_unit) {
    Fixture f;
    DeployerUnit unit=tank_at(0.0F,0.0F);
    unit.heading=0x1000U;
    auto ctx=f.context(1);
    const DeployResult r=fire_gun_buddy_weapon(ctx,unit);
    REQUIRE(r.status==DeployStatus::placed);
    REQUIRE(f.squads.size()==1U);
    const SquadSpawnRequest& q=f.squads[0];
    REQUIRE(q.squad_kind==0U);
    REQUIRE(q.team==1U);
    REQUIRE(q.owner_unit==3);
    REQUIRE(q.heading==0x9000U);
    REQUIRE(q.check_area);
    REQUIRE(runtime_test::near(q.z,-145.0));
    REQUIRE(runtime_test::near(q.half_extent,0.726*45.0,1e-3));
    f.squad_ok=false;
    unit.unit_kind=1U;
    const DeployResult failed=fire_gun_buddy_weapon(ctx,unit);
    REQUIRE(failed.return_before_bookkeeping());
    REQUIRE(runtime_test::near(f.squads[1].z,-82.0));
    return true;
}

RUNTIME_TEST(powered_buddy_grid_requests_eight_units) {
    Fixture f;
    DeployerUnit unit=tank_at(0.0F,0.0F);
    unit.powered=true;
    auto ctx=f.context(1);
    REQUIRE(fire_gun_buddy_weapon(ctx,unit).status==DeployStatus::placed);
    for (std::int32_t tick=2; tick<40; ++tick) {
        auto t=f.context(tick);
        update_deployables(t);
    }
    REQUIRE(f.squads.size()==8U);   // (column+row)%3==0 over 5x5: sums 0, 3 (x4), 6 (x3)
    for (const SquadSpawnRequest& q : f.squads) {
        REQUIRE(q.squad_kind==1U);
        REQUIRE(q.team==2U);
        REQUIRE(!q.check_area);
    }
    REQUIRE(f.state.mines.size()==0U);
    return true;
}

RUNTIME_TEST(mine_blocks_area_sees_live_mines_only) {
    Fixture f;
    auto ctx=f.context(1);
    const auto id=place_mine(ctx,100.0F,0.0F,4);
    REQUIRE(id.has_value());
    const float half=squad_size_scale[0]*squad_half_extent_unit;
    REQUIRE(f.state.mine_blocks_area(100.0F+5.0F+half,0.0F,half));
    REQUIRE(!f.state.mine_blocks_area(100.0F+15.0F+half,0.0F,half));
    REQUIRE(!f.state.mine_blocks_area(0.0F,0.0F,half));
    f.state.mines.get(*id)->detonate_tick=5;
    REQUIRE(!f.state.mine_blocks_area(100.0F,0.0F,half));
    return true;
}

RUNTIME_TEST(float_heading_rounds_after_reversal) {
    Fixture f;
    DeployerUnit unit=tank_at(0.0F,0.0F);
    unit.heading_degrees=22.5F;     // 22.5 * 182.04445 + 32768 = 36864.0 -> 0x9000
    auto ctx=f.context(1);
    REQUIRE(fire_gun_buddy_weapon(ctx,unit).status==DeployStatus::placed);
    REQUIRE(f.squads.size()==1U);
    REQUIRE(f.squads[0].heading==0x9000U);
    REQUIRE(runtime_test::near(f.squads[0].origin_x,0.0));
    REQUIRE(runtime_test::near(f.squads[0].origin_z,0.0));
    return true;
}

RUNTIME_TEST(free_slot_generation_is_hashed) {
    Fixture a;
    Fixture b;
    auto ca=a.context(5);
    const auto id=place_mine(ca,1.0F,2.0F,4);
    REQUIRE(id.has_value());
    a.state.mines.release(*id);
    StateWriter wa;
    StateWriter wb;
    a.state.serialize(wa);
    b.state.serialize(wb);
    REQUIRE(wa.bytes()!=wb.bytes());
    return true;
}

RUNTIME_TEST(grid_pool_exhaustion_is_silent) {
    Fixture f;
    DeployerUnit unit=tank_at(0.0F,0.0F);
    unit.powered=true;
    auto ctx=f.context(1);
    for (std::size_t i=0U; i<grid_capacity; ++i) REQUIRE(fire_mine_weapon(ctx,unit).spawned.valid());
    const DeployResult r=fire_mine_weapon(ctx,unit);
    REQUIRE(r.status==DeployStatus::placed);
    REQUIRE(!r.spawned.valid());
    return true;
}

RUNTIME_TEST(serialization_is_deterministic_and_state_sensitive) {
    Fixture a;
    Fixture b;
    auto ca=a.context(5);
    auto cb=b.context(5);
    REQUIRE(place_mine(ca,1.0F,2.0F,4).has_value());
    REQUIRE(place_mine(cb,1.0F,2.0F,4).has_value());
    StateWriter wa;
    StateWriter wb;
    a.state.serialize(wa);
    b.state.serialize(wb);
    REQUIRE(wa.bytes()==wb.bytes());
    REQUIRE(wake_mine(cb,b.state.mines.slot_id(0U)));
    StateWriter wc;
    b.state.serialize(wc);
    REQUIRE(wa.bytes()!=wc.bytes());
    b.state.clear();
    REQUIRE(b.state.mines.size()==0U);
    return true;
}

RUNTIME_TEST_MAIN()
