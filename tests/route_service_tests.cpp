#include "battletanx/route_service.hpp"

#include "runtime_test_support.hpp"

#include <array>
#include <map>
#include <utility>

using namespace battletanx;

namespace {

// Synthetic grid mesh (same construction as navigation_route_tests): index 0
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

std::uint16_t tri(std::uint16_t width,std::uint16_t x,std::uint16_t y,bool upper) {
    return static_cast<std::uint16_t>(1U+(y*width+x)*2U+(upper ? 1U : 0U));
}

EntityId unit_id(std::uint16_t slot,std::uint16_t generation=1U) {
    return EntityId{.slot=slot,.generation=generation};
}

RouteRequest request(EntityId unit,GoalMode mode,NavigationVertex from,NavigationVertex to,
                     std::uint32_t serial) {
    return RouteRequest{.unit=unit,.mode=mode,.from_x=from.x,.from_z=from.z,
                        .to_x=to.x,.to_z=to.z,.serial=serial};
}

NavigationVertex centre(const NavigationLevel& level,std::uint16_t triangle) {
    return navigation_triangle_centroid(level,triangle);
}

} // namespace

RUNTIME_TEST(submit_rejects_stale_invalid_unknown_and_unsupported) {
    const auto level=grid(3U,1U);
    RouteService service;
    const EntityId unit=unit_id(2U);
    const auto from=centre(level,tri(3U,0U,0U,false)),to=centre(level,tri(3U,2U,0U,false));
    REQUIRE(service.submit(request(unit,GoalMode::chase,from,to,1U))==RouteSubmitResult::unknown_unit);
    REQUIRE(service.bind_unit(unit,tri(3U,0U,0U,false)));
    REQUIRE(service.submit(request(unit,GoalMode::chase,from,to,0U))==RouteSubmitResult::invalid_serial);
    REQUIRE(service.submit(request(unit,GoalMode::unit,from,to,1U))==RouteSubmitResult::unsupported_mode);
    REQUIRE(service.submit(request(unit,GoalMode::path,from,to,1U))==RouteSubmitResult::unsupported_mode);
    REQUIRE(service.submit(request(unit,GoalMode::chase,from,to,5U))==RouteSubmitResult::accepted);
    REQUIRE(service.submit(request(unit,GoalMode::chase,from,to,5U))==RouteSubmitResult::stale_serial);
    REQUIRE(service.submit(request(unit,GoalMode::chase,from,to,4U))==RouteSubmitResult::stale_serial);
    REQUIRE(service.submit(request(unit,GoalMode::chase,from,to,6U))==RouteSubmitResult::retargeted);
    // A stale generation does not reach the slot.
    REQUIRE(service.submit(request(unit_id(2U,9U),GoalMode::chase,from,to,7U))==RouteSubmitResult::unknown_unit);
    return true;
}

RUNTIME_TEST(round_robin_plans_one_unit_per_tick) {
    const auto level=grid(3U,1U);
    RouteService service;
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,0U,false);
    for (const std::uint16_t slot : std::array<std::uint16_t,3>{7U,1U,4U}) {
        const EntityId unit=unit_id(slot);
        REQUIRE(service.bind_unit(unit,start));
        REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,start),centre(level,goal),1U))==
                RouteSubmitResult::accepted);
    }
    // Without budget nothing is planned and the cursor still walks dead slots.
    REQUIRE(!service.tick(level,false).has_value());
    std::array<std::uint8_t,3> order{};
    for (auto& slot : order) {
        const auto result=service.tick(level);
        REQUIRE(result.has_value());
        REQUIRE(result->status==RouteServiceStatus::routed);
        slot=result->slot;
    }
    REQUIRE(order[0]==1U && order[1]==4U && order[2]==7U);
    return true;
}

RUNTIME_TEST(active_request_is_replanned_on_each_visit_with_moved_target) {
    const auto level=grid(4U,1U);
    RouteService service;
    const EntityId unit=unit_id(0U);
    const auto start=tri(4U,0U,0U,false);
    REQUIRE(service.bind_unit(unit,start));
    REQUIRE(service.submit(request(unit,GoalMode::chase,centre(level,start),
                                   centre(level,tri(4U,1U,0U,false)),1U))==RouteSubmitResult::accepted);
    auto first=service.tick(level);
    REQUIRE(first.has_value() && first->response.serial==1U);
    const std::uint16_t first_count=service.state(unit)->route.count;
    // Target moved: a newer serial replaces the goal; the next visit replans.
    REQUIRE(service.submit(request(unit,GoalMode::chase,centre(level,start),
                                   centre(level,tri(4U,3U,0U,true)),2U))==RouteSubmitResult::retargeted);
    std::optional<RouteServiceResult> second;
    for (int i=0; i<20 && !second.has_value(); ++i) second=service.tick(level);
    REQUIRE(second.has_value() && second->response.serial==2U);
    REQUIRE(service.state(unit)->route.count>first_count);
    REQUIRE(service.state(unit)->route_serial==2U);
    REQUIRE(!service.is_current(first->response));
    REQUIRE(service.is_current(second->response));
    return true;
}

RUNTIME_TEST(cancel_drops_route_and_keeps_serials_stale) {
    const auto level=grid(3U,1U);
    RouteService service;
    const EntityId unit=unit_id(3U);
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,0U,false);
    REQUIRE(service.bind_unit(unit,start));
    REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,start),centre(level,goal),4U))==
            RouteSubmitResult::accepted);
    const auto planned=service.tick(level);
    REQUIRE(planned.has_value());
    REQUIRE(service.cancel(unit));
    REQUIRE(!service.state(unit)->has_route);
    REQUIRE(!service.is_current(planned->response));
    REQUIRE(!service.follow(unit,centre(level,start)).has_value());
    for (int i=0; i<20; ++i) REQUIRE(!service.tick(level).has_value());
    REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,start),centre(level,goal),4U))==
            RouteSubmitResult::stale_serial);
    REQUIRE(!service.cancel(unit_id(3U,2U)));
    // Release then rebind (respawn) resets the serial history.
    REQUIRE(service.release_unit(unit));
    REQUIRE(service.bind_unit(unit_id(3U,2U),start));
    REQUIRE(service.submit(request(unit_id(3U,2U),GoalMode::point_route,centre(level,start),
                                   centre(level,goal),1U))==RouteSubmitResult::accepted);
    return true;
}

RUNTIME_TEST(unreachable_and_blocked_keep_previous_route) {
    auto level=grid(3U,1U);
    RouteService service;
    const EntityId unit=unit_id(0U);
    const auto start=tri(3U,0U,0U,true),goal=tri(3U,2U,0U,false);
    REQUIRE(service.bind_unit(unit,start));
    REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,start),centre(level,goal),1U))==
            RouteSubmitResult::accepted);
    REQUIRE(service.tick(level)->status==RouteServiceStatus::routed);
    const std::uint16_t kept=service.state(unit)->route.count;
    // Wall off the middle cell with class 4 (kind 7 enabled).
    level.kind_enabled[7]=1U;
    level.triangles[tri(3U,1U,0U,false)].kind=7U;
    level.triangles[tri(3U,1U,0U,true)].kind=7U;
    std::optional<RouteServiceResult> blocked;
    for (int i=0; i<20 && !blocked.has_value(); ++i) blocked=service.tick(level);
    REQUIRE(blocked.has_value());
    REQUIRE(blocked->status==RouteServiceStatus::unreachable);
    REQUIRE(!blocked->response.reachable);
    REQUIRE(service.state(unit)->route.count==kept);
    REQUIRE(service.state(unit)->route_serial==1U);
    return true;
}

RUNTIME_TEST(route_overflow_is_truncated) {
    const auto level=grid(6U,1U);
    RoutePlannerLimits limits;
    limits.route_capacity=2U;
    RouteService service(limits);
    const EntityId unit=unit_id(0U);
    const auto start=tri(6U,0U,0U,false),goal=tri(6U,5U,0U,true);
    REQUIRE(service.bind_unit(unit,start));
    REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,start),centre(level,goal),1U))==
            RouteSubmitResult::accepted);
    const auto result=service.tick(level);
    REQUIRE(result.has_value() && result->status==RouteServiceStatus::truncated);
    REQUIRE(service.state(unit)->route.count==2U);
    REQUIRE(service.state(unit)->route.truncated);
    return true;
}

RUNTIME_TEST(occupancy_counts_teammates_within_radius) {
    const auto level=grid(3U,1U);
    const EntityId self=unit_id(0U);
    const std::array<RouteTeammate,6> mates{{
        {.unit=self,.alive=true,.excluded=false,.position={0.0F,0.0F},.triangle=2U},
        {.unit=unit_id(1U),.alive=true,.excluded=false,.position={3000.0F,0.0F},.triangle=3U},
        {.unit=unit_id(2U),.alive=true,.excluded=false,.position={3001.0F,0.0F},.triangle=3U},
        {.unit=unit_id(3U),.alive=false,.excluded=false,.position={0.0F,0.0F},.triangle=3U},
        {.unit=unit_id(4U),.alive=true,.excluded=true,.position={0.0F,0.0F},.triangle=3U},
        {.unit=unit_id(5U),.alive=true,.excluded=false,.position={0.0F,0.0F},.triangle=1U},
    }};
    const auto counts=build_route_occupancy(level,self,{0.0F,0.0F},1U,mates);
    REQUIRE(counts.size()==level.triangles.size());
    REQUIRE(counts[3]==1U);  // 3000 inclusive, 3001 skipped, dead/excluded skipped
    REQUIRE(counts[2]==0U);  // self skipped
    REQUIRE(counts[1]==0U);  // own triangle cleared
    return true;
}

RUNTIME_TEST(occupancy_steers_service_route) {
    const auto level=grid(3U,3U);
    RouteService service;
    const EntityId unit=unit_id(0U);
    const auto start=tri(3U,0U,1U,false),goal=tri(3U,2U,1U,true);
    REQUIRE(service.bind_unit(unit,start));
    REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,start),centre(level,goal),1U))==
            RouteSubmitResult::accepted);
    REQUIRE(service.tick(level)->status==RouteServiceStatus::routed);
    const NavigationRoute plain=service.state(unit)->route;
    std::vector<std::uint8_t> occupancy(level.triangles.size(),0U);
    for (std::uint16_t i=0U; i<plain.count; ++i) if (plain.triangles[i]!=start) occupancy[plain.triangles[i]]=3U;
    std::vector<std::vector<std::uint8_t>> per_slot(route_service_slots);
    per_slot[0]=occupancy;
    std::optional<RouteServiceResult> again;
    for (int i=0; i<20 && !again.has_value(); ++i) again=service.tick(level,true,per_slot);
    REQUIRE(again.has_value());
    const NavigationRoute& avoided=service.state(unit)->route;
    for (std::uint16_t i=1U; i<avoided.count; ++i) REQUIRE(occupancy[avoided.triangles[i]]==0U);
    return true;
}

RUNTIME_TEST(equal_cost_ties_are_identical_across_services) {
    const auto level=grid(3U,3U);
    RouteService a,b;
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,2U,true);
    for (RouteService* service : {&a,&b}) {
        REQUIRE(service->bind_unit(unit_id(5U),start));
        REQUIRE(service->submit(request(unit_id(5U),GoalMode::point_route,centre(level,start),
                                        centre(level,goal),1U))==RouteSubmitResult::accepted);
        REQUIRE(service->tick(level).has_value());
    }
    StateWriter wa,wb;
    a.serialize(wa);
    b.serialize(wb);
    REQUIRE(wa.bytes()==wb.bytes());
    return true;
}

RUNTIME_TEST(direct_point_goal_skips_planner_and_passes_budget) {
    // grid(8,1): far goal centroid (733.3,66.7) from (66.7,33.3) has approx
    // distance 666.7+33.3*3/8=679.2 > 500, so only slot 0 is direct.
    const auto level=grid(8U,1U);
    RouteService service;
    const auto start=tri(8U,0U,0U,false);
    const EntityId near_unit=unit_id(0U),far_unit=unit_id(1U);
    REQUIRE(service.bind_unit(near_unit,start));
    REQUIRE(service.bind_unit(far_unit,start));
    const auto from=centre(level,start);
    REQUIRE(service.submit(request(near_unit,GoalMode::point,from,{from.x+100.0F,from.z},1U))==
            RouteSubmitResult::accepted);
    REQUIRE(service.submit(request(far_unit,GoalMode::point,from,centre(level,tri(8U,7U,0U,true)),1U))==
            RouteSubmitResult::accepted);
    // Slot 0 is direct (< 300), so the cursor hands the budget on to slot 1.
    const auto result=service.tick(level);
    REQUIRE(result.has_value() && result->slot==1U);
    const auto steer=service.follow(near_unit,from);
    REQUIRE(steer.has_value() && steer->nav_state==1U);
    REQUIRE(runtime_test::near(steer->waypoint_x,from.x+100.0F));
    // Between 300 and 500 the unit +0x13C flag decides.
    REQUIRE(!route_goal_is_direct(GoalMode::point,{0.0F,0.0F},{400.0F,0.0F},false));
    REQUIRE(route_goal_is_direct(GoalMode::point_alt,{0.0F,0.0F},{400.0F,0.0F},true));
    REQUIRE(!route_goal_is_direct(GoalMode::point_route,{0.0F,0.0F},{10.0F,0.0F},true));
    return true;
}

RUNTIME_TEST(evasion_mode_pushes_one_edge_three_times) {
    const auto level=grid(3U,3U);
    RouteService service;
    const EntityId unit=unit_id(0U);
    const auto cell=tri(3U,1U,1U,false);
    REQUIRE(service.bind_unit(unit,cell));
    REQUIRE(service.submit(request(unit,GoalMode::evasion,centre(level,cell),
                                   {0.0F,0.0F},1U))==RouteSubmitResult::accepted);
    const auto result=service.tick(level);
    REQUIRE(result.has_value() && result->status==RouteServiceStatus::evasion);
    const NavigationRoute& route=service.state(unit)->route;
    REQUIRE(route.count==3U);
    REQUIRE(route.corners[0]==route.corners[1] && route.corners[1]==route.corners[2]);
    REQUIRE(route.corners[0]/3U==cell);
    return true;
}

RUNTIME_TEST(off_mesh_unit_keeps_route_and_reports) {
    const auto level=grid(3U,1U);
    RouteService service;
    const EntityId unit=unit_id(0U);
    REQUIRE(service.bind_unit(unit,0U));
    REQUIRE(service.submit(request(unit,GoalMode::chase,{50.0F,10.0F},{250.0F,10.0F},1U))==
            RouteSubmitResult::accepted);
    const auto result=service.tick(level);
    REQUIRE(result.has_value() && result->status==RouteServiceStatus::off_mesh);
    REQUIRE(!result->response.reachable);
    REQUIRE(runtime_test::near(result->response.waypoint_x,250.0F));
    REQUIRE(!service.state(unit)->has_route);
    return true;
}

RUNTIME_TEST(unit_triangle_is_retraced_only_when_flag_set) {
    const auto level=grid(3U,1U);
    const auto cached=tri(3U,0U,0U,false),actual=tri(3U,1U,0U,false),goal=tri(3U,2U,0U,true);
    for (int retrace=0; retrace<2; ++retrace) {
        RouteService service;
        const EntityId unit=unit_id(0U);
        REQUIRE(service.bind_unit(unit,cached));
        REQUIRE(service.set_retrace(unit,retrace!=0));
        REQUIRE(service.submit(request(unit,GoalMode::point_route,centre(level,actual),centre(level,goal),1U))==
                RouteSubmitResult::accepted);
        REQUIRE(service.tick(level).has_value());
        REQUIRE(service.state(unit)->unit_triangle==(retrace!=0 ? actual : cached));
    }
    return true;
}

RUNTIME_TEST(follower_advances_through_waypoints_to_goal) {
    const auto level=grid(3U,1U);
    RouteService service;
    const EntityId unit=unit_id(0U);
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,0U,true);
    const auto from=centre(level,start),to=centre(level,goal);
    REQUIRE(service.bind_unit(unit,start));
    REQUIRE(service.submit(request(unit,GoalMode::point_route,from,to,1U))==RouteSubmitResult::accepted);
    REQUIRE(service.tick(level).has_value());
    const NavigationRoute route=service.state(unit)->route;
    REQUIRE(route.count>=2U);
    auto step=service.follow(unit,from);
    REQUIRE(step.has_value() && step->nav_state==0U);
    REQUIRE(runtime_test::near(step->waypoint_x,route.waypoints[0].x));
    for (std::uint16_t i=0U; i<route.count; ++i) step=service.follow(unit,route.waypoints[i]);
    REQUIRE(step.has_value() && step->nav_state==1U);
    return true;
}

RUNTIME_TEST_MAIN()
