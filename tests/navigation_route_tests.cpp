#include "battletanx/navigation_route.hpp"

#include "runtime_test_support.hpp"

#include <map>
#include <utility>

using namespace battletanx;

namespace {

// Synthetic grid mesh (clockwise in x/z, matching the trace convention): index 0 vertex/triangle are unused sentinels, as in
// the retail tables. Each cell is split into two triangles.
NavigationLevel make_grid(std::uint16_t width,std::uint16_t height,float offset_x=0.0F) {
    NavigationLevel level;
    level.vertices.push_back({});
    level.triangles.push_back({});
    const auto vertex_id=[&](std::uint16_t x,std::uint16_t y) {
        return static_cast<std::uint16_t>(1U+y*(width+1U)+x);
    };
    for (std::uint16_t y=0U; y<=height; ++y) {
        for (std::uint16_t x=0U; x<=width; ++x) {
            level.vertices.push_back({offset_x+static_cast<float>(x)*100.0F,static_cast<float>(y)*100.0F});
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
    return level;
}

void link_adjacency(NavigationLevel& level) {
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
}

NavigationLevel grid(std::uint16_t width,std::uint16_t height) {
    NavigationLevel level=make_grid(width,height);
    link_adjacency(level);
    return level;
}

std::uint16_t tri(std::uint16_t width,std::uint16_t x,std::uint16_t y,bool upper) {
    return static_cast<std::uint16_t>(1U+(y*width+x)*2U+(upper ? 1U : 0U));
}

RouteEndpoint at(const NavigationLevel& level,std::uint16_t triangle) {
    return {navigation_triangle_centroid(level,triangle),triangle};
}

bool route_is_connected(const NavigationLevel& level,const NavigationRoute& route) {
    if (route.count==0U) return route.start_triangle==route.goal_triangle;
    if (route.triangles[0]!=route.start_triangle) return false;
    for (std::uint16_t i=0U; i<route.count; ++i) {
        const std::uint16_t corner=route.corners[i];
        if (corner==0U || corner/3U!=route.triangles[i]) return false;
        const std::uint16_t adjacent=level.triangles[corner/3U].corners[corner%3U].adjacent;
        if (adjacent==0U) return false;
        const std::uint16_t next=static_cast<std::uint16_t>(adjacent/3U);
        const std::uint16_t expected=i+1U<route.count ? route.triangles[i+1U] : route.goal_triangle;
        if (next!=expected) return false;
    }
    return true;
}

bool contains(const NavigationRoute& route,std::uint16_t triangle) {
    for (std::uint16_t i=0U; i<route.count; ++i) if (route.triangles[i]==triangle) return true;
    return false;
}

} // namespace

RUNTIME_TEST(distance_uses_octagonal_approximation) {
    REQUIRE(runtime_test::near(navigation_route_distance({0.0F,0.0F},{80.0F,0.0F}),80.0F));
    REQUIRE(runtime_test::near(navigation_route_distance({0.0F,0.0F},{80.0F,-40.0F}),95.0F));
    REQUIRE(runtime_test::near(navigation_route_distance({10.0F,40.0F},{0.0F,0.0F}),43.75F));
    return true;
}

RUNTIME_TEST(direct_route_in_same_triangle_is_empty) {
    const auto level=grid(2U,1U);
    const auto route=plan_navigation_route(level,at(level,1U),at(level,1U),0xF8U);
    REQUIRE(route.stop==NavigationRouteStop::reached);
    REQUIRE(route.count==0U);
    return true;
}

RUNTIME_TEST(adjacent_route_uses_shared_edge_midpoint) {
    const auto level=grid(1U,1U);
    const auto route=plan_navigation_route(level,at(level,1U),at(level,2U),0xF8U);
    REQUIRE(route.stop==NavigationRouteStop::reached);
    REQUIRE(route.count==1U);
    REQUIRE(route.corners[0]==1U*3U+0U); // edge (0,0)-(1,1) of triangle 1
    REQUIRE(runtime_test::near(route.waypoints[0].x,50.0F));
    REQUIRE(runtime_test::near(route.waypoints[0].z,50.0F));
    REQUIRE(route_is_connected(level,route));
    return true;
}

RUNTIME_TEST(multi_triangle_route_is_connected_in_travel_order) {
    const auto level=grid(5U,3U);
    const auto start=tri(5U,0U,0U,false),goal=tri(5U,4U,2U,true);
    const auto route=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(route.stop==NavigationRouteStop::reached);
    REQUIRE(route.count>=6U);
    REQUIRE(route_is_connected(level,route));
    return true;
}

RUNTIME_TEST(equal_cost_tie_is_deterministic) {
    const auto level=grid(3U,3U);
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,2U,true);
    const auto first=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    StateWriter a;
    first.serialize(a);
    for (int repeat=0; repeat<4; ++repeat) {
        const auto again=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
        StateWriter b;
        again.serialize(b);
        REQUIRE(a.bytes()==b.bytes());
    }
    REQUIRE(route_is_connected(level,first));
    return true;
}

RUNTIME_TEST(blocked_class_triangle_is_avoided) {
    auto level=grid(3U,1U);
    const auto start=tri(3U,0U,0U,true),goal=tri(3U,2U,0U,false);
    const auto open=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(open.stop==NavigationRouteStop::reached);
    // Make every middle-cell triangle class 4 (kind 7 enabled): no way through.
    level.kind_enabled[7]=1U;
    level.triangles[tri(3U,1U,0U,false)].kind=7U;
    level.triangles[tri(3U,1U,0U,true)].kind=7U;
    REQUIRE(navigation_route_class(level,tri(3U,1U,0U,false),true)==4U);
    const auto blocked=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(blocked.stop==NavigationRouteStop::unreachable);
    REQUIRE(blocked.count==0U);
    // A clear mask lets the planner cross it again.
    const auto crossing=plan_navigation_route(level,at(level,start),at(level,goal),0x00U);
    REQUIRE(crossing.stop==NavigationRouteStop::reached);
    return true;
}

RUNTIME_TEST(blocked_triangle_forces_detour) {
    auto level=grid(3U,2U);
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,0U,false);
    level.kind_enabled[4]=1U;
    const auto wall=tri(3U,1U,0U,false);
    level.triangles[wall].kind=4U; // class 3
    level.triangles[tri(3U,1U,0U,true)].kind=4U;
    const auto route=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(route.stop==NavigationRouteStop::reached);
    REQUIRE(!contains(route,wall));
    REQUIRE(!contains(route,tri(3U,1U,0U,true)));
    REQUIRE(route_is_connected(level,route));
    // Class 3 becomes passable when an endpoint has runtime status (retail limit 3).
    level.triangles[start].kind=4U;
    REQUIRE(retail_route_blocked_mask(level,start,goal)==0xF0U);
    return true;
}

RUNTIME_TEST(boundary_edges_are_never_used) {
    const auto level=grid(4U,1U);
    const auto start=tri(4U,0U,0U,true),goal=tri(4U,3U,0U,false);
    const auto route=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(route.stop==NavigationRouteStop::reached);
    REQUIRE(route_is_connected(level,route));
    for (std::uint16_t i=0U; i<route.count; ++i) {
        REQUIRE(route.waypoints[i].z>=0.0F && route.waypoints[i].z<=100.0F);
    }
    return true;
}

RUNTIME_TEST(disconnected_regions_are_unreachable) {
    auto level=make_grid(1U,1U);
    const auto right=make_grid(1U,1U,500.0F);
    const auto base=static_cast<std::uint16_t>(level.vertices.size()-1U);
    for (std::size_t v=1U; v<right.vertices.size(); ++v) level.vertices.push_back(right.vertices[v]);
    for (std::size_t t=1U; t<right.triangles.size(); ++t) {
        auto triangle=right.triangles[t];
        for (auto& corner : triangle.corners) corner.vertex=static_cast<std::uint16_t>(corner.vertex+base);
        level.triangles.push_back(triangle);
    }
    link_adjacency(level);
    const auto route=plan_navigation_route(level,at(level,1U),at(level,4U),0xF8U);
    REQUIRE(route.stop==NavigationRouteStop::unreachable);
    REQUIRE(route.count==0U);
    REQUIRE(route.expansions==2U);
    return true;
}

RUNTIME_TEST(invalid_endpoints_and_meshes_are_typed) {
    auto level=grid(1U,1U);
    REQUIRE(plan_navigation_route(level,{{},0U},at(level,1U),0xF8U).stop==
            NavigationRouteStop::invalid_endpoint);
    REQUIRE(plan_navigation_route(level,at(level,1U),{{},9U},0xF8U).stop==
            NavigationRouteStop::invalid_endpoint);
    level.triangles[1].corners[2].adjacent=60U;
    REQUIRE(plan_navigation_route(level,at(level,2U),at(level,1U),0xF8U).stop==
            NavigationRouteStop::invalid_mesh);
    return true;
}

RUNTIME_TEST(route_capacity_truncates_from_start_side) {
    const auto level=grid(6U,1U);
    const auto start=tri(6U,0U,0U,false),goal=tri(6U,5U,0U,true);
    const auto full=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    RoutePlannerLimits limits;
    limits.route_capacity=3U;
    const auto capped=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U,limits);
    REQUIRE(full.count>3U);
    REQUIRE(capped.stop==NavigationRouteStop::reached);
    REQUIRE(capped.truncated);
    REQUIRE(capped.count==3U);
    for (std::uint16_t i=0U; i<3U; ++i) REQUIRE(capped.corners[i]==full.corners[i]);
    limits=RoutePlannerLimits{};
    limits.max_expansions=2U;
    REQUIRE(plan_navigation_route(level,at(level,start),at(level,goal),0xF8U,limits).stop==
            NavigationRouteStop::expansion_limit);
    return true;
}

RUNTIME_TEST(occupancy_cost_steers_route_away) {
    const auto level=grid(3U,3U);
    const auto start=tri(3U,0U,1U,false),goal=tri(3U,2U,1U,true);
    const auto plain=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(plain.stop==NavigationRouteStop::reached);
    std::vector<std::uint8_t> occupancy(level.triangles.size(),0U);
    for (std::uint16_t i=0U; i<plain.count; ++i) {
        if (plain.triangles[i]!=start) occupancy[plain.triangles[i]]=3U;
    }
    const auto avoided=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U,{},occupancy);
    REQUIRE(avoided.stop==NavigationRouteStop::reached);
    REQUIRE(route_is_connected(level,avoided));
    for (std::uint16_t i=1U; i<avoided.count; ++i) REQUIRE(occupancy[avoided.triangles[i]]==0U);
    return true;
}

RUNTIME_TEST(moving_target_replan_follows_new_goal) {
    const auto level=grid(4U,4U);
    const auto start=tri(4U,0U,0U,false);
    const auto first_goal=tri(4U,3U,0U,true),second_goal=tri(4U,0U,3U,true);
    const auto before=plan_navigation_route(level,at(level,start),at(level,first_goal),0xF8U);
    const auto after=plan_navigation_route(level,at(level,start),at(level,second_goal),0xF8U);
    REQUIRE(before.stop==NavigationRouteStop::reached);
    REQUIRE(after.stop==NavigationRouteStop::reached);
    REQUIRE(after.goal_triangle==second_goal);
    REQUIRE(route_is_connected(level,after));
    StateWriter a,b;
    before.serialize(a);
    after.serialize(b);
    REQUIRE(a.bytes()!=b.bytes());
    // locate_route_triangle walks from a known triangle to the new point.
    REQUIRE(locate_route_triangle(level,first_goal,navigation_triangle_centroid(level,first_goal),
                                  navigation_triangle_centroid(level,second_goal))==second_goal);
    return true;
}

RUNTIME_TEST(follower_advances_inside_arrival_radius) {
    const auto level=grid(4U,1U);
    const auto start=tri(4U,0U,0U,true),goal=tri(4U,3U,0U,false);
    const auto route=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(route.count>=2U);
    RouteFollower follower;
    REQUIRE(update_route_follower(follower,route,{-500.0F,50.0F})==RouteFollowResult::steering);
    REQUIRE(follower.index==0U && follower.active);
    REQUIRE(update_route_follower(follower,route,{-500.0F,50.0F})==RouteFollowResult::steering);
    // 40 is inside (retail keeps steering only while distance > 40).
    const auto first=route.waypoints[0];
    REQUIRE(update_route_follower(follower,route,{first.x+40.0F,first.z})==RouteFollowResult::advanced);
    REQUIRE(follower.index==1U);
    for (std::uint16_t i=1U; i+1U<route.count; ++i) {
        REQUIRE(update_route_follower(follower,route,route.waypoints[i])==RouteFollowResult::advanced);
    }
    REQUIRE(follower.index==route.count-1U);
    REQUIRE(update_route_follower(follower,route,route.waypoints[route.count-1U])==
            RouteFollowResult::finished);
    REQUIRE(!follower.active);
    StateWriter writer;
    follower.serialize(writer);
    REQUIRE(writer.bytes().size()==15U);
    RouteFollower idle;
    REQUIRE(update_route_follower(idle,NavigationRoute{},{})==RouteFollowResult::idle);
    return true;
}

RUNTIME_TEST(replan_budget_round_robin_one_unit_per_tick) {
    RouteReplanScheduler scheduler;
    std::array<bool,navigation_route_replan_slots> alive{},consumes{};
    alive.fill(true);
    consumes.fill(true);
    alive[1]=false;
    REQUIRE(scheduler.run_tick(alive,consumes)==
            std::optional<std::uint8_t>{std::uint8_t{0U}});
    REQUIRE(scheduler.cursor==2U); // dead slot 1 skipped in the same pass
    REQUIRE(scheduler.run_tick(alive,consumes)==
            std::optional<std::uint8_t>{std::uint8_t{2U}});
    consumes[3]=false; // slot 3 does not route, so the budget passes to 4
    REQUIRE(scheduler.run_tick(alive,consumes)==
            std::optional<std::uint8_t>{std::uint8_t{4U}});
    REQUIRE(scheduler.cursor==5U);
    REQUIRE(!scheduler.run_tick(alive,consumes,false).has_value());
    REQUIRE(scheduler.cursor==5U);
    scheduler.cursor=19U;
    REQUIRE(scheduler.run_tick(alive,consumes)==
            std::optional<std::uint8_t>{std::uint8_t{19U}});
    REQUIRE(scheduler.cursor==0U);
    return true;
}

RUNTIME_TEST(evasion_steps_repeat_farthest_open_edge) {
    const auto level=grid(2U,2U);
    const auto triangle=tri(2U,1U,1U,false);
    const auto steps=plan_evasion_steps(level,triangle,{0.0F,0.0F});
    REQUIRE(steps.has_value());
    REQUIRE(steps->count==3U);
    REQUIRE(steps->corners[0]==steps->corners[1] && steps->corners[1]==steps->corners[2]);
    REQUIRE(steps->corners[0]/3U==triangle);
    REQUIRE(!plan_evasion_steps(level,0U,{}).has_value());
    return true;
}

RUNTIME_TEST(class_cost_table_defaults_to_zero_and_adds_step_cost) {
    // D_80135840 has no writer: the default table is all zero.
    const RoutePlannerLimits defaults{};
    for (const float cost : defaults.class_costs) REQUIRE(cost==0.0F);
    auto level=grid(3U,2U);
    const auto start=tri(3U,0U,0U,false),goal=tri(3U,2U,0U,false);
    level.kind_enabled[5]=1U; // kind 5 = class 1, not blocked by 0xF8
    level.triangles[tri(3U,1U,0U,false)].kind=5U;
    level.triangles[tri(3U,1U,0U,true)].kind=5U;
    const auto plain=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U);
    REQUIRE(plain.stop==NavigationRouteStop::reached);
    RoutePlannerLimits costly;
    costly.class_costs[1]=50000.0F;
    const auto detour=plan_navigation_route(level,at(level,start),at(level,goal),0xF8U,costly);
    REQUIRE(detour.stop==NavigationRouteStop::reached);
    REQUIRE(route_is_connected(level,detour));
    REQUIRE(!contains(detour,tri(3U,1U,0U,false)));
    REQUIRE(!contains(detour,tri(3U,1U,0U,true)));
    return true;
}

RUNTIME_TEST_MAIN()
