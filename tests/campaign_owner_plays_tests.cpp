#include "battletanx/campaign_owner_plays.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <vector>

using namespace battletanx;
using namespace battletanx::owner_plays;

namespace {

MemberView member(std::uint16_t slot,std::uint8_t root_play=0U) {
    MemberView m{};
    m.unit=EntityId{.slot=slot,.generation=1U};
    m.in_use=true;
    m.unit_class=0x41U;
    m.root_play=root_play;
    return m;
}

std::array<MemberView,5> squad() {
    return {member(0),member(1),member(2),member(3),member(4)};
}

OwnerPlayInput input_for(std::int32_t mode,bool computer,const OwnerWorld* world) {
    OwnerPlayInput input{};
    input.owner=0U;
    input.active=true;
    input.computer=computer;
    input.game_mode=mode;
    input.members=squad();
    input.world=world;
    return input;
}

std::vector<std::uint8_t> bytes_of(const OwnerPlayState& state) {
    StateWriter writer;
    state.serialize(writer);
    return writer.bytes();
}

} // namespace

RUNTIME_TEST(owner_root_play_table) {
    for (std::int32_t mode : {0,1,3,6}) REQUIRE(select_owner_root_play(mode,true)==15U);
    REQUIRE(select_owner_root_play(2,true)==25U);
    for (std::int32_t mode : {4,5,7,9}) REQUIRE(select_owner_root_play(mode,true)==2U);
    REQUIRE(select_owner_root_play(8,true)==12U);
    REQUIRE(select_owner_root_play(-1,true)==12U);
    REQUIRE(select_owner_root_play(2,false)==26U);
    REQUIRE(select_owner_root_play(0,false)==10U);
    for (int play : {2,10,12,14,15,25,26}) REQUIRE(owner_play_supported(static_cast<std::uint8_t>(play)));
    REQUIRE(!owner_play_supported(5U));
    return true;
}

RUNTIME_TEST(role1_assignment_is_slot_ordered) {
    const auto members=squad();
    const auto result=assign_member_plays(1U,true,members,std::nullopt);
    REQUIRE(result.status==AssignStatus::ok);
    REQUIRE(result.matched==5U);
    REQUIRE(result.change_count==5U);
    for (std::uint8_t i=0U;i<5U;++i) {
        REQUIRE(result.changes[i].member_slot==i);
        REQUIRE(result.changes[i].play==5U);
    }
    return true;
}

RUNTIME_TEST(role1_keeps_members_already_on_slot_play) {
    auto members=squad();
    members[3].root_play=0x10U;
    members[1].flags=4U; // class-4 member takes play 1
    const auto result=assign_member_plays(1U,true,members,std::nullopt);
    REQUIRE(result.matched==5U);
    // member 3 already on 0x10: no change entry for it.
    for (std::uint8_t i=0U;i<result.change_count;++i) REQUIRE(result.changes[i].member_slot!=3U);
    REQUIRE(result.changes[0].member_slot==1U);
    REQUIRE(result.changes[0].play==1U);
    return true;
}

RUNTIME_TEST(role_assignment_ties_keep_earlier_member) {
    auto members=squad();
    members[2].x=100.0F;
    members[4].x=-100.0F; // same origin distance as member 2
    for (auto& m : members) if (m.x==0.0F) { m.x=8000.0F; }
    const auto result=assign_member_plays(8U,true,members,std::nullopt);
    REQUIRE(result.change_count>=1U);
    REQUIRE(result.changes[0].member_slot==2U);
    REQUIRE(result.changes[0].play==5U);
    return true;
}

RUNTIME_TEST(assignment_skips_excluded_members) {
    auto members=squad();
    members[0].unit_class=0x42U;
    members[1].state=8;
    members[2].in_use=false;
    const auto result=assign_member_plays(1U,true,members,std::nullopt);
    REQUIRE(result.matched==2U);
    REQUIRE(assign_member_plays(1U,false,members,std::nullopt).status==AssignStatus::owner_inactive);
    REQUIRE(assign_member_plays(0U,true,members,std::nullopt).status==AssignStatus::role_without_table);
    return true;
}

RUNTIME_TEST(objective_distance_scorer) {
    auto m=member(0);
    m.x=500.0F;
    const float near_score=score_member(SlotScorer::objective_distance,ScoreInput{.member=&m,.slot_play=8U,
                                                                                .objective_point=Point2{}});
    REQUIRE(near_score==500.0F/2000.0F+0.5F);
    REQUIRE(score_member(SlotScorer::objective_distance,ScoreInput{.member=&m,.slot_play=8U,
                                                                   .objective_point=std::nullopt})==0.0F);
    // Diagonal offset: retail octagonal length (func_800E0BC4) gives
    // 400+300*3/8=512.5, not the Euclidean 500.
    m.x=300.0F;
    m.z=400.0F;
    const float diagonal=score_member(SlotScorer::objective_distance,ScoreInput{.member=&m,.slot_play=8U,
                                                                              .objective_point=Point2{}});
    REQUIRE(diagonal==512.5F/2000.0F+0.5F);
    return true;
}

RUNTIME_TEST(play2_sets_role4_then_assigns) {
    OwnerWorld world{};
    OwnerPlayState state{};
    RetailRandom rng{1U};
    const auto input=input_for(5,true,&world);
    auto r=update_owner_plays(state,input,rng);
    REQUIRE(r.status==OwnerPlayStatus::ok);
    REQUIRE(state.stack.depth==1U && state.stack.frames[0].play==2U);
    REQUIRE(state.role==4U && r.role_changed && !r.assigned);
    REQUIRE(state.stack.frames[0].state==2);
    r=update_owner_plays(state,input,rng);
    REQUIRE(r.assigned && r.assignment.matched==5U);
    // Role 4: plain members fall through to play 5 (member_mode scorer, 0.9).
    REQUIRE(r.assignment.changes[0].play==5U);
    return true;
}

RUNTIME_TEST(play10_human_role5) {
    OwnerWorld world{};
    OwnerPlayState state{};
    RetailRandom rng{1U};
    const auto input=input_for(0,false,&world);
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].play==10U && state.role==5U);
    const auto r=update_owner_plays(state,input,rng);
    REQUIRE(r.assigned);
    return true;
}

RUNTIME_TEST(play12_idles) {
    OwnerWorld world{};
    OwnerPlayState state{};
    RetailRandom rng{1U};
    const auto input=input_for(8,true,&world);
    for (int i=0;i<3;++i) {
        const auto r=update_owner_plays(state,input,rng);
        REQUIRE(!r.assigned && !r.role_changed);
    }
    REQUIRE(state.stack.depth==1U && state.stack.frames[0].play==12U && state.role==0U);
    return true;
}

RUNTIME_TEST(play15_hunts_and_changes_objective) {
    OwnerWorld world{};
    world.owner_count=3U;
    world.owners[1].has_flagged_member=true;
    world.team_list={0U,0U,0U};
    world.team_list_count=1U;
    OwnerPlayState state{};
    RetailRandom rng{7U};
    const auto input=input_for(0,true,&world);

    auto r=update_owner_plays(state,input,rng);
    REQUIRE(state.stack.depth==2U && state.stack.frames[0].play==15U && state.stack.frames[1].play==12U);
    REQUIRE(state.role==1U && state.objective==0xFFU && r.assigned && r.release_target);

    // Only owner 1 is an enemy with presence: hunt owner 1 (state 5).
    r=update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].state==5);
    REQUIRE(state.objective==1U && r.objective_changed);
    REQUIRE(state.stack.depth==2U && state.stack.frames[1].play==14U);

    // Play 14: no own flagged member, chain free -> option 4 (role 1, pop).
    r=update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[1].state==4);
    r=update_owner_plays(state,input,rng);
    REQUIRE(state.role==1U && r.assigned && state.stack.frames[1].state==-2);
    r=update_owner_plays(state,input,rng);
    REQUIRE(state.stack.depth==1U);

    // Objective change: owner 1 gone, owner 2 present.
    world.owners[1].has_flagged_member=false;
    world.owners[2].has_flagged_member=true;
    r=update_owner_plays(state,input,rng); // re-runs state 5, pushes 14
    REQUIRE(state.stack.depth==2U);
    r=update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].state==6 && state.objective==2U && r.objective_changed);
    return true;
}

RUNTIME_TEST(play15_two_hunt_options_weigh_only_the_first) {
    OwnerWorld world{};
    world.owner_count=3U;
    world.owners[1].has_flagged_member=true;
    world.owners[2].has_flagged_member=true;
    world.owners[2].chain_active=true;
    world.owners[2].chain_nodes=4U;
    world.team_list_count=0U;
    world.team_list={9U,9U,9U};
    world.owners[0].has_flagged_member=false;
    OwnerPlayState state{};
    RetailRandom rng{7U};
    const auto input=input_for(0,true,&world);
    (void)update_owner_plays(state,input,rng);
    // Owner 0 has no presence (0x810C == 0), so option 4 is ineligible.
    const std::uint32_t before=rng.seed();
    (void)update_owner_plays(state,input,rng);
    // Options 5 and 6 eligible; only option 5 (before the last) is weighed.
    REQUIRE(state.stack.frames[0].state==5);
    REQUIRE(rng.seed()!=before); // option 5 weights read 0x8100 once
    RetailRandom check{before};
    (void)check.next_float(1.0F);
    REQUIRE(check.seed()==rng.seed());
    return true;
}

RUNTIME_TEST(play25_and_play26_timers) {
    OwnerWorld world{};
    OwnerPlayState state{};
    RetailRandom rng{1U};
    const auto input=input_for(2,true,&world);
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].play==25U && state.role==8U && state.stack.frames[0].state==1);
    world.elapsed_ticks=300;
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].state==2);
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.role==9U && state.stack.frames[0].state==2);
    world.elapsed_ticks=9000;
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.stack.frames[0].state==3);
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.role==10U && state.stack.frames[0].state==4);
    (void)update_owner_plays(state,input,rng);
    REQUIRE(state.role==1U && state.stack.frames[0].state==4);

    OwnerWorld world26{};
    world26.elapsed_ticks=150;
    OwnerPlayState human{};
    const auto input26=input_for(2,false,&world26);
    (void)update_owner_plays(human,input26,rng);
    REQUIRE(human.stack.frames[0].play==26U && human.stack.frames[0].state==2);
    return true;
}

RUNTIME_TEST(owner_play_state_serialization_is_deterministic) {
    OwnerWorld world{};
    world.owner_count=2U;
    world.owners[1].has_flagged_member=true;
    world.team_list_count=1U;
    const auto input=input_for(0,true,&world);
    OwnerPlayState a{}, b{};
    RetailRandom ra{3U}, rb{3U};
    for (int i=0;i<4;++i) {
        (void)update_owner_plays(a,input,ra);
        (void)update_owner_plays(b,input,rb);
    }
    REQUIRE(bytes_of(a)==bytes_of(b));
    b.objective=0xFFU;
    REQUIRE(bytes_of(a)!=bytes_of(b));
    // Only frames below depth are hashed: 1 depth byte + 2 per live frame + role + objective.
    REQUIRE(bytes_of(OwnerPlayState{}).size()==3U);
    REQUIRE(bytes_of(a).size()==1U+2U*a.stack.depth+2U);
    OwnerPlayState stale{};
    stale.stack.frames[1]=OwnerPlayFrame{.play=14U,.state=-2};
    REQUIRE(bytes_of(stale)==bytes_of(OwnerPlayState{}));
    return true;
}

RUNTIME_TEST(inactive_owner_does_nothing) {
    OwnerWorld world{};
    OwnerPlayState state{};
    RetailRandom rng{1U};
    auto input=input_for(0,true,&world);
    input.active=false;
    REQUIRE(update_owner_plays(state,input,rng).status==OwnerPlayStatus::inactive);
    REQUIRE(state.stack.depth==0U);
    return true;
}

RUNTIME_TEST(owner_tuning_from_synthetic_wave) {
    CampaignWaveDefinition wave{};
    wave.owner_count=2;
    wave.owners[1].computer_controlled=true;
    wave.owners[1].team=1;
    wave.owners[1].skills[2].aggressiveness=0.75F;
    wave.owners[1].skills[2].intelligence=0.25F;
    const auto tuning=owner_tuning_from_wave(wave,2U,1U);
    REQUIRE(tuning && tuning->computer && tuning->team==1);
    REQUIRE(tuning->skill_a==0.75F && tuning->skill_b==0.25F);
    REQUIRE(!owner_tuning_from_wave(wave,3U,1U));
    REQUIRE(!owner_tuning_from_wave(wave,0U,2U));
    return true;
}

RUNTIME_TEST_MAIN()
