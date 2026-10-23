#include "battletanx/navigation_route.hpp"

#include <algorithm>
#include <vector>

namespace battletanx {
namespace {

struct PlannerNode {
    bool active{};
    bool closed{};
    std::uint16_t parent{};
    float path_cost{},total_cost{};
    NavigationVertex position{};
};

bool vertex_valid(const NavigationLevel& level,std::uint16_t vertex) noexcept {
    return vertex!=0U && vertex<level.vertices.size();
}

bool triangle_valid(const NavigationLevel& level,std::uint16_t triangle) noexcept {
    if (triangle==0U || triangle>=level.triangles.size()) return false;
    for (const auto& corner : level.triangles[triangle].corners) {
        if (!vertex_valid(level,corner.vertex)) return false;
    }
    return true;
}

std::uint8_t runtime_status(const NavigationLevel& level,std::uint16_t triangle) noexcept {
    return triangle<level.triangles.size() ? level.kind_enabled[level.triangles[triangle].kind]
                                           : std::uint8_t{0U};
}

// Ordered open set equivalent to the retail binary tree: insertion walks left
// when key <= node key, so a new entry lands before every equal key, and
// pop takes the leftmost entry (sort_tree_pop_min_node, func_800DF6F4).
class OpenSet {
public:
    explicit OpenSet(const std::vector<PlannerNode>& nodes) : nodes_(nodes) {}
    void insert(std::uint16_t node) {
        const float key=nodes_[node].total_cost;
        const auto at=std::lower_bound(order_.begin(),order_.end(),key,
            [this](std::uint16_t entry,float value) { return nodes_[entry].total_cost<value; });
        order_.insert(at,node);
    }
    void remove(std::uint16_t node) {
        const auto at=std::find(order_.begin(),order_.end(),node);
        if (at!=order_.end()) order_.erase(at);
    }
    [[nodiscard]] std::uint16_t pop() {
        if (order_.empty()) return 0U;
        const std::uint16_t node=order_.front();
        order_.erase(order_.begin());
        return node;
    }
private:
    const std::vector<PlannerNode>& nodes_;
    std::vector<std::uint16_t> order_;
};

} // namespace

void NavigationRoute::serialize(StateWriter& writer) const {
    writer.u16(count);
    writer.u16(start_triangle);
    writer.u16(goal_triangle);
    writer.u32(expansions);
    writer.u8(static_cast<std::uint8_t>(stop));
    writer.boolean(truncated);
    for (std::size_t index=0; index<count; ++index) {
        writer.u16(corners[index]);
        writer.u16(triangles[index]);
        writer.f32(waypoints[index].x);
        writer.f32(waypoints[index].z);
    }
}

float navigation_route_distance(NavigationVertex a,NavigationVertex b) noexcept {
    float dz=a.z-b.z;
    if (!(0.0F<dz)) dz=-dz;
    float dx=a.x-b.x;
    if (!(0.0F<dx)) dx=-dx;
    if (dx<dz) return dz+dx*3.0F/8.0F;
    return dx+dz*3.0F/8.0F;
}

std::uint8_t navigation_route_class(const NavigationLevel& level,std::uint16_t triangle,
                                    bool campaign_mode) {
    if (triangle>=level.triangles.size()) return 4U;
    const std::uint8_t kind=level.triangles[triangle].kind;
    if (level.kind_enabled[kind]==0U) return 0U;
    if (kind>=15U) return 1U;
    switch (kind) {
    case 0U: return 0U;
    case 5U: return 1U;
    case 13U: return campaign_mode ? 1U : 0U;
    case 6U: return 2U;
    case 14U: return campaign_mode ? 2U : 0U;
    case 4U: return 3U;
    case 12U: return campaign_mode ? 3U : 0U;
    default: return 4U;
    }
}

std::uint8_t retail_route_blocked_mask(const NavigationLevel& level,std::uint16_t start_triangle,
                                       std::uint16_t goal_triangle) {
    const bool relaxed=runtime_status(level,start_triangle)!=0U ||
                       runtime_status(level,goal_triangle)!=0U;
    return relaxed ? std::uint8_t{0xF0U} : std::uint8_t{0xF8U};
}

std::uint16_t locate_route_triangle(const NavigationLevel& level,std::uint16_t known_triangle,
                                    NavigationVertex from,NavigationVertex to) {
    if (!triangle_valid(level,known_triangle) ||
        level.triangles.size()>navigation_route_node_capacity) return 0U;
    for (std::size_t index=1U; index<level.triangles.size(); ++index) {
        for (const auto& corner : level.triangles[index].corners) {
            if (!vertex_valid(level,corner.vertex)) return 0U;
            if (corner.adjacent!=0U && (corner.adjacent/3U==0U ||
                corner.adjacent/3U>=level.triangles.size())) return 0U;
        }
    }
    return trace_navigation_segment(level,known_triangle,from,to,0U).triangle;
}

NavigationRoute plan_navigation_route(const NavigationLevel& level,RouteEndpoint start,
                                      RouteEndpoint goal,std::uint8_t blocked_mask,
                                      const RoutePlannerLimits& limits,
                                      std::span<const std::uint8_t> occupancy) {
    NavigationRoute route;
    route.start_triangle=start.triangle;
    route.goal_triangle=goal.triangle;
    if (level.triangles.size()>navigation_route_node_capacity) {
        route.stop=NavigationRouteStop::invalid_mesh;
        return route;
    }
    if (!triangle_valid(level,start.triangle) || !triangle_valid(level,goal.triangle)) {
        route.stop=NavigationRouteStop::invalid_endpoint;
        return route;
    }

    const auto occupancy_of=[&](std::uint16_t triangle) {
        if (triangle==goal.triangle || triangle>=occupancy.size()) return 0.0F;
        return static_cast<float>(occupancy[triangle]);
    };
    const auto step_cost=[&](std::uint16_t from) {
        return occupancy_of(from)*navigation_route_occupancy_cost+navigation_route_step_cost+
               limits.class_costs[navigation_route_class(level,from,limits.campaign_mode)];
    };

    // The retail search runs from the goal (target) triangle toward the
    // start (unit) triangle, scoring by distance to the start point, so the
    // parent chain from the start reads in travel order.
    std::vector<PlannerNode> nodes(level.triangles.size());
    OpenSet open(nodes);
    PlannerNode& root=nodes[goal.triangle];
    root.active=true;
    root.position=navigation_triangle_centroid(level,goal.triangle);
    root.path_cost=0.0F;
    root.total_cost=navigation_route_distance(root.position,start.point)+root.path_cost;
    open.insert(goal.triangle);

    bool reached=false;
    for (;;) {
        const std::uint16_t current=open.pop();
        if (current==0U) break;
        nodes[current].closed=true;
        if (current==start.triangle) { reached=true; break; }
        if (limits.max_expansions!=0U && route.expansions>=limits.max_expansions) {
            route.stop=NavigationRouteStop::expansion_limit;
            return route;
        }
        ++route.expansions;
        for (const auto& corner : level.triangles[current].corners) {
            if (corner.adjacent==0U) continue;
            const std::uint16_t next=static_cast<std::uint16_t>(corner.adjacent/3U);
            if (!triangle_valid(level,next)) {
                route.stop=NavigationRouteStop::invalid_mesh;
                return route;
            }
            const std::uint8_t kind_class=navigation_route_class(level,next,limits.campaign_mode);
            if ((blocked_mask>>kind_class&1U)!=0U) continue;
            PlannerNode& from=nodes[current];
            PlannerNode& node=nodes[next];
            if (node.active) {
                const float cost=from.path_cost+
                                 navigation_route_distance(from.position,node.position)+
                                 step_cost(current);
                if (node.path_cost<=cost) continue;
                if (!node.closed) open.remove(next);
                node.parent=current;
                node.total_cost=cost+(node.total_cost-node.path_cost);
                node.path_cost=cost;
                node.closed=false;
                open.insert(next);
                continue;
            }
            node.position=navigation_triangle_centroid(level,next);
            node.active=true;
            node.closed=false;
            node.parent=current;
            const float heuristic=navigation_route_distance(node.position,start.point);
            node.path_cost=from.path_cost+navigation_route_distance(from.position,node.position)+
                           step_cost(current);
            node.total_cost=node.path_cost+heuristic;
            open.insert(next);
        }
    }
    if (!reached) {
        route.stop=NavigationRouteStop::unreachable;
        return route;
    }

    // build_route_from_path (func_800DF7D0).
    route.stop=NavigationRouteStop::reached;
    const std::size_t capacity=std::min<std::size_t>(limits.route_capacity,navigation_route_capacity);
    std::uint16_t node=start.triangle;
    std::uint16_t parent=nodes[node].parent;
    while (parent!=0U) {
        if (route.count>=capacity) { route.truncated=true; break; }
        std::uint16_t value=0U;
        const auto& corners=level.triangles[node].corners;
        for (std::uint16_t side=0U; side<3U; ++side) {
            if (parent==corners[side].adjacent/3U) {
                value=static_cast<std::uint16_t>(node*3U+side);
                break;
            }
        }
        route.corners[route.count]=value;
        route.triangles[route.count]=node;
        if (value!=0U) route.waypoints[route.count]=navigation_edge_midpoint(level,value);
        ++route.count;
        node=parent;
        parent=nodes[node].parent;
    }
    return route;
}

std::optional<EvasionSteps> plan_evasion_steps(const NavigationLevel& level,
                                               std::uint16_t triangle,NavigationVertex threat) {
    // choose_cell_exit_edges (func_800E004C) with requireNearOpen=0, requireFarOpen=1.
    if (!triangle_valid(level,triangle)) return std::nullopt;
    const auto& corners=level.triangles[triangle].corners;
    std::array<bool,3> open{};
    float far_distance=0.01F;
    std::uint16_t far_index=0U;
    for (std::uint16_t side=0U; side<3U; ++side) {
        const std::uint16_t adjacent=corners[side].adjacent;
        if (adjacent!=0U) {
            const std::uint16_t next=static_cast<std::uint16_t>(adjacent/3U);
            if (next==0U || next>=level.triangles.size()) return std::nullopt;
            open[side]=runtime_status(level,next)==0U;
        }
        const float distance=navigation_route_distance(
            navigation_edge_midpoint(level,static_cast<std::uint16_t>(triangle*3U+side)),threat);
        if (far_distance<distance) { far_distance=distance; far_index=side; }
    }
    // The near index never leaves 0 (its start value is 16000 and it keeps maxima).
    const std::uint16_t near_index=0U;
    if (near_index==far_index) far_index=static_cast<std::uint16_t>((near_index+1U)%3U);
    const std::uint16_t other_index=static_cast<std::uint16_t>(3U-(near_index+far_index));
    std::uint16_t chosen=0U;
    if (open[far_index]) chosen=far_index;
    else if (open[other_index]) chosen=other_index;
    else return std::nullopt;
    EvasionSteps steps;
    steps.triangle=triangle;
    for (auto& corner : steps.corners) corner=static_cast<std::uint16_t>(triangle*3U+chosen);
    steps.count=3U;
    return steps;
}

void RouteFollower::serialize(StateWriter& writer) const {
    writer.u16(index);
    writer.boolean(active);
    writer.f32(target.x);
    writer.f32(target.z);
    writer.f32(distance);
}

RouteFollowResult update_route_follower(RouteFollower& follower,const NavigationRoute& route,
                                        NavigationVertex position) {
    if (follower.active) {
        follower.distance=navigation_route_distance(position,follower.target);
        if (navigation_route_arrival_radius<follower.distance) return RouteFollowResult::steering;
        follower.active=false;
        // ring_buffer_advance keeps the final entry (count<2 fails).
        if (follower.index+1U>=route.count) return RouteFollowResult::finished;
        ++follower.index;
        follower.target=route.waypoints[follower.index];
        follower.distance=navigation_route_distance(position,follower.target);
        follower.active=route.corners[follower.index]!=0U;
        return RouteFollowResult::advanced;
    }
    if (follower.index>=route.count || route.corners[follower.index]==0U) {
        return RouteFollowResult::idle;
    }
    follower.target=route.waypoints[follower.index];
    follower.distance=navigation_route_distance(position,follower.target);
    follower.active=true;
    return RouteFollowResult::steering;
}

std::optional<std::uint8_t> RouteReplanScheduler::run_tick(
    std::span<const bool,navigation_route_replan_slots> alive,
    std::span<const bool,navigation_route_replan_slots> consumes,bool budget) {
    std::optional<std::uint8_t> spent;
    for (std::uint8_t slot=0U; slot<navigation_route_replan_slots; ++slot) {
        if (!alive[slot]) {
            if (slot==cursor) cursor=static_cast<std::uint8_t>((slot+1U)%navigation_route_replan_slots);
            continue;
        }
        if (slot==cursor && budget) {
            if (consumes[slot]) { budget=false; spent=slot; }
            cursor=static_cast<std::uint8_t>((cursor+1U)%navigation_route_replan_slots);
        }
    }
    return spent;
}

void RouteReplanScheduler::serialize(StateWriter& writer) const { writer.u8(cursor); }

} // namespace battletanx
