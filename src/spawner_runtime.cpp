#include "battletanx/spawner_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace battletanx::spawner_runtime {
namespace {

struct ShapeBounds { float min_a, max_a, min_b, max_b; };

// D_80072120..D_80072148 (ROM 0x2120..0x2148), in the argument order of
// build_area_box_by_shape: shape 0 tank, 1 bike, 2 goliath footprints.
constexpr std::array<ShapeBounds,3> shape_bounds{{
    {-35.0F,35.0F,-73.0F,81.0F},
    {-26.0F,26.0F,-38.0F,26.0F},
    {-60.0F,60.0F,-75.0F,81.0F}}};

std::optional<std::int32_t> truncate_s32(float value) noexcept {
    if (!std::isfinite(value)) return std::nullopt;
    if (value<=-2147483904.0F || value>=2147483648.0F) return std::nullopt;
    return static_cast<std::int32_t>(value);
}

std::int32_t wrap_add(std::int32_t a,std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));
}

std::int32_t wrap_sub(std::int32_t a,std::int32_t b) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b));
}

// integer_sqrt (func_80109094): floor square root (STRONG INFERENCE).
std::uint32_t integer_sqrt(std::uint32_t value) noexcept {
    std::uint32_t result=0U;
    std::uint32_t bit=1U<<30U;
    while (bit>value) bit>>=2U;
    while (bit!=0U) {
        if (value>=result+bit) {
            value-=result+bit;
            result=(result>>1U)+bit;
        } else {
            result>>=1U;
        }
        bit>>=2U;
    }
    return result;
}

// 64-bit magnitude: INT_MIN must not overflow (truncate_s32 accepts it).
std::int64_t abs_s64(std::int32_t value) noexcept {
    const std::int64_t wide=value;
    return wide<0 ? -wide : wide;
}

std::optional<std::size_t> free_member(const OwnerSpawnRecord& owner) noexcept {
    for (std::size_t slot=0U; slot<member_slots; ++slot) {
        if (!owner.unit_active[slot]) return slot;
    }
    return std::nullopt;
}

AreaVerdict check_area(float x,float z,std::uint16_t angle,std::uint32_t shape,float scale,
                       const AreaQuery& query,AreaRule rule,bool has_link,bool& box_ok) {
    const auto box=build_area_box(x,z,angle,shape,scale);
    box_ok=box.has_value();
    if (!box_ok) return AreaVerdict{false,{}};
    if (!query) return AreaVerdict{};
    return evaluate_area(query(*box),rule,has_link);
}

void append(std::vector<std::uint32_t>& out,const std::vector<std::uint32_t>& in) {
    out.insert(out.end(),in.begin(),in.end());
}

void write_order(StateWriter& writer,const SpawnOrder& order) {
    writer.u32(order.sequence);
    writer.u8(static_cast<std::uint8_t>(order.source));
    writer.u8(order.owner);
    writer.u8(order.member_slot);
    writer.u16(order.unit_id);
    writer.u8(static_cast<std::uint8_t>(order.kind));
    writer.f32(order.x);
    writer.f32(order.z);
    writer.u16(order.heading);
    writer.u8(order.parameter);
    writer.u16(order.flags);
    writer.s32(order.tick);
}

} // namespace

std::optional<UnitKind> unit_kind_for_mode(std::uint8_t mode) noexcept {
    switch (mode) {
    case 0U: return UnitKind::tank;
    case 1U: return UnitKind::bike;
    case 2U: case 3U: return UnitKind::goliath;
    default: return std::nullopt;
    }
}

std::optional<AreaBox> build_area_box(float x,float z,std::uint16_t angle,std::uint32_t shape,
                                      float scale) noexcept {
    if (shape>=shape_bounds.size()) return std::nullopt;
    const ShapeBounds& b=shape_bounds[shape];
    const auto ix=truncate_s32(x);
    const auto iz=truncate_s32(z);
    const auto min_a=truncate_s32(scale*b.min_a);
    const auto max_a=truncate_s32(scale*b.max_a);
    const auto min_b=truncate_s32(scale*b.min_b);
    const auto max_b=truncate_s32(scale*b.max_b);
    if (!ix || !iz || !min_a || !max_a || !min_b || !max_b) return std::nullopt;
    // Radius bound: keep a*a+b*b inside 32 bits (retail scale is 1.0).
    if (abs_s64(*min_a)>0x7FFF || abs_s64(*max_a)>0x7FFF ||
        abs_s64(*min_b)>0x7FFF || abs_s64(*max_b)>0x7FFF) return std::nullopt;
    AreaBox box;
    box.x=static_cast<std::int16_t>(*ix);
    box.z=static_cast<std::int16_t>(*iz);
    box.min_a=static_cast<std::int16_t>(*min_a);
    box.max_a=static_cast<std::int16_t>(*max_a);
    box.min_b=static_cast<std::int16_t>(*min_b);
    box.max_b=static_cast<std::int16_t>(*max_b);
    box.angle=angle;
    // Bounds are within +-0x7FFF here, so the 64-bit magnitudes fit in u32.
    const std::int64_t a=abs_s64(abs_s64(*min_a)>abs_s64(*max_a) ? *min_a : *max_a);
    const std::int64_t c=abs_s64(abs_s64(*min_b)>abs_s64(*max_b) ? *min_b : *max_b);
    const std::uint32_t sum=static_cast<std::uint32_t>(a)*static_cast<std::uint32_t>(a)+
                            static_cast<std::uint32_t>(c)*static_cast<std::uint32_t>(c);
    box.radius=static_cast<std::uint16_t>(integer_sqrt(sum));
    return box;
}

AreaVerdict evaluate_area(const AreaQueryResult& result,AreaRule rule,bool has_link) {
    AreaVerdict verdict;
    if (!result.found) return verdict;
    const std::size_t count=std::min(result.hits.size(),area_query_capacity);
    for (std::size_t i=0U; i<count; ++i) {
        const AreaHit& hit=result.hits[i];
        if (hit.reserved_handle) continue;
        if (rule==AreaRule::spawner && hit.kind==linked_effect_kind) {
            if (has_link && hit.is_linked_effect) continue;
            verdict.clear=false;
            return verdict;
        }
        if (hit.kind==wreck_kind) {
            verdict.wreck_notifications.push_back(hit.object_token);
            continue;
        }
        verdict.clear=false;
        if (rule==AreaRule::spawner) return verdict;
    }
    return verdict;
}

std::uint16_t order_flags(std::int32_t game_kind,std::uint8_t owner,bool option,bool force) noexcept {
    const std::uint16_t base=option ? order_flag_option : std::uint16_t{0U};
    std::uint16_t flags=(game_kind!=forced_flag_game_kind && !force)
        ? base : static_cast<std::uint16_t>(base|order_flag_force);
    if (campaign_kind(game_kind) && owner==0U && !option) {
        flags=static_cast<std::uint16_t>(flags|order_flag_campaign_player);
    }
    return flags;
}

bool SpawnOrderQueue::push(SpawnOrder order) {
    if (full()) return false;
    order.sequence=next_sequence_++;
    orders_.push_back(order);
    return true;
}

std::vector<SpawnOrder> SpawnOrderQueue::drain() {
    std::vector<SpawnOrder> out;
    out.swap(orders_);
    return out;
}

void SpawnOrderQueue::serialize(StateWriter& writer) const {
    writer.u32(next_sequence_);
    writer.u32(static_cast<std::uint32_t>(orders_.size()));
    for (const SpawnOrder& order : orders_) write_order(writer,order);
}

void OwnerSpawnRecord::serialize(StateWriter& writer) const {
    for (std::uint16_t id : unit_ids) writer.u16(id);
    for (bool active : unit_active) writer.boolean(active);
    writer.s32(budget);
    writer.boolean(gate_open);
}

void serialize_spawner_state(StateWriter& writer,const SpawnerState& state) {
    const SpawnerDefinition& d=state.definition;
    writer.u32(d.placement_index);
    writer.f32(d.x);
    writer.f32(d.z);
    writer.u16(d.heading);
    writer.u16(d.id);
    writer.u8(d.owner);
    writer.u8(d.mode);
    writer.u8(d.shape);
    writer.u8(d.count_field);
    writer.s16(d.interval_ticks);
    writer.u8(d.parameter);
    writer.u8(d.gate_bits);
    writer.boolean(d.linked);
    writer.u8(static_cast<std::uint8_t>(d.source));
    writer.s32(state.last_tick);
    writer.s32(state.linked_serial);
    writer.boolean(state.finished);
}

SpawnerTickResult tick_spawner(SpawnerState& spawner,const std::optional<LinkedEffectView>& linked,
                               std::int32_t game_kind,std::int32_t tick,OwnerSpawnRecords& owners,
                               RetailRandom& rng,const AreaQuery& query,SpawnOrderQueue& queue) {
    SpawnerTickResult result;
    const SpawnerDefinition& def=spawner.definition;
    if (spawner.finished) {
        result.status=SpawnerTickStatus::finished;
        return result;
    }
    if (def.linked) {
        if (!linked.has_value() || linked->serial!=spawner.linked_serial) {
            spawner.finished=true;
            result.status=SpawnerTickStatus::finished;
            return result;
        }
        if (linked->destroyed) {
            spawner.finished=true;
            result.destroyed_event=true;
            result.status=SpawnerTickStatus::finished_destroyed;
            return result;
        }
    }
    const std::size_t owner_index=def.owner&3U;
    OwnerSpawnRecord& owner=owners[owner_index];
    if ((def.parameter==gated_parameter || def.gate_bits==gated_bits_value) && !owner.gate_open) {
        result.status=SpawnerTickStatus::gated;
        return result;
    }
    if (!(wrap_add(spawner.last_tick,def.interval_ticks)<tick)) {
        result.status=SpawnerTickStatus::interval_pending;
        return result;
    }
    if ((rng.next_u32()&spawn_roll_mask)!=0U) {
        result.status=SpawnerTickStatus::roll_failed;
        return result;
    }
    if (owner.budget==0) {
        result.status=SpawnerTickStatus::no_budget;
        return result;
    }
    // unpack_spawner masks mode to 0..3; anything else mirrors the retail
    // switch fallthrough (shape 0).
    const UnitKind kind=unit_kind_for_mode(def.mode).value_or(UnitKind::tank);
    if (def.mode==3U) (void)rng.next_u32();   // both outcomes select shape 2
    bool box_ok=false;
    AreaVerdict verdict=check_area(def.x,def.z,static_cast<std::uint16_t>(def.heading+0x200U),
                                   static_cast<std::uint32_t>(kind),spawner_area_scale,query,
                                   AreaRule::spawner,def.linked,box_ok);
    result.wreck_notifications=std::move(verdict.wreck_notifications);
    if (!verdict.clear) {
        result.status=SpawnerTickStatus::area_blocked;
        return result;
    }
    const auto slot=free_member(owner);
    if (!slot) {
        result.status=SpawnerTickStatus::no_free_member;
        return result;
    }
    if (queue.full()) {
        result.status=SpawnerTickStatus::order_queue_full;
        return result;
    }
    SpawnOrder order;
    order.source=OrderSource::spawner;
    order.owner=static_cast<std::uint8_t>(owner_index);
    order.member_slot=static_cast<std::uint8_t>(*slot);
    order.unit_id=owner.unit_ids[*slot];
    order.kind=kind;
    order.x=def.x;
    order.z=def.z;
    order.heading=def.heading;
    order.parameter=def.parameter;
    order.flags=order_flags(game_kind,order.owner,false,true);
    order.tick=tick;
    if (!queue.push(order)) {
        result.status=SpawnerTickStatus::order_queue_full;
        return result;
    }
    // spawn_unit runs synchronously in retail; mark the member live so later
    // spawners in the same tick see it.
    owner.unit_active[*slot]=true;
    spawner.last_tick=tick;
    if (owner.budget>0) --owner.budget;
    result.status=SpawnerTickStatus::spawned;
    return result;
}

std::optional<SpawnerCreateError> SpawnerSet::create(const SpawnerDefinition& definition,
        std::int32_t game_kind,std::optional<std::int32_t> linked_serial) {
    if (definition.linked) {
        if (!campaign_kind(game_kind)) return SpawnerCreateError::linked_outside_campaign;
    } else if (linked_serial) {
        return SpawnerCreateError::unexpected_link;
    }
    // Finished spawners free their pool slot; reuse the first inactive one
    // before growing so capacity counts live spawners only.
    std::size_t reuse=slots_.size();
    for (std::size_t i=0U; i<slots_.size(); ++i) {
        if (!slots_[i].active) { reuse=i; break; }
    }
    if (reuse==slots_.size() && slots_.size()>=capacity_) return SpawnerCreateError::pool_full;
    SpawnerSlot slot;
    // TEMPORARY POLICY "linked effect allocation failed": retail creates the
    // spawner with a null link (load_level_data.c:470-476); it runs unlinked.
    const bool link_failed=definition.linked && !linked_serial;
    SpawnerDefinition created=definition;
    if (link_failed) created.linked=false;
    slot.state=make_spawner_state(created,linked_serial.value_or(0));
    slot.active=true;
    slot.link_failed=link_failed;
    if (reuse<slots_.size()) slots_[reuse]=slot;
    else slots_.push_back(slot);
    std::uint8_t& count=spawn_counts_[definition.owner&3U];
    count=static_cast<std::uint8_t>(count+1U);
    return std::nullopt;
}

std::vector<SpawnerTickResult> SpawnerSet::tick(std::int32_t game_kind,std::int32_t now,
        OwnerSpawnRecords& owners,RetailRandom& rng,const AreaQuery& query,
        const std::function<std::optional<LinkedEffectView>(std::size_t)>& linked_lookup,
        SpawnOrderQueue& queue) {
    std::vector<SpawnerTickResult> results;
    results.reserve(slots_.size());
    for (std::size_t i=0U; i<slots_.size(); ++i) {
        SpawnerSlot& slot=slots_[i];
        if (!slot.active) {
            results.push_back(SpawnerTickResult{SpawnerTickStatus::finished,{},false});
            continue;
        }
        std::optional<LinkedEffectView> linked;
        if (slot.state.definition.linked && linked_lookup) linked=linked_lookup(i);
        SpawnerTickResult result=tick_spawner(slot.state,linked,game_kind,now,owners,rng,query,queue);
        // Retail *out=2 frees the effect record (slot stays reserved here so
        // indices remain stable).
        if (slot.state.finished) slot.active=false;
        results.push_back(std::move(result));
    }
    return results;
}

void SpawnerSet::serialize(StateWriter& writer) const {
    writer.u32(static_cast<std::uint32_t>(slots_.size()));
    for (const SpawnerSlot& slot : slots_) {
        writer.boolean(slot.active);
        writer.boolean(slot.link_failed);
        serialize_spawner_state(writer,slot.state);
    }
    for (std::uint8_t count : spawn_counts_) writer.u8(count);
}

std::vector<MemberMarker> build_member_markers(std::span<const SpawnRespawnPoint> points) {
    std::vector<MemberMarker> markers;
    markers.reserve(points.size());
    for (const SpawnRespawnPoint& point : points) markers.push_back(MemberMarker{.point=point,.last_used=0});
    return markers;
}

std::optional<UnitKind> next_lineup_kind(LineupCycle& lineup,LineupError* error) noexcept {
    auto fail=[error](LineupError e) -> std::optional<UnitKind> {
        if (error!=nullptr) *error=e;
        return std::nullopt;
    };
    if (lineup.count==0U) return fail(LineupError::empty);
    if (lineup.count>member_slots || lineup.next_index>=lineup.count) {
        return fail(LineupError::index_out_of_range);
    }
    if (lineup.count==1U && lineup.kinds[0]==2U) return fail(LineupError::only_goliath);
    std::uint8_t kind=0U;
    // At most two passes: only the last entry can loop, onto entry 0.
    for (int guard=0; guard<=static_cast<int>(member_slots); ++guard) {
        const std::uint8_t index=lineup.next_index;
        kind=lineup.kinds[index];
        lineup.next_index=static_cast<std::uint8_t>((index+1U)%lineup.count);
        if (!(kind==2U && lineup.next_index==0U)) break;
    }
    if (kind>2U) return fail(LineupError::invalid_kind);
    return static_cast<UnitKind>(kind);
}

void SquadSpawnRecord::serialize(StateWriter& writer) const {
    writer.u8(owner);
    for (std::uint8_t kind : lineup.kinds) writer.u8(kind);
    writer.u8(lineup.count);
    writer.u8(lineup.next_index);
    writer.s16(reserve);
    writer.s16(member_respawns[0]);
    writer.s16(member_respawns[1]);
    writer.boolean(human_controlled);
    writer.boolean(respawn_pending);
}

void MemberMarker::serialize(StateWriter& writer) const {
    writer.f32(point.x);
    writer.f32(point.z);
    writer.u16(point.heading);
    writer.u8(point.parameter);
    writer.u32(point.placement_index);
    writer.s32(last_used);
}

void serialize_member_markers(StateWriter& writer,std::span<const MemberMarker> markers) {
    writer.u32(static_cast<std::uint32_t>(markers.size()));
    for (const MemberMarker& marker : markers) marker.serialize(writer);
}

RespawnResult respawn_squad_unit(SquadSpawnRecord& squad,OwnerSpawnRecord& owner,
                                 const RespawnContext& context,RetailRandom& rng,
                                 const AreaQuery& query,SpawnOrderQueue& queue) {
    RespawnResult result;
    LineupError lineup_error{};
    const auto kind=next_lineup_kind(squad.lineup,&lineup_error);
    if (!kind) {
        result.status=RespawnStatus::lineup_error;
        result.lineup_error=lineup_error;
        return result;
    }
    const std::uint32_t shape=static_cast<std::uint32_t>(*kind);
    float x=0.0F;
    float z=0.0F;
    std::uint16_t heading=0U;
    std::uint8_t parameter=0U;
    if (campaign_kind(context.game_kind)) {
        const std::size_t count=std::min(context.markers.size(),member_slots);
        if (count==0U) {
            result.status=RespawnStatus::no_markers;
            return result;
        }
        for (std::size_t i=0U; i<count; ++i) {
            const SpawnRespawnPoint& point=context.markers[i].point;
            x=point.x;
            z=point.z;
            heading=point.heading;
            parameter=point.parameter;
            result.point_index=i;
            bool box_ok=false;
            AreaVerdict verdict=check_area(x,z,heading,shape,respawn_area_scale,query,
                                           AreaRule::respawn,false,box_ok);
            if (!box_ok) {
                result.status=RespawnStatus::invalid_point;
                return result;
            }
            append(result.wreck_notifications,verdict.wreck_notifications);
            if (verdict.clear) break;
            // Not clear: the retail loop ends with the last marker selected.
        }
    } else {
        if (!context.team_row) {
            result.status=RespawnStatus::no_team_row;
            return result;
        }
        const std::uint32_t offset=rng.next_u32()%5U;
        bool found=false;
        for (std::uint32_t i=0U; i<5U; ++i) {
            const std::size_t column=(i+offset)%5U;
            const TeamSpawnPoint& point=(*context.team_row)[column];
            bool box_ok=false;
            AreaVerdict verdict=check_area(point.x,point.z,point.heading,shape,respawn_area_scale,
                                           query,AreaRule::respawn,false,box_ok);
            append(result.wreck_notifications,verdict.wreck_notifications);
            if (verdict.clear) {
                x=point.x;
                z=point.z;
                heading=point.heading;
                result.point_index=column;
                found=true;
                break;
            }
        }
        if (!found) {
            result.status=RespawnStatus::no_clear_point;
            return result;
        }
    }
    const auto slot=free_member(owner);
    if (!slot) {
        result.status=RespawnStatus::no_free_member;
        return result;
    }
    if (queue.full()) {
        result.status=RespawnStatus::order_queue_full;
        return result;
    }
    SpawnOrder order;
    order.source=OrderSource::squad_unit_respawn;
    order.owner=squad.owner;
    order.member_slot=static_cast<std::uint8_t>(*slot);
    order.unit_id=owner.unit_ids[*slot];
    order.kind=*kind;
    order.x=x;
    order.z=z;
    order.heading=heading;
    order.parameter=parameter;
    order.flags=order_flags(context.game_kind,squad.owner,false,false);
    order.tick=context.tick;
    if (!queue.push(order)) {
        result.status=RespawnStatus::order_queue_full;
        return result;
    }
    owner.unit_active[*slot]=true;
    if (squad.reserve>0 && (!context.reserve_lock_player || squad.owner!=0U)) --squad.reserve;
    if (squad.human_controlled) squad.respawn_pending=true;
    result.status=RespawnStatus::spawned;
    return result;
}

RespawnResult respawn_squad_member(SquadSpawnRecord& squad,OwnerSpawnRecord& owner,
                                   const RespawnContext& context,const AreaQuery& query,
                                   SpawnOrderQueue& queue) {
    RespawnResult result;
    const std::span<MemberMarker> markers=context.markers;
    const std::int32_t game_kind=context.game_kind;
    const std::int32_t tick=context.tick;
    const std::size_t shape=squad.member_respawns[0]==0 ? 1U : 0U;
    const std::size_t count=std::min(markers.size(),member_slots);
    std::optional<std::size_t> chosen;
    for (std::size_t i=1U; i<count; ++i) {
        MemberMarker& marker=markers[i];
        if (marker.last_used>wrap_sub(tick,marker_reuse_ticks)) continue;
        bool box_ok=false;
        AreaVerdict verdict=check_area(marker.point.x,marker.point.z,marker.point.heading,
                                       static_cast<std::uint32_t>(shape),respawn_area_scale,query,
                                       AreaRule::respawn,false,box_ok);
        append(result.wreck_notifications,verdict.wreck_notifications);
        if (verdict.clear) {
            chosen=i;
            break;
        }
    }
    if (!chosen) {
        result.status=RespawnStatus::no_clear_point;
        return result;
    }
    result.point_index=chosen;
    const auto slot=free_member(owner);
    if (!slot) {
        result.status=RespawnStatus::no_free_member;
        return result;
    }
    if (queue.full()) {
        result.status=RespawnStatus::order_queue_full;
        return result;
    }
    MemberMarker& marker=markers[*chosen];
    SpawnOrder order;
    order.source=OrderSource::squad_member_respawn;
    order.owner=squad.owner;
    order.member_slot=static_cast<std::uint8_t>(*slot);
    order.unit_id=owner.unit_ids[*slot];
    order.kind=static_cast<UnitKind>(shape);
    order.x=marker.point.x;
    order.z=marker.point.z;
    order.heading=marker.point.heading;
    order.parameter=marker.point.parameter;
    order.flags=order_flags(game_kind,squad.owner,true,true);
    order.tick=tick;
    if (!queue.push(order)) {
        result.status=RespawnStatus::order_queue_full;
        return result;
    }
    owner.unit_active[*slot]=true;
    marker.last_used=tick;
    if (squad.member_respawns[shape]>0) --squad.member_respawns[shape];
    result.status=RespawnStatus::spawned;
    return result;
}

void SquadRespawnClock::serialize(StateWriter& writer) const {
    writer.boolean(active);
    writer.u8(alive);
    writer.u8(unit_cap);
    writer.boolean(unlimited);
    writer.s32(due_tick);
    writer.s32(delay);
    writer.f32(delay_scale);
}

bool member_respawn_due(const SquadRespawnClock& clock,const SquadSpawnRecord& squad,
                        std::int32_t game_kind) noexcept {
    if (!clock.active || !campaign_kind(game_kind)) return false;
    if (squad.member_respawns[0]==0 && squad.member_respawns[1]==0) return false;
    const int pending=squad.respawn_pending ? 1 : 0;
    return static_cast<int>(clock.alive)-pending<static_cast<int>(clock.unit_cap)-1;
}

bool unit_respawn_due(SquadRespawnClock& clock,const SquadSpawnRecord& squad,
                      std::int32_t game_kind,std::int32_t tick) noexcept {
    if (!clock.active) return false;
    bool can=(squad.reserve!=0 || clock.unlimited) && clock.alive<clock.unit_cap;
    if (campaign_kind(game_kind) && squad.human_controlled && squad.respawn_pending) can=false;
    if (clock.due_tick!=0) return clock.due_tick<tick && can;
    if (!can) return false;
    clock.due_tick=wrap_add(tick,clock.delay);
    if (game_kind==0 && squad.reserve<=0) {
        const float ratio=clock.delay_scale/100.0F;
        if (const auto step=truncate_s32(ratio*30.0F)) clock.delay=wrap_add(clock.delay,*step);
        const float limit=ratio*360.0F;
        if (limit<static_cast<float>(clock.delay)) {
            if (const auto clamped=truncate_s32(limit)) clock.delay=*clamped;
        }
    }
    return false;
}

void finish_unit_respawn(SquadRespawnClock& clock,const RespawnResult& result) noexcept {
    if (result.status==RespawnStatus::spawned) clock.due_tick=0;
}

} // namespace battletanx::spawner_runtime
