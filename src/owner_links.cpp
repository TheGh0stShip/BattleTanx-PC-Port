#include "battletanx/owner_links.hpp"

#include <initializer_list>

namespace battletanx::owner_links {
namespace {

using namespace retail_values;

// ai_horizontal_distance (func_800E0C8C): max + min*3/8 with
// D_8007443C..48 = 3.0, 8.0, 3.0, 8.0 (ROM 0x443C..0x4448).
float horizontal_distance(float ax,float az,float bx,float bz) noexcept {
    float dz=az-bz;
    if (!(dz>0.0F)) dz=-dz;
    float dx=ax-bx;
    if (!(dx>0.0F)) dx=-dx;
    if (dx<dz) return dz+(dx*3.0F)/8.0F;
    return dx+(dz*3.0F)/8.0F;
}

void push(UpdateResult& out,CommandKind kind,std::uint8_t unit,std::uint8_t other=0U,float value=0.0F) noexcept {
    if (out.command_count>=command_capacity) {
        out.commands_truncated=true;
        return;
    }
    out.commands[out.command_count]=Command{.kind=kind,.unit=unit,.other=other,.value=value};
    ++out.command_count;
}

void clear_pair(PairRecord& pair) noexcept {
    pair.checked=false;
    pair.linked=false;
    pair.in_range=false;
    pair.value07=false;
    pair.distance=0.0F;
    pair.value03=0U;
}

} // namespace

void UnitLinkFields::serialize(StateWriter& writer) const {
    writer.id(unit);
    writer.u32(mode);
    writer.s32(action);
    writer.boolean(action_point);
    writer.f32(point_x);
    writer.f32(point_z);
    writer.u8(follow);
    writer.u16(value344);
    writer.u8(value348);
    writer.boolean(pair_eligible);
    writer.boolean(tracking);
    writer.s32(track_start);
}

void PairRecord::serialize(StateWriter& writer) const {
    writer.u8(first);
    writer.u8(second);
    writer.boolean(same_set);
    writer.u8(value03);
    writer.boolean(checked);
    writer.boolean(linked);
    writer.boolean(in_range);
    writer.boolean(value07);
    writer.f32(distance);
}

// get_scaled_difficulty (func_800DDC68).
float scaled_link_range(std::int32_t range_source) noexcept {
    const float value=static_cast<float>(range_source);
    return range_threshold<value ? value*range_scale : range_default;
}

LinkTracker::LinkTracker() noexcept {
    for (auto& row : index_) row.fill(no_pair);
}

void LinkTracker::add_pair(std::uint8_t a,std::uint8_t b,bool same_set,BuildStatus& status) noexcept {
    if (a>=unit_slots || b>=unit_slots) {
        status=BuildStatus::id_out_of_range;
        return;
    }
    if (a==b) {
        // TEMPORARY POLICY: retail records a self pair for duplicated ids (lo==hi); skipped.
        status=BuildStatus::duplicate_id;
        return;
    }
    const std::uint8_t lo=a<b ? a : b;
    const std::uint8_t hi=a<b ? b : a;
    if (index_[lo][hi]!=no_pair) return;
    if (pair_count_>=pair_capacity) {
        status=BuildStatus::pair_overflow;
        return;
    }
    index_[lo][hi]=static_cast<std::uint8_t>(pair_count_);
    // initialize_byte_record (func_800E5688): everything but ids/flag zeroed.
    pairs_[pair_count_]=PairRecord{.first=lo,.second=hi,.same_set=same_set,.value03=0U,.checked=false,.linked=false,
                                   .in_range=false,.value07=false,.distance=0.0F};
    ++pair_count_;
}

BuildStatus LinkTracker::build(std::span<const UnitLinkSet> sets) noexcept {
    // Validate first so an error leaves the previous table intact.
    for (const UnitLinkSet& set : sets) {
        if (set.friend_count>set_members || set.other_count>set_members) return BuildStatus::too_many_members;
    }
    for (const UnitLinkSet& set : sets) {
        for (std::uint8_t i=0U; i<set.friend_count; ++i)
            if (set.friends[i]>=unit_slots) return BuildStatus::id_out_of_range;
        for (std::uint8_t j=0U; j<set.other_count; ++j)
            if (set.others[j]>=unit_slots) return BuildStatus::id_out_of_range;
    }
    BuildStatus status=BuildStatus::ok;
    phase_=0U;
    pair_count_=0U;
    for (auto& row : index_) row.fill(no_pair);
    for (const UnitLinkSet& set : sets) {
        for (std::uint8_t i=0U; i<set.friend_count; ++i)
            for (std::uint8_t j=0U; j<set.friend_count; ++j)
                if (i!=j) add_pair(set.friends[i],set.friends[j],true,status);
        for (std::uint8_t i=0U; i<set.friend_count; ++i)
            for (std::uint8_t j=0U; j<set.other_count; ++j)
                add_pair(set.friends[i],set.others[j],false,status);
    }
    return status;
}

void LinkTracker::bind_unit(std::uint8_t id,EntityId unit) noexcept {
    if (id<unit_slots) units_[id].unit=unit;
}

void LinkTracker::set_mode(std::uint8_t id,std::uint32_t mode) noexcept {
    if (id<unit_slots) units_[id].mode=mode;
}

void LinkTracker::set_action(std::uint8_t id,std::int32_t action,std::optional<float> point_x,
                             std::optional<float> point_z) noexcept {
    if (id>=unit_slots) return;
    UnitLinkFields& unit=units_[id];
    unit.action=action;
    unit.action_point=point_x.has_value() && point_z.has_value();
    unit.point_x=point_x.value_or(0.0F);
    unit.point_z=point_z.value_or(0.0F);
    // +0xB0 is left as is (retail writes it only in init_unit_link_command).
}

// init_unit_link_command (func_800E36FC): +0xA0=0, +0x9C=8, +0xB0=target, then
// update_action_goal (func_800E360C) on the source's +0x9C goal.
void LinkTracker::follow(std::uint8_t source,std::uint8_t target,const UpdateInput& input) noexcept {
    if (source>=unit_slots || target>=unit_slots) return;
    UnitLinkFields& unit=units_[source];
    const UnitLinkFields& goal=units_[target];
    unit.action=action_kind::follow_unit;
    unit.follow=target;
    unit.action_point=false;
    if (!input.units[target].in_use) {          // target +0x01 == 0: kind=0, point=0
        unit.action=action_kind::none;
        return;
    }
    if (goal.value344==0U) {                    // fallback, then the switch still runs
        unit.action=action_kind::move_point;
        unit.action_point=true;
        unit.point_x=input.units[source].x;
        unit.point_z=input.units[source].z;
    }
    switch (goal.action) {
    case 6: case 7: case 8:
        unit.point_x=goal.point_x;
        unit.point_z=goal.point_z;
        return;
    case 5:
        // UNRESOLVED: retail copies the target's action target (+0xAC) position
        // when alive, else clears; that entity is not tracked, point kept.
        return;
    default:
        clear_action(source);
        return;
    }
}

void LinkTracker::clear_action(std::uint8_t id) noexcept {
    if (id>=unit_slots) return;
    UnitLinkFields& unit=units_[id];
    unit.action=action_kind::none;
    unit.action_point=false;
    unit.value344=0U;
    unit.value348=0U;
    unit.pair_eligible=false;
}

void LinkTracker::set_follow_allowed(std::uint8_t id,std::uint16_t value344) noexcept {
    if (id<unit_slots) units_[id].value344=value344;
}

void LinkTracker::clear_unit_links(std::uint8_t id) noexcept {
    if (id>=unit_slots) return;
    for (std::uint8_t other=0U; other<unit_slots; ++other) {
        if (other==id) continue;
        const std::uint8_t lo=id<other ? id : other;
        const std::uint8_t hi=id<other ? other : id;
        // TEMPORARY POLICY: retail clears record D_8033B450 says (0xFF -> past
        // the array); unbuilt pairs are skipped here.
        const std::uint8_t slot=index_[lo][hi];
        if (slot!=no_pair && slot<pair_count_) clear_pair(pairs_[slot]);
    }
}

const PairRecord* LinkTracker::pair(std::uint8_t a,std::uint8_t b) const noexcept {
    if (a>=unit_slots || b>=unit_slots || a==b) return nullptr;
    const std::uint8_t lo=a<b ? a : b;
    const std::uint8_t hi=a<b ? b : a;
    const std::uint8_t slot=index_[lo][hi];
    return slot!=no_pair && slot<pair_count_ ? &pairs_[slot] : nullptr;
}

const UnitLinkFields* LinkTracker::unit(std::uint8_t id) const noexcept {
    return id<unit_slots ? &units_[id] : nullptr;
}

// resolve_unit_pair (func_800E1F14).
void LinkTracker::resolve(std::uint8_t first,std::uint8_t second,float amount,const UpdateInput& input,
                          UpdateResult& out) noexcept {
    if (first>=unit_slots || second>=unit_slots) return;
    UnitLinkFields& f=units_[first];
    UnitLinkFields& s=units_[second];
    if (s.action==action_kind::move_point) {
        clear_action(second);
        push(out,CommandKind::clear_action,second);
    } else if (s.action==action_kind::follow_unit) {
        if (amount<resolve_follow_distance) {
            clear_action(first);
            push(out,CommandKind::clear_action,first);
        }
    } else if (f.action==action_kind::follow_unit) {
        if (amount<resolve_credit_distance) {
            if (f.follow!=second) {
                push(out,CommandKind::self_kill_credit,first);
            } else {
                s.action_point=false;
                s.pair_eligible=false;
                push(out,CommandKind::release_follow,second);
            }
        }
    } else if (resolve_release_distance<amount) {
        f.action_point=false;
        f.pair_eligible=false;
        push(out,CommandKind::release_follow,first);
    } else if (s.value344!=0U) {
        follow(first,second,input);
        s.action_point=false;
        s.pair_eligible=false;
        // func_800E36FC always runs update_linked_unit (func_800E5F30) on the
        // source, whatever update_action_goal left; the consumer reads the final
        // +0x9C/+0xA0 from unit(first). A dead target leaves +0x344/+0x348/+0x34A.
        push(out,CommandKind::follow_unit,first,second);
        push(out,CommandKind::release_follow,second);
    }
}

// update_unit_pair_link (func_800E56B0).
void LinkTracker::update_pair_link(PairRecord& pair,const UpdateInput& input,UpdateResult& out) noexcept {
    const UnitSample& a=input.units[pair.first];
    const UnitSample& b=input.units[pair.second];
    const UnitLinkFields& la=units_[pair.first];
    const UnitLinkFields& lb=units_[pair.second];
    const bool was_linked=pair.linked;
    clear_pair(pair);
    if (!a.in_use || !b.in_use) return;
    const float distance=horizontal_distance(a.x,a.z,b.x,b.z);
    pair.distance=distance;
    const bool in_range=distance<scaled_link_range(input.range_source);
    pair.checked=true;
    pair.in_range=in_range;
    const bool both_ready=a.status_two && b.status_two &&
                          (a.flags&unit_flag_excluded)==0U && (b.flags&unit_flag_excluded)==0U;
    if (pair.same_set && in_range) {
        const bool no_a=!la.action_point;
        const bool no_b=!lb.action_point;
        if (no_a || no_b) {
            if (distance<=link_distance && both_ready) {
                if (no_a && !no_b) resolve(pair.first,pair.second,distance,input,out);
                else if (!no_a && no_b) resolve(pair.second,pair.first,distance,input,out);
            }
            return;
        }
        const float anchor_distance=horizontal_distance(la.point_x,la.point_z,lb.point_x,lb.point_z);
        const float to_anchor_b=horizontal_distance(a.x,a.z,lb.point_x,lb.point_z);
        const float to_anchor_a=horizontal_distance(b.x,b.z,la.point_x,la.point_z);
        if (!both_ready) return;
        if (anchor_distance<=link_distance || to_anchor_b<=link_distance || to_anchor_a<=link_distance ||
            (was_linked && distance<=link_distance)) {
            pair.linked=true;
            if (la.pair_eligible && lb.pair_eligible) {
                const auto target_carrying=[&](const UnitLinkFields& unit) noexcept {
                    return unit.follow<unit_slots && input.units[unit.follow].carrying;
                };
                if (lb.action==action_kind::follow_unit && target_carrying(lb))
                    resolve(pair.first,pair.second,distance,input,out);
                else if (la.action==action_kind::follow_unit && target_carrying(la))
                    resolve(pair.second,pair.first,distance,input,out);
                else if (b.carrying) resolve(pair.first,pair.second,distance,input,out);
                else if (a.carrying) resolve(pair.second,pair.first,distance,input,out);
                else if (to_anchor_b<to_anchor_a) resolve(pair.second,pair.first,distance,input,out);
                else resolve(pair.first,pair.second,distance,input,out);
            }
        }
    } else if (in_range) {
        if (input.route!=nullptr && input.route->route_clear(a.x,a.z,b.x,b.z)) pair.linked=true;
    }
}

// update_unit_pairings (func_800E5BA0).
void LinkTracker::update_pairings(bool friendly,bool cross,const UpdateInput& input,UpdateResult& out) noexcept {
    if (friendly) {
        for (std::uint8_t i=0U; i<unit_slots; ++i)
            if (input.units[i].in_use) units_[i].pair_eligible=true;
    }
    if (cross) {
        for (std::uint8_t i=0U; i<unit_slots; ++i) push(out,CommandKind::restart_parameter,i,0U,0.0F);
    }
    for (std::uint16_t j=0U; j<pair_count_; ++j) {
        PairRecord& pair=pairs_[j];
        if (pair.same_set) {
            if (friendly) update_pair_link(pair,input,out);
            continue;
        }
        if (!cross) continue;
        const UnitSample& a=input.units[pair.first];
        const UnitSample& b=input.units[pair.second];
        if (!a.in_use) {
            clear_pair(pair);
        } else if (((a.flags&unit_flag_excluded)!=0U && (b.flags&unit_flag_excluded)!=0U) ||
                   (a.status_one && b.status_one)) {
            clear_pair(pair);
            pair.checked=true;
            pair.value07=true;
        } else {
            update_pair_link(pair,input,out);
            if (pair.linked) {
                for (const std::uint8_t id : {pair.first,pair.second}) {
                    push(out,CommandKind::restart_parameter,id,0U,1.0F);
                    units_[id].track_start=input.tick;
                }
            }
        }
    }
}

// update_unit_tracked_kind1b (func_800E5AB0), inlined in phase 1.
void LinkTracker::track_kind1b(const UpdateInput& input,UpdateResult& out) noexcept {
    for (std::uint8_t i=0U; i<unit_slots; ++i) {
        const UnitSample& sample=input.units[i];
        if (!sample.in_use) continue;
        UnitLinkFields& unit=units_[i];
        const bool match=sample.tracked_kind1b_owner.has_value() && *sample.tracked_kind1b_owner==i;
        if (match==unit.tracking) continue;
        if (match) {
            push(out,CommandKind::restart_parameter,i,0U,1.0F);
            unit.track_start=input.tick;
            unit.tracking=true;
        } else {
            push(out,CommandKind::clear_tracked,i);
            unit.tracking=false;
        }
    }
}

// ai_run_update_phase (func_800E6754).
UpdateResult LinkTracker::update(const UpdateInput& input) noexcept {
    UpdateResult out{};
    if (toggle_timer_>0) {
        --toggle_timer_;
    } else {
        toggle_timer_=toggle_period;
        toggle_=!toggle_;
        out.toggled=true;
    }
    out.phase=phase_;
    switch (phase_) {
    case 0U:
        update_pairings(true,false,input,out);
        phase_=1U;
        break;
    case 1U:
        update_pairings(false,true,input,out);
        track_kind1b(input,out);
        phase_=2U;
        break;
    case 2U:
        push(out,CommandKind::run_phase_two,0U);
        phase_=3U;
        break;
    default: {
        float flag=0.0F;
        if (round_robin_>0) {
            --round_robin_;
        } else {
            flag=1.0F;
            round_robin_=round_robin_period;
        }
        push(out,CommandKind::run_round_robin,0U,0U,flag);
        phase_=0U;
        break;
    }
    }
    return out;
}

void LinkTracker::serialize(StateWriter& writer) const {
    writer.u16(pair_count_);
    for (std::uint16_t i=0U; i<pair_count_; ++i) pairs_[i].serialize(writer);
    for (const auto& row : index_)
        for (const std::uint8_t slot : row) writer.u8(slot);
    for (const UnitLinkFields& unit : units_) unit.serialize(writer);
    writer.u8(phase_);
    writer.s16(toggle_timer_);
    writer.boolean(toggle_);
    writer.s16(round_robin_);
}

// ---------------------------------------------------------------------------

std::uint8_t foreign_link_count(const OwnerSnapshot& snapshot,std::uint16_t query_group) noexcept {
    std::uint8_t count=0U;
    for (const QueenSample& queen : snapshot.queens) {
        // Base-held queens point +0x28 at the kind-20 base record (+8 != 0) and
        // are skipped; dropped queens: TEMPORARY POLICY not counted.
        if (queen.place==QueenPlace::carried && queen.carrier_group!=query_group) ++count;
    }
    return count;
}

owner_plays::OwnerWorld owner_world(const OwnerSnapshot& snapshot,
                                    std::span<const UnitSample,unit_slots> units,
                                    std::uint8_t owner) noexcept {
    owner_plays::OwnerWorld world{};
    world.owner_count=snapshot.owner_count<=owner_slots ? snapshot.owner_count
                                                         : static_cast<std::uint8_t>(owner_slots);
    for (std::size_t o=0U; o<owner_slots; ++o) {
        owner_plays::OwnerFacts& facts=world.owners[o];
        const BaseSample& base=snapshot.bases[o];
        facts.chain_active=base.present;
        facts.chain_nodes=base.present ? base.queens_held : std::uint8_t{0U};
        facts.chain_state=base.has_record ? std::optional<std::int32_t>{base.state} : std::nullopt;
        for (std::size_t m=0U; m<squad_members; ++m) {
            const UnitSample& unit=units[o*squad_members+m];
            if (unit.in_use && unit.carrying) facts.has_flagged_member=true;
        }
    }
    // STRONG INFERENCE: team record = present owners sharing the team, ascending.
    if (owner<owner_slots) {
        for (std::uint8_t o=0U; o<owner_slots && world.team_list_count<world.team_list.size(); ++o) {
            if (snapshot.owner_present[o] && snapshot.owner_team[o]==snapshot.owner_team[owner]) {
                world.team_list[world.team_list_count]=o;
                ++world.team_list_count;
            }
        }
    }
    if (owner<owner_slots) world.foreign_links=foreign_link_count(snapshot,snapshot.owner_group[owner]);
    world.elapsed_ticks=snapshot.elapsed_ticks;
    world.objective_points=snapshot.objective_points;
    return world;
}

void apply_link_fields(const LinkTracker& tracker,std::uint8_t owner,
                       std::span<owner_plays::MemberView,squad_members> members) noexcept {
    if (owner>=owner_slots) return;
    for (std::size_t m=0U; m<squad_members; ++m) {
        const UnitLinkFields* fields=tracker.unit(static_cast<std::uint8_t>(owner*squad_members+m));
        if (fields==nullptr) continue;
        members[m].state=fields->action;
        members[m].mode=fields->mode;
    }
}

} // namespace battletanx::owner_links
