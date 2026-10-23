#include "battletanx/owner_links.hpp"

#include "runtime_test_support.hpp"

#include <array>
#include <optional>

using namespace battletanx;
using namespace battletanx::owner_links;

namespace {

class FixedRoute final : public RouteProbe {
public:
    explicit FixedRoute(bool clear) : clear_(clear) {}
    [[nodiscard]] bool route_clear(float,float,float,float) const override { return clear_; }
private:
    bool clear_;
};

UnitLinkSet squad_set() {
    UnitLinkSet set{};
    set.friends={0U,1U,2U};
    set.friend_count=3U;
    set.others={5U,6U};
    set.other_count=2U;
    return set;
}

UnitSample ready_unit(std::uint8_t owner,float x,float z) {
    return UnitSample{.in_use=true,.owner=owner,.flags=0U,.x=x,.z=z,.status_one=false,.status_two=true,
                      .carrying=false,.tracked_kind1b_owner=std::nullopt};
}

// Runs calls until the rotor has executed `phase` once.
UpdateResult run_to_phase(LinkTracker& tracker,const UpdateInput& input,std::uint8_t phase) {
    for (int i=0; i<4; ++i) {
        UpdateResult result=tracker.update(input);
        if (result.phase==phase) return result;
    }
    return UpdateResult{};
}

owner_plays::OwnerWorld single_world(const OwnerSnapshot& snapshot,std::uint8_t owner) {
    const std::array<UnitSample,retail_values::unit_slots> units{};
    return owner_world(snapshot,units,owner);
}

} // namespace

RUNTIME_TEST(pair_records_follow_build_order) {
    LinkTracker tracker;
    const std::array<UnitLinkSet,1> sets{squad_set()};
    REQUIRE(tracker.build(sets)==BuildStatus::ok);
    REQUIRE(tracker.pair_count()==9U);           // 3 friendly + 3*2 cross
    const PairRecord* friendly=tracker.pair(1U,0U);
    REQUIRE(friendly!=nullptr && friendly->same_set && friendly->first==0U && friendly->second==1U);
    const PairRecord* cross=tracker.pair(6U,2U);
    REQUIRE(cross!=nullptr && !cross->same_set && cross->first==2U && cross->second==6U);
    REQUIRE(tracker.pair(5U,6U)==nullptr);
    REQUIRE(tracker.pair(3U,3U)==nullptr);
    UnitLinkSet bad=squad_set();
    bad.friends[0]=20U;
    const std::array<UnitLinkSet,1> bad_sets{bad};
    REQUIRE(tracker.build(bad_sets)==BuildStatus::id_out_of_range);
    REQUIRE(tracker.pair_count()==9U);           // earlier build survives
    return true;
}

RUNTIME_TEST(rotor_runs_retail_phase_order) {
    LinkTracker tracker;
    UpdateInput input{};
    std::array<std::uint8_t,5> phases{};
    for (std::size_t i=0U; i<phases.size(); ++i) phases[i]=tracker.update(input).phase;
    REQUIRE(phases==(std::array<std::uint8_t,5>{0U,1U,2U,3U,0U}));
    LinkTracker fresh;
    REQUIRE(fresh.update(input).toggled);       // timer starts at 0: first call reloads
    REQUIRE(!fresh.update(input).toggled);
    return true;
}

RUNTIME_TEST(cross_link_expires_when_units_separate) {
    LinkTracker tracker;
    const std::array<UnitLinkSet,1> sets{squad_set()};
    REQUIRE(tracker.build(sets)==BuildStatus::ok);
    const FixedRoute route{true};
    UpdateInput input{};
    input.route=&route;
    input.tick=77;
    input.units[0]=ready_unit(0U,0.0F,0.0F);
    input.units[5]=ready_unit(1U,300.0F,0.0F);
    run_to_phase(tracker,input,1U);
    const PairRecord* pair=tracker.pair(0U,5U);
    REQUIRE(pair!=nullptr && pair->checked && pair->in_range && pair->linked);
    REQUIRE(runtime_test::near(pair->distance,300.0));
    REQUIRE(tracker.unit(0U)->track_start==77);

    input.units[5].x=5000.0F;                   // beyond the 2000 default range
    run_to_phase(tracker,input,1U);
    REQUIRE(pair->checked && !pair->in_range && !pair->linked);

    input.units[0].in_use=false;
    run_to_phase(tracker,input,1U);
    REQUIRE(!pair->checked && pair->distance==0.0F);
    return true;
}

RUNTIME_TEST(friendly_pair_resolves_to_follow) {
    LinkTracker tracker;
    const std::array<UnitLinkSet,1> sets{squad_set()};
    REQUIRE(tracker.build(sets)==BuildStatus::ok);
    tracker.set_action(0U,6,10.0F,0.0F);
    tracker.set_action(1U,6,20.0F,0.0F);
    tracker.set_follow_allowed(1U,1U);
    UpdateInput input{};
    input.units[0]=ready_unit(0U,0.0F,0.0F);
    input.units[1]=ready_unit(0U,100.0F,0.0F);
    input.units[1].carrying=true;
    const UpdateResult result=run_to_phase(tracker,input,0U);
    REQUIRE(tracker.pair(0U,1U)->linked);
    REQUIRE(tracker.unit(0U)->action==action_kind::follow_unit);
    REQUIRE(tracker.unit(0U)->follow==1U);
    REQUIRE(!tracker.unit(0U)->action_point);
    REQUIRE(runtime_test::near(tracker.unit(0U)->point_x,20.0));
    REQUIRE(!tracker.unit(1U)->pair_eligible);
    bool saw_follow=false;
    for (std::uint16_t i=0U; i<result.command_count; ++i)
        if (result.commands[i].kind==CommandKind::follow_unit && result.commands[i].unit==0U) saw_follow=true;
    REQUIRE(saw_follow);
    return true;
}

RUNTIME_TEST(foreign_links_count_other_owner_carriers) {
    OwnerSnapshot snapshot{};
    snapshot.owner_count=4U;
    snapshot.queens[0]={QueenPlace::carried,1U};
    snapshot.queens[1]={QueenPlace::carried,2U};
    snapshot.queens[2]={QueenPlace::in_base,0xFFU};
    snapshot.queens[3]={QueenPlace::dropped,0xFFU};
    REQUIRE(foreign_link_count(snapshot,0U)==2U);
    REQUIRE(foreign_link_count(snapshot,1U)==1U);
    const owner_plays::OwnerWorld world=single_world(snapshot,0U);
    REQUIRE(world.foreign_links==2U);
    const owner_plays::ParameterContext context{.owner=0U,.objective=owner_plays::retail_values::no_objective,
                                                .world=&world,.rng=nullptr};
    const auto value=owner_plays::owner_parameter(context,0x2111U);
    REQUIRE(value.has_value() && runtime_test::near(*value,0.4));
    return true;
}

RUNTIME_TEST(base_chain_feeds_owner_facts) {
    OwnerSnapshot snapshot{};
    snapshot.owner_count=2U;
    snapshot.owner_present={true,true,false,false};
    snapshot.owner_team={0U,1U,0U,0U};
    snapshot.bases[1]={.present=true,.queens_held=3U,.state=0,.has_record=true};
    std::array<UnitSample,retail_values::unit_slots> units{};
    units[7]=ready_unit(1U,0.0F,0.0F);         // owner 1, squad slot 2
    units[7].carrying=true;
    const owner_plays::OwnerWorld world=owner_world(snapshot,units,0U);
    REQUIRE(!world.owners[0].chain_active && !world.owners[0].chain_state.has_value());
    REQUIRE(world.owners[1].chain_active && world.owners[1].chain_nodes==3U);
    REQUIRE(world.owners[1].chain_state==std::optional<std::int32_t>{0});
    REQUIRE(world.owners[1].has_flagged_member && !world.owners[0].has_flagged_member);
    REQUIRE(world.team_list_count==1U && world.team_list[0]==0U);
    const owner_plays::ParameterContext context{.owner=0U,.objective=1U,.world=&world,.rng=nullptr};
    const auto chain_free=owner_plays::owner_parameter(context,0x2117U);
    REQUIRE(chain_free.has_value() && runtime_test::near(*chain_free,0.4));
    return true;
}

RUNTIME_TEST(member_views_take_link_fields) {
    LinkTracker tracker;
    tracker.set_mode(6U,2U);
    UpdateInput input{};
    input.units[7].in_use=true;
    tracker.set_action(7U,6,1.0F,1.0F);
    tracker.set_follow_allowed(7U,1U);
    tracker.follow(6U,7U,input);
    std::array<owner_plays::MemberView,retail_values::squad_members> members{};
    apply_link_fields(tracker,1U,members);
    REQUIRE(members[1].state==action_kind::follow_unit && members[1].mode==2U);
    REQUIRE(members[0].state==0 && members[0].mode==0U);
    return true;
}

RUNTIME_TEST(tracker_is_deterministic) {
    const auto run=[] {
        LinkTracker tracker;
        const std::array<UnitLinkSet,1> sets{squad_set()};
        (void)tracker.build(sets);
        const FixedRoute route{true};
        UpdateInput input{};
        input.route=&route;
        input.units[0]=ready_unit(0U,0.0F,0.0F);
        input.units[1]=ready_unit(0U,50.0F,30.0F);
        input.units[5]=ready_unit(1U,700.0F,-40.0F);
        input.units[6]=ready_unit(1U,900.0F,10.0F);
        input.units[6].tracked_kind1b_owner=std::uint8_t{6U};
        for (std::int32_t tick=0; tick<12; ++tick) {
            input.tick=tick;
            input.units[5].x-=25.0F;
            (void)tracker.update(input);
        }
        StateWriter writer;
        tracker.serialize(writer);
        return writer.bytes();
    };
    const auto a=run();
    const auto b=run();
    REQUIRE(!a.empty() && a==b);
    return true;
}

RUNTIME_TEST(follow_matches_update_action_goal) {
    UpdateInput input{};
    input.units[0]=ready_unit(0U,11.0F,22.0F);
    input.units[1]=ready_unit(0U,0.0F,0.0F);
    LinkTracker t;
    // dead target: kind 0, no point, +0xB0 kept, +0x344 untouched
    t.set_follow_allowed(0U,5U);
    input.units[1].in_use=false;
    t.follow(0U,1U,input);
    REQUIRE(t.unit(0U)->action==action_kind::none && !t.unit(0U)->action_point);
    REQUIRE(t.unit(0U)->follow==1U && t.unit(0U)->value344==5U);
    // 344==0 + target action 7: kind 9, point = target point
    input.units[1].in_use=true;
    t.set_action(1U,7,30.0F,40.0F);
    t.follow(0U,1U,input);
    REQUIRE(t.unit(0U)->action==action_kind::move_point && t.unit(0U)->action_point);
    REQUIRE(t.unit(0U)->point_x==30.0F && t.unit(0U)->point_z==40.0F);
    // target action 0: source cleared (+0x344 too)
    t.set_action(1U,0,std::nullopt,std::nullopt);
    t.set_follow_allowed(1U,1U);
    t.follow(0U,1U,input);
    REQUIRE(t.unit(0U)->action==action_kind::none && !t.unit(0U)->action_point && t.unit(0U)->value344==0U);
    return true;
}

RUNTIME_TEST(duplicate_ids_skip_self_pairs) {
    LinkTracker t;
    const UnitLinkSet s{.friends={3U,3U},.friend_count=2U,.others={},.other_count=0U};
    const std::array<UnitLinkSet,1> sets{s};
    REQUIRE(t.build(sets)==BuildStatus::duplicate_id);
    REQUIRE(t.pair_count()==0U);
    return true;
}

RUNTIME_TEST_MAIN()
