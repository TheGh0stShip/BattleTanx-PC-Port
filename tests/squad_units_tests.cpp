#include "battletanx/squad_units.hpp"
#include "battletanx/runtime_math.hpp"
#include "runtime_test_support.hpp"

#include <cmath>
#include <vector>

using namespace battletanx;
using namespace battletanx::squad;
namespace dep=battletanx::deployables;

namespace {

struct Fixture {
    AngleTable angles{reference_angle_table()};
    RetailRandom rng{11U};
    float blocked_x{1.0e9F}, blocked_z{1.0e9F};
    placed::World world;
    placed::UpdateContext update{};
    SquadAdapter adapter{world,update};
    dep::DeployablesState state;
    dep::DeployableEventQueue events;
    RetailRandom deploy_rng{5U};
    dep::DeployablesWorld hooks;

    Fixture() {
        update.angles=&angles;
        update.rng=&rng;
        update.area_clear=[this](float x,float z,float half) {
            return !(std::fabs(x-blocked_x)<=half && std::fabs(z-blocked_z)<=half);
        };
        hooks.spawn_squad_unit=adapter.hook();
        hooks.unit_team=[](std::int16_t) { return std::uint8_t{3U}; };
    }
    dep::DeployablesContext context(std::int32_t tick) {
        return dep::DeployablesContext{.state=state,.events=events,.random=deploy_rng,.world=hooks,.tick=tick};
    }
    std::vector<const placed::Entry*> turrets() const {
        std::vector<const placed::Entry*> out;
        for (const placed::Snapshot& snap : world.snapshots()) {
            const placed::Entry* entry=world.get(snap.id);
            if (entry!=nullptr && entry->kind==placed::Kind::turret) out.push_back(entry);
        }
        return out;
    }
};

dep::DeployerUnit tank(bool powered) {
    return dep::DeployerUnit{.id=4,.unit_kind=0U,.team=2U,.powered=powered,.x=1000.0F,.z=2000.0F,
                             .hull_forward_x=0.0F,.hull_forward_z=1.0F,.heading=0x1000U,
                             .heading_degrees=std::nullopt};
}

bool approx_equal(float a,float b) { return std::fabs(a-b)<0.01F; }

} // namespace

RUNTIME_TEST(gun_buddy_spawns_through_hook) {
    Fixture f;
    auto ctx=f.context(100);
    const dep::DeployResult result=dep::fire_gun_buddy_weapon(ctx,tank(false));
    REQUIRE(result.status==dep::DeployStatus::placed);
    const auto units=f.turrets();
    REQUIRE(units.size()==1U);
    const TurretRecord& unit=units[0]->turret;
    REQUIRE(unit.type==dep::buddy_squad_kind);
    REQUIRE(unit.heading==0x9000U);                      // heading + 0x8000
    REQUIRE(approx_equal(unit.x,1000.0F));
    REQUIRE(approx_equal(unit.z,2000.0F+dep::buddy_rear_offset_tank));
    REQUIRE(unit.health==static_cast<std::int16_t>(turret_health[0]));
    REQUIRE((unit.flags&turret_scaled)==0U);
    REQUIRE(f.adapter.stats().spawned==1U);
    return true;
}

RUNTIME_TEST(gun_buddy_blocked_area_returns_before_bookkeeping) {
    Fixture f;
    f.blocked_x=1000.0F;
    f.blocked_z=2000.0F+dep::buddy_rear_offset_tank;
    auto ctx=f.context(100);
    const dep::DeployResult result=dep::fire_gun_buddy_weapon(ctx,tank(false));
    REQUIRE(result.status==dep::DeployStatus::blocked);
    REQUIRE(result.return_before_bookkeeping());
    REQUIRE(f.world.size()==0U);
    REQUIRE(f.adapter.stats().blocked==1U);
    return true;
}

RUNTIME_TEST(team_and_owner_assignment) {
    Fixture f;
    auto ctx=f.context(100);
    static_cast<void>(dep::fire_gun_buddy_weapon(ctx,tank(false)));
    const auto units=f.turrets();
    REQUIRE(units.size()==1U);
    REQUIRE(units[0]->turret.team==2U);
    REQUIRE(units[0]->turret.owner==4U);
    dep::SquadSpawnRequest bad{.team=0U,.owner_unit=300,.squad_kind=0U,.heading=0U,.x=0.0F,.z=0.0F,
                               .check_area=false,.half_extent=0.0F,
                               .origin_x=0.0F,.origin_z=0.0F};
    REQUIRE(spawn_squad_unit(f.world,f.update,bad).status==SpawnStatus::invalid_owner);
    bad.owner_unit=1;
    bad.squad_kind=4U;
    REQUIRE(spawn_squad_unit(f.world,f.update,bad).status==SpawnStatus::invalid_kind);
    return true;
}

RUNTIME_TEST(powered_grid_spawns_every_third_cell_unchecked) {
    Fixture f;
    // Blocking the grid origin must not matter: grid requests skip the area check.
    f.blocked_x=1000.0F;
    f.blocked_z=2000.0F;
    std::int32_t tick=100;
    auto ctx=f.context(tick);
    const dep::DeployResult result=dep::fire_gun_buddy_weapon(ctx,tank(true));
    REQUIRE(result.status==dep::DeployStatus::placed);
    REQUIRE(result.consume_whole_stock);
    for (int i=0; i<25; ++i) {
        auto step=f.context(++tick);
        dep::update_deployables(step);
    }
    REQUIRE(f.state.grids.size()==0U);
    // (column + row) % 3 == 0 over a 5x5 grid selects 8 cells (step_grid_barrage.c:47).
    const auto units=f.turrets();
    REQUIRE(units.size()==8U);
    for (const placed::Entry* entry : units) {
        REQUIRE(entry->turret.type==dep::grid_buddy_squad_kind);
        REQUIRE(entry->turret.team==3U);                // unit_team hook (+0x1AE)
        REQUIRE(entry->turret.owner==4U);
        REQUIRE(entry->turret.heading==0x9000U);
    }
    REQUIRE(f.adapter.stats().requested==8U);
    return true;
}

RUNTIME_TEST(serialization_is_deterministic) {
    auto run=[]() {
        Fixture f;
        auto ctx=f.context(100);
        static_cast<void>(dep::fire_gun_buddy_weapon(ctx,tank(false)));
        StateWriter writer;
        f.adapter.serialize(writer);
        f.world.serialize(writer);
        return writer.bytes();
    };
    const auto a=run();
    const auto b=run();
    REQUIRE(a==b);
    REQUIRE(a.size()>=16U);
    // requested counter, big-endian u32 = 1
    REQUIRE(a[0]==0U && a[1]==0U && a[2]==0U && a[3]==1U);
    return true;
}

RUNTIME_TEST_MAIN()
