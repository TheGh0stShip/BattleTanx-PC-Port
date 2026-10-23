#include "battletanx/route_service.hpp"

namespace battletanx {
namespace {

bool is_evasion(GoalMode mode) noexcept {
    return mode==GoalMode::evasion;
}

// func_800E5FA8 jump table D_80074668: modes 5, 6, 7, 8, 12 call
// func_800DFA68; mode 10 calls func_800E031C.
bool routing_mode(GoalMode mode) noexcept {
    switch (mode) {
    case GoalMode::chase:
    case GoalMode::point:
    case GoalMode::point_route:
    case GoalMode::follow:
    case GoalMode::point_alt:
    case GoalMode::evasion:
        return true;
    case GoalMode::none:
    case GoalMode::unit:
    case GoalMode::object_list:
    case GoalMode::path:
        return false;
    }
    return false;
}

bool triangle_in_range(const NavigationLevel& level,std::uint16_t triangle) noexcept {
    return triangle!=0U && triangle<level.triangles.size();
}

void write_request(StateWriter& writer,const RouteRequest& request) {
    writer.id(request.unit);
    writer.u8(static_cast<std::uint8_t>(request.mode));
    writer.f32(request.from_x);
    writer.f32(request.from_z);
    writer.f32(request.to_x);
    writer.f32(request.to_z);
    writer.u32(request.serial);
}

// Remaining length from `from` through the waypoints to `to`.
// TEMPORARY POLICY: the retail goal distance refresh is unresolved.
float route_length(const NavigationRoute& route,std::uint16_t first,NavigationVertex from,
                   NavigationVertex to) noexcept {
    float total=0.0F;
    NavigationVertex at=from;
    for (std::uint16_t i=first; i<route.count && i<navigation_route_capacity; ++i) {
        total+=navigation_route_distance(at,route.waypoints[i]);
        at=route.waypoints[i];
    }
    return total+navigation_route_distance(at,to);
}

bool wants_budget(const RouteUnitState& state) noexcept {
    if (!state.unit.valid() || !state.active) return false;
    const NavigationVertex from{state.request.from_x,state.request.from_z};
    const NavigationVertex to{state.request.to_x,state.request.to_z};
    return !route_goal_is_direct(state.request.mode,from,to,state.direct_flag);
}

} // namespace

bool route_goal_is_direct(GoalMode mode,NavigationVertex from,NavigationVertex to,
                          bool direct_flag) noexcept {
    if (mode!=GoalMode::point && mode!=GoalMode::point_alt) return false;
    const float distance=navigation_route_distance(from,to);
    if (!(distance<route_direct_flag_radius)) return false;
    return distance<route_direct_radius || direct_flag;
}

std::vector<std::uint8_t> build_route_occupancy(const NavigationLevel& level,EntityId self,
                                                NavigationVertex self_position,
                                                std::uint16_t self_triangle,
                                                std::span<const RouteTeammate> teammates) {
    std::vector<std::uint8_t> counts(level.triangles.size(),std::uint8_t{0U});
    for (const RouteTeammate& mate : teammates) {
        if (!mate.alive || mate.unit==self || mate.excluded) continue;
        const float distance=navigation_route_distance(self_position,mate.position);
        if (route_teammate_radius<distance) continue;
        // Retail weight is 4 - trunc(d/1000); with d <= 3000 it is never 0,
        // so the clear branch is unreachable and the entry is incremented.
        if (mate.triangle>=counts.size()) continue;
        counts[mate.triangle]=static_cast<std::uint8_t>(counts[mate.triangle]+1U);
    }
    if (self_triangle<counts.size()) counts[self_triangle]=0U;
    return counts;
}

void RouteUnitState::serialize(StateWriter& writer) const {
    writer.id(unit);
    writer.u32(latest_serial);
    writer.u32(route_serial);
    writer.boolean(active);
    write_request(writer,request);
    writer.u16(unit_triangle);
    writer.boolean(direct_flag);
    writer.boolean(retrace);
    writer.boolean(has_route);
    writer.f32(route_goal.x);
    writer.f32(route_goal.z);
    if (has_route) route.serialize(writer);
    follower.serialize(writer);
}

RouteUnitState* RouteService::owned(EntityId unit) noexcept {
    if (!unit.valid() || unit.slot>=units_.size()) return nullptr;
    RouteUnitState& slot_state=units_[unit.slot];
    return slot_state.unit==unit ? &slot_state : nullptr;
}

const RouteUnitState* RouteService::state(EntityId unit) const noexcept {
    if (!unit.valid() || unit.slot>=units_.size()) return nullptr;
    const RouteUnitState& slot_state=units_[unit.slot];
    return slot_state.unit==unit ? &slot_state : nullptr;
}

bool RouteService::bind_unit(EntityId unit,std::uint16_t triangle) {
    if (!unit.valid() || unit.slot>=units_.size()) return false;
    RouteUnitState& slot_state=units_[unit.slot];
    if (slot_state.unit!=unit) {
        slot_state=RouteUnitState{};
        slot_state.unit=unit;
    }
    slot_state.unit_triangle=triangle;
    return true;
}

bool RouteService::release_unit(EntityId unit) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr) return false;
    *slot_state=RouteUnitState{};
    return true;
}

bool RouteService::set_unit_triangle(EntityId unit,std::uint16_t triangle) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr) return false;
    slot_state->unit_triangle=triangle;
    return true;
}

bool RouteService::set_retrace(EntityId unit,bool flag) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr) return false;
    slot_state->retrace=flag;
    return true;
}

bool RouteService::set_direct_flag(EntityId unit,bool flag) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr) return false;
    slot_state->direct_flag=flag;
    return true;
}

bool RouteService::set_unit_position(EntityId unit,NavigationVertex position) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr) return false;
    slot_state->request.from_x=position.x;
    slot_state->request.from_z=position.z;
    return true;
}

RouteSubmitResult RouteService::submit(const RouteRequest& request) {
    RouteUnitState* slot_state=owned(request.unit);
    if (slot_state==nullptr) return RouteSubmitResult::unknown_unit;
    if (request.serial==0U) return RouteSubmitResult::invalid_serial;
    if (request.serial<=slot_state->latest_serial) return RouteSubmitResult::stale_serial;
    if (!routing_mode(request.mode)) return RouteSubmitResult::unsupported_mode;
    const bool replacing=slot_state->active;
    slot_state->latest_serial=request.serial;
    slot_state->request=request;
    slot_state->active=true;
    return replacing ? RouteSubmitResult::retargeted : RouteSubmitResult::accepted;
}

bool RouteService::cancel(EntityId unit) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr) return false;
    slot_state->active=false;
    slot_state->request=RouteRequest{};
    slot_state->has_route=false;
    slot_state->route=NavigationRoute{};
    slot_state->route_serial=0U;
    slot_state->route_goal=NavigationVertex{};
    slot_state->follower=RouteFollower{};
    return true;
}

bool RouteService::is_current(const RouteResponse& response) const noexcept {
    const RouteUnitState* owner=state(response.unit);
    return owner!=nullptr && owner->active && response.serial!=0U &&
           response.serial==owner->latest_serial;
}

std::optional<RouteServiceResult> RouteService::tick(const NavigationLevel& level,bool budget,
                                                     std::span<const std::vector<std::uint8_t>> occupancy_for) {
    std::array<bool,navigation_route_replan_slots> alive{};
    std::array<bool,navigation_route_replan_slots> consumes{};
    for (std::size_t slot=0U; slot<units_.size(); ++slot) {
        alive[slot]=units_[slot].unit.valid();
        consumes[slot]=wants_budget(units_[slot]);
    }
    const std::optional<std::uint8_t> spent=scheduler_.run_tick(
        std::span<const bool,navigation_route_replan_slots>(alive),
        std::span<const bool,navigation_route_replan_slots>(consumes),budget);
    if (!spent.has_value()) return std::nullopt;
    std::span<const std::uint8_t> occupancy{};
    if (*spent<occupancy_for.size()) occupancy=occupancy_for[*spent];
    return plan(level,*spent,occupancy);
}

RouteServiceResult RouteService::plan(const NavigationLevel& level,std::uint8_t slot,
                                      std::span<const std::uint8_t> occupancy) {
    RouteUnitState& slot_state=units_[slot];
    const RouteRequest request=slot_state.request;

    RouteServiceResult result;
    result.slot=slot;
    result.response.unit=slot_state.unit;
    result.response.serial=request.serial;
    result.response.reachable=false;
    const NavigationVertex from{request.from_x,request.from_z};
    const NavigationVertex to{request.to_x,request.to_z};
    result.response.waypoint_x=to.x;
    result.response.waypoint_z=to.z;
    result.response.nav_distance=navigation_route_distance(from,to);

    // Off-mesh recovery: the cached triangle is refreshed by tracing from its
    // centroid to the unit point (func_800DE3EC). A trace that leaves the
    // mesh stops in the last triangle crossed, which becomes the start.
    if (!triangle_in_range(level,slot_state.unit_triangle)) {
        result.status=RouteServiceStatus::off_mesh;
        return result;
    }
    // CONFIRMED gate (func_800DEE8C 800DEF60..800DEF7C): unit +0x346 is
    // re-traced only when unit +0x35A != 0; otherwise the cached triangle is
    // used as-is. The meaning of +0x35A is UNRESOLVED.
    std::uint16_t start=slot_state.unit_triangle;
    if (slot_state.retrace) {
        const NavigationVertex known=navigation_triangle_centroid(level,slot_state.unit_triangle);
        start=locate_route_triangle(level,slot_state.unit_triangle,known,from);
        if (!triangle_in_range(level,start)) {
            result.status=RouteServiceStatus::off_mesh;
            return result;
        }
        slot_state.unit_triangle=start;
    }

    if (is_evasion(request.mode)) {
        // func_800E031C: `to` is the threat. The same far edge is pushed
        // three times (node = edge/3 stays on the start triangle).
        const std::optional<EvasionSteps> steps=plan_evasion_steps(level,start,to);
        if (!steps.has_value()) {
            result.status=RouteServiceStatus::unreachable;
            return result;
        }
        NavigationRoute route;
        route.start_triangle=start;
        route.goal_triangle=start;
        route.stop=NavigationRouteStop::reached;
        for (std::uint16_t i=0U; i<steps->count && i<steps->corners.size(); ++i) {
            route.corners[i]=steps->corners[i];
            route.triangles[i]=start;
            route.waypoints[i]=navigation_edge_midpoint(level,steps->corners[i]);
            ++route.count;
        }
        slot_state.route=route;
        slot_state.has_route=true;
        slot_state.route_serial=request.serial;
        slot_state.route_goal=route.count!=0U ? route.waypoints[route.count-1U] : from;
        slot_state.follower=RouteFollower{};
        result.status=RouteServiceStatus::evasion;
        result.response.reachable=true;
        result.response.waypoint_x=route.waypoints[0].x;
        result.response.waypoint_z=route.waypoints[0].z;
        result.response.nav_distance=navigation_route_distance(from,route.waypoints[0]);
        return result;
    }

    // Goal-side trace (retail stores it at session +0x11954 from the stack
    // record at sp+0x10). UNRESOLVED: which known triangle retail starts the
    // trace from; this starts from the unit triangle.
    const std::uint16_t goal=locate_route_triangle(level,start,from,to);
    if (!triangle_in_range(level,goal)) {
        result.status=RouteServiceStatus::off_mesh;
        return result;
    }

    const std::uint8_t blocked=retail_route_blocked_mask(level,start,goal);
    const RouteEndpoint start_point{.point=from,.triangle=start};
    const RouteEndpoint goal_point{.point=to,.triangle=goal};
    const NavigationRoute route=plan_navigation_route(level,start_point,goal_point,blocked,limits_,occupancy);

    switch (route.stop) {
    case NavigationRouteStop::reached:
        break;
    case NavigationRouteStop::unreachable: {
        const std::uint8_t start_class=navigation_route_class(level,start,limits_.campaign_mode);
        const bool start_blocked=start_class<8U && ((blocked>>start_class)&1U)!=0U;
        result.status=start_blocked ? RouteServiceStatus::blocked_endpoint : RouteServiceStatus::unreachable;
        return result; // previous route stays attached
    }
    case NavigationRouteStop::invalid_endpoint:
        result.status=RouteServiceStatus::off_mesh;
        return result;
    case NavigationRouteStop::invalid_mesh:
        result.status=RouteServiceStatus::invalid_mesh;
        return result;
    case NavigationRouteStop::expansion_limit:
        result.status=RouteServiceStatus::expansion_limit;
        return result;
    }

    slot_state.route=route;
    slot_state.has_route=true;
    slot_state.route_serial=request.serial;
    slot_state.route_goal=to;
    slot_state.follower=RouteFollower{};
    result.response.reachable=true;
    if (route.count==0U) {
        result.status=RouteServiceStatus::same_triangle;
        result.response.nav_state=1U;
    } else {
        result.status=route.truncated ? RouteServiceStatus::truncated : RouteServiceStatus::routed;
        result.response.waypoint_x=route.waypoints[0].x;
        result.response.waypoint_z=route.waypoints[0].z;
        // TEMPORARY POLICY: final-leg flag when one waypoint remains.
        result.response.nav_state=route.count==1U ? std::uint8_t{1U} : std::uint8_t{0U};
    }
    result.response.nav_distance=route_length(route,0U,from,to);
    return result;
}

std::optional<RouteResponse> RouteService::follow(EntityId unit,NavigationVertex position) {
    RouteUnitState* slot_state=owned(unit);
    if (slot_state==nullptr || !slot_state->active) return std::nullopt;
    slot_state->request.from_x=position.x;
    slot_state->request.from_z=position.z;
    RouteResponse response;
    response.unit=unit;
    const NavigationVertex request_goal{slot_state->request.to_x,slot_state->request.to_z};
    if (route_goal_is_direct(slot_state->request.mode,position,request_goal,slot_state->direct_flag)) {
        // func_800E2058 straight steer; no route is consulted.
        response.serial=slot_state->request.serial;
        response.reachable=true;
        response.waypoint_x=request_goal.x;
        response.waypoint_z=request_goal.z;
        response.nav_distance=navigation_route_distance(position,request_goal);
        response.nav_state=1U;
        return response;
    }
    if (!slot_state->has_route) return std::nullopt;
    const RouteFollowResult step=update_route_follower(slot_state->follower,slot_state->route,position);
    response.serial=slot_state->route_serial;
    response.reachable=true;
    const NavigationVertex goal=slot_state->route_goal;
    if (step==RouteFollowResult::idle || step==RouteFollowResult::finished) {
        response.waypoint_x=goal.x;
        response.waypoint_z=goal.z;
        response.nav_distance=navigation_route_distance(position,goal);
        response.nav_state=1U;
        return response;
    }
    response.waypoint_x=slot_state->follower.target.x;
    response.waypoint_z=slot_state->follower.target.z;
    response.nav_distance=route_length(slot_state->route,slot_state->follower.index,position,goal);
    response.nav_state=slot_state->follower.index+1U>=slot_state->route.count ? std::uint8_t{1U} : std::uint8_t{0U};
    return response;
}

void RouteService::serialize(StateWriter& writer) const {
    // Constructor limits are hashed so differently configured services differ.
    for (const float cost : limits_.class_costs) writer.f32(cost);
    writer.u16(limits_.route_capacity);
    writer.u32(limits_.max_expansions);
    writer.boolean(limits_.campaign_mode);
    scheduler_.serialize(writer);
    for (const RouteUnitState& slot_state : units_) slot_state.serialize(writer);
}

} // namespace battletanx
