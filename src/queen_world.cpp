#include "battletanx/queen_world.hpp"

#include <algorithm>

namespace battletanx::queen_world {
namespace {

constexpr std::uint8_t serialize_version=2U;   // 2: dropped_events_
constexpr std::uint8_t retail_at_base=0U;   // effect +0x10 values
constexpr std::uint8_t retail_carried=1U;
constexpr std::uint8_t retail_dropped=2U;
constexpr std::size_t no_slot=static_cast<std::size_t>(-1);

bool queen_kind(std::uint32_t game_kind) noexcept {
    // apply_mode_display_rows: assign + recount only for kinds 0, 3, 4, 6.
    return game_kind==0U || game_kind==3U || game_kind==4U || game_kind==6U;
}

std::uint16_t next_generation(std::uint16_t generation) noexcept {
    const std::uint16_t next=static_cast<std::uint16_t>(generation+1U);
    return next==0U ? std::uint16_t{1U} : next;
}

template <typename Slot,std::size_t N>
std::size_t resolve(const std::array<Slot,N>& pool,EntityId id) noexcept {
    if (!id.valid() || id.slot>=N) return no_slot;
    const Slot& slot=pool[id.slot];
    return slot.live && slot.generation==id.generation ? std::size_t{id.slot} : no_slot;
}

template <typename Slot,std::size_t N>
std::size_t free_slot(const std::array<Slot,N>& pool) noexcept {
    for (std::size_t i=0U; i<N; ++i) if (!pool[i].live) return i;
    return no_slot;
}

EntityId make_id(std::size_t slot,std::uint16_t generation) noexcept {
    return EntityId{static_cast<std::uint16_t>(slot),generation};
}

} // namespace

CreateResult QueenWorld::create(const Config& config) {
    CreateResult out;
    if (config.owner_count>owner_capacity) { out.error=ConfigError::owner_count_invalid; return out; }
    for (const OwnerInfo& owner : config.owners) {
        if (owner.present && owner.team>=owner_capacity) { out.error=ConfigError::team_out_of_range; return out; }
    }
    for (std::uint8_t rank : config.slot_rank) {
        if (rank>=owner_capacity) { out.error=ConfigError::slot_rank_invalid; return out; }
    }
    out.world=QueenWorld(config);
    return out;
}

QueenWorld::QueenWorld(const Config& config) : config_(config) {}

bool QueenWorld::owner_ok(std::uint8_t owner) const noexcept {
    return owner<config_.owner_count && owner<owner_capacity && config_.owners[owner].present;
}

QueenHandle QueenWorld::queen_handle(std::size_t slot) const noexcept {
    return QueenHandle{make_id(slot,queens_[slot].generation)};
}

BaseHandle QueenWorld::base_handle(std::size_t slot) const noexcept {
    return BaseHandle{make_id(slot,bases_[slot].generation)};
}

std::size_t QueenWorld::queen_slot(QueenHandle handle) const noexcept { return resolve(queens_,handle.id); }
std::size_t QueenWorld::base_slot(BaseHandle handle) const noexcept { return resolve(bases_,handle.id); }
std::size_t QueenWorld::marker_slot(MarkerHandle handle) const noexcept { return resolve(markers_,handle.id); }

std::size_t QueenWorld::base_index_of(std::uint8_t owner) const noexcept {
    for (std::size_t i=0U; i<base_capacity; ++i) {
        if (bases_[i].live && bases_[i].owner==owner) return i;
    }
    return no_slot;
}

bool QueenWorld::has_base(std::size_t owner) const noexcept {
    return owner<owner_capacity && base_index_of(static_cast<std::uint8_t>(owner))!=no_slot;
}

BaseHandle QueenWorld::base_of(std::size_t owner) const noexcept {
    if (owner>=owner_capacity) return BaseHandle{};
    const std::size_t index=base_index_of(static_cast<std::uint8_t>(owner));
    return index==no_slot ? BaseHandle{} : base_handle(index);
}

std::uint8_t QueenWorld::queens_at(std::uint8_t owner) const noexcept {
    std::uint8_t count=0U;
    for (const QueenSlot& queen : queens_) {
        if (queen.live && queen.retail_state==retail_at_base && queen.holder_owner==owner) ++count;
    }
    return count;
}

QueenState QueenWorld::state_of(const QueenSlot& queen) const noexcept {
    if (queen.rescued) return QueenState::rescued;
    if (queen.retail_state==retail_carried) return QueenState::carried;
    if (queen.retail_state==retail_dropped) return QueenState::dropped;
    return queen.moved ? QueenState::stored : QueenState::at_base;
}

PlaceResult QueenWorld::place_level_object(const LevelObject& object) {
    PlaceResult out;
    if (ready_) { out.outcome=PlaceOutcome::placement_closed; return out; }
    if (object.kind<base_object_kind_first || object.kind>base_object_kind_last) {
        out.outcome=PlaceOutcome::not_base_kind;
        return out;
    }
    if (!object.active) { out.outcome=PlaceOutcome::inactive; return out; }
    if (!queen_kind(config_.game_kind)) { out.outcome=PlaceOutcome::kind_has_no_bases; return out; }
    const std::uint8_t owner=config_.slot_rank[object.kind-base_object_kind_first];
    out.owner=owner;
    // assign_group_object_points: `slot < get_entity_capacity()`. A missing
    // owner record inside the capacity is a TEMPORARY POLICY skip.
    if (!owner_ok(owner)) { out.outcome=PlaceOutcome::owner_out_of_range; return out; }
    if (base_index_of(owner)!=no_slot) { out.outcome=PlaceOutcome::duplicate_base; return out; }
    const bool spawn_queen=!(config_.game_kind==queenless_owner0_game_kind && owner==0U);
    const std::size_t base=free_slot(bases_);
    const std::size_t queen=spawn_queen ? free_slot(queens_) : std::size_t{0U};
    if (base==no_slot || queen==no_slot) { out.outcome=PlaceOutcome::pool_exhausted; return out; }

    BaseSlot& b=bases_[base];
    b.live=true;
    b.owner=owner;
    b.team=team_of(owner);
    b.x=object.x;
    b.z=object.z;
    out.base=base_handle(base);
    if (spawn_queen) {
        // spawn_effect_kind_15: +0xC = +0xD = +0x14 = owner, state 0, at the
        // object point; it is the base list head (record->link).
        QueenSlot& q=queens_[queen];
        q.live=true;
        q.rescued=false;
        q.moved=false;
        q.retail_state=retail_at_base;
        q.home_owner=owner;
        q.spawn_owner=owner;
        q.holder_owner=owner;
        q.slot=0U;
        q.carrier=no_entity;
        q.carrier_owner=no_owner;
        q.link_order=++link_counter_;
        q.x=object.x;
        q.z=object.z;
        q.expire_tick=0;
        out.queen=queen_handle(queen);
    }
    out.outcome=PlaceOutcome::placed;
    return out;
}

MarkerPlaceResult QueenWorld::place_marker(float x,float z,std::int16_t extent,std::uint8_t view_count) {
    MarkerPlaceResult out;
    if (ready_) { out.outcome=PlaceOutcome::placement_closed; return out; }
    if (view_count!=1U) { out.outcome=PlaceOutcome::split_screen; return out; }
    const std::size_t slot=free_slot(markers_);
    if (slot==no_slot) { out.outcome=PlaceOutcome::pool_exhausted; return out; }
    MarkerSlot& m=markers_[slot];
    m.live=true;
    m.x=x;
    m.z=z;
    m.extent=extent;
    out.marker=MarkerHandle{make_id(slot,m.generation)};
    out.outcome=PlaceOutcome::placed;
    return out;
}

MarkerPlaceResult QueenWorld::place_bank_entry(const MarkerBankEntry& entry,std::uint8_t view_count) {
    if (entry.kind!=bank_entry_kind_marker) {
        MarkerPlaceResult out;
        out.outcome=PlaceOutcome::not_marker_kind;
        return out;
    }
    return place_marker(entry.x,entry.z,marker_extent(entry.param),view_count);
}

void QueenWorld::finish_placement() noexcept {
    // recount_team_active_entities: every owner with pointCount 1 counts once
    // for its team, including kind 6's queenless owner-0 base.
    team_counts_.fill(0U);
    total_bases_=0U;
    for (const BaseSlot& base : bases_) {
        if (!base.live) continue;
        ++total_bases_;
        ++team_counts_[base.team];
    }
    ready_=true;
}

void QueenWorld::reset() noexcept {
    for (QueenSlot& q : queens_) {
        const std::uint16_t generation=next_generation(q.generation);
        q=QueenSlot{};
        q.generation=generation;
    }
    for (BaseSlot& b : bases_) {
        const std::uint16_t generation=next_generation(b.generation);
        b=BaseSlot{};
        b.generation=generation;
    }
    for (MarkerSlot& m : markers_) {
        const std::uint16_t generation=next_generation(m.generation);
        m=MarkerSlot{};
        m.generation=generation;
    }
    // event_sequence_ is deliberately NOT reset (like the slot generations):
    // sequence numbers stay monotonic across resets so events from before and
    // after a reset never compare equal. It is serialized. TEMPORARY POLICY.
    ready_=false;
    total_bases_=0U;
    team_counts_.fill(0U);
    link_counter_=0U;
    event_count_=0U;
}

std::size_t QueenWorld::carried_slots(EntityId unit,std::array<std::size_t,queen_capacity>& out) const noexcept {
    std::size_t count=0U;
    if (!unit.valid()) return 0U;
    for (std::size_t i=0U; i<queen_capacity; ++i) {
        const QueenSlot& q=queens_[i];
        if (q.live && q.retail_state==retail_carried && q.carrier==unit) out[count++]=i;
    }
    // Head first: the latest collected queen leads the +0x98 list.
    std::sort(out.begin(),out.begin()+static_cast<std::ptrdiff_t>(count),[this](std::size_t a,std::size_t b) {
        return queens_[a].link_order>queens_[b].link_order;
    });
    return count;
}

std::size_t QueenWorld::carried_by(EntityId unit,std::array<QueenHandle,queen_capacity>& out) const noexcept {
    std::array<std::size_t,queen_capacity> slots{};
    const std::size_t count=carried_slots(unit,slots);
    for (std::size_t i=0U; i<count; ++i) out[i]=queen_handle(slots[i]);
    return count;
}

void QueenWorld::push(QueenEvent event) noexcept {
    if (event_count_>=event_capacity) {   // callers reserve first
        ++dropped_events_;
        return;
    }
    event.sequence=++event_sequence_;
    events_[event_count_++]=event;
}

// Validates an attach of `extra`+1 queens at base_owner without mutating.
Status QueenWorld::check_attach(std::size_t queen,std::uint8_t base_owner,std::uint8_t extra) const noexcept {
    (void)queen;
    const std::size_t base=base_index_of(base_owner);
    if (base==no_slot) return Status::home_base_missing;
    const std::size_t held=std::size_t{queens_at(base_owner)}+std::size_t{extra};
    if (held>=base_slot_count) return Status::slot_overflow;
    const std::uint32_t team_total=std::uint32_t{team_counts_[bases_[base].team]}+std::uint32_t{extra};
    if (team_total>=0xFFU) return Status::counter_overflow;
    return Status::applied;
}

// attach_effect_to_owner_group (func_800A6E30).
void QueenWorld::attach(std::size_t queen,std::uint8_t base_owner,QueenEventKind kind,ReturnReason reason,
                        EntityId unit,std::int32_t tick) {
    QueenSlot& q=queens_[queen];
    const std::size_t base=base_index_of(base_owner);
    const BaseSlot& b=bases_[base];
    const QueenState from=state_of(q);
    const std::uint8_t slot=queens_at(base_owner);   // list length before the push
    const std::uint8_t previous_home=q.home_owner;
    q.retail_state=retail_at_base;
    q.moved=true;
    q.holder_owner=base_owner;   // +0x14 = ownerId
    q.slot=slot;
    q.carrier=no_entity;
    q.carrier_owner=no_owner;
    q.link_order=++link_counter_;
    q.x=b.x+slot_offsets[slot].dx;
    q.z=b.z+slot_offsets[slot].dz;
    ++team_counts_[b.team];   // increment_effect_owner_count(owner->team)
    // Home team read before the D_801266F0 rehome (source line order).
    const std::int32_t progress=team_of(previous_home)!=b.team ? store_progress : 0;
    if (config_.policies.store_rehomes_queen) q.home_owner=base_owner;
    QueenEvent event;
    event.kind=kind;
    event.reason=reason;
    event.from=from;
    event.queen=queen_handle(queen);
    event.base=base_handle(base);
    event.unit=unit;
    event.owner=base_owner;
    event.team=b.team;
    event.previous_home=previous_home;
    event.slot=slot;
    event.progress=progress;
    event.tick=tick;
    push(event);
}

Status QueenWorld::touch_queen(QueenHandle handle,const UnitContact& contact,std::int32_t tick) {
    if (!ready_) return Status::not_ready;
    const std::size_t index=queen_slot(handle);
    if (index==no_slot) return Status::stale_handle;
    if (!owner_ok(contact.owner)) return Status::invalid_owner;
    // func_800A739C: event 0 and contact kind 0 only.
    if (contact.event_code!=0U || contact.contact_kind!=0U) return Status::ignored;
    if (free_events()<2U) return Status::event_queue_full;
    QueenSlot& q=queens_[index];
    const std::uint8_t collector_team=team_of(contact.owner);
    bool collect=!contact.collector_blocked && q.retail_state!=retail_carried;
    std::uint8_t holder_team=no_owner;
    if (collect && q.retail_state==retail_at_base) {
        holder_team=team_of(q.holder_owner);
        if (holder_team==collector_team) collect=false;   // same team: no-op
        else if (team_counts_[holder_team]==0U) return Status::counter_underflow;
    }
    const bool rescue=config_.game_kind==rescue_game_kind && contact.owner==0U;
    if (!collect && !rescue) return Status::ignored;
    if (collect) {
        const QueenState from=state_of(q);
        const bool from_base=q.retail_state==retail_at_base;
        if (from_base) --team_counts_[holder_team];   // decrement_effect_owner_count
        q.retail_state=retail_carried;
        q.carrier=contact.unit;
        q.carrier_owner=contact.owner;
        q.link_order=++link_counter_;   // pushed at the head of the carried list
        QueenEvent event;
        event.kind=from_base ? QueenEventKind::taken_from_base : QueenEventKind::picked_up;
        event.from=from;
        event.queen=handle;
        event.unit=contact.unit;
        event.owner=contact.owner;
        event.team=collector_team;
        event.holder_team=holder_team;
        event.previous_home=q.home_owner;
        event.progress=team_of(q.home_owner)!=collector_team ? take_progress : 0;
        event.tick=tick;
        push(event);
    }
    if (rescue) {
        // Result 9 is set whether or not the collect succeeded.
        QueenEvent event;
        event.kind=QueenEventKind::rescue_contact;
        event.from=state_of(q);
        event.queen=handle;
        event.unit=contact.unit;
        event.owner=contact.owner;
        event.team=collector_team;
        event.previous_home=q.home_owner;
        event.tick=tick;
        q.rescued=true;
        push(event);
    }
    return Status::applied;
}

Status QueenWorld::touch_base(BaseHandle handle,const UnitContact& contact,std::int32_t tick) {
    if (!ready_) return Status::not_ready;
    const std::size_t index=base_slot(handle);
    if (index==no_slot) return Status::stale_handle;
    if (!owner_ok(contact.owner)) return Status::invalid_owner;
    // func_800A74AC: blocked (event) == 0, contact kind == 0, same team.
    if (contact.event_code!=0U || contact.contact_kind!=0U) return Status::ignored;
    const BaseSlot& b=bases_[index];
    if (team_of(contact.owner)!=b.team) return Status::ignored;
    std::array<std::size_t,queen_capacity> carried{};
    const std::size_t count=carried_slots(contact.unit,carried);
    if (count==0U) return Status::ignored;
    if (free_events()<count) return Status::event_queue_full;
    const Status ok=check_attach(carried[0],b.owner,static_cast<std::uint8_t>(count-1U));
    if (ok!=Status::applied) return ok;
    const std::uint8_t base_owner=b.owner;
    for (std::size_t i=0U; i<count; ++i) {
        attach(carried[i],base_owner,QueenEventKind::stored,ReturnReason::none,contact.unit,tick);
    }
    return Status::applied;
}

Status QueenWorld::touch_marker(MarkerHandle handle,const UnitContact& contact,std::int32_t tick) {
    if (!ready_) return Status::not_ready;
    const std::size_t index=marker_slot(handle);
    if (index==no_slot) return Status::stale_handle;
    if (!owner_ok(contact.owner)) return Status::invalid_owner;
    // func_800A7614: event == 0, contact kind == 0 and touching unit owner
    // (+0x4) == 0 -> result 9. No game-kind check (unlike func_800A739C).
    // The marker stays live after contact. Layout STRONG INFERENCE.
    if (contact.event_code!=0U || contact.contact_kind!=0U || contact.owner!=0U) return Status::ignored;
    // Retail rewrites owner +0x10=2 on every contact (idempotent). A contact
    // repeated before the queue is drained adds no second marker_reached.
    for (std::size_t i=0U; i<event_count_; ++i) {
        const QueenEvent& pending=events_[i];
        if (pending.kind==QueenEventKind::marker_reached && pending.marker==handle && pending.owner==contact.owner) return Status::applied;
    }
    if (free_events()<1U) return Status::event_queue_full;
    QueenEvent event;
    event.kind=QueenEventKind::marker_reached;
    event.marker=handle;
    event.unit=contact.unit;
    event.owner=contact.owner;
    event.team=team_of(contact.owner);
    event.tick=tick;
    push(event);
    return Status::applied;
}

Status QueenWorld::carrier_destroyed(EntityId unit,float x,float z,std::int32_t tick) {
    if (!ready_) return Status::not_ready;
    std::array<std::size_t,queen_capacity> carried{};
    const std::size_t count=carried_slots(unit,carried);
    if (count==0U) return Status::ignored;
    if (free_events()<count) return Status::event_queue_full;
    for (std::size_t i=0U; i<count; ++i) {
        // start_effect_slot_motion(queen, unit x, unit z, i++).
        QueenSlot& q=queens_[carried[i]];
        QueenEvent event;
        event.kind=QueenEventKind::dropped;
        event.from=state_of(q);
        event.queen=queen_handle(carried[i]);
        event.unit=unit;
        event.owner=q.carrier_owner;
        event.previous_home=q.home_owner;
        event.slot=static_cast<std::uint8_t>(i);
        event.tick=tick;
        q.retail_state=retail_dropped;
        q.carrier=no_entity;
        q.carrier_owner=no_owner;
        q.slot=static_cast<std::uint8_t>(i);
        q.x=x+slot_offsets[i].dx;
        q.z=z+slot_offsets[i].dz;
        q.expire_tick=tick+drop_return_ticks;
        push(event);
    }
    return Status::applied;
}

Status QueenWorld::carrier_invalidated(EntityId unit,std::int32_t tick) {
    if (!ready_) return Status::not_ready;
    std::array<std::size_t,queen_capacity> carried{};
    const std::size_t count=carried_slots(unit,carried);
    if (count==0U) return Status::ignored;
    if (free_events()<count) return Status::event_queue_full;
    // Validate every home first so a failure leaves nothing changed.
    std::array<std::uint8_t,owner_capacity> extra{};
    for (std::size_t i=0U; i<count; ++i) {
        const std::uint8_t home=queens_[carried[i]].home_owner;
        const Status ok=check_attach(carried[i],home,extra[home]);
        if (ok!=Status::applied) return ok;
        ++extra[home];
    }
    for (std::size_t i=0U; i<count; ++i) {
        attach(carried[i],queens_[carried[i]].home_owner,QueenEventKind::returned,
               ReturnReason::carrier_invalidated,unit,tick);
    }
    return Status::applied;
}

Status QueenWorld::step(std::int32_t tick) {
    if (!ready_) return Status::not_ready;
    std::array<std::size_t,queen_capacity> due{};
    std::size_t count=0U;
    for (std::size_t i=0U; i<queen_capacity; ++i) {
        const QueenSlot& q=queens_[i];
        // expire_effect_after_timer: strictly greater than the expire tick.
        if (q.live && q.retail_state==retail_dropped && tick>q.expire_tick) due[count++]=i;
    }
    if (count==0U) return Status::ignored;
    if (free_events()<count) return Status::event_queue_full;
    std::array<std::uint8_t,owner_capacity> extra{};
    for (std::size_t i=0U; i<count; ++i) {
        const std::uint8_t home=queens_[due[i]].home_owner;
        const Status ok=check_attach(due[i],home,extra[home]);
        if (ok!=Status::applied) return ok;
        ++extra[home];
    }
    for (std::size_t i=0U; i<count; ++i) {
        // dispatch_record_byte_0c: attach to the +0xC owner.
        attach(due[i],queens_[due[i]].home_owner,QueenEventKind::returned,ReturnReason::drop_expired,
               no_entity,tick);
    }
    return Status::applied;
}

bool QueenWorld::view(QueenHandle handle,QueenView& out) const noexcept {
    const std::size_t index=queen_slot(handle);
    if (index==no_slot) return false;
    const QueenSlot& q=queens_[index];
    out=QueenView{
        .state=state_of(q),
        .retail_state=q.retail_state,
        .home_owner=q.home_owner,
        .spawn_owner=q.spawn_owner,
        .holder_owner=q.holder_owner,
        .slot=q.slot,
        .carrier=q.carrier,
        .carrier_owner=q.carrier_owner,
        .x=q.x,
        .z=q.z,
        .expire_tick=q.expire_tick,
    };
    return true;
}

bool QueenWorld::view(BaseHandle handle,BaseView& out) const noexcept {
    const std::size_t index=base_slot(handle);
    if (index==no_slot) return false;
    const BaseSlot& b=bases_[index];
    out=BaseView{.owner=b.owner,.team=b.team,.x=b.x,.z=b.z,.queens_held=queens_at(b.owner)};
    return true;
}

bool QueenWorld::view(MarkerHandle handle,MarkerView& out) const noexcept {
    const std::size_t index=marker_slot(handle);
    if (index==no_slot) return false;
    const MarkerSlot& m=markers_[index];
    out=MarkerView{.x=m.x,.z=m.z,.extent=m.extent};
    return true;
}

std::uint8_t QueenWorld::matching_team() const noexcept {
    for (std::uint8_t team=0U; team<owner_capacity; ++team) {
        if (team_counts_[team]==total_bases_) return team;
    }
    return no_owner;
}

void QueenWorld::serialize(StateWriter& writer) const {
    writer.u8(serialize_version);
    writer.u32(config_.game_kind);
    writer.u8(config_.owner_count);
    for (const OwnerInfo& owner : config_.owners) { writer.boolean(owner.present); writer.u8(owner.team); }
    for (std::uint8_t rank : config_.slot_rank) writer.u8(rank);
    writer.boolean(config_.policies.store_rehomes_queen);
    writer.boolean(ready_);
    for (const QueenSlot& q : queens_) {
        writer.u16(q.generation);
        writer.boolean(q.live);
        writer.boolean(q.rescued);
        writer.boolean(q.moved);
        writer.u8(q.retail_state);
        writer.u8(q.home_owner);
        writer.u8(q.spawn_owner);
        writer.u8(q.holder_owner);
        writer.u8(q.slot);
        writer.id(q.carrier);
        writer.u8(q.carrier_owner);
        writer.u32(q.link_order);
        writer.f32(q.x);
        writer.f32(q.z);
        writer.s32(q.expire_tick);
    }
    for (const BaseSlot& b : bases_) {
        writer.u16(b.generation);
        writer.boolean(b.live);
        writer.u8(b.owner);
        writer.u8(b.team);
        writer.f32(b.x);
        writer.f32(b.z);
    }
    for (const MarkerSlot& m : markers_) {
        writer.u16(m.generation);
        writer.boolean(m.live);
        writer.f32(m.x);
        writer.f32(m.z);
        writer.s16(m.extent);
    }
    writer.u8(total_bases_);
    for (std::uint8_t count : team_counts_) writer.u8(count);
    writer.u32(link_counter_);
    writer.u32(event_sequence_);
    writer.u32(dropped_events_);
    writer.u16(static_cast<std::uint16_t>(event_count_));
    for (std::size_t i=0U; i<event_count_; ++i) {
        const QueenEvent& e=events_[i];
        writer.u32(e.sequence);
        writer.u8(static_cast<std::uint8_t>(e.kind));
        writer.u8(static_cast<std::uint8_t>(e.reason));
        writer.u8(static_cast<std::uint8_t>(e.from));
        writer.id(e.queen.id);
        writer.id(e.base.id);
        writer.id(e.marker.id);
        writer.id(e.unit);
        writer.u8(e.owner);
        writer.u8(e.team);
        writer.u8(e.holder_team);
        writer.u8(e.previous_home);
        writer.u8(e.slot);
        writer.s32(e.progress);
        writer.s32(e.tick);
    }
}

void seed_objective_bases(const QueenWorld& world,ObjectiveConfig& config) noexcept {
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        config.owners[i].bases=world.has_base(i) ? std::uint8_t{1U} : std::uint8_t{0U};
    }
}

ObjectiveCall objective_call(const QueenEvent& event) noexcept {
    switch (event.kind) {
    case QueenEventKind::taken_from_base:
        return ObjectiveCall{.entry=ObjectiveEntry::queen_taken,.first=event.holder_team,.second=event.team};
    case QueenEventKind::stored:
    case QueenEventKind::returned:
        return ObjectiveCall{.entry=ObjectiveEntry::queen_stored,.first=event.team,.second=0U};
    case QueenEventKind::rescue_contact:
        return ObjectiveCall{.entry=ObjectiveEntry::queen_contact,.first=event.owner,.second=0U};
    case QueenEventKind::marker_reached:
        return ObjectiveCall{.entry=ObjectiveEntry::marker_contact,.first=event.owner,.second=0U};
    case QueenEventKind::picked_up:
    case QueenEventKind::dropped:
        break;
    }
    return ObjectiveCall{};
}

ForwardSummary forward_events(QueenWorld& world,ObjectiveRuntime& objective) {
    ForwardSummary summary;
    for (std::size_t i=0U; i<world.pending_events(); ++i) {
        const ObjectiveCall call=objective_call(world.event(i));
        ObjectiveEventStatus status=ObjectiveEventStatus::ignored;
        switch (call.entry) {
        case ObjectiveEntry::none: ++summary.local_only; continue;
        case ObjectiveEntry::queen_taken: status=objective.on_queen_taken(call.first,call.second); break;
        case ObjectiveEntry::queen_stored: status=objective.on_queen_stored(call.first); break;
        case ObjectiveEntry::queen_contact: status=objective.on_queen_contact(call.first); break;
        case ObjectiveEntry::marker_contact: status=objective.on_marker_contact(call.first); break;
        }
        summary.statuses[summary.forwarded++]=status;
        if (status==ObjectiveEventStatus::applied) ++summary.applied;
        else if (call.entry==ObjectiveEntry::queen_taken || call.entry==ObjectiveEntry::queen_stored) ++summary.mismatched;
    }
    world.clear_events();
    return summary;
}

} // namespace battletanx::queen_world
