// Multi-squad scenarios across the computer-controlled opponent layers:
// owner plays (campaign_owner_plays) -> member plays (campaign_ai) -> route
// requests -> RouteService planning -> route responses -> commands.
// Synthetic data only.

#include "battletanx/campaign_ai.hpp"
#include "battletanx/campaign_owner_plays.hpp"
#include "battletanx/enemy_control.hpp"
#include "battletanx/navigation_route.hpp"
#include "battletanx/route_service.hpp"
#include "battletanx/state_hash.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace battletanx;
namespace op=battletanx::owner_plays;

namespace {

constexpr std::uint16_t grid_cells=40U; // 4000 x 4000 world units

// Synthetic grid mesh (same construction as route_service_tests): index 0
// vertex/triangle are unused sentinels; each 100x100 cell is two triangles.
NavigationLevel grid(std::uint16_t width,std::uint16_t height) {
    NavigationLevel level;
    level.vertices.push_back({});
    level.triangles.push_back({});
    const auto vertex_id=[&](std::uint16_t x,std::uint16_t y) {
        return static_cast<std::uint16_t>(1U+y*(width+1U)+x);
    };
    for (std::uint16_t y=0U; y<=height; ++y) {
        for (std::uint16_t x=0U; x<=width; ++x) {
            level.vertices.push_back({static_cast<float>(x)*100.0F,static_cast<float>(y)*100.0F});
        }
    }
    for (std::uint16_t y=0U; y<height; ++y) {
        for (std::uint16_t x=0U; x<width; ++x) {
            NavigationTriangle a,b;
            a.corners={{{vertex_id(x,y),0U},{vertex_id(x+1U,y+1U),0U},{vertex_id(x+1U,y),0U}}};
            b.corners={{{vertex_id(x,y),0U},{vertex_id(x,y+1U),0U},{vertex_id(x+1U,y+1U),0U}}};
            level.triangles.push_back(a);
            level.triangles.push_back(b);
        }
    }
    std::map<std::pair<std::uint16_t,std::uint16_t>,std::uint16_t> edges;
    for (std::size_t t=1U; t<level.triangles.size(); ++t) {
        for (std::uint16_t side=0U; side<3U; ++side) {
            auto& corners=level.triangles[t].corners;
            std::uint16_t a=corners[side].vertex,b=corners[(side+1U)%3U].vertex;
            if (b<a) std::swap(a,b);
            const std::uint16_t id=static_cast<std::uint16_t>(t*3U+side);
            const auto found=edges.find({a,b});
            if (found==edges.end()) { edges[{a,b}]=id; continue; }
            corners[side].adjacent=found->second;
            level.triangles[found->second/3U].corners[found->second%3U].adjacent=id;
        }
    }
    return level;
}

// Lower ("a") triangle of a cell; points at (cell*100+70, cell*100+30) lie inside it.
std::uint16_t cell_triangle(std::uint16_t cx,std::uint16_t cz) {
    return static_cast<std::uint16_t>(1U+(cz*grid_cells+cx)*2U);
}
float cell_x(std::uint16_t cx) { return static_cast<float>(cx)*100.0F+70.0F; }
float cell_z(std::uint16_t cz) { return static_cast<float>(cz)*100.0F+30.0F; }

UnitRecord unit_at(std::uint8_t owner,std::uint8_t team,float x,float z,UnitControl control) {
    UnitRecord unit{};
    unit.kind=retail::UnitKind::tank;
    unit.team=team;
    unit.owner=owner;
    unit.alive=true;
    unit.control=control;
    unit.x=x;
    unit.z=z;
    unit.health=100;
    unit.max_health=100;
    unit.spawn_tick=0;
    return unit;
}

EntityId add(UnitPool& pool,const UnitRecord& unit) {
    const EntityId id=*pool.allocate();
    *pool.get(id)=unit;
    return id;
}

AngleTable flat_angles() { return AngleTable{std::array<float,AngleTable::value_count>{}}; }

OwnerDifficulty squad_difficulty() {
    AiProfile profile{};
    profile.skill_a=0.3F;
    profile.skill_b=0.8F;
    profile.speed=8U;
    profile.rate_primary=50U;
    profile.rate_class4=30U;
    profile.rate_kind2=50U;
    profile.value_32c=70U;
    return owner_difficulty_from_profile(profile);
}

// Two computer owners (1 and 2, both team 1) with two tanks each, and one
// human owner (0, team 0) with a single unit.
struct Squads {
    NavigationLevel level=grid(grid_cells,grid_cells);
    UnitPool pool;
    std::array<OwnerRecord,owner_capacity> owners{};
    AngleTable angles=flat_angles();
    RetailRandom ai_rng;
    RetailRandom owner_rng;
    CampaignAiController ai{};
    RouteService routes_service{};
    std::array<ExternalParameters,unit_capacity> params{};
    std::array<CampaignAiCommand,unit_capacity> commands{};
    std::array<RouteRequest,4> routes{};
    std::array<op::OwnerPlayState,3> owner_state{};
    op::OwnerWorld world{};
    std::array<std::array<EntityId,2>,3> members{};
    EntityId player{};
    std::int32_t tick=1000;
    CampaignAiOutput last{};

    explicit Squads(std::uint32_t seed) : ai_rng(seed), owner_rng(seed^0x5A5AU) {
        owners[0].present=true;
        owners[0].team=0U;
        for (std::uint8_t o=1U; o<=2U; ++o) {
            owners[o].present=true;
            owners[o].computer=true;
            owners[o].team=1U;
            ai.set_owner_difficulty(o,squad_difficulty());
        }
        const std::array<std::array<std::uint16_t,2>,4> cells{{{5U,5U},{10U,5U},{20U,5U},{25U,5U}}};
        for (std::size_t i=0U; i<cells.size(); ++i) {
            const std::uint8_t owner=static_cast<std::uint8_t>(1U+i/2U);
            const EntityId id=add(pool,unit_at(owner,1U,cell_x(cells[i][0]),cell_z(cells[i][1]),UnitControl::computer));
            members[owner][i%2U]=id;
        }
        UnitRecord human=unit_at(0U,0U,cell_x(15U),cell_z(35U),UnitControl::player);
        human.primary=true;
        player=add(pool,human);
        const std::array<EntityId,5> bound{members[1][0],members[1][1],members[2][0],members[2][1],player};
        for (const EntityId id : bound) {
            const UnitRecord* unit=pool.get(id);
            const auto cx=static_cast<std::uint16_t>(unit->x/100.0F);
            const auto cz=static_cast<std::uint16_t>(unit->z/100.0F);
            (void)routes_service.bind_unit(id,cell_triangle(cx,cz));
        }
        world.owner_count=3U;
        world.team_list={1U,2U,0U};
        world.team_list_count=2U;
    }

    CampaignAiOutput step(bool budget) {
        CampaignAiFrame frame{};
        frame.tick=tick++;
        frame.route_budget=budget;
        frame.parameters=params;
        last=ai.update(pool,owners,angles,ai_rng,frame,commands,routes);
        return last;
    }

    op::OwnerPlayInput owner_input(std::uint8_t owner,std::int32_t game_mode) const {
        op::OwnerPlayInput input{};
        input.owner=owner;
        input.active=true;
        input.computer=true;
        input.game_mode=game_mode;
        input.world=&world;
        for (std::size_t i=0U; i<members[owner].size(); ++i) {
            const EntityId id=members[owner][i];
            const UnitRecord* unit=pool.get(id);
            op::MemberView& m=input.members[i];
            m.unit=id;
            m.in_use=unit!=nullptr && unit->alive;
            m.unit_class=0x41U;
            m.state=static_cast<std::int32_t>(ai.member_goal_mode(id));
            // m.mode (unit +0x18) left 0: no port source; role 4 slot 5 resolves on root_play.
            m.flags=unit!=nullptr ? unit->flags : std::uint16_t{0U};
            m.x=unit!=nullptr ? unit->x : 0.0F;
            m.z=unit!=nullptr ? unit->z : 0.0F;
            m.root_play=ai.member_root_play(id);
        }
        return input;
    }

    // Applies AssignResult.changes through the controller's unconditional hook.
    bool apply_assignment(const op::OwnerPlayResult& result) {
        for (std::uint8_t i=0U; i<result.assignment.change_count; ++i) {
            const op::MemberPlayAssignment& change=result.assignment.changes[i];
            if (!ai.force_member_root_play(change.unit,change.play)) return false;
            if (ai.member_root_play(change.unit)!=change.play) return false;
        }
        return true;
    }

    [[nodiscard]] const CampaignAiCommand* command_for(EntityId id) const {
        for (std::size_t i=0U; i<last.commands; ++i) if (commands[i].command.unit==id) return &commands[i];
        return nullptr;
    }
};

struct PipelineTrace {
    std::size_t requests{};
    std::size_t accepted{};
    std::size_t planned{};
    std::size_t applied{};
    std::size_t commands{};
    std::array<bool,unit_capacity> applied_slot{};
    std::vector<std::uint8_t> planned_order{};
};

// Scenario 1 body: owners 1 and 2 in game mode 5 (root play 2, role 4). Members already run root play 5, so the role-4 walk matches both (slot 5 member_mode, slot 10 first_candidate) with no stack change.
bool run_pipeline(Squads& s,PipelineTrace& trace,int ticks) {
    s.step(false); // creates member state for every unit
    for (std::uint8_t owner=1U; owner<=2U; ++owner) {
        const auto input=s.owner_input(owner,5);
        auto r=op::update_owner_plays(s.owner_state[owner],input,s.owner_rng);
        if (r.status!=op::OwnerPlayStatus::ok || r.assigned) return false;
        if (s.owner_state[owner].stack.frames[0].play!=2U || s.owner_state[owner].role!=4U) return false;
        r=op::update_owner_plays(s.owner_state[owner],input,s.owner_rng);
        if (!r.assigned || r.assignment.status!=op::AssignStatus::ok) return false;
        if (r.assignment.matched!=2U) return false;
        if (r.assignment.change_count!=0U) return false; // both members already root play 5
        if (!s.apply_assignment(r)) return false;
    }
    for (int i=0; i<ticks; ++i) {
        const CampaignAiOutput out=s.step(true);
        trace.commands+=out.commands;
        for (std::size_t k=0U; k<out.routes; ++k) {
            ++trace.requests;
            const RouteSubmitResult submit=s.routes_service.submit(s.routes[k]);
            if (submit==RouteSubmitResult::accepted || submit==RouteSubmitResult::retargeted) ++trace.accepted;
        }
        const auto planned=s.routes_service.tick(s.level);
        if (!planned) continue;
        ++trace.planned;
        trace.planned_order.push_back(planned->slot);
        if (s.ai.apply_route_response(planned->response)) {
            ++trace.applied;
            trace.applied_slot[planned->response.unit.slot]=true;
        }
    }
    return true;
}

std::string pipeline_digest(std::uint32_t seed) {
    Squads s(seed);
    PipelineTrace trace;
    if (!run_pipeline(s,trace,90)) return {};
    StateWriter writer;
    s.ai.serialize(writer);
    s.routes_service.serialize(writer);
    for (const auto& state : s.owner_state) state.serialize(writer);
    writer.u32(s.ai_rng.seed());
    for (const EntityId id : {s.members[1][0],s.members[1][1],s.members[2][0],s.members[2][1]}) {
        const CampaignAiUnitState* unit=s.ai.state(id.slot);
        if (unit==nullptr) return {};
        writer.f32(unit->skill0);
        writer.f32(unit->skill1);
    }
    writer.u32(static_cast<std::uint32_t>(trace.requests));
    writer.u32(static_cast<std::uint32_t>(trace.applied));
    for (const std::uint8_t slot : trace.planned_order) writer.u8(slot);
    return writer.hex();
}

op::OwnerPlayInput standalone_input(std::int32_t game_mode,bool computer,const op::OwnerWorld* world) {
    op::OwnerPlayInput input{};
    input.owner=0U;
    input.active=true;
    input.computer=computer;
    input.game_mode=game_mode;
    input.world=world;
    for (std::uint16_t i=0U; i<op::retail_values::squad_members; ++i) {
        input.members[i].unit=EntityId{.slot=i,.generation=1U};
        input.members[i].in_use=true;
        input.members[i].unit_class=0x41U;
    }
    return input;
}

} // namespace

// Scenario 1: owner play 2 -> member plays -> route requests -> round-robin
// planning -> responses -> commands.
RUNTIME_TEST(scenario_mode5_owner_assigns_members_and_routes_flow) {
    REQUIRE(op::select_owner_root_play(5,true)==2U);
    Squads s(0x1234U);
    PipelineTrace trace;
    REQUIRE(run_pipeline(s,trace,90));
    // Every member still runs root play 5 (role 4 slots 5 and 10).
    for (std::uint8_t owner=1U; owner<=2U; ++owner) {
        for (const EntityId id : s.members[owner]) REQUIRE(s.ai.member_root_play(id)==5U);
    }
    REQUIRE(trace.requests>0U);
    REQUIRE(trace.accepted==trace.requests);
    REQUIRE(trace.planned>0U);
    REQUIRE(trace.applied>0U);
    // Each of the four computer units received at least one applied response.
    for (std::uint8_t owner=1U; owner<=2U; ++owner) {
        for (const EntityId id : s.members[owner]) REQUIRE(trace.applied_slot[id.slot]);
    }
    // Commands: one per live computer unit, aimed at the only opposing unit.
    REQUIRE(trace.commands>0U);
    for (std::uint8_t owner=1U; owner<=2U; ++owner) {
        for (const EntityId id : s.members[owner]) {
            const CampaignAiCommand* command=s.command_for(id);
            REQUIRE(command!=nullptr);
            REQUIRE(command->status==CampaignAiStatus::ok);
            REQUIRE(command->command.target==s.player);
            REQUIRE(find_nearest_enemy_unit(s.pool,id,s.tick,enemy_retail::drive_far_distance)==s.player);
            const RouteUnitState* route=s.routes_service.state(id);
            REQUIRE(route!=nullptr);
            REQUIRE(route->latest_serial!=0U);
        }
    }
    return true;
}

// Scenario 2: owner play 15 hunting toggles role 1 <-> 3 from the caller's
// link inputs (own flagged member, parameter 0x210F / 0x2110).
RUNTIME_TEST(scenario_play15_role_switch_follows_link_inputs) {
    op::OwnerWorld world{};
    world.owner_count=1U;
    world.team_list={0U,0U,0U};
    world.team_list_count=1U; // own team listed: "hunt owner 0" stays ineligible
    op::OwnerPlayState state{};
    RetailRandom rng{11U};
    const auto input=standalone_input(0,true,&world);

    auto r=op::update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].play==15U && state.role==1U && r.assigned);
    REQUIRE(state.stack.depth==2U && state.stack.frames[1].play==12U);
    r=op::update_owner_plays(state,input,rng);
    REQUIRE(state.role==1U && !r.role_changed);

    world.owners[0].has_flagged_member=true;
    r=op::update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].state==3);
    REQUIRE(state.role==3U && r.role_changed && r.assigned);
    REQUIRE(r.assignment.status==op::AssignStatus::ok);
    r=op::update_owner_plays(state,input,rng); // regroup monitor: still flagged
    REQUIRE(state.role==3U && !r.role_changed);

    world.owners[0].has_flagged_member=false;
    r=op::update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].state==1);
    REQUIRE(state.role==1U && r.role_changed && r.release_target);
    REQUIRE(state.objective==op::retail_values::no_objective);

    world.owners[0].has_flagged_member=true;
    r=op::update_owner_plays(state,input,rng);
    REQUIRE(state.role==3U && r.role_changed);
    return true;
}

// Scenario 3: plays 25 (computer) and 26 (human) change role at 10/20/30 s and
// 5/15/25 s on the 9000-tick normalised clock (30 ticks per second). The role
// for the next stage is written on the update after the threshold is crossed.
RUNTIME_TEST(scenario_play25_and_play26_timed_roles) {
    const auto first_tick_of_role=[](bool computer,std::array<std::int32_t,3>& seen) {
        op::OwnerWorld world{};
        op::OwnerPlayState state{};
        RetailRandom rng{5U};
        const auto input=standalone_input(2,computer,&world);
        std::array<std::uint8_t,3> roles{9U,10U,1U};
        seen={-1,-1,-1};
        for (std::int32_t t=0; t<1000; ++t) {
            world.elapsed_ticks=t;
            const auto r=op::update_owner_plays(state,input,rng);
            if (r.status!=op::OwnerPlayStatus::ok) return false;
            if (t==0 && (state.role!=8U || state.stack.frames[0].play!=(computer ? 25U : 26U))) return false;
            for (std::size_t i=0U; i<roles.size(); ++i) {
                if (seen[i]<0 && state.role==roles[i]) seen[i]=t;
            }
        }
        return true;
    };
    constexpr std::int32_t tps=static_cast<std::int32_t>(campaign_ai_retail::ticks_per_second);
    std::array<std::int32_t,3> seen{};
    REQUIRE(first_tick_of_role(true,seen));
    REQUIRE(seen[0]==10*tps+1 && seen[1]==20*tps+1 && seen[2]==30*tps+1);
    REQUIRE(first_tick_of_role(false,seen));
    REQUIRE(seen[0]==5*tps+1 && seen[1]==15*tps+1 && seen[2]==25*tps+1);
    REQUIRE(op::normalized_elapsed_time(9000)==1.0F);
    REQUIRE(op::normalized_elapsed_time(20000)==1.0F);
    return true;
}

// Scenario 4: the 600-tick stuck timer fires, clearing the move goal recovers,
// and a new follow goal re-arms a full timer.
RUNTIME_TEST(scenario_stuck_timer_and_recovery) {
    constexpr std::int32_t stuck_ticks=static_cast<std::int32_t>(campaign_ai_retail::stuck_seconds*
                                                                 campaign_ai_retail::ticks_per_second);
    REQUIRE(stuck_ticks==600);
    UnitPool pool;
    std::array<OwnerRecord,owner_capacity> owners{};
    owners[1].present=true;
    owners[1].computer=true;
    owners[1].team=1U;
    const AngleTable angles=flat_angles();
    RetailRandom rng{0x77U};
    CampaignAiController ai{};
    ai.set_owner_difficulty(1U,squad_difficulty());
    const EntityId unit=add(pool,unit_at(1U,1U,0.0F,0.0F,UnitControl::computer));
    std::array<CampaignAiCommand,unit_capacity> commands{};
    std::array<RouteRequest,4> routes{};
    std::int32_t tick=2000;
    bool self_destruct=false;
    const auto step=[&] {
        CampaignAiFrame frame{};
        frame.tick=tick++;
        frame.route_budget=true;
        const CampaignAiOutput out=ai.update(pool,owners,angles,rng,frame,commands,routes);
        self_destruct=false;
        for (std::size_t i=0U; i<out.commands; ++i) {
            if (commands[i].command.unit==unit) self_destruct=commands[i].self_destruct;
        }
    };

    step();
    REQUIRE(ai.assign_move_goal(unit,AiGoal{GoalMode::follow,no_entity,1000.0F,0.0F,true}));
    const std::int32_t start=tick;
    step();
    REQUIRE(ai.state(unit.slot)->stuck_deadline==start+stuck_ticks);
    bool fired=false;
    std::int32_t fired_tick=0;
    while (tick<=start+stuck_ticks+1) {
        const std::int32_t now=tick;
        step();
        if (self_destruct && !fired) { fired=true; fired_tick=now; }
    }
    REQUIRE(fired);
    REQUIRE(fired_tick>start+stuck_ticks);

    // Recovery: clearing the move goal resets the timer on the next turn.
    REQUIRE(ai.assign_move_goal(unit,AiGoal{}));
    step();
    REQUIRE(!self_destruct);
    REQUIRE(ai.state(unit.slot)->stuck_deadline==0);
    REQUIRE(ai.member_goal_mode(unit)==0U);

    // A new follow goal re-arms a full 600-tick window.
    REQUIRE(ai.assign_move_goal(unit,AiGoal{GoalMode::follow,no_entity,-1000.0F,0.0F,true}));
    const std::int32_t rearm=tick;
    step();
    REQUIRE(ai.state(unit.slot)->stuck_deadline==rearm+stuck_ticks);
    for (int i=0; i<100; ++i) { step(); REQUIRE(!self_destruct); }
    return true;
}

// Scenario 5: identical seeds give identical serialized digests; a different
// seed gives a different digest.
RUNTIME_TEST(scenario_pipeline_digest_is_deterministic) {
    const std::string a=pipeline_digest(0x1234U);
    const std::string b=pipeline_digest(0x1234U);
    const std::string c=pipeline_digest(0x9876U);
    REQUIRE(!a.empty());
    REQUIRE(a==b);
    REQUIRE(!c.empty());
    REQUIRE(a!=c);
    return true;
}

RUNTIME_TEST_MAIN()
