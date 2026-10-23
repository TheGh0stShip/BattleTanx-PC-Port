#include "battletanx/campaign_spawn.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;
using retail::UnitKind;

namespace {

WorldItem item(std::int32_t type,std::uint8_t flags,std::uint8_t parameter,std::uint16_t id,
               float x=0.0F,float z=0.0F,std::uint16_t angle=0U) {
    WorldItem result;
    result.type=type;
    result.flags=flags;
    result.parameter=parameter;
    result.id=id;
    result.x=x;
    result.z=z;
    result.angle=angle;
    return result;
}

WorldItem marker(std::uint8_t group,std::uint8_t kind,float x,float z=0.0F,
                 std::uint8_t param=0U,std::uint8_t flags=0x10U) {
    return item(25,flags,static_cast<std::uint8_t>((group<<4U)|kind),param,x,z,
                static_cast<std::uint16_t>(x));
}

// Group 0 is the bank, group 1 holds placed objects.
WorldLevel level_with(std::vector<WorldItem> bank,std::vector<WorldItem> placed={}) {
    WorldLevel level;
    WorldGroup g0;
    g0.item_count=static_cast<std::uint16_t>(bank.size());
    WorldGroup g1;
    g1.first_item=g0.item_count;
    g1.item_count=static_cast<std::uint16_t>(placed.size());
    level.groups={g0,g1};
    level.bank_group=0U;
    level.items=std::move(bank);
    level.items.insert(level.items.end(),placed.begin(),placed.end());
    return level;
}

CampaignSpawnInput campaign_input() {
    CampaignSpawnInput input;
    input.game_kind=3;
    input.active_player_count=4;
    for (std::size_t owner=0U; owner<spawn_owner_count; ++owner) {
        SpawnOwnerSetup& setup=input.owners[owner];
        setup.present=true;
        setup.computer=owner!=0U;
        setup.team=static_cast<std::uint8_t>(owner);
        setup.lineup={0U,1U,2U,0U,1U};
        setup.lineup_count=5U;
    }
    return input;
}

TeamSpawnTable table() {
    TeamSpawnTable result{};
    for (std::size_t row=0U; row<team_spawn_rows; ++row) {
        for (std::size_t column=0U; column<team_spawns_per_row; ++column) {
            result[row][column]=TeamSpawnPoint{static_cast<float>(row*100U+column),
                                               static_cast<float>(row),
                                               static_cast<std::uint16_t>(column)};
        }
    }
    return result;
}

const PlannedUnit* find_unit(const CampaignSpawnPlan& plan,std::uint16_t id) {
    for (const PlannedUnit& unit : plan.units) {
        if (unit.unit_id==id) return &unit;
    }
    return nullptr;
}

} // namespace

RUNTIME_TEST(campaign_single_kind10_marker_is_player_start) {
    const WorldLevel level=level_with({marker(0,10,5.0F,6.0F,7U),marker(1,0,50.0F)});
    RetailRandom rng(1U);
    SpawnPlanError error{};
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,&error);
    REQUIRE(plan.has_value());
    REQUIRE(plan->campaign);
    REQUIRE(plan->player.unit_id==0U);
    REQUIRE(plan->player.kind==UnitKind::tank);
    REQUIRE(plan->player.x==5.0F && plan->player.z==6.0F);
    REQUIRE(plan->player.source==PlacementSource::campaign_marker);
    REQUIRE(plan->owners[0].reserve==1);
    REQUIRE(plan->units[0].parameter==7U && plan->units[0].selectable);
    return true;
}

RUNTIME_TEST(campaign_player_start_is_last_kind10_marker_not_first) {
    // Lineup-relative kinds c,d,b,a,e: kind 10 is the fourth marker.
    const WorldLevel level=level_with({marker(0,12,1.0F),marker(0,13,2.0F),marker(0,11,3.0F),
                                       marker(0,10,4.0F),marker(0,14,5.0F)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->player.unit_id==3U);
    REQUIRE(plan->player.x==4.0F);
    REQUIRE(plan->owners[0].active_unit==3U);
    REQUIRE(plan->owners[0].reserve==5);
    REQUIRE(find_unit(*plan,0U)->kind==UnitKind::goliath);  // lineup[2]
    REQUIRE(find_unit(*plan,2U)->kind==UnitKind::bike);     // lineup[1]
    REQUIRE((find_unit(*plan,0U)->flags&spawn_flag_held)!=0U);
    REQUIRE((find_unit(*plan,3U)->flags&spawn_flag_held)==0U);
    REQUIRE(plan->audit_count(PlanAuditReason::goliath_hold_rule_untraced)==1U);
    return true;
}

RUNTIME_TEST(campaign_without_kind10_uses_marker_zero) {
    const WorldLevel level=level_with({marker(0,11,1.0F),marker(0,12,2.0F)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->player.unit_id==0U && plan->player.kind==UnitKind::bike);
    return true;
}

RUNTIME_TEST(campaign_enemy_groups_use_direct_kinds_and_computer_flags) {
    const WorldLevel level=level_with({marker(0,10,0.0F),marker(2,0,10.0F),marker(2,1,11.0F),
                                       marker(2,2,12.0F)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    const PlannedUnit* first=find_unit(*plan,10U);
    REQUIRE(first!=nullptr && first->primary && !first->selectable);
    REQUIRE(first->flags==spawn_flag_computer && first->owner==2U && first->team==2U);
    REQUIRE(find_unit(*plan,11U)->kind==UnitKind::bike);
    REQUIRE(find_unit(*plan,12U)->kind==UnitKind::goliath);
    REQUIRE(plan->owners[2].reserve==5);
    REQUIRE(!plan->owners[2].human_scale.has_value());
    REQUIRE(plan->owners[0].human_scale.has_value());
    REQUIRE(plan->respawn_points[2].policy==RespawnPolicy::campaign_markers);
    REQUIRE(plan->respawn_points[2].points.size()==3U);
    REQUIRE(plan->respawn_points[2].member_points.size()==2U);
    return true;
}

RUNTIME_TEST(campaign_skirmish_only_markers_are_filtered) {
    const WorldLevel level=level_with({marker(0,10,1.0F,0.0F,0U,0x40U),marker(0,10,2.0F)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->player.x==2.0F);
    REQUIRE(plan->skipped.size()==1U && plan->skipped[0].reason==SkipReason::mode_flag_filtered);
    return true;
}

RUNTIME_TEST(campaign_missing_player_markers_is_an_error) {
    const WorldLevel level=level_with({marker(1,0,1.0F)});
    RetailRandom rng(1U);
    SpawnPlanError error{};
    REQUIRE(!build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,&error));
    REQUIRE(error==SpawnPlanError::no_player_markers);
    return true;
}

RUNTIME_TEST(invalid_marker_kinds_become_audits) {
    CampaignSpawnInput input=campaign_input();
    input.owners[0].lineup_count=2U;
    const WorldLevel level=level_with({marker(0,10,0.0F),marker(0,13,1.0F),marker(0,15,2.0F),
                                       marker(1,5,3.0F)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(input,level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->audit_count(PlanAuditReason::marker_lineup_beyond_count)==1U);
    REQUIRE(plan->audit_count(PlanAuditReason::marker_lineup_out_of_range)==1U);
    REQUIRE(plan->audit_count(PlanAuditReason::marker_kind_invalid)==1U);
    REQUIRE(plan->units.size()==1U);
    return true;
}

RUNTIME_TEST(invalid_player_marker_kind_is_an_error) {
    const WorldLevel level=level_with({marker(0,7,0.0F)});
    RetailRandom rng(1U);
    SpawnPlanError error{};
    REQUIRE(!build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,&error));
    REQUIRE(error==SpawnPlanError::invalid_player_kind);
    return true;
}

RUNTIME_TEST(type22_items_become_owned_objects_not_units) {
    // owner bits 1 (id>>1), sub-kind 2 (id>>3), respawn bit 8 ignored in 3..6.
    const std::uint16_t id=static_cast<std::uint16_t>((1U<<1U)|(2U<<3U)|0x100U);
    const WorldLevel level=level_with({marker(0,10,0.0F),item(22,0U,0U,id,9.0F,8.0F,0x4000U)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->units.size()==1U);
    REQUIRE(plan->owned_objects.size()==1U);
    const OwnedObjectPlacement& object=plan->owned_objects[0];
    REQUIRE(object.owner==1U && object.team==1U && object.sub_kind==2U);
    REQUIRE(object.health==100U && !object.flagged && object.heading==0x4000U);
    return true;
}

RUNTIME_TEST(spawner_mode_maps_to_unit_kind) {
    const auto spawner_id=[](std::uint16_t mode) {
        return static_cast<std::uint16_t>(1U|(mode<<2U)|(2U<<4U));
    };
    const WorldLevel level=level_with({marker(0,10,0.0F)},
        {item(20,0U,0x41U,spawner_id(0U)),item(20,0U,0x41U,spawner_id(1U)),
         item(20,0U,0x45U,spawner_id(2U)),item(20,0U,0x41U,spawner_id(3U))});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->spawners.size()==4U);
    REQUIRE(plan->spawners[0].unit_kind==UnitKind::tank);
    REQUIRE(plan->spawners[1].unit_kind==UnitKind::bike);
    REQUIRE(plan->spawners[2].unit_kind==UnitKind::goliath);
    REQUIRE(plan->spawners[3].unit_kind==UnitKind::goliath && plan->spawners[3].random_shape);
    REQUIRE(plan->spawners[0].definition.linked && plan->spawners[0].definition.owner==1U);
    REQUIRE(plan->spawners[2].definition.parameter==0x45U);
    return true;
}

RUNTIME_TEST(skirmish_uses_team_spawn_table_and_shuffled_rows) {
    CampaignSpawnInput input=campaign_input();
    input.game_kind=1;
    input.team_spawns=table();
    input.owners[0].lineup_count=3U;
    input.owners[0].lives=5;
    const WorldLevel level=level_with({});
    RetailRandom rng(0x1234U);
    const auto plan=build_campaign_spawn_plan(input,level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(!plan->campaign);
    std::array<bool,4> seen{};
    for (std::size_t n=0U; n<4U; ++n) {
        seen[plan->slot_order[n]]=true;
        REQUIRE(plan->slot_rank[plan->slot_order[n]]==n);
    }
    REQUIRE(seen[0] && seen[1] && seen[2] && seen[3]);
    const std::size_t row=plan->slot_order[0];
    // Index order 0, count-1, 1: slots map to columns 0,2,1.
    REQUIRE(find_unit(*plan,0U)->x==static_cast<float>(row*100U+0U));
    REQUIRE(find_unit(*plan,1U)->x==static_cast<float>(row*100U+2U));
    REQUIRE(find_unit(*plan,1U)->kind==UnitKind::goliath);
    REQUIRE(find_unit(*plan,2U)->x==static_cast<float>(row*100U+1U));
    REQUIRE(plan->player.source==PlacementSource::team_spawn_table);
    REQUIRE(plan->respawn_points[0].policy==RespawnPolicy::team_spawn_row_random_offset);
    REQUIRE(plan->respawn_points[0].points.size()==5U);
    return true;
}

RUNTIME_TEST(skirmish_unit_cap_and_tanks_only_option) {
    CampaignSpawnInput input=campaign_input();
    input.game_kind=0;
    input.team_spawns=table();
    input.pickup_reroll=true;
    input.owners[0].lives=2;
    input.owners[0].lineup_count=4U;
    RetailRandom rng(7U);
    const auto plan=build_campaign_spawn_plan(input,level_with({}),nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(find_unit(*plan,1U)->kind==UnitKind::tank);
    REQUIRE(find_unit(*plan,2U)==nullptr);
    REQUIRE(plan->owners[0].extra_tank_respawns==2);
    REQUIRE(plan->audit_count(PlanAuditReason::unit_cap_reached)==2U);
    return true;
}

RUNTIME_TEST(campaign_kinds_keep_identity_slot_order_and_rng) {
    RetailRandom rng(99U);
    std::array<std::uint8_t,4> order{},rank{};
    assign_slot_order(5,rng,order,rank);
    REQUIRE(rng.seed()==99U);
    REQUIRE(order[3]==3U && rank[2]==2U);
    assign_slot_order(9,rng,order,rank);
    REQUIRE(rng.seed()!=99U);
    return true;
}

RUNTIME_TEST(missing_table_demo_and_absent_player_are_errors) {
    SpawnPlanError error{};
    RetailRandom rng(1U);
    CampaignSpawnInput input=campaign_input();
    input.game_kind=1;
    REQUIRE(!build_campaign_spawn_plan(input,level_with({}),nullptr,rng,&error));
    REQUIRE(error==SpawnPlanError::no_team_spawn_table);
    input.game_kind=demo_game_kind;
    REQUIRE(!build_campaign_spawn_plan(input,level_with({}),nullptr,rng,&error));
    REQUIRE(error==SpawnPlanError::demo_playback);
    input=campaign_input();
    input.owners[0].present=false;
    REQUIRE(!build_campaign_spawn_plan(input,level_with({marker(0,10,0.0F)}),nullptr,rng,&error));
    REQUIRE(error==SpawnPlanError::player_owner_absent);
    return true;
}

RUNTIME_TEST(absent_owner_markers_are_audited) {
    CampaignSpawnInput input=campaign_input();
    input.owners[3].present=false;
    const WorldLevel level=level_with({marker(0,10,0.0F),marker(3,0,1.0F)});
    RetailRandom rng(1U);
    const auto plan=build_campaign_spawn_plan(input,level,nullptr,rng,nullptr);
    REQUIRE(plan.has_value());
    REQUIRE(plan->audit_count(PlanAuditReason::owner_absent_markers_unused)==1U);
    REQUIRE(!plan->owners[3].present && plan->units.size()==1U);
    return true;
}

RUNTIME_TEST(player_start_navigation_lookup) {
    NavigationLevel navigation;
    navigation.vertices={{0.0F,0.0F},{100.0F,0.0F},{0.0F,100.0F}};
    NavigationTriangle triangle;
    triangle.corners={NavigationCorner{0U,0U},NavigationCorner{1U,0U},NavigationCorner{2U,0U}};
    navigation.triangles={NavigationTriangle{},triangle};
    RetailRandom rng(1U);
    auto plan=build_campaign_spawn_plan(campaign_input(),level_with({marker(0,10,10.0F,10.0F)}),
                                        &navigation,rng,nullptr);
    REQUIRE(plan.has_value());
    const bool inside=plan->player.navigation_triangle.has_value();
    REQUIRE(inside==(plan->audit_count(PlanAuditReason::player_start_off_navigation)==0U));
    plan=build_campaign_spawn_plan(campaign_input(),level_with({marker(0,10,500.0F,500.0F)}),
                                   &navigation,rng,nullptr);
    REQUIRE(plan.has_value() && !plan->player.navigation_triangle.has_value());
    REQUIRE(plan->audit_count(PlanAuditReason::player_start_off_navigation)==1U);
    return true;
}

RUNTIME_TEST(plan_is_deterministic_and_serializes) {
    CampaignSpawnInput input=campaign_input();
    input.game_kind=1;
    input.team_spawns=table();
    input.pickup_reroll=true;
    WorldLevel level=level_with({item(10,0U,22U,0U,1.0F),item(10,0U,22U,0U,2.0F),
                                 item(10,0U,5U,0U,3.0F)});
    level.pickup_spawn_counts[5]=2;
    RetailRandom a(42U),b(42U);
    const auto first=build_campaign_spawn_plan(input,level,nullptr,a,nullptr);
    const auto second=build_campaign_spawn_plan(input,level,nullptr,b,nullptr);
    REQUIRE(first && second);
    REQUIRE(first->pickups.size()==3U);
    StateWriter wa,wb;
    first->serialize(wa);
    second->serialize(wb);
    REQUIRE(wa.bytes()==wb.bytes() && !wa.bytes().empty());
    REQUIRE(a.seed()==b.seed());
    return true;
}

RUNTIME_TEST(team_spawn_table_parser_reads_header_offset) {
    std::vector<std::uint8_t> stream(0x24U+team_spawn_table_offset+240U,0U);
    stream[3]=0x24U;
    const std::size_t entry=0x24U+team_spawn_table_offset+(1U*5U+2U)*12U;
    stream[entry]=0x3FU;   // x = 1.0f
    stream[entry+1U]=0x80U;
    stream[entry+8U]=0x12U;
    stream[entry+9U]=0x34U;
    const auto parsed=parse_team_spawn_table(stream);
    REQUIRE(parsed.has_value());
    REQUIRE((*parsed)[1][2].x==1.0F && (*parsed)[1][2].heading==0x1234U);
    stream.resize(0x100U);
    REQUIRE(!parse_team_spawn_table(stream).has_value());
    return true;
}

RUNTIME_TEST_MAIN()
