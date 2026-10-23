#include "battletanx/campaign_ai_parameters.hpp"
#include "battletanx/runtime_math.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <optional>
#include <vector>

using namespace battletanx;
namespace cap=battletanx::campaign_ai_parameters;
using runtime_test::near;

namespace {

UnitRecord unit_at(std::uint8_t owner,float x,float z,float heading=0.0F) {
    UnitRecord unit{};
    unit.owner=owner;
    unit.team=owner;
    unit.alive=true;
    unit.control=UnitControl::computer;
    unit.x=x;
    unit.z=z;
    unit.heading_degrees=heading;
    return unit;
}

EntityId add(UnitPool& pool,const UnitRecord& unit) {
    const EntityId id=*pool.allocate();
    *pool.get(id)=unit;
    return id;
}

RailState rail(float length,float travel) {
    RailState r{};
    r.origin_x=5.0F; r.origin_z=6.0F; r.dir_x=0.0F; r.dir_z=1.0F;
    r.length=length; r.travel=travel;
    return r;
}

class FixedProbe final : public owner_links::RouteProbe {
public:
    explicit FixedProbe(bool clear) : clear_(clear) {}
    [[nodiscard]] bool route_clear(float from_x,float from_z,float to_x,float to_z) const override {
        last_from_x=from_x; last_from_z=from_z; last_to_x=to_x; last_to_z=to_z;
        return clear_;
    }
    mutable float last_from_x{}, last_from_z{}, last_to_x{}, last_to_z{};
private:
    bool clear_;
};

} // namespace

RUNTIME_TEST(progress_ratio_and_reverse) {
    const RailState r=rail(200.0F,50.0F);
    REQUIRE(near(cap::route_progress(&r,false),0.25));
    REQUIRE(near(cap::route_progress(&r,true),0.75));
    REQUIRE(near(cap::route_progress_inverse(&r,false),0.75));
    REQUIRE(near(cap::route_progress_inverse(&r,true),0.25));
    return true;
}

RUNTIME_TEST(progress_missing_rail_policy) {
    REQUIRE(cap::route_progress(nullptr,false)==0.0F);
    REQUIRE(cap::route_progress(nullptr,true)==1.0F);
    REQUIRE(cap::route_progress_inverse(nullptr,false)==1.0F);
    const RailState zero=rail(0.0F,10.0F);
    REQUIRE(cap::route_progress(&zero,false)==0.0F);
    return true;
}

RUNTIME_TEST(linked_side_ahead_behind_and_ties) {
    const AngleTable angles=reference_angle_table();
    const UnitRecord self=unit_at(0,0.0F,0.0F,0.0F);
    const UnitRecord right=unit_at(0,10.0F,10.0F);
    const UnitRecord left=unit_at(0,-10.0F,10.0F);
    const UnitRecord same=unit_at(0,0.0F,0.0F);
    const UnitRecord opposite=unit_at(0,0.0F,-10.0F);
    REQUIRE(cap::linked_side(self,&right,angles).ahead==1.0F);
    REQUIRE(cap::linked_side(self,&right,angles).behind==0.0F);
    REQUIRE(cap::linked_side(self,&left,angles).behind==1.0F);
    REQUIRE(cap::linked_side(self,&left,angles).ahead==0.0F);
    // Coincident point: angle = heading, delta 0 -> ahead.
    REQUIRE(cap::linked_side(self,&same,angles).ahead==1.0F);
    // Exactly opposite: delta 0x8000 is not wrapped (>= 0) -> ahead.
    REQUIRE(cap::relative_head_delta(0.0F,0.0F,0.0F,0.0F,-10.0F,angles)==0x8000);
    REQUIRE(cap::linked_side(self,&opposite,angles).ahead==1.0F);
    // Missing linked unit.
    REQUIRE(cap::linked_side(self,nullptr,angles).ahead==0.0F);
    REQUIRE(cap::linked_side(self,nullptr,angles).behind==0.0F);
    return true;
}

RUNTIME_TEST(relative_delta_uses_retail_wrap) {
    const AngleTable angles=reference_angle_table();
    // heading 270 deg (0xC000), point at +z (angle 0): raw +0 - 0xC000 = -0xC000 -> +0x3FFF.
    REQUIRE(cap::relative_head_delta(0.0F,0.0F,270.0F,0.0F,10.0F,angles)==-0xC000+0xFFFF);
    return true;
}

RUNTIME_TEST(chain_weights_accumulate_in_float) {
    REQUIRE(cap::chain_weight(0U,0.2F)==0.0F);
    float expected=0.0F;
    for (int i=0; i<3; ++i) expected+=0.2F;
    REQUIRE(cap::chain_weight(3U,0.2F)==expected);
    return true;
}

RUNTIME_TEST(range_progress_edges) {
    REQUIRE(near(cap::range_progress(100,300,200),0.5));
    REQUIRE(cap::range_progress(100,300,50)==0.0F);
    REQUIRE(cap::range_progress(100,300,300)==1.0F);
    REQUIRE(cap::range_progress(100,100,100)==1.0F);   // zero length
    REQUIRE(cap::range_progress(300,100,0)==1.0F);     // negative length -> 1 (checked before value<0)
    return true;
}

RUNTIME_TEST(objective_state_weight_cases) {
    REQUIRE(cap::objective_state_weight(nullptr)==1.0F);
    owner_links::BaseSample base{};
    REQUIRE(cap::objective_state_weight(&base)==1.0F); // not present
    base.present=true;
    base.state=0;
    REQUIRE(near(cap::objective_state_weight(&base),0.5));
    base.state=1;
    REQUIRE(near(cap::objective_state_weight(&base),0.6));
    base.state=7;
    REQUIRE(cap::objective_state_weight(&base)==1.0F);
    return true;
}

RUNTIME_TEST(reachability_modes) {
    const FixedProbe yes(true);
    const FixedProbe no(false);
    cap::ReachInput in{.mode=7U,.x=1.0F,.z=2.0F,.goal_x=3.0F,.goal_z=4.0F,.chase_flag=false,
                      .range_source=0,.route=&yes};
    REQUIRE(cap::member_reachability(in)==1.0F);
    REQUIRE(yes.last_from_x==3.0F && yes.last_to_z==2.0F);   // goal -> unit
    in.route=&no;
    REQUIRE(cap::member_reachability(in)==0.0F);
    in.route=nullptr;
    REQUIRE(cap::member_reachability(in)==0.0F);
    // Mode 6: range 2000 default; distance max+min*3/8.
    in.mode=6U; in.route=&yes; in.x=0.0F; in.z=0.0F; in.goal_x=2000.0F; in.goal_z=0.0F;
    REQUIRE(cap::member_reachability(in)==1.0F);       // tie: 2000 < 2000 false
    in.goal_x=2001.0F;
    REQUIRE(cap::member_reachability(in)==0.0F);
    in.mode=5U; in.chase_flag=true;
    REQUIRE(cap::member_reachability(in)==1.0F);
    in.chase_flag=false;
    REQUIRE(cap::member_reachability(in)==0.0F);
    in.mode=9U;
    REQUIRE(cap::member_reachability(in)==0.0F);
    return true;
}

RUNTIME_TEST(fill_from_pool_and_snapshot) {
    UnitPool pool;
    const EntityId a=add(pool,unit_at(1,0.0F,0.0F));
    const EntityId b=add(pool,unit_at(1,-10.0F,10.0F));
    CampaignAiController controller;
    MemberOwnerFields* owner=controller.owner_fields(1);
    REQUIRE(owner!=nullptr);
    owner->target_member=static_cast<std::uint8_t>(b.slot);
    owner->flag_requested=true;
    owner->timer_start=0;
    owner->timer_end=100;
    owner->objective=2;
    owner_links::OwnerSnapshot snapshot{};
    snapshot.owner_count=4U;
    snapshot.bases[2]=owner_links::BaseSample{.present=true,.queens_held=2U,.state=1,.has_record=true};
    std::vector<std::optional<RailState>> rails(unit_capacity);
    rails[a.slot]=rail(100.0F,25.0F);

    std::array<ExternalParameters,unit_capacity> out{};
    out[a.slot].p1100=7.0F;   // not owned: must survive
    const cap::ProviderReport report=cap::fill(cap::ProviderInput{
        .units=&pool,.controller=&controller,.queens=nullptr,.snapshot=&snapshot,.route=nullptr,
        .rails=rails,.chase_flags={},.range_source=0,.tick=25,.angles=nullptr},out);
    REQUIRE(report.filled==2U);
    const ExternalParameters& p=out[a.slot];
    REQUIRE(p.p1100==7.0F);
    REQUIRE(near(p.p1102,0.75));
    REQUIRE(near(p.p1103,0.25));
    REQUIRE(p.segment_x==5.0F && p.segment_length==100.0F);
    REQUIRE(p.p1108==1.0F && p.p1109==0.0F);
    REQUIRE(p.p1110==0.0F && p.p1111==1.0F);      // no queen world
    REQUIRE(p.p1113==0.0F);                       // controller has no bound state
    REQUIRE(near(p.p210E,0.25));
    REQUIRE(near(p.p2113,0.6));
    REQUIRE(near(p.p2116,0.4));
    REQUIRE(near(p.p2117,0.6));
    REQUIRE((report.gaps[a.slot]&static_cast<std::uint8_t>(cap::Gap::no_rail))==0U);
    REQUIRE((report.gaps[b.slot]&static_cast<std::uint8_t>(cap::Gap::no_rail))!=0U);
    // b's linked unit is itself: coincident -> ahead.
    REQUIRE(out[b.slot].p1109==1.0F);
    return true;
}

RUNTIME_TEST(fill_missing_data) {
    UnitPool pool;
    const EntityId a=add(pool,unit_at(1,0.0F,0.0F));
    UnitRecord dead=unit_at(1,0.0F,0.0F);
    dead.alive=false;
    const EntityId d=add(pool,dead);
    CampaignAiController controller;
    MemberOwnerFields* owner=controller.owner_fields(1);
    owner->target_member=19U;   // empty slot
    owner->objective=0xFFU;
    std::array<ExternalParameters,unit_capacity> out{};
    out[d.slot].p2117=9.0F;
    const cap::ProviderReport report=cap::fill(cap::ProviderInput{.units=&pool,.controller=&controller,.queens=nullptr,.snapshot=nullptr,
        .route=nullptr,.rails={},.chase_flags={},.range_source=0,.tick=0,.angles=nullptr},out);
    REQUIRE(report.filled==1U);
    REQUIRE(out[d.slot].p2117==0.0F);   // dead slot reset
    const ExternalParameters& p=out[a.slot];
    REQUIRE(p.p1108==0.0F && p.p1109==0.0F);
    REQUIRE(p.p2113==1.0F && p.p2116==0.0F && p.p2117==1.0F);
    REQUIRE(p.p1102==0.0F && p.p1103==1.0F);
    const std::uint8_t g=report.gaps[a.slot];
    REQUIRE((g&static_cast<std::uint8_t>(cap::Gap::linked_unit_missing))!=0U);
    REQUIRE((g&static_cast<std::uint8_t>(cap::Gap::no_snapshot))==0U);   // objective 0xFF: snapshot unused
    // Objective beyond owner slots.
    owner->objective=9U;
    const cap::ProviderReport r2=cap::fill(cap::ProviderInput{.units=&pool,.controller=&controller,.queens=nullptr,.snapshot=nullptr,
        .route=nullptr,.rails={},.chase_flags={},.range_source=0,.tick=0,.angles=nullptr},out);
    REQUIRE((r2.gaps[a.slot]&static_cast<std::uint8_t>(cap::Gap::objective_out_of_range))!=0U);
    REQUIRE((r2.gaps[a.slot]&static_cast<std::uint8_t>(cap::Gap::no_snapshot))!=0U);
    REQUIRE(out[a.slot].p2113==1.0F);
    // No pool at all.
    const cap::ProviderReport r3=cap::fill(cap::ProviderInput{},out);
    REQUIRE(r3.filled==0U && out[a.slot].p2113==0.0F);
    return true;
}

RUNTIME_TEST(owner_points_snapshot_and_missing) {
    owner_links::OwnerSnapshot snapshot{};
    snapshot.objective_points[2]=owner_plays::Point2{.x=11.0F,.z=12.0F};
    const cap::OwnerPoints pts=cap::owner_points(&snapshot,nullptr,1U,2U);
    REQUIRE(pts.objective_point.has_value());
    REQUIRE((*pts.objective_point)[0]==11.0F && (*pts.objective_point)[1]==12.0F);
    REQUIRE(!pts.anchor.has_value());
    REQUIRE(!cap::owner_points(&snapshot,nullptr,1U,0xFFU).objective_point.has_value());
    REQUIRE(!cap::owner_point(nullptr,nullptr,7U).has_value());
    CampaignAiController controller;
    controller.owner_fields(0)->objective=2U;
    cap::apply_owner_points(controller,&snapshot,nullptr);
    REQUIRE(controller.owner_fields(0)->objective==2U);
    REQUIRE(controller.owner_fields(0)->objective_point.has_value());
    REQUIRE(!controller.owner_fields(0)->anchor.has_value());
    return true;
}

RUNTIME_TEST_MAIN()
