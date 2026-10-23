#pragma once

#include "battletanx/rom.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battletanx {

struct RomRange {
    std::size_t start{};
    std::size_t end{};
};

struct LevelStreamDescriptor {
    int index{};
    RomRange world;
    std::optional<RomRange> navigation;
};

struct WorldGroup {
    std::uint16_t item_count{};
    std::uint16_t first_item{};
    std::array<std::int32_t,4> bounds{};
};

struct WorldItem {
    std::uint8_t flags{};
    std::uint8_t kind{};
    std::uint8_t parameter{};
    std::uint8_t attributes{};
    std::uint16_t id{};
    std::int32_t type{};
    float x{};
    float z{};
    std::uint16_t angle{};
    std::int16_t link{};
    std::int16_t shape{};
};

struct WorldShape {
    std::uint8_t flags{};
    std::uint16_t slot{};
    std::int16_t min_x{};
    std::int16_t min_z{};
    std::int16_t max_x{};
    std::int16_t max_z{};
};

struct WorldLevel {
    std::uint32_t flags{};
    std::array<std::size_t,9> section_offsets{};
    std::vector<WorldGroup> groups;
    std::vector<WorldItem> items;
    std::vector<WorldShape> shapes;
};

struct NavigationVertex { float x{},z{}; };
struct NavigationCorner { std::uint16_t vertex{},adjacent{}; };
struct NavigationTriangle {
    std::array<NavigationCorner,3> corners{};
    std::uint8_t kind{};
};
struct NavigationLevel {
    std::vector<NavigationVertex> vertices;
    std::vector<NavigationTriangle> triangles;
    std::array<std::uint8_t,256> kind_enabled{};
};

enum class NavigationTraceStop {
    target,
    boundary,
    blocked,
    iteration_limit,
};

struct NavigationTrace {
    std::vector<std::uint16_t> edges;
    std::uint16_t triangle{};
    NavigationTraceStop stop{NavigationTraceStop::boundary};
};

std::span<const LevelStreamDescriptor> level_stream_descriptors() noexcept;
WorldLevel parse_world_level(std::span<const std::uint8_t> bytes);
NavigationLevel parse_navigation_level(std::span<const std::uint8_t> bytes);
WorldLevel load_world_level(const Rom& rom,const LevelStreamDescriptor& descriptor);
NavigationLevel load_navigation_level(const Rom& rom,const LevelStreamDescriptor& descriptor);
NavigationVertex navigation_triangle_centroid(const NavigationLevel& level,
                                               std::uint16_t triangle);
NavigationVertex navigation_edge_midpoint(const NavigationLevel& level,
                                           std::uint16_t corner);
bool navigation_point_inside_triangle(const NavigationLevel& level,
                                      NavigationVertex point,std::uint16_t triangle);
std::uint16_t find_nearest_navigation_triangle(const NavigationLevel& level,
                                               NavigationVertex point);
NavigationTrace trace_navigation_segment(const NavigationLevel& level,
                                         std::uint16_t start_triangle,
                                         NavigationVertex from,
                                         NavigationVertex to,
                                         std::uint8_t blocked_mask=0xFFU);

} // namespace battletanx
