#pragma once

#include "battletanx/level.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace battletanx {

// Retail route ring capacity: func_800DDCA4 stores capacity 0xF8 entries.
inline constexpr std::size_t navigation_route_capacity=0xF8U;
// Retail planner node table: func_800DEE8C clears 0xBB8 nodes of 0x18 bytes.
inline constexpr std::size_t navigation_route_node_capacity=0xBB8U;
// Follower arrival radius: D_800744E0 (ROM 0x44E0) = 40.0.
inline constexpr float navigation_route_arrival_radius=40.0F;
// Replan budget round-robin over the unit table (func_800E63F4).
inline constexpr std::size_t navigation_route_replan_slots=20U;
// Edge cost terms: D_80074404/D_8007440C = 1000.0 per occupancy unit,
// D_80074408/D_80074410 = 200.0 per step (ROM 0x4404..0x4410).
inline constexpr float navigation_route_occupancy_cost=1000.0F;
inline constexpr float navigation_route_step_cost=200.0F;

enum class NavigationRouteStop : std::uint8_t {
    reached,          // the start triangle was popped from the open set
    unreachable,      // the open set emptied first (retail returns 0, keeps the old route)
    invalid_endpoint, // an endpoint triangle is 0 or out of range
    invalid_mesh,     // bad vertex/adjacency index or more than 3000 triangles
    expansion_limit,  // native safety cap (RoutePlannerLimits::max_expansions) hit
};

struct RoutePlannerLimits {
    // D_80135840: per-class extra step cost. It is runtime BSS; the writer
    // was not recovered, so the defaults are zero (UNRESOLVED).
    std::array<float,5> class_costs{};
    // Entries kept by reconstruction; extra steps are dropped (retail push fails).
    std::uint16_t route_capacity{static_cast<std::uint16_t>(navigation_route_capacity)};
    // 0 = unbounded, as in retail. A nonzero value is a native safety stop.
    std::uint32_t max_expansions{};
    // game_mode_is_3_through_6 (func_8007C6D8), consulted by the kind classifier.
    bool campaign_mode{true};
};

struct RouteEndpoint {
    NavigationVertex point{};
    std::uint16_t triangle{};
};

// A route ordered from the start (unit) side to the goal (target) side.
// Each entry is a flattened corner id (triangle*3+side) of the triangle being
// left; its waypoint is that edge's midpoint (get_waypoint_midpoint).
struct NavigationRoute {
    std::array<NavigationVertex,navigation_route_capacity> waypoints{};
    std::array<std::uint16_t,navigation_route_capacity> corners{};
    std::array<std::uint16_t,navigation_route_capacity> triangles{};
    std::uint16_t count{};
    std::uint16_t start_triangle{},goal_triangle{};
    std::uint32_t expansions{};
    NavigationRouteStop stop{NavigationRouteStop::unreachable};
    bool truncated{};

    void serialize(StateWriter& writer) const;
};

// ai_horizontal_distance (func_800E0C8C): max + min*3/8 (D_8007443C/D_80074440).
[[nodiscard]] float navigation_route_distance(NavigationVertex a,NavigationVertex b) noexcept;

// classify_object_record_kind (func_800DF214): 0..4. Class 0 when the kind is
// disabled in kind_enabled.
[[nodiscard]] std::uint8_t navigation_route_class(const NavigationLevel& level,
                                                  std::uint16_t triangle,bool campaign_mode);

// func_800DEE8C: classes above 2 are blocked, above 3 when either endpoint
// triangle has a nonzero runtime status. Bit c set = class c blocked.
[[nodiscard]] std::uint8_t retail_route_blocked_mask(const NavigationLevel& level,
                                                     std::uint16_t start_triangle,
                                                     std::uint16_t goal_triangle);

// func_800DE3EC: walk from a known triangle toward a point and return the
// triangle reached (mask 0, so runtime status never stops the walk). Returns
// 0 for an invalid known triangle.
[[nodiscard]] std::uint16_t locate_route_triangle(const NavigationLevel& level,
                                                  std::uint16_t known_triangle,
                                                  NavigationVertex from,NavigationVertex to);

// find_ai_route_step (func_800DFA68). `occupancy` is the per-triangle
// teammate count built by func_800DEE8C (empty = all zero); the goal entry is
// treated as zero as retail clears it.
[[nodiscard]] NavigationRoute plan_navigation_route(const NavigationLevel& level,
                                                    RouteEndpoint start,RouteEndpoint goal,
                                                    std::uint8_t blocked_mask,
                                                    const RoutePlannerLimits& limits={},
                                                    std::span<const std::uint8_t> occupancy={});

// extend_route_three_steps (func_800E031C), mode 10: pick the open edge whose
// midpoint is farthest from `threat` and push it three times (the retail loop
// sets node = edge/3, which is the same triangle).
struct EvasionSteps {
    std::array<std::uint16_t,3> corners{};
    std::uint16_t count{};
    std::uint16_t triangle{};
};
[[nodiscard]] std::optional<EvasionSteps> plan_evasion_steps(const NavigationLevel& level,
                                                             std::uint16_t triangle,
                                                             NavigationVertex threat);

// ai_update_route_goal (func_800E1D40) state, index based.
struct RouteFollower {
    std::uint16_t index{};
    bool active{};
    NavigationVertex target{};
    float distance{};

    void serialize(StateWriter& writer) const;
};

enum class RouteFollowResult : std::uint8_t { idle, steering, advanced, finished };

RouteFollowResult update_route_follower(RouteFollower& follower,const NavigationRoute& route,
                                        NavigationVertex position);

// func_800E63F4 replan budget cursor (D_801358B0).
struct RouteReplanScheduler {
    std::uint8_t cursor{};

    // `alive` false skips a slot (and moves the cursor past it). The cursor
    // slot receives the budget when it is still available; `consumes` says
    // whether that unit's goal mode spends it. Returns the slot that spent it.
    std::optional<std::uint8_t> run_tick(std::span<const bool,navigation_route_replan_slots> alive,
                                         std::span<const bool,navigation_route_replan_slots> consumes,
                                         bool budget=true);
    void serialize(StateWriter& writer) const;
};

} // namespace battletanx
