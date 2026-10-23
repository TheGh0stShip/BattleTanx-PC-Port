#include "battletanx/campaign_ai_parameters.hpp"

#include "battletanx/navigation_route.hpp"
#include "battletanx/placed_objects.hpp"
#include "battletanx/runtime_math.hpp"

#include <cmath>

// Evidence (decomp paths under src/code/gameplay unless noted):
//   CONFIRMED  0x1102 ai_parameter_member_progress_ratio.c, 0x1103 ai_param_member_progress_weight.c
//   CONFIRMED  0x1108 ai_parameter_linked_unit_behind.c, 0x1109 ai_parameter_linked_unit_ahead.c,
//              ai_measure_relative_point.c (result = head delta vs +0x400)
//   CONFIRMED  0x1110/0x1111 ai_parameter_member_chain.c
//   CONFIRMED  0x1113 ai_parameter_member_route_reachable.c, ai_horizontal_distance.c, get_scaled_difficulty.c
//   CONFIRMED  0x210E ai_parameter_owner_objective.c, runtime/get_range_progress.c, ai_schedule_owner_timer.c
//   CONFIRMED  0x2113 ai_param_target_class_weight_b.c, 0x2116 ai_param_owner_chain_length_weight.c,
//              0x2117 ai_parameter_inverse_chain_weight.c
//   STRONG INFERENCE  unit +0x4E0/+0x4E4 = RailState length/travel (func_80097038 writer);
//              unit +0x98 list = queen_world carried_by; +0x1B8 base = owner_links BaseSample;
//   CONFIRMED  ai_measure_relative_point (asm func_800E28DC): angle = cosine_to_angle(dz/len)
//              (func_800F3BE4 table search, truncated fraction), negated when dx/len < 0.
//   TEMPORARY POLICY  no rail / zero length -> ratio 0; missing linked unit -> 0/0;
//              objective >= owner slots in 0x2113 -> weight 0; retail unit id = pool slot;
//              owner points fall back to the base position; 0x1113 modes 6/7 with no
//              goal point (AiGoal::has_point false) evaluate as mode 0 (retail reads a
//              possibly stale +0xA4/+0xA8 point; campaign_ai may hold default zeros).
//   UNRESOLVED  single port owner of unit +0x9C: campaign_ai move.mode (read here via
//              member_goal_mode) vs owner_links UnitLinkFields::action (integration item).
//   UNRESOLVED  unit +0xB0 -> +0x05 byte read by 0x1113 mode 5 (caller-supplied chase_flags).

namespace battletanx::campaign_ai_parameters {

using namespace retail_values;

namespace {

float rail_ratio(const RailState* rail) noexcept {
    if (rail==nullptr || !(rail->length>0.0F)) return 0.0F;
    return rail->travel/rail->length;
}

std::int32_t retail_wrap(std::int32_t delta) noexcept {
    if (delta>0) {
        if (delta>0x8000) return delta-0xFFFF;
    } else if (delta< -0x8000) {
        return delta+0xFFFF;
    }
    return delta;
}

const UnitRecord* unit_at_slot(const UnitPool& pool,std::uint8_t slot) noexcept {
    if (slot>=unit_capacity) return nullptr;
    const EntityId id=pool.slot_id(slot);
    return id.valid() ? pool.get(id) : nullptr;
}

const owner_links::BaseSample* base_sample(const owner_links::OwnerSnapshot* snapshot,std::uint8_t owner) noexcept {
    if (snapshot==nullptr || owner>=owner_links::retail_values::owner_slots) return nullptr;
    return &snapshot->bases[owner];
}

std::size_t carried_count(const queen_world::QueenWorld* queens,EntityId unit) noexcept {
    if (queens==nullptr) return 0U;
    std::array<queen_world::QueenHandle,queen_world::queen_capacity> handles{};
    return queens->carried_by(unit,handles);
}

void clear_owned(ExternalParameters& p) noexcept {
    p.p1102=0.0F; p.p1103=0.0F; p.p1108=0.0F; p.p1109=0.0F;
    p.p1110=0.0F; p.p1111=0.0F; p.p1113=0.0F;
    p.p210E=0.0F; p.p2113=0.0F; p.p2116=0.0F; p.p2117=0.0F;
    p.segment_x=0.0F; p.segment_z=0.0F; p.segment_dx=0.0F; p.segment_dz=0.0F; p.segment_length=0.0F;
}

} // namespace

float route_progress(const RailState* rail,bool reversed) noexcept {
    const float ratio=rail_ratio(rail);
    return reversed ? one-ratio : ratio;
}

float route_progress_inverse(const RailState* rail,bool reversed) noexcept {
    return one-route_progress(rail,reversed);
}

std::int32_t relative_head_delta(float self_x,float self_z,float heading_degrees,
                                 float point_x,float point_z,const AngleTable& angles) noexcept {
    const float dx=point_x-self_x;
    const float dz=point_z-self_z;
    const std::uint16_t head=degrees_to_retail_angle(heading_degrees);
    const float length=std::sqrt(dx*dx+dz*dz);
    std::uint16_t angle=head;
    if (!(length<near_point_length)) {
        const float nx=dx/length, nz=dz/length;
        const std::uint16_t a=placed::retail_cosine_to_angle(nz,angles);
        angle=nx<0.0F ? static_cast<std::uint16_t>(-a) : a;
    }
    return retail_wrap(static_cast<std::int32_t>(angle)-static_cast<std::int32_t>(head));
}

AheadBehind linked_side(const UnitRecord& member,const UnitRecord* linked,const AngleTable& angles) noexcept {
    if (linked==nullptr) return {};
    const std::int32_t delta=relative_head_delta(member.x,member.z,member.heading_degrees,linked->x,linked->z,angles);
    // Sign of the heading-relative angle (retail names); not a front/back test.
    return delta<0 ? AheadBehind{.behind=one,.ahead=0.0F} : AheadBehind{.behind=0.0F,.ahead=one};
}

float chain_weight(std::size_t records,float step) noexcept {
    float total=0.0F;
    for (std::size_t i=0U; i<records; ++i) total+=step;
    return total;
}

float range_progress(std::int32_t start,std::int32_t end,std::int32_t now) noexcept {
    const auto len=static_cast<std::int32_t>(static_cast<std::uint32_t>(end)-static_cast<std::uint32_t>(start));
    const auto value=static_cast<std::int32_t>(static_cast<std::uint32_t>(now)-static_cast<std::uint32_t>(start));
    if (value>=len) return one;
    if (len<=0) return one;
    if (value<0) return 0.0F;
    return static_cast<float>(value)/static_cast<float>(len);
}

float objective_state_weight(const owner_links::BaseSample* base) noexcept {
    float weight=0.0F;
    if (base!=nullptr && base->present) {
        if (base->state==0) weight=base_state0_weight;
        else if (base->state==1) weight=base_state1_weight;
    }
    return one-weight;
}

float member_reachability(const ReachInput& input) noexcept {
    const auto probe=[&]() {
        return input.route!=nullptr && input.route->route_clear(input.goal_x,input.goal_z,input.x,input.z);
    };
    switch (input.mode) {
    case 6U: {
        const float distance=navigation_route_distance(NavigationVertex{.x=input.x,.z=input.z},
                                                       NavigationVertex{.x=input.goal_x,.z=input.goal_z});
        if (owner_links::scaled_link_range(input.range_source)<distance) return 0.0F;
        return probe() ? one : 0.0F;
    }
    case 7U:
        return probe() ? one : 0.0F;
    case 5U:
        return input.chase_flag ? one : 0.0F;
    default:
        return 0.0F;
    }
}

ProviderReport fill(const ProviderInput& input,std::span<ExternalParameters,unit_capacity> out) noexcept {
    ProviderReport report{};
    for (ExternalParameters& p : out) clear_owned(p);
    if (input.units==nullptr) return report;
    const UnitPool& pool=*input.units;
    static const AngleTable fallback_angles=reference_angle_table();
    const AngleTable& angles=input.angles!=nullptr ? *input.angles : fallback_angles;
    pool.for_each([&](EntityId id,const UnitRecord& unit) {
        if (id.slot>=unit_capacity || !unit.alive) return;
        ExternalParameters& p=out[id.slot];
        std::uint8_t& gaps=report.gaps[id.slot];
        const auto gap=[&](Gap g) { gaps=static_cast<std::uint8_t>(gaps|static_cast<std::uint8_t>(g)); };

        const MemberOwnerFields* owner=input.controller!=nullptr ? input.controller->owner_fields(unit.owner) : nullptr;
        if (owner==nullptr) gap(Gap::no_owner_fields);
        const bool reversed=owner!=nullptr && owner->flag_requested;

        // 0x1102 / 0x1103 and the +0x4D0..+0x4E0 segment.
        const RailState* rail=nullptr;
        if (id.slot<input.rails.size() && input.rails[id.slot].has_value()) rail=&*input.rails[id.slot];
        if (rail==nullptr) gap(Gap::no_rail);
        else if (!(rail->length>0.0F)) gap(Gap::zero_rail_length);
        p.p1102=route_progress(rail,reversed);
        p.p1103=route_progress_inverse(rail,reversed);
        if (rail!=nullptr) {
            p.segment_x=rail->origin_x; p.segment_z=rail->origin_z;
            p.segment_dx=rail->dir_x; p.segment_dz=rail->dir_z;
            p.segment_length=rail->length;
        }

        // 0x1108 / 0x1109.
        if (owner!=nullptr && owner->target_member!=no_unit) {
            const UnitRecord* linked=unit_at_slot(pool,owner->target_member);
            if (linked==nullptr) gap(Gap::linked_unit_missing);
            const AheadBehind side=linked_side(unit,linked,angles);
            p.p1108=side.behind;
            p.p1109=side.ahead;
        }

        // 0x1110 / 0x1111.
        p.p1110=chain_weight(carried_count(input.queens,id),member_chain_weight);
        p.p1111=one-p.p1110;

        // 0x1113.
        if (input.controller!=nullptr) {
            const CampaignAiUnitState* state=input.controller->state(id.slot);
            if (state!=nullptr && state->unit==id) {
                const std::uint8_t mode=input.controller->member_goal_mode(id);
                const bool pointless=!state->move.has_point &&
                    (mode==static_cast<std::uint8_t>(GoalMode::point) || mode==static_cast<std::uint8_t>(GoalMode::point_route));
                p.p1113=member_reachability(ReachInput{
                    .mode=pointless ? std::uint8_t{0U} : mode,
                    .x=unit.x,.z=unit.z,.goal_x=state->move.x,.goal_z=state->move.z,
                    .chase_flag=id.slot<input.chase_flags.size() && input.chase_flags[id.slot],
                    .range_source=input.range_source,.route=input.route});
            }
        }

        // Owner keys, copied to every member of the owner.
        if (owner!=nullptr) {
            p.p210E=range_progress(owner->timer_start,owner->timer_end,input.tick);
            const std::uint8_t objective=owner->objective;
            const owner_links::BaseSample* base=nullptr;
            if (objective!=no_unit) {
                if (input.snapshot==nullptr) gap(Gap::no_snapshot);
                base=base_sample(input.snapshot,objective);
                if (objective>=owner_links::retail_values::owner_slots) gap(Gap::objective_out_of_range);
            }
            p.p2113=objective_state_weight(base);
            float stored=0.0F;
            if (base!=nullptr && input.snapshot!=nullptr && objective<input.snapshot->owner_count && base->present)
                stored=chain_weight(base->queens_held,owner_chain_weight);
            p.p2116=stored;
            p.p2117=one-stored;
        } else {
            p.p2113=one;
            p.p2117=one;
        }
        ++report.filled;
    });
    return report;
}

std::optional<std::array<float,2>> owner_point(const owner_links::OwnerSnapshot* snapshot,
                                               const queen_world::QueenWorld* queens,
                                               std::uint8_t owner) noexcept {
    if (owner>=owner_links::retail_values::owner_slots) return std::nullopt;
    if (snapshot!=nullptr && snapshot->objective_points[owner].has_value()) {
        const owner_plays::Point2 point=*snapshot->objective_points[owner];
        return std::array<float,2>{point.x,point.z};
    }
    if (queens!=nullptr && queens->has_base(owner)) {
        queen_world::BaseView view{};
        if (queens->view(queens->base_of(owner),view)) return std::array<float,2>{view.x,view.z};
    }
    return std::nullopt;
}

OwnerPoints owner_points(const owner_links::OwnerSnapshot* snapshot,const queen_world::QueenWorld* queens,
                         std::uint8_t owner,std::uint8_t objective) noexcept {
    OwnerPoints points{};
    if (objective!=no_unit) points.objective_point=owner_point(snapshot,queens,objective);
    points.anchor=owner_point(snapshot,queens,owner);
    return points;
}

void apply_owner_points(CampaignAiController& controller,const owner_links::OwnerSnapshot* snapshot,
                        const queen_world::QueenWorld* queens) noexcept {
    for (std::uint8_t owner=0U; owner<owner_capacity; ++owner) {
        const MemberOwnerFields* fields=controller.owner_fields(owner);
        if (fields==nullptr) continue;
        const std::uint8_t objective=fields->objective;
        const OwnerPoints points=owner_points(snapshot,queens,owner,objective);
        controller.set_owner_objective(owner,objective,points.objective_point);
        controller.set_owner_anchor(owner,points.anchor);
    }
}

} // namespace battletanx::campaign_ai_parameters
