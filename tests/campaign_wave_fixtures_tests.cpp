#include "campaign_wave_fixtures.hpp"

#include "battletanx/spawner_runtime.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <numeric>
#include <optional>

using namespace battletanx;
using namespace battletanx::wave_fixture;

namespace {

std::size_t present_marker_units(const CampaignWaveFixture& f) {
    std::size_t total=0U;
    for (std::size_t owner=0U; owner<f.owner_count && owner<marker_groups; ++owner) {
        total+=f.expected.markers[owner];
    }
    return total;
}

std::optional<CampaignSpawnPlan> plan_for(const CampaignWaveFixture& f,bool optional=true,
                                          SpawnPlanError* error=nullptr) {
    RetailRandom rng(7U);
    return build_campaign_spawn_plan(campaign_wave_spawn_input(f,optional),f.world,
                                     &f.navigation,rng,error);
}

std::size_t all_items_inside(const CampaignWaveFixture& f) {
    std::size_t inside=0U;
    for (const WorldItem& item : f.world.items) {
        const NavigationVertex at{item.x,item.z};
        const std::uint16_t t=find_nearest_navigation_triangle(f.navigation,at);
        if (navigation_point_inside_triangle(f.navigation,at,t)) ++inside;
    }
    return inside;
}

} // namespace

RUNTIME_TEST(fixtures_match_catalog_kind_and_owner_count) {
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture f=campaign_wave_fixture(w);
        REQUIRE(f.wave!=nullptr);
        REQUIRE(f.wave->wave_index==w);
        REQUIRE(f.wave->game_kind==f.game_kind);
        REQUIRE(f.wave->owner_count==static_cast<std::int32_t>(f.owner_count));
        for (std::size_t owner=0U; owner<f.owner_count; ++owner) {
            REQUIRE(f.wave->owners[owner].team>=0 && f.wave->owners[owner].team<4);
        }
        const bool kind4_marker=w==1U || w==3U || w==9U || w==13U;
        REQUIRE((f.expected.objective_markers==1U)==kind4_marker);
        if (kind4_marker) REQUIRE(f.game_kind==4);
    }
    return true;
}

RUNTIME_TEST(fixture_world_layout_is_consistent) {
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture f=campaign_wave_fixture(w);
        REQUIRE(f.world.groups.size()==3U);
        REQUIRE(f.world.bank_group==std::optional<std::size_t>{1U});
        std::size_t covered=0U;
        for (const WorldGroup& g : f.world.groups) {
            REQUIRE(g.first_item==covered);
            covered+=g.item_count;
        }
        REQUIRE(covered==f.world.items.size());
        REQUIRE(f.world.shapes.size()==scenery_item_count);
        for (const std::uint32_t index : f.scenery_items) {
            const auto shape=f.world.items[index].shape;
            REQUIRE(shape>=0 && static_cast<std::size_t>(shape)<f.world.shapes.size());
        }
        REQUIRE(f.owned_items.size()==f.expected.owned_objects);
        REQUIRE(f.pickup_items.size()==fixed_pickups);
        REQUIRE(f.bank_spawner_items.size()==f.expected.bank_spawners);
        REQUIRE(f.base_items.size()==f.expected.bases_present);
        for (const std::uint32_t index : f.base_items) {
            const std::int32_t type=f.world.items[index].type;
            REQUIRE(type>=11 && type<=14);
        }
        if (!f.team_table_filled) REQUIRE(f.team_spawns[0][0].x==0.0F);
        if (f.game_kind==7) REQUIRE(f.team_table_filled);
    }
    return true;
}

RUNTIME_TEST(fixture_navigation_is_valid_clockwise_grid) {
    const CampaignWaveFixture f=campaign_wave_fixture(0U);
    const NavigationLevel& nav=f.navigation;
    REQUIRE(nav.triangles.size()==1U+grid_cells*grid_cells*2U);
    for (std::size_t t=1U; t<nav.triangles.size(); ++t) {
        for (std::size_t side=0U; side<3U; ++side) {
            const std::uint16_t adjacent=nav.triangles[t].corners[side].adjacent;
            if (adjacent==0U) continue;
            const auto& back=nav.triangles[adjacent/3U].corners[adjacent%3U];
            REQUIRE(back.adjacent==static_cast<std::uint16_t>(t*3U+side));
        }
        // Centroid lies inside its own triangle only with the expected winding.
        REQUIRE(navigation_point_inside_triangle(nav,
            navigation_triangle_centroid(nav,static_cast<std::uint16_t>(t)),
            static_cast<std::uint16_t>(t)));
    }
    const NavigationVertex from=detail::grid_position(0U);
    const NavigationVertex to=detail::grid_position(63U);
    const std::uint16_t start=find_nearest_navigation_triangle(nav,from);
    REQUIRE(navigation_point_inside_triangle(nav,from,start));
    const NavigationTrace trace=trace_navigation_segment(nav,start,from,to);
    REQUIRE(trace.stop==NavigationTraceStop::target);
    REQUIRE(navigation_point_inside_triangle(nav,to,trace.triangle));
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture g=campaign_wave_fixture(w);
        REQUIRE(all_items_inside(g)==g.world.items.size());
    }
    return true;
}

RUNTIME_TEST(fixtures_pass_interpret_level_spawns) {
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture f=campaign_wave_fixture(w);
        SpawnContext context;
        context.game_mode=f.game_kind;
        context.view_count=1;
        context.bank_group=f.world.bank_group;
        context.optional_placements_enabled=true;
        context.active_player_count=f.owner_count;
        const LevelSpawnPlan plan=interpret_level_spawns(f.world,context);
        for (std::size_t g=0U; g<marker_groups; ++g) REQUIRE(plan.markers[g].size()==f.expected.markers[g]);
        REQUIRE(plan.units.size()==f.expected.owned_objects);
        REQUIRE(plan.pickups.size()==f.expected.pickups);
        REQUIRE(plan.random_spots.empty());
        REQUIRE(plan.spawners.size()==f.expected.placed_spawners+f.expected.bank_spawners);
        std::size_t linked=0U;
        for (const SpawnerDefinition& s : plan.spawners) {
            if (s.linked) ++linked;
            REQUIRE(s.owner>=1U && s.owner<f.owner_count);
            REQUIRE(s.interval_ticks==45*4);
        }
        REQUIRE(linked==f.expected.placed_spawners);
        REQUIRE(plan.skipped_count(SkipReason::untraced_type)==f.expected.objective_markers);
        REQUIRE(plan.skipped_count(SkipReason::marker_group_full)==0U);
        REQUIRE(plan.skipped_count(SkipReason::marker_group_out_of_range)==0U);
        REQUIRE(plan.skipped_count(SkipReason::player_slot_absent)==0U);
        REQUIRE(plan.skipped_count(SkipReason::unknown_placed_type)==0U);
        REQUIRE(plan.skipped_count(SkipReason::retail_ignored_type)==0U);
        REQUIRE(plan.skipped_count(SkipReason::pickup_mode_filtered)==0U);
        REQUIRE(plan.skipped_count(SkipReason::linked_spawner_mode)==0U);
        REQUIRE(plan.skipped_count(SkipReason::placed_object_other)==
                scenery_item_count+f.expected.bases_active);
        REQUIRE(plan.skipped_count(SkipReason::mode_flag_filtered)==
                f.expected.bases_present-f.expected.bases_active);
    }
    return true;
}

RUNTIME_TEST(fixtures_pass_campaign_spawn_plan) {
    std::size_t owned_total=0U,spawner_total=0U,marker_total=0U;
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture f=campaign_wave_fixture(w);
        SpawnPlanError error{};
        const auto plan=plan_for(f,true,&error);
        REQUIRE(plan.has_value());
        REQUIRE(plan->campaign==campaign_mode(f.game_kind));
        REQUIRE(plan->audits.empty());
        REQUIRE(plan->player.navigation_triangle.has_value());
        REQUIRE(plan->player.kind==retail::UnitKind::tank);
        REQUIRE(plan->owned_objects.size()==f.expected.owned_objects);
        std::array<std::size_t,4> by_sub{};
        for (const OwnedObjectPlacement& o : plan->owned_objects) {
            REQUIRE(o.owner<f.owner_count);
            ++by_sub[o.sub_kind&3U];
        }
        REQUIRE(by_sub==f.expected.owned_by_sub_kind);
        REQUIRE(plan->spawners.size()==f.expected.placed_spawners+f.expected.bank_spawners);
        REQUIRE(plan->pickups.size()==f.expected.pickups);
        if (plan->campaign) {
            REQUIRE(plan->units.size()==present_marker_units(f));
            REQUIRE(plan->player.source==PlacementSource::campaign_marker);
            REQUIRE(plan->player.placement_index==f.marker_items[0].front());
        } else {
            REQUIRE(plan->units.size()==f.owner_count);
            REQUIRE(plan->player.source==PlacementSource::team_spawn_table);
        }
        for (std::size_t owner=0U; owner<f.owner_count; ++owner) REQUIRE(plan->owners[owner].present);
        owned_total+=plan->owned_objects.size();
        spawner_total+=plan->spawners.size();
        for (std::size_t g=0U; g<marker_groups; ++g) marker_total+=f.expected.markers[g];
    }
    REQUIRE(owned_total==420U);
    REQUIRE(spawner_total==65U);
    REQUIRE(marker_total==209U);
    return true;
}

RUNTIME_TEST(optional_owned_objects_are_gated_in_wave_1) {
    const CampaignWaveFixture f=campaign_wave_fixture(1U);
    REQUIRE(f.expected.owned_optional==7U);
    const auto plan=plan_for(f,false);
    REQUIRE(plan.has_value());
    REQUIRE(plan->owned_objects.empty());
    std::size_t gated=0U;
    for (const SkippedItem& s : plan->skipped) {
        if (s.reason==SkipReason::optional_entry_gated) ++gated;
    }
    REQUIRE(gated==7U);
    return true;
}

RUNTIME_TEST(fixtures_place_bases_queens_and_markers) {
    std::size_t queen_total=0U,marker_total=0U;
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture f=campaign_wave_fixture(w);
        const auto plan=plan_for(f);
        REQUIRE(plan.has_value());
        queen_world::CreateResult created=queen_world::QueenWorld::create(campaign_wave_queen_config(f));
        REQUIRE(created.error==queen_world::ConfigError::none);
        REQUIRE(created.world.has_value());
        queen_world::QueenWorld& world=*created.world;
        const auto objects=queen_level_objects(*plan,f.world);
        REQUIRE(objects.size()==f.expected.bases_present);
        std::size_t queens=0U,bases=0U;
        for (const queen_world::LevelObject& object : objects) {
            const queen_world::PlaceResult placed=world.place_level_object(object);
            if (placed.outcome==queen_world::PlaceOutcome::placed) {
                ++bases;
                if (placed.queen.valid()) ++queens;
            } else if (object.active) {
                REQUIRE(placed.outcome==queen_world::PlaceOutcome::kind_has_no_bases ||
                        placed.outcome==queen_world::PlaceOutcome::owner_out_of_range);
            } else {
                REQUIRE(placed.outcome==queen_world::PlaceOutcome::inactive);
            }
        }
        REQUIRE(queens==f.expected.queens);
        const auto entries=objective_marker_entries(*plan,f.world);
        REQUIRE(entries.size()==f.expected.objective_markers);
        for (const queen_world::MarkerBankEntry& entry : entries) {
            const queen_world::MarkerPlaceResult placed=world.place_bank_entry(entry,1U);
            REQUIRE(placed.outcome==queen_world::PlaceOutcome::placed);
            queen_world::MarkerView view;
            REQUIRE(world.view(placed.marker,view));
            REQUIRE(view.extent==static_cast<std::int16_t>(objective_marker_extent));
            ++marker_total;
        }
        world.finish_placement();
        REQUIRE(world.ready());
        REQUIRE(world.total_bases()==bases);
        queen_total+=queens;
    }
    // Kind-3 waves 6,7,10,11,14,16 plus the kind-4 base of wave 5. Wave 0's
    // bases are kind 5, which never assigns bases.
    REQUIRE(queen_total==20U);
    REQUIRE(marker_total==4U);
    return true;
}

RUNTIME_TEST(fixtures_create_runtime_spawners) {
    for (std::size_t w=0U; w<campaign_wave_count; ++w) {
        const CampaignWaveFixture f=campaign_wave_fixture(w);
        const auto plan=plan_for(f);
        REQUIRE(plan.has_value());
        spawner_runtime::SpawnerSet set;
        std::int32_t serial=1;
        for (const PlannedSpawner& spawner : plan->spawners) {
            const std::optional<std::int32_t> link=spawner.definition.linked
                ? std::optional<std::int32_t>{serial++} : std::nullopt;
            REQUIRE(!set.create(spawner.definition,f.game_kind,link).has_value());
        }
        REQUIRE(set.slots().size()==plan->spawners.size());
        const auto& counts=set.spawn_counts();
        REQUIRE(counts[0]==0U);
        REQUIRE(static_cast<std::size_t>(std::accumulate(counts.begin(),counts.end(),0))==
                plan->spawners.size());
    }
    return true;
}

RUNTIME_TEST_MAIN()
