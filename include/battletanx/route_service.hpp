#pragma once

#include "battletanx/campaign_ai.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/level.hpp"
#include "battletanx/navigation_route.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace battletanx {

// func_800DEE8C teammate radius D_800743AC (ROM 0x43AC) = 3000.0. A teammate
// at exactly this distance still counts (the skip test is `3000 < d`).
inline constexpr float route_teammate_radius=3000.0F;
// func_800E5FA8 modes 6/12: below D_800746A4 (ROM 0x46A4) = 300.0 the unit
// steers straight at the point; below D_800746A0 (ROM 0x46A0) = 500.0 it
// does so only when unit +0x13C == 1. Neither case calls the planner.
inline constexpr float route_direct_radius=300.0F;
inline constexpr float route_direct_flag_radius=500.0F;
// Retail goal mode 10 (func_800E031C, jump table D_80074668 entry 10) is
// GoalMode::evasion (campaign_ai.hpp).
static_assert(static_cast<std::uint8_t>(GoalMode::evasion)==10U);
// One request slot per unit; the replan cursor walks the same 20 slots.
inline constexpr std::size_t route_service_slots=unit_capacity;
static_assert(route_service_slots==navigation_route_replan_slots);
static_assert(std::is_same_v<std::underlying_type_t<GoalMode>,std::uint8_t>);

// A teammate as seen by func_800DEE8C (team member list, team +0x08/+0x26).
struct RouteTeammate {
    EntityId unit{};
    bool alive{};      // unit +0x01
    bool excluded{};   // unit +0x14 & 4, or func_800E3470 true
    NavigationVertex position{};
    std::uint16_t triangle{}; // unit +0x346
};

// func_800DEE8C occupancy: +1 at each live, non-excluded teammate's triangle
// when approx distance (navigation_route_distance, func_800E0C8C; max+min*3/8) to the unit is <= 3000; the unit
// itself is skipped and its own triangle entry is cleared afterwards.
// Counters are u8 and wrap as the retail `sb` does. Out-of-range triangles
// are ignored. The result has one entry per level triangle.
[[nodiscard]] std::vector<std::uint8_t> build_route_occupancy(const NavigationLevel& level,EntityId self,
                                                              NavigationVertex self_position,
                                                              std::uint16_t self_triangle,
                                                              std::span<const RouteTeammate> teammates);

enum class RouteSubmitResult : std::uint8_t {
    accepted,         // the unit had no active request
    retargeted,       // replaced the active request (target moved / new goal)
    stale_serial,     // serial not newer than the last accepted one for this unit
    invalid_serial,   // serial 0 is reserved for "none"
    unknown_unit,     // slot not bound, or generation does not match
    unsupported_mode, // only modes 5/6/7/8/12 and 10 reach the planner
};
static_assert(std::is_same_v<std::underlying_type_t<RouteSubmitResult>,std::uint8_t>);

enum class RouteServiceStatus : std::uint8_t {
    routed,           // new route attached
    same_triangle,    // start == goal triangle: empty route attached, steer to the goal point
    truncated,        // route attached; steps past the 248-entry ring were dropped
    evasion,          // mode 10: three copies of one exit edge attached
    unreachable,      // disconnected graph; previous route kept (retail returns 0)
    blocked_endpoint, // unreachable and the unit triangle's class is blocked
    off_mesh,         // unit triangle unknown; previous route kept, steer at goal
    invalid_mesh,
    expansion_limit,
};
static_assert(std::is_same_v<std::underlying_type_t<RouteServiceStatus>,std::uint8_t>);

struct RouteServiceResult {
    RouteResponse response{};
    RouteServiceStatus status{RouteServiceStatus::unreachable};
    std::uint8_t slot{};
};

// Route ownership for one unit slot.
struct RouteUnitState {
    EntityId unit{};               // owner (slot + generation); generation 0 = unbound
    std::uint32_t latest_serial{}; // newest accepted request serial (kept across cancel)
    std::uint32_t route_serial{};  // serial of the attached route, 0 = none
    bool active{};                 // request replanned on every cursor visit
    RouteRequest request{};        // valid while active
    std::uint16_t unit_triangle{}; // cached unit +0x346
    bool direct_flag{};            // unit +0x13C == 1 (meaning UNRESOLVED)
    bool retrace{};                // unit +0x35A != 0: re-trace +0x346 before planning (gate CONFIRMED, meaning UNRESOLVED)
    bool has_route{};
    NavigationVertex route_goal{}; // target point of the attached route
    NavigationRoute route{};
    RouteFollower follower{};

    void serialize(StateWriter& writer) const;
};

// Deterministic route service. Each unit owns one request slot (newest serial
// wins); one unit is planned per tick, chosen by the func_800E63F4 cursor.
// An active request is replanned every time the cursor grants it the budget,
// as retail re-runs func_800DFA68 on every budgeted visit.
class RouteService {
public:
    explicit RouteService(RoutePlannerLimits limits={}) : limits_(limits) {}

    // Binds a unit to its slot with its current triangle. A new generation
    // replaces whatever the slot owned (route, serials, request).
    bool bind_unit(EntityId unit,std::uint16_t triangle);
    // Unit destroyed: drops its route and request. Ignores stale ids.
    bool release_unit(EntityId unit);
    bool set_unit_triangle(EntityId unit,std::uint16_t triangle);
    bool set_direct_flag(EntityId unit,bool flag);
    // Unit +0x35A (func_800DEE8C gate): when set, plan() re-traces the cached
    // triangle from its centroid to the unit point first.
    bool set_retrace(EntityId unit,bool flag);
    // Moves the unit end of the active request (unit +0x24/+0x2C).
    bool set_unit_position(EntityId unit,NavigationVertex position);

    RouteSubmitResult submit(const RouteRequest& request);
    // Goal cleared (func_800E3480): drops the request and the route. The
    // latest serial is kept, so older requests stay stale.
    bool cancel(EntityId unit);
    // True when the response belongs to the unit's newest active request.
    [[nodiscard]] bool is_current(const RouteResponse& response) const noexcept;

    // Runs the round-robin cursor once with the tick's budget and plans at
    // most one unit. `occupancy_for[slot]` is build_route_occupancy for that
    // unit (missing or empty = zeros).
    std::optional<RouteServiceResult> tick(const NavigationLevel& level,bool budget=true,
                                           std::span<const std::vector<std::uint8_t>> occupancy_for={});

    // ai_update_route_goal on the attached route, or the modes 6/12 direct
    // steer. Empty without an active request or route.
    std::optional<RouteResponse> follow(EntityId unit,NavigationVertex position);

    [[nodiscard]] const RouteUnitState* state(EntityId unit) const noexcept;
    [[nodiscard]] std::uint8_t cursor() const noexcept { return scheduler_.cursor; }
    [[nodiscard]] const RoutePlannerLimits& limits() const noexcept { return limits_; }

    void serialize(StateWriter& writer) const;

private:
    RouteUnitState* owned(EntityId unit) noexcept;
    RouteServiceResult plan(const NavigationLevel& level,std::uint8_t slot,
                            std::span<const std::uint8_t> occupancy);

    RoutePlannerLimits limits_;
    std::array<RouteUnitState,route_service_slots> units_{};
    RouteReplanScheduler scheduler_{};
};

// Modes 6/12 near-goal bypass (func_800E5FA8 800E6190..800E6204).
[[nodiscard]] bool route_goal_is_direct(GoalMode mode,NavigationVertex from,NavigationVertex to,
                                        bool direct_flag) noexcept;

} // namespace battletanx
