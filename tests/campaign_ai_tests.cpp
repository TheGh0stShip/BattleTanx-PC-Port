#include "battletanx/campaign_ai.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <vector>

using namespace battletanx;

namespace {

UnitRecord unit_at(std::uint8_t team,float x,float z,UnitControl control,std::int32_t spawn_tick=0) {
    UnitRecord unit{};
    unit.kind=retail::UnitKind::tank;
    unit.team=team;
    unit.owner=team;
    unit.alive=true;
    unit.control=control;
    unit.x=x;
    unit.z=z;
    unit.health=100;
    unit.max_health=100;
    unit.spawn_tick=spawn_tick;
    return unit;
}

EntityId add(UnitPool& pool,const UnitRecord& unit) {
    const EntityId id=*pool.allocate();
    *pool.get(id)=unit;
    return id;
}

AngleTable flat_angles() { return AngleTable{std::array<float,AngleTable::value_count>{}}; }

std::array<OwnerRecord,owner_capacity> owners_cpu_team1() {
    std::array<OwnerRecord,owner_capacity> owners{};
    owners[1].present=true;
    owners[1].computer=true;
    owners[1].team=1;
    owners[0].present=true;
    return owners;
}

OwnerDifficulty test_difficulty() {
    AiProfile profile{};
    profile.skill_a=0.3F;
    profile.skill_b=0.8F;
    profile.speed=8U;
    profile.rate_primary=50U;
    profile.rate_class4=30U;
    profile.rate_kind2=50U;
    profile.value_32c=70U;
    return owner_difficulty_from_profile(profile);
}

struct Rig {
    UnitPool pool;
    std::array<OwnerRecord,owner_capacity> owners=owners_cpu_team1();
    AngleTable angles=flat_angles();
    RetailRandom rng{0x1234U};
    CampaignAiController ai{};
    std::array<ExternalParameters,unit_capacity> params{};
    std::array<CampaignAiCommand,unit_capacity> commands{};
    std::array<RouteRequest,4> routes{};
    std::int32_t tick=1000;
    CampaignAiOutput last{};

    Rig() { ai.set_owner_difficulty(1U,test_difficulty()); }
    explicit Rig(CampaignAiConfig config) : ai(config) { ai.set_owner_difficulty(1U,test_difficulty()); }

    CampaignAiOutput step(bool budget=false) {
        CampaignAiFrame frame{};
        frame.tick=tick++;
        frame.route_budget=budget;
        frame.parameters=params;
        last=ai.update(pool,owners,angles,rng,frame,commands,routes);
        return last;
    }
    [[nodiscard]] const CampaignAiCommand* command_for(EntityId id) const {
        for (std::size_t i=0U; i<last.commands; ++i) if (commands[i].command.unit==id) return &commands[i];
        return nullptr;
    }
    [[nodiscard]] std::uint8_t top(EntityId id) const { return command_for(id)->top_play; }
    [[nodiscard]] const PlayStack& stack(EntityId id) const { return ai.state(id.slot)->stack; }
};

// One computer unit at the origin and a player 1200 units away; the unit's
// state exists after the first update, then the play under test is assigned.
struct PlayRig : Rig {
    EntityId enemy{};
    EntityId player{};
    explicit PlayRig(std::uint8_t play,CampaignAiConfig config={}) : Rig(config) {
        enemy=add(pool,unit_at(1,0,0,UnitControl::computer));
        UnitRecord p=unit_at(0,0,1200,UnitControl::player);
        p.primary=true;
        player=add(pool,p);
        step();
        if (play!=0U) (void)ai.assign_member_play(enemy,play);
    }
    ExternalParameters& ext() { return params[enemy.slot]; }
};

CandidatePerception candidate(std::uint16_t slot,float metric) {
    CandidatePerception c{};
    c.unit=EntityId{slot,1U};
    c.active=true;
    c.targetable=true;
    c.linked=true;
    c.link_metric=metric;
    return c;
}

} // namespace

RUNTIME_TEST(scoring_weights_match_retail_table) {
    REQUIRE(target_score_weights.values[0]==1000.0F);
    REQUIRE(target_score_weights.values[5]==8.0F);
    // linked + targetable + 1000 units of metric: 1000 + 8*1000*4e-5.
    REQUIRE(runtime_test::near(score_candidate(candidate(1U,1000.0F),target_score_weights,true),1000.32,1e-3));
    REQUIRE(runtime_test::near(candidate_chain_weight(3U),0.6,1e-6));
    return true;
}

RUNTIME_TEST(scoring_ties_keep_earlier_candidate) {
    const std::array<CandidatePerception,3> list{candidate(4U,500.0F),candidate(2U,500.0F),candidate(9U,100.0F)};
    const ScoredTarget best=select_best_scored(list,target_score_weights,true);
    REQUIRE(best.found);
    REQUIRE(best.unit==(EntityId{4U,1U}));
    return true;
}

RUNTIME_TEST(scoring_threshold_is_strict) {
    // 1000 - 1e-10 rounds to 1000 in binary32: a bare linked target is not enough.
    const std::array<CandidatePerception,1> bare{candidate(1U,0.0F)};
    REQUIRE(!select_best_scored(bare,target_score_weights,true).found);
    CandidatePerception idle=candidate(1U,0.0F);
    idle.idle_mode=true;
    const std::array<CandidatePerception,1> with_idle{idle};
    REQUIRE(select_best_scored(with_idle,target_score_weights,true).found);
    return true;
}

RUNTIME_TEST(scoring_class4_zeroed_for_human_owner) {
    CandidatePerception flagged=candidate(1U,100.0F);
    flagged.class4=true;
    REQUIRE(score_candidate(flagged,target_score_weights,false)==0.0F);
    REQUIRE(score_candidate(flagged,target_score_weights,true)>1200.0F);
    return true;
}

RUNTIME_TEST(perception_respects_spawn_grace) {
    UnitPool pool;
    const EntityId self=add(pool,unit_at(1,0,0,UnitControl::computer));
    add(pool,unit_at(0,300,0,UnitControl::player,1000));
    std::array<CandidatePerception,unit_capacity> out{};
    std::size_t n=perceive_candidates(pool,self,1050,PerceptionPolicy::opposing_units_linked_temporary,out);
    REQUIRE(n==1U);
    REQUIRE(!out[0].targetable);
    n=perceive_candidates(pool,self,1090,PerceptionPolicy::opposing_units_linked_temporary,out);
    REQUIRE(out[0].targetable);
    return true;
}

RUNTIME_TEST(play_transitions_reach_engagement) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId player=add(rig.pool,unit_at(0,0,1200,UnitControl::player));
    rig.step();
    REQUIRE(rig.command_for(enemy)->top_play==4U);  // play 5 state 1 pushed play 4
    std::vector<std::uint8_t> tops;
    for (int i=0; i<9; ++i) { rig.step(); tops.push_back(rig.command_for(enemy)->top_play); }
    REQUIRE(tops[2]==5U);    // play 4 popped back to play 5
    REQUIRE(tops[3]==6U);    // play 5 state 2 pushed play 6
    REQUIRE(tops.back()==12U); // play 9 parked in play 12
    REQUIRE(rig.ai.state(enemy.slot)->aim.target==player);
    REQUIRE(rig.command_for(enemy)->command.target==player);
    REQUIRE(rig.ai.state(enemy.slot)->thresholds.close_range>0U);
    REQUIRE(rig.command_for(enemy)->status==CampaignAiStatus::ok);
    return true;
}

RUNTIME_TEST(target_persists_until_lost) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId first=add(rig.pool,unit_at(0,0,1200,UnitControl::player));
    for (int i=0; i<10; ++i) rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->aim.target==first);
    // A higher-scoring candidate appears; the held target is kept.
    add(rig.pool,unit_at(0,9000,0,UnitControl::player));
    for (int i=0; i<5; ++i) rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->aim.target==first);
    // Loss: the goal clears and play 5 drops back to state 2 (play 6 on top).
    rig.pool.get(first)->alive=false;
    rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->aim.mode==GoalMode::none);
    REQUIRE(rig.command_for(enemy)->command.target==no_entity);
    REQUIRE(rig.command_for(enemy)->top_play==6U);
    return true;
}

RUNTIME_TEST(spawn_grace_blocks_then_allows_target) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId player=add(rig.pool,unit_at(0,0,1200,UnitControl::player,rig.tick));
    for (int i=0; i<20; ++i) rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->aim.mode==GoalMode::none);
    for (int i=0; i<90; ++i) rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->aim.target==player);
    return true;
}

RUNTIME_TEST(round_robin_one_route_per_budgeted_tick) {
    Rig rig;
    const EntityId a=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId b=add(rig.pool,unit_at(1,500,0,UnitControl::computer));
    add(rig.pool,unit_at(0,0,3000,UnitControl::player));
    for (int i=0; i<12; ++i) rig.step(false);
    REQUIRE(rig.ai.round_robin_unit()==0U);
    std::vector<std::uint16_t> slots;
    for (int i=0; i<9; ++i) {
        const CampaignAiOutput out=rig.step(true);
        REQUIRE(out.routes<=1U);
        slots.push_back(out.routes==1U ? rig.routes[0].unit.slot : 0xFFFFU);
    }
    // Slots 0, 1, then the player's turn (no request), repeating.
    const std::vector<std::uint16_t> expected{0U,1U,0xFFFFU,0U,1U,0xFFFFU,0U,1U,0xFFFFU};
    REQUIRE(slots==expected);
    REQUIRE(rig.routes[0].unit==b || rig.routes[0].unit==a);
    return true;
}

RUNTIME_TEST(route_response_requires_matching_serial) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    add(rig.pool,unit_at(0,0,3000,UnitControl::player));
    for (int i=0; i<10; ++i) rig.step(false);
    REQUIRE(rig.step(true).routes==1U);
    const RouteRequest request=rig.routes[0];
    REQUIRE(request.unit==enemy);
    REQUIRE(request.mode==GoalMode::chase);
    RouteResponse response{enemy,request.serial+1U,true,100.0F,100.0F,2000.0F,0U};
    REQUIRE(!rig.ai.apply_route_response(response));
    response.serial=request.serial;
    REQUIRE(rig.ai.apply_route_response(response));
    rig.step(false);
    REQUIRE(runtime_test::near(rig.ai.state(enemy.slot)->move_far,2000.0/16000.0));
    return true;
}

RUNTIME_TEST(stuck_timer_expires_after_twenty_seconds) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    rig.step(true);
    REQUIRE(rig.ai.assign_move_goal(enemy,AiGoal{GoalMode::follow,no_entity,1000.0F,0.0F,true}));
    const std::int32_t start=rig.tick;
    rig.step(true);
    REQUIRE(rig.ai.state(enemy.slot)->stuck_deadline==start+600);
    bool fired=false;
    while (rig.tick<=start+601) {
        rig.step(true);
        fired=fired || rig.command_for(enemy)->self_destruct;
    }
    REQUIRE(fired);
    return true;
}

RUNTIME_TEST(controller_is_deterministic) {
    const auto run=[] {
        Rig rig;
        add(rig.pool,unit_at(1,0,0,UnitControl::computer));
        add(rig.pool,unit_at(1,400,200,UnitControl::computer));
        add(rig.pool,unit_at(0,0,900,UnitControl::player));
        for (int i=0; i<60; ++i) rig.step(i%2==0);
        StateWriter writer;
        rig.ai.serialize(writer);
        writer.u32(rig.rng.seed());
        return writer.hex();
    };
    REQUIRE(run()==run());
    return true;
}

RUNTIME_TEST(difficulty_profile_decodes_big_endian) {
    std::array<std::uint8_t,campaign_ai_retail::profile_bytes> bytes{};
    bytes[0]=0x3EU; bytes[1]=0x99U; bytes[2]=0x99U; bytes[3]=0x9AU; // 0.3f
    bytes[4]=0x3FU; bytes[5]=0x4CU; bytes[6]=0xCCU; bytes[7]=0xCDU; // 0.8f
    bytes[0x1C]=10U; bytes[0x1D]=45U; bytes[0x1E]=25U; bytes[0x1F]=40U; bytes[0x20]=80U;
    const AiProfile profile=decode_ai_profile(bytes);
    REQUIRE(profile.skill_a==0.3F);
    REQUIRE(profile.rate_class4==25U);
    const OwnerDifficulty owner=owner_difficulty_from_profile(profile);
    REQUIRE(owner.speed==10.0F);
    REQUIRE(owner.rate_kind2==40.0F);
    REQUIRE(runtime_test::near(owner.skill_b_scaled,80.0,1e-4));
    REQUIRE(ai_profile_offset(2U,3U)==std::optional<std::uint32_t>{0x1CU+0x120U+0x6CU});
    REQUIRE(!ai_profile_offset(3U,0U).has_value());
    REQUIRE(!ai_profile_offset(0U,4U).has_value());
    return true;
}

RUNTIME_TEST(difficulty_scales_ranges_and_cooldown) {
    const UnitThresholds low=apply_engagement_profile(EngagementProfile::state2,0.0F,100,100);
    const UnitThresholds high=apply_engagement_profile(EngagementProfile::state2,1.0F,100,100);
    REQUIRE(low.close_range==1350U);
    REQUIRE(high.close_range==450U);
    REQUIRE(low.health_threshold==75U);
    REQUIRE(high.health_threshold==25U);
    REQUIRE(apply_engagement_profile(EngagementProfile::state2,0.0F,1500,1500).health_threshold==1000U);
    REQUIRE(apply_engagement_profile(EngagementProfile::state3,0.5F,80,200).health_threshold==20U);
    REQUIRE(apply_engagement_profile(EngagementProfile::state5,0.5F,80,200).health_threshold==79U);
    REQUIRE(apply_engagement_profile(EngagementProfile::state6,1.0F,80,200).close_range==1350U);
    // Owner rate 16 gives the 30-tick weapon-1 cadence; higher rates cool longer.
    REQUIRE(fire_cooldown_ticks(16.0F)==30);
    REQUIRE(fire_cooldown_ticks(30.0F)==57);
    REQUIRE(fire_cooldown_ticks(0.0F)==0);
    const float cool=30.0F*0.003333F/16.0F;
    REQUIRE(56.0F/9000.0F<cool);
    REQUIRE(!(57.0F/9000.0F<cool));
    return true;
}

RUNTIME_TEST(skill_roll_blends_owner_and_noise) {
    RetailRandom a{77U};
    RetailRandom b{77U};
    const float skill=roll_unit_skill(0.5F,a);
    const float first=b.next_float(1.0F);
    const float second=b.next_float(1.0F);
    REQUIRE(skill==0.5F*0.6F+(first+second)*0.5F*0.4F);
    REQUIRE(a.seed()==b.seed());
    REQUIRE(skill>=0.3F && skill<0.7F);
    return true;
}

RUNTIME_TEST(class4_root_play_one_runs) {
    Rig rig;
    UnitRecord turret=unit_at(1,0,0,UnitControl::computer);
    turret.flags=enemy_retail::flag_ai_class4;
    const EntityId id=add(rig.pool,turret);
    rig.step();
    REQUIRE(rig.command_for(id)->status==CampaignAiStatus::ok);
    REQUIRE(rig.top(id)==1U);
    REQUIRE(select_member_root_play(0x42U,false,false)==3U);
    REQUIRE(select_member_root_play(0U,false,true)==0x0BU);
    REQUIRE(play_supported(3U));
    REQUIRE(play_supported(5U));
    REQUIRE(!play_supported(15U));
    return true;
}

RUNTIME_TEST(missing_owner_difficulty_is_reported) {
    Rig rig;
    UnitRecord other=unit_at(2,0,0,UnitControl::computer);
    const EntityId id=add(rig.pool,other);
    rig.step();
    REQUIRE(rig.command_for(id)->status==CampaignAiStatus::owner_difficulty_missing);
    return true;
}

RUNTIME_TEST(slot_generation_change_resets_state) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    add(rig.pool,unit_at(0,0,1200,UnitControl::player));
    for (int i=0; i<10; ++i) rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->aim.mode==GoalMode::unit);
    REQUIRE(rig.pool.release(enemy));
    const EntityId again=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    REQUIRE(again.slot==enemy.slot);
    rig.step();
    REQUIRE(rig.ai.state(again.slot)->unit==again);
    REQUIRE(rig.ai.state(again.slot)->aim.mode==GoalMode::none);
    return true;
}

// ---------------------------------------------------------------------------
// Member plays: one test per member-scope play id.

RUNTIME_TEST(play_scope_partitions_script_table) {
    const std::array<std::uint8_t,19> member{1,3,4,5,6,8,9,11,12,13,16,17,19,20,21,22,23,27,28};
    for (const std::uint8_t p : member) {
        REQUIRE(play_scope(p)==PlayScope::member);
        REQUIRE(play_supported(p));
    }
    for (const std::uint8_t p : std::array<std::uint8_t,7>{2,7,10,14,15,25,26}) {
        REQUIRE(play_scope(p)==PlayScope::owner);
        REQUIRE(!play_supported(p));
    }
    REQUIRE(play_scope(18U)==PlayScope::unreachable);
    REQUIRE(play_scope(24U)==PlayScope::unreachable);
    REQUIRE(play_scope(0U)==PlayScope::invalid);
    REQUIRE(play_scope(29U)==PlayScope::invalid);
    return true;
}

RUNTIME_TEST(play01_escort_dispatch_reaches_play22) {
    PlayRig rig{1U};
    rig.step();
    REQUIRE(rig.top(rig.enemy)==1U);
    REQUIRE(rig.stack(rig.enemy).frames[0].state==3);   // D_80135D80: contact -> 3
    rig.step();
    REQUIRE(rig.top(rig.enemy)==19U);
    REQUIRE(rig.ai.state(rig.enemy.slot)->aim.target==rig.player);
    REQUIRE(rig.ai.owner_fields(1U)->action_value==70.0F); // func_800E7064, no params -> +0x32C
    rig.step();                                           // play 19: no objective -> state 2
    rig.step();
    REQUIRE(rig.top(rig.enemy)==22U);
    rig.step();
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    REQUIRE(rig.stack(rig.enemy).depth==4U);
    const MemberOwnerFields& owner=*rig.ai.owner_fields(1U);
    REQUIRE(owner.timer_start==rig.tick-1+2);
    REQUIRE(owner.timer_end>=owner.timer_start);
    return true;
}

RUNTIME_TEST(play03_waits_for_contact_then_pushes_play5) {
    PlayRig rig{3U};
    rig.step();
    REQUIRE(rig.top(rig.enemy)==3U);
    REQUIRE(rig.stack(rig.enemy).frames[0].state==2);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==5U);
    return true;
}

RUNTIME_TEST(play04_sets_profile_then_pops) {
    PlayRig rig{4U};
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==3);
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==-2);
    REQUIRE(rig.ai.state(rig.enemy.slot)->thresholds.health_threshold==10U);
    rig.step();
    REQUIRE(rig.stack(rig.enemy).depth==0U);
    return true;
}

RUNTIME_TEST(play05_pushes_engagement_profile) {
    PlayRig rig{5U};
    rig.step();
    REQUIRE(rig.top(rig.enemy)==4U);
    REQUIRE(rig.ai.state(rig.enemy.slot)->owner_suggestion==rig.player);
    return true;
}

RUNTIME_TEST(play06_resolves_target_then_pushes_play9) {
    PlayRig rig{6U};
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==3);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==9U);
    REQUIRE(rig.ai.state(rig.enemy.slot)->aim.target==rig.player);
    return true;
}

RUNTIME_TEST(play08_attack_objective_cycle) {
    CampaignAiConfig config{};
    config.metric_levels={255U,255U,255U};
    PlayRig rig{8U,config};
    rig.ai.set_owner_objective(1U,0U,std::array<float,2>{500.0F,0.0F});
    rig.step();
    const CampaignAiUnitState& s=*rig.ai.state(rig.enemy.slot);
    REQUIRE(s.move.mode==GoalMode::point);
    REQUIRE(s.move.x==500.0F);
    REQUIRE(s.stack.frames[0].state==2);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==6U);
    rig.ext().p1113=1.0F;                       // D_801363E0 -> state 3
    rig.step();
    REQUIRE(s.stack.depth==1U);
    REQUIRE(s.stack.frames[0].state==4);
    REQUIRE(s.flag_2ba);
    REQUIRE(rig.command_for(rig.enemy)->marker_request);
    REQUIRE(rig.command_for(rig.enemy)->ammo_grant_slot==2U);
    rig.ext().p1113=0.0F;
    rig.step();
    REQUIRE(rig.top(rig.enemy)==6U);
    rig.ext().p2117=1.0F;                       // D_80136424 -> state 7
    rig.step();
    REQUIRE(rig.top(rig.enemy)==5U);
    REQUIRE(rig.command_for(rig.enemy)->owner_release_request);
    REQUIRE(rig.ai.owner_fields(1U)->release_pending);
    return true;
}

RUNTIME_TEST(play09_pops_when_far) {
    PlayRig rig{9U};
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==-2);
    rig.step();
    REQUIRE(rig.stack(rig.enemy).depth==0U);
    return true;
}

RUNTIME_TEST(play11_guard_through_play23) {
    PlayRig rig{11U};
    rig.step();
    REQUIRE(rig.top(rig.enemy)==23U);
    REQUIRE(!rig.ai.state(rig.enemy.slot)->flag_2ba);
    rig.step();
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    REQUIRE(rig.ai.state(rig.enemy.slot)->aim.target==rig.player);
    return true;
}

RUNTIME_TEST(play12_parks) {
    PlayRig rig{12U};
    for (int i=0; i<5; ++i) {
        rig.step();
        REQUIRE(rig.top(rig.enemy)==12U);
        REQUIRE(rig.stack(rig.enemy).depth==1U);
    }
    return true;
}

RUNTIME_TEST(play13_capture_objective) {
    PlayRig rig{13U};
    rig.ai.set_owner_objective(1U,0U,std::array<float,2>{500.0F,0.0F});
    rig.step();
    const CampaignAiUnitState& s=*rig.ai.state(rig.enemy.slot);
    REQUIRE(s.thresholds.close_range==100U);
    REQUIRE(s.stack.frames[0].state==2);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==6U);
    REQUIRE(s.move.mode==GoalMode::point);
    REQUIRE(s.move.x==500.0F);
    rig.ext().p2113=1.0F;                        // D_801366A8 -> state 4
    rig.step();
    REQUIRE(rig.top(rig.enemy)==5U);
    REQUIRE(rig.command_for(rig.enemy)->owner_release_request);
    return true;
}

RUNTIME_TEST(play16_timed_return_then_self_destruct) {
    PlayRig rig{16U};
    rig.ai.set_owner_anchor(1U,std::array<float,2>{100.0F,100.0F});
    const std::int32_t start=rig.tick;
    rig.step();
    const CampaignAiUnitState& s=*rig.ai.state(rig.enemy.slot);
    REQUIRE(s.deadline>=start+3599 && s.deadline<=start+3600);
    REQUIRE(s.move.mode==GoalMode::point);
    REQUIRE(s.move.x==100.0F);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==27U);
    rig.step();
    rig.step();
    REQUIRE(rig.top(rig.enemy)==6U);
    REQUIRE(!rig.command_for(rig.enemy)->self_destruct);
    rig.tick=s.deadline;
    rig.step();
    REQUIRE(rig.command_for(rig.enemy)->self_destruct);
    REQUIRE(s.deadline==0);
    return true;
}

RUNTIME_TEST(play17_rejects_then_pushes_play5) {
    PlayRig rig{17U};
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==2);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==5U);
    return true;
}

RUNTIME_TEST(play19_hostile_link_selects_play21) {
    PlayRig rig{19U};
    rig.ai.set_owner_objective(1U,0U,std::nullopt); // owner 0's primary unit is the player
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==4);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==21U);
    rig.step();
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    return true;
}

RUNTIME_TEST(play20_progress_ends_escort) {
    PlayRig rig{20U};
    rig.ai.owner_fields(1U)->action_value=5.0F;
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==3);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    rig.ext().p1103=1.0F;                        // D_80137398 -> state 4
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==-2);
    REQUIRE(rig.ai.owner_fields(1U)->action_value==0.0F);
    return true;
}

RUNTIME_TEST(play21_behind_link_inverts_flag) {
    PlayRig rig{21U};
    rig.ext().p1108=1.0F;
    rig.step();
    REQUIRE(rig.stack(rig.enemy).frames[0].state==2);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    REQUIRE(rig.ai.owner_fields(1U)->flag_active);
    return true;
}

RUNTIME_TEST(play22_schedules_owner_timer) {
    PlayRig rig{22U};
    rig.step();
    const std::int8_t chosen=rig.stack(rig.enemy).frames[0].state;
    REQUIRE(chosen==2 || chosen==3);
    const std::int32_t tick=rig.tick;
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    const MemberOwnerFields& owner=*rig.ai.owner_fields(1U);
    REQUIRE(owner.timer_start==tick+2);
    REQUIRE(owner.timer_end>=owner.timer_start && owner.timer_end<=owner.timer_start+240);
    return true;
}

RUNTIME_TEST(play23_drops_when_target_lost) {
    PlayRig rig{23U};
    rig.step();
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    rig.pool.get(rig.player)->alive=false;
    rig.step();
    REQUIRE(rig.stack(rig.enemy).depth==0U);
    return true;
}

RUNTIME_TEST(play27_returns_to_anchor_then_play6) {
    PlayRig rig{27U};
    rig.ai.set_owner_anchor(1U,std::array<float,2>{-300.0F,40.0F});
    rig.step();
    REQUIRE(rig.ai.state(rig.enemy.slot)->move.x==-300.0F);
    rig.step();
    REQUIRE(rig.top(rig.enemy)==6U);
    return true;
}

RUNTIME_TEST(play28_parks_in_play12) {
    PlayRig rig{28U};
    rig.step();
    REQUIRE(rig.top(rig.enemy)==12U);
    REQUIRE(rig.stack(rig.enemy).frames[0].play==28U);
    return true;
}

RUNTIME_TEST(unresolved_objective_holds_position) {
    PlayRig rig{13U};
    rig.step();
    rig.step();
    const CampaignAiUnitState& s=*rig.ai.state(rig.enemy.slot);
    REQUIRE(s.move.mode==GoalMode::point);
    REQUIRE(!s.move.has_point);
    REQUIRE(rig.command_for(rig.enemy)->command.input.throttle==0);
    return true;
}

// ---------------------------------------------------------------------------
// Multi-unit scenarios.

RUNTIME_TEST(roots_follow_class_flags_and_status) {
    Rig rig;
    UnitRecord turret=unit_at(1,0,0,UnitControl::computer);
    turret.flags=enemy_retail::flag_ai_class4;
    const EntityId a=add(rig.pool,turret);
    const EntityId b=add(rig.pool,unit_at(1,300,0,UnitControl::computer));
    const EntityId c=add(rig.pool,unit_at(1,600,0,UnitControl::computer));
    add(rig.pool,unit_at(0,0,1500,UnitControl::player));
    rig.params[b.slot].unit_class=0x42U;
    rig.params[c.slot].status_one=true;
    rig.step(true);
    REQUIRE(rig.ai.member_root_play(a)==1U);
    REQUIRE(rig.ai.member_root_play(b)==3U);
    REQUIRE(rig.ai.member_root_play(c)==0U);   // skipped by func_800E63F4
    REQUIRE(rig.command_for(c)==nullptr);
    return true;
}

RUNTIME_TEST(member_play_assignment_hooks) {
    Rig rig;
    const EntityId a=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    rig.step();
    REQUIRE(rig.ai.assign_member_play(a,15U)==PlayAssignStatus::not_member_play);
    REQUIRE(rig.ai.assign_member_play(a,16U)==PlayAssignStatus::ok);
    REQUIRE(rig.ai.assign_member_play(a,16U)==PlayAssignStatus::unchanged);
    REQUIRE(rig.ai.assign_member_play(EntityId{a.slot,static_cast<std::uint16_t>(a.generation+1U)},5U)==
            PlayAssignStatus::unknown_unit);
    REQUIRE(rig.ai.member_root_play(a)==16U);
    REQUIRE(rig.ai.force_member_root_play(a,15U));
    rig.step();
    REQUIRE(rig.command_for(a)->status==CampaignAiStatus::unsupported_play);
    REQUIRE(rig.command_for(a)->unsupported_play==15U);
    REQUIRE(rig.ai.force_member_root_play(a,5U));
    REQUIRE(!rig.ai.force_member_root_play(a,0U));
    rig.step();
    REQUIRE(rig.command_for(a)->status==CampaignAiStatus::ok);
    return true;
}

RUNTIME_TEST(force_member_root_play_reports_unit_pending_update) {
    Rig rig;
    const EntityId a=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    REQUIRE(rig.ai.force_member_root_play_status(a,5U)==ForceRootPlayStatus::pending_update);
    REQUIRE(!rig.ai.force_member_root_play(a,5U));
    REQUIRE(rig.ai.force_member_root_play_status(a,0U)==ForceRootPlayStatus::invalid_play);
    REQUIRE(rig.ai.force_member_root_play_status(no_entity,5U)==ForceRootPlayStatus::unknown_unit);
    rig.step();
    REQUIRE(rig.ai.force_member_root_play_status(a,5U)==ForceRootPlayStatus::ok);
    REQUIRE(rig.ai.member_root_play(a)==5U);
    return true;
}

RUNTIME_TEST(squad_mixed_plays_share_owner_fields_deterministically) {
    const auto run=[] {
        Rig rig;
        const EntityId a=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
        const EntityId b=add(rig.pool,unit_at(1,200,0,UnitControl::computer));
        const EntityId c=add(rig.pool,unit_at(1,400,0,UnitControl::computer));
        add(rig.pool,unit_at(0,0,1500,UnitControl::player));
        rig.ai.set_owner_objective(1U,0U,std::array<float,2>{0.0F,1500.0F});
        rig.ai.set_owner_anchor(1U,std::array<float,2>{0.0F,-500.0F});
        rig.step();
        (void)rig.ai.assign_member_play(a,8U);
        (void)rig.ai.assign_member_play(b,13U);
        (void)rig.ai.assign_member_play(c,22U);
        std::size_t total_routes=0U;
        for (int i=0; i<80; ++i) total_routes+=rig.step(i%3==0).routes;
        StateWriter writer;
        rig.ai.serialize(writer);
        writer.u32(rig.rng.seed());
        writer.u32(static_cast<std::uint32_t>(total_routes));
        return writer.hex();
    };
    REQUIRE(run()==run());
    return true;
}

// ---------------------------------------------------------------------------
// Evasion move goal (mode 10, ai_update_goal_unit.c:269-289). Only
// assign_move_goal() sets this mode; a friendly target keeps plays quiet.

RUNTIME_TEST(evasion_goal_tracks_live_target_position) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId target=add(rig.pool,unit_at(1,800,0,UnitControl::computer));
    rig.step();
    REQUIRE(rig.ai.assign_move_goal(enemy,AiGoal{GoalMode::evasion,target,0.0F,0.0F,false}));
    rig.step(true);
    const CampaignAiUnitState* s=rig.ai.state(enemy.slot);
    REQUIRE(s->move.mode==GoalMode::evasion);
    REQUIRE(s->move.has_point);
    REQUIRE(runtime_test::near(s->move.x,800.0));
    REQUIRE(runtime_test::near(s->move.z,0.0));
    rig.pool.get(target)->x=300.0F;
    rig.pool.get(target)->z=-450.0F;
    rig.step(true);
    s=rig.ai.state(enemy.slot);
    REQUIRE(s->move.mode==GoalMode::evasion);
    REQUIRE(s->move.target==target);
    REQUIRE(runtime_test::near(s->move.x,300.0));
    REQUIRE(runtime_test::near(s->move.z,-450.0));
    return true;
}

RUNTIME_TEST(evasion_goal_partner_clears_only_point) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    rig.step();
    // TEMPORARY POLICY: the unit itself stands in for the link partner.
    REQUIRE(rig.ai.assign_move_goal(enemy,AiGoal{GoalMode::evasion,enemy,123.0F,456.0F,true}));
    rig.step(true);
    const CampaignAiUnitState* s=rig.ai.state(enemy.slot);
    REQUIRE(s->move.mode==GoalMode::evasion);
    REQUIRE(s->move.target==enemy);
    REQUIRE(!s->move.has_point);
    REQUIRE(runtime_test::near(s->move.x,123.0));
    REQUIRE(runtime_test::near(s->move.z,456.0));
    REQUIRE(rig.last.routes==0U);
    return true;
}

RUNTIME_TEST(evasion_goal_clears_when_target_dies) {
    Rig rig;
    const EntityId enemy=add(rig.pool,unit_at(1,0,0,UnitControl::computer));
    const EntityId target=add(rig.pool,unit_at(1,800,0,UnitControl::computer));
    rig.step();
    REQUIRE(rig.ai.assign_move_goal(enemy,AiGoal{GoalMode::evasion,target,0.0F,0.0F,false}));
    rig.step();
    REQUIRE(rig.ai.state(enemy.slot)->move.mode==GoalMode::evasion);
    rig.pool.get(target)->alive=false;
    rig.step();
    const CampaignAiUnitState* s=rig.ai.state(enemy.slot);
    REQUIRE(s->move.mode==GoalMode::none);
    REQUIRE(s->move.target==EntityId{});
    REQUIRE(!s->move.has_point);
    REQUIRE(!s->route.has_value());
    return true;
}

RUNTIME_TEST_MAIN()
