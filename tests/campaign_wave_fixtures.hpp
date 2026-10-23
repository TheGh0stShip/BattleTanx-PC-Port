#pragma once

// Synthetic per-wave level fixtures for the 17 campaign waves (test support).
//
// No retail bytes: every position, id and shape is generated. Only the
// structure follows the recovered wave data: game kind, owner count, marker
// counts per group, type-22 owned-object counts per sub-kind, spawner counts
// (placed type 20 / bank type 21), placed bases of types 11..14, the kind-4
// objective marker (bank type 24) and whether the team spawn table is filled.
// Counts come from the world-objective / spawner audits of the campaign
// streams (numeric metadata only).
//
// Layout of every fixture world:
//   group 0  placed scenery (type 0, one shape each)
//   group 1  entity bank (WorldLevel::bank_group = 1): markers (25), owned
//            objects (22), pickups (10), bank spawners (21), objective marker (24)
//   group 2  placed objects: bases (11..14) and linked spawners (20)
// The navigation mesh is an 8x8 grid of 1000-unit cells covering
// [-4000,4000] in x and z, wound clockwise as trace_navigation_segment and
// navigation_point_inside_triangle expect. Every generated position lies
// strictly inside one triangle (never on an edge or diagonal).

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_spawn.hpp"
#include "battletanx/level.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/queen_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace battletanx::wave_fixture {

inline constexpr std::size_t grid_cells=8U;
inline constexpr float grid_cell_size=1000.0F;
inline constexpr float grid_origin=-4000.0F;
inline constexpr std::size_t scenery_item_count=6U;
inline constexpr std::size_t fixed_pickups=4U;
inline constexpr std::uint16_t objective_marker_extent=600U;
inline constexpr std::uint8_t flag_campaign_only=0x10U;
inline constexpr std::uint8_t flag_skirmish_only=0x40U;
inline constexpr std::uint8_t flag_capacity_gated=0x02U;

// Per-wave structure (numeric metadata recovered from the wave streams).
struct WaveShape {
    std::int32_t game_kind{};
    std::uint8_t owner_count{};
    std::array<std::uint8_t,marker_groups> markers{};        // bank type 25 per group
    std::array<std::uint16_t,4> owned_by_sub_kind{};         // bank type 22, sub-kind (id>>3)&3
    bool owned_optional{};                                   // all type-22 entries carry id&1
    std::uint8_t placed_spawners{};                          // placed type 20
    std::uint8_t bank_spawners{};                            // bank type 21
    std::array<bool,4> base_active{};                        // placed type 11+n kept in campaign
    std::array<bool,4> base_filtered{};                      // placed type 11+n present but flag 0x40
    bool objective_marker{};                                 // bank type 24
    bool team_table_filled{};
};

inline constexpr std::array<WaveShape,campaign_wave_count> wave_shapes{{
    // kind owners markers        owned             opt  p20 b21 bases active    bases filtered  t24    table
    {5,2,{1,5,0,0},{1,0,0,2},   false,0,0,{true,true,true,true},{false,false,false,false},false,true},
    {4,3,{1,4,3,0},{7,0,0,0},   true, 0,0,{},{},                                           true, false},
    {5,4,{1,5,5,5},{5,0,0,0},   false,0,0,{},{true,true,true,true},                        false,true},
    {4,3,{1,4,4,0},{16,0,0,0},  false,2,0,{},{},                                           true, false},
    {7,4,{0,0,0,0},{0,0,0,0},   false,0,3,{},{},                                           false,true},
    {4,4,{1,5,5,4},{23,2,0,0},  false,2,0,{false,true,false,false},{},                     false,false},
    {3,4,{5,5,5,5},{17,18,0,0}, false,2,4,{true,true,false,false},{false,false,true,true}, false,true},
    {3,4,{5,5,5,5},{23,23,1,0}, false,5,5,{true,true,false,true},{false,false,true,false}, false,true},
    {7,4,{0,0,0,0},{0,0,0,0},   false,0,3,{},{},                                           false,true},
    {4,4,{4,5,5,5},{18,6,4,0},  false,3,0,{},{},                                           true, false},
    {3,4,{5,5,5,5},{47,24,6,3}, false,6,4,{true,true,true,true},{},                        false,true},
    {3,4,{5,5,5,5},{6,20,2,2},  false,1,4,{true,true,true,true},{},                        false,true},
    {7,4,{0,0,0,0},{0,0,0,0},   false,0,4,{},{},                                           false,true},
    {4,4,{1,5,5,5},{11,15,14,2},false,2,1,{},{},                                           true, true},
    {3,4,{5,5,5,5},{1,8,4,5},   false,3,2,{true,true,true,true},{},                        false,true},
    {7,4,{0,0,0,0},{0,0,0,0},   false,0,5,{},{},                                           false,true},
    {3,4,{5,5,5,5},{49,23,5,7}, false,2,2,{true,false,false,true},{},                      false,false},
}};

struct ExpectedCounts {
    std::array<std::size_t,marker_groups> markers{};
    std::size_t owned_objects{};          // with optional placements enabled
    std::size_t owned_optional{};
    std::array<std::size_t,4> owned_by_sub_kind{};
    std::size_t placed_spawners{};
    std::size_t bank_spawners{};
    std::size_t pickups{};
    std::size_t objective_markers{};
    std::size_t bases_present{};          // placed 11..14 items, active or filtered
    std::size_t bases_active{};
    std::size_t queens{};                 // queen_world placement result
};

struct CampaignWaveFixture {
    std::uint8_t wave_index{};
    const CampaignWaveDefinition* wave{};
    std::int32_t game_kind{};
    std::uint8_t owner_count{};
    WorldLevel world;
    NavigationLevel navigation;
    TeamSpawnTable team_spawns{};
    bool team_table_filled{};
    // Item indices into world.items, by role.
    std::vector<std::uint32_t> scenery_items;
    std::array<std::vector<std::uint32_t>,marker_groups> marker_items;
    std::vector<std::uint32_t> owned_items;
    std::vector<std::uint32_t> pickup_items;
    std::vector<std::uint32_t> bank_spawner_items;
    std::vector<std::uint32_t> objective_items;
    std::vector<std::uint32_t> base_items;
    std::vector<std::uint32_t> placed_spawner_items;
    ExpectedCounts expected;
};

namespace detail {

inline std::uint16_t grid_vertex(std::size_t x,std::size_t y) noexcept {
    return static_cast<std::uint16_t>(1U+y*(grid_cells+1U)+x);
}

inline NavigationLevel make_grid_navigation() {
    NavigationLevel level;
    level.vertices.push_back({});
    level.triangles.push_back({});
    for (std::size_t y=0U; y<=grid_cells; ++y) {
        for (std::size_t x=0U; x<=grid_cells; ++x) {
            level.vertices.push_back({grid_origin+static_cast<float>(x)*grid_cell_size,
                                      grid_origin+static_cast<float>(y)*grid_cell_size});
        }
    }
    for (std::size_t y=0U; y<grid_cells; ++y) {
        for (std::size_t x=0U; x<grid_cells; ++x) {
            NavigationTriangle a,b;
            a.corners={{{grid_vertex(x,y),0U},{grid_vertex(x+1U,y+1U),0U},{grid_vertex(x+1U,y),0U}}};
            b.corners={{{grid_vertex(x,y),0U},{grid_vertex(x,y+1U),0U},{grid_vertex(x+1U,y+1U),0U}}};
            level.triangles.push_back(a);
            level.triangles.push_back(b);
        }
    }
    std::map<std::pair<std::uint16_t,std::uint16_t>,std::uint16_t> edges;
    for (std::size_t t=1U; t<level.triangles.size(); ++t) {
        for (std::size_t side=0U; side<3U; ++side) {
            auto& corners=level.triangles[t].corners;
            std::uint16_t a=corners[side].vertex,b=corners[(side+1U)%3U].vertex;
            if (b<a) std::swap(a,b);
            const auto id=static_cast<std::uint16_t>(t*3U+side);
            const auto found=edges.find({a,b});
            if (found==edges.end()) { edges.emplace(std::make_pair(a,b),id); continue; }
            corners[side].adjacent=found->second;
            level.triangles[found->second/3U].corners[found->second%3U].adjacent=id;
        }
    }
    return level;
}

// Position n: cell (n%8, (n/8)%8), offset (250,600) inside the cell, so the
// point is in the upper (b) triangle and never on an edge or the diagonal.
inline NavigationVertex grid_position(std::size_t n) noexcept {
    const std::size_t cx=n%grid_cells;
    const std::size_t cz=(n/grid_cells)%grid_cells;
    return {grid_origin+static_cast<float>(cx)*grid_cell_size+250.0F,
            grid_origin+static_cast<float>(cz)*grid_cell_size+600.0F};
}

inline WorldItem make_item(std::int32_t type,std::uint8_t flags,std::uint8_t parameter,
                           std::uint16_t id,std::size_t position,std::int16_t shape=-1) {
    const NavigationVertex at=grid_position(position);
    WorldItem item;
    item.flags=flags;
    item.kind=0U;
    item.parameter=parameter;
    item.id=id;
    item.type=type;
    item.x=at.x;
    item.z=at.z;
    item.angle=static_cast<std::uint16_t>((position*0x200U)&0xFFFU);
    item.link=-1;
    item.shape=shape;
    return item;
}

inline void close_group(WorldLevel& world,std::size_t first) {
    WorldGroup group;
    group.first_item=static_cast<std::uint16_t>(first);
    group.item_count=static_cast<std::uint16_t>(world.items.size()-first);
    group.bounds={static_cast<std::int32_t>(grid_origin),static_cast<std::int32_t>(grid_origin),
                  static_cast<std::int32_t>(-grid_origin),static_cast<std::int32_t>(-grid_origin)};
    world.groups.push_back(group);
}

} // namespace detail

[[nodiscard]] inline CampaignWaveFixture campaign_wave_fixture(std::size_t wave_index) {
    if (wave_index>=campaign_wave_count) throw std::out_of_range("campaign wave fixture index");
    const WaveShape& shape=wave_shapes[wave_index];
    CampaignWaveFixture f;
    f.wave_index=static_cast<std::uint8_t>(wave_index);
    f.wave=&campaign_wave_definitions()[wave_index];
    f.game_kind=shape.game_kind;
    f.owner_count=shape.owner_count;
    f.team_table_filled=shape.team_table_filled;
    f.navigation=detail::make_grid_navigation();
    const bool campaign=campaign_mode(shape.game_kind);
    const std::uint8_t mode_flags=campaign ? flag_campaign_only : std::uint8_t{0U};
    std::size_t position=wave_index;   // vary layouts per wave
    WorldLevel& world=f.world;
    const auto add=[&](std::int32_t type,std::uint8_t flags,std::uint8_t parameter,
                       std::uint16_t id,std::int16_t item_shape=-1) {
        world.items.push_back(detail::make_item(type,flags,parameter,id,position++,item_shape));
        return static_cast<std::uint32_t>(world.items.size()-1U);
    };

    // Group 0: scenery with shapes.
    for (std::size_t n=0U; n<scenery_item_count; ++n) {
        WorldShape s;
        s.lod_count=1U;
        s.first_range=static_cast<std::uint16_t>(n);
        s.min_x=-100; s.min_z=-100; s.max_x=100; s.max_z=100;
        world.shapes.push_back(s);
        world.model_ranges.push_back(WorldModelRange{.part_count=0U,.first_part=0U});
        f.scenery_items.push_back(add(0,0U,0U,static_cast<std::uint16_t>(n),
                                      static_cast<std::int16_t>(n)));
    }
    detail::close_group(world,0U);

    // Group 1: entity bank.
    const std::size_t bank_first=world.items.size();
    for (std::size_t group=0U; group<marker_groups; ++group) {
        for (std::size_t n=0U; n<shape.markers[group]; ++n) {
            // Owner 0's first marker selects lineup slot 0 (kind 10); every
            // other marker places a tank or bike directly.
            const std::uint8_t low=(group==0U && n==0U) ? marker_lineup_base
                                                         : static_cast<std::uint8_t>(n%2U);
            f.marker_items[group].push_back(add(25,mode_flags,
                static_cast<std::uint8_t>((group<<4U)|low),static_cast<std::uint16_t>(n)));
        }
    }
    for (std::size_t sub=0U; sub<4U; ++sub) {
        for (std::size_t n=0U; n<shape.owned_by_sub_kind[sub]; ++n) {
            const auto owner=static_cast<std::uint16_t>(n%shape.owner_count);
            const auto id=static_cast<std::uint16_t>((sub<<3U)|(owner<<1U)|
                                                     (shape.owned_optional ? 1U : 0U));
            f.owned_items.push_back(add(22,0U,0U,id));
        }
    }
    for (std::size_t n=0U; n<fixed_pickups; ++n) {
        f.pickup_items.push_back(add(10,mode_flags,static_cast<std::uint8_t>(n),0U));
    }
    const auto spawner_id=[&](std::size_t n) {
        // Computer owners 1..owner_count-1, mode n%4, count field 4, gate 0.
        const std::size_t owner=1U+n%(shape.owner_count-1U);
        return static_cast<std::uint16_t>(owner|((n%4U)<<2U)|(4U<<4U));
    };
    for (std::size_t n=0U; n<shape.bank_spawners; ++n) {
        f.bank_spawner_items.push_back(add(21,0U,0U,spawner_id(n)));
    }
    if (shape.objective_marker) {
        f.objective_items.push_back(add(24,0U,0U,objective_marker_extent));
    }
    detail::close_group(world,bank_first);
    world.bank_group=1U;

    // Group 2: placed bases and linked spawners.
    const std::size_t placed_first=world.items.size();
    for (std::size_t slot=0U; slot<4U; ++slot) {
        if (!shape.base_active[slot] && !shape.base_filtered[slot]) continue;
        auto flags=static_cast<std::uint8_t>(flag_capacity_gated|(slot<<2U));
        if (shape.base_filtered[slot]) flags=static_cast<std::uint8_t>(flags|(campaign ? flag_skirmish_only
                                                                                     : flag_campaign_only));
        f.base_items.push_back(add(static_cast<std::int32_t>(11U+slot),flags,
                                   static_cast<std::uint8_t>(11U),0U));
    }
    for (std::size_t n=0U; n<shape.placed_spawners; ++n) {
        f.placed_spawner_items.push_back(add(20,0U,0U,spawner_id(n+shape.bank_spawners)));
    }
    detail::close_group(world,placed_first);

    // Team spawn table (rows 0..3, five points each).
    if (shape.team_table_filled) {
        for (std::size_t row=0U; row<team_spawn_rows; ++row) {
            for (std::size_t column=0U; column<team_spawns_per_row; ++column) {
                const NavigationVertex at=detail::grid_position(40U+row*team_spawns_per_row+column);
                f.team_spawns[row][column]=TeamSpawnPoint{at.x,at.z,
                    static_cast<std::uint16_t>(row*0x400U)};
            }
        }
    }

    ExpectedCounts& e=f.expected;
    for (std::size_t g=0U; g<marker_groups; ++g) e.markers[g]=shape.markers[g];
    for (std::size_t s=0U; s<4U; ++s) {
        e.owned_by_sub_kind[s]=shape.owned_by_sub_kind[s];
        e.owned_objects+=shape.owned_by_sub_kind[s];
    }
    e.owned_optional=shape.owned_optional ? e.owned_objects : 0U;
    e.placed_spawners=campaign ? shape.placed_spawners : 0U;
    e.bank_spawners=shape.bank_spawners;
    e.pickups=fixed_pickups;
    e.objective_markers=shape.objective_marker ? 1U : 0U;
    e.bases_present=f.base_items.size();
    const bool bases_used=shape.game_kind==0 || shape.game_kind==3 || shape.game_kind==4 ||
                          shape.game_kind==6;
    for (std::size_t slot=0U; slot<4U; ++slot) {
        if (!shape.base_active[slot]) continue;
        ++e.bases_active;
        if (bases_used && slot<shape.owner_count &&
            !(shape.game_kind==6 && slot==0U)) ++e.queens;
    }
    return f;
}

// Spawn-plan input for the fixture: owner 0 human (lineup {tank}), the rest
// computer-controlled, teams from the catalog, single view, all owners active.
[[nodiscard]] inline CampaignSpawnInput campaign_wave_spawn_input(const CampaignWaveFixture& f,
                                                                 bool optional_placements=true) {
    CampaignSpawnInput input;
    input.game_kind=f.game_kind;
    input.view_count=1;
    input.active_player_count=f.owner_count;
    input.optional_placements_enabled=optional_placements;
    for (std::size_t owner=0U; owner<f.owner_count && owner<spawn_owner_count; ++owner) {
        SpawnOwnerSetup& setup=input.owners[owner];
        setup.present=true;
        setup.computer=owner!=0U;
        setup.team=static_cast<std::uint8_t>(f.wave->owners[owner].team);
        setup.lineup={0U,0U,0U,0U,0U};
        setup.lineup_count=1U;
    }
    if (!campaign_mode(f.game_kind)) input.team_spawns=f.team_spawns;
    return input;
}

[[nodiscard]] inline queen_world::Config campaign_wave_queen_config(const CampaignWaveFixture& f) {
    queen_world::Config config;
    config.game_kind=static_cast<std::uint32_t>(f.game_kind);
    config.owner_count=f.owner_count;
    for (std::size_t owner=0U; owner<f.owner_count && owner<config.owners.size(); ++owner) {
        config.owners[owner]=queen_world::OwnerInfo{.present=true,
            .team=static_cast<std::uint8_t>(f.wave->owners[owner].team)};
    }
    return config;
}

} // namespace battletanx::wave_fixture
