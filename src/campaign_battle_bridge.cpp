#include "battletanx/campaign_battle_bridge.hpp"

#include "battletanx/runtime_constants.hpp"
#include "battletanx/unit_kinds.hpp"
#include "battletanx/unit_runtime.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <utility>

namespace battletanx::campaign_bridge {
namespace {

using queen_world::QueenEvent;
using queen_world::QueenEventKind;

bool inside(float cx,float cz,float half,float x,float z) noexcept {
    const float dx=x-cx;
    const float dz=z-cz;
    return dx>=-half && dx<=half && dz>=-half && dz<=half;
}

// Deploy tags carry the owner index (unit +0x04, fire_unit_weapon.c:450).
std::uint8_t owner_of_tag(std::int16_t tag) noexcept {
    if (tag<0 || static_cast<std::size_t>(tag)>=owner_capacity) return no_attacker;
    return static_cast<std::uint8_t>(tag);
}

retail::Footprint footprint_of(const UnitRecord& unit) noexcept {
    const retail::Footprint& f=unit.footprint;
    if (f.min_x!=0 || f.max_x!=0 || f.min_z!=0 || f.max_z!=0) return f;
    const auto kind=static_cast<std::size_t>(unit.kind);
    return kind<retail::unit_footprint.size() ? retail::unit_footprint[kind] : f;
}

void bump(std::uint32_t& counter) noexcept { ++counter; }

} // namespace

Bridge::Bridge(BridgeConfig config,const AngleTable& angles)
    : config_(std::move(config)),angles_(angles) {
    if (config_.queen) {
        queen_world::CreateResult created=queen_world::QueenWorld::create(*config_.queen);
        if (created.world) queen_=std::move(created.world);
        else error_=CreateError::queen_config;
    }
    for (std::size_t i=0U; i<spawner_runtime::owner_count; ++i) {
        spawner_owners_[i].budget=config_.spawner_owners[i].budget;
        spawner_owners_[i].gate_open=config_.spawner_owners[i].gate_open;
    }
}

void TurretBullet::serialize(StateWriter& writer) const {
    writer.f32(x); writer.f32(y); writer.f32(z);
    writer.f32(velocity_x); writer.f32(velocity_z);
    writer.f32(speed); writer.f32(travelled); writer.f32(range);
    writer.u16(heading);
    writer.s16(life);
    writer.u8(bullet_kind);
    writer.u8(trail);
    writer.u8(damage);
    writer.u8(attacker);
    writer.id(turret);
}

// Rotated footprint bounds vs the box (cx, cz) +- half. Rotation sense as in
// pickup_overlaps (heading from +z toward +x).
bool Bridge::overlaps(const UnitRecord& unit,float cx,float cz,float half) const noexcept {
    const retail::Footprint f=footprint_of(unit);
    const std::uint16_t angle=degrees_to_retail_angle(unit.heading_degrees);
    const float c=angles_.cosine(angle);
    const float s=angles_.sine(angle);
    const std::array<float,2> lx{static_cast<float>(f.min_x),static_cast<float>(f.max_x)};
    const std::array<float,2> lz{static_cast<float>(f.min_z),static_cast<float>(f.max_z)};
    float min_x=unit.x,max_x=unit.x,min_z=unit.z,max_z=unit.z;
    bool first=true;
    for (const float px : lx) {
        for (const float pz : lz) {
            const float wx=unit.x+px*c+pz*s;
            const float wz=unit.z-px*s+pz*c;
            if (first) { min_x=max_x=wx; min_z=max_z=wz; first=false; continue; }
            min_x=std::min(min_x,wx); max_x=std::max(max_x,wx);
            min_z=std::min(min_z,wz); max_z=std::max(max_z,wz);
        }
    }
    return min_x<=cx+half && max_x>=cx-half && min_z<=cz+half && max_z>=cz-half;
}

void Bridge::mark(TickReport& report,Phase phase) noexcept {
    if (report.phases_run<phase_count) report.order[report.phases_run++]=static_cast<std::uint8_t>(phase);
}

// ---- Level setup -------------------------------------------------------------

queen_world::PlaceResult Bridge::place_queen_object(const queen_world::LevelObject& object) {
    if (!queen_) return queen_world::PlaceResult{.outcome=queen_world::PlaceOutcome::kind_has_no_bases,.base={},.queen={},.owner=no_owner};
    const queen_world::PlaceResult result=queen_->place_level_object(object);
    if (result.outcome==queen_world::PlaceOutcome::placed && result.queen.valid())
        queen_handles_.push_back(result.queen);
    return result;
}

queen_world::MarkerPlaceResult Bridge::place_marker(float x,float z,std::int16_t extent,std::uint8_t view_count) {
    if (!queen_) return queen_world::MarkerPlaceResult{.outcome=queen_world::PlaceOutcome::kind_has_no_bases,.marker={}};
    const queen_world::MarkerPlaceResult result=queen_->place_marker(x,z,extent,view_count);
    if (result.outcome==queen_world::PlaceOutcome::placed) marker_handles_.push_back(result.marker);
    return result;
}

void Bridge::finish_queen_placement() noexcept {
    if (queen_) queen_->finish_placement();
}

std::optional<EntityId> Bridge::spawn_turret(const placed::TurretSpawn& spawn,BattleSession& session) {
    std::array<TurretTarget,unit_capacity> targets{};
    return placed_.spawn_turret(spawn,placed_context(session,targets));
}

std::optional<EntityId> Bridge::spawn_placed(const PlacedObjectRecord& record,const placed::Bounds& bounds) {
    return placed_.spawn_placed(record,bounds);
}

std::optional<EntityId> Bridge::spawn_breakable(const placed::BreakableRecord& record,const placed::Bounds& bounds) {
    return placed_.spawn_breakable(record,bounds);
}

std::optional<spawner_runtime::SpawnerCreateError> Bridge::add_spawner(const SpawnerDefinition& definition,
                                                                       std::optional<std::int32_t> linked_serial) {
    return spawners_.create(definition,config_.game_kind,linked_serial);
}

// ---- Queen world -------------------------------------------------------------

queen_world::MarkerPlaceResult Bridge::place_bank_entry(const queen_world::MarkerBankEntry& entry,
                                                        std::uint8_t view_count) {
    if (!queen_) return queen_world::MarkerPlaceResult{.outcome=queen_world::PlaceOutcome::kind_has_no_bases,.marker={}};
    const queen_world::MarkerPlaceResult result=queen_->place_bank_entry(entry,view_count);
    if (result.outcome==queen_world::PlaceOutcome::placed) marker_handles_.push_back(result.marker);
    return result;
}

std::size_t Bridge::place_markers(std::span<const queen_world::MarkerBankEntry> entries,std::uint8_t view_count) {
    std::size_t placed=0U;
    for (const queen_world::MarkerBankEntry& entry : entries)
        if (place_bank_entry(entry,view_count).outcome==queen_world::PlaceOutcome::placed) ++placed;
    return placed;
}

void Bridge::run_queen_contacts(BattleSession& session,TickReport& report) {
    mark(report,Phase::queen_contacts);
    if (!queen_ || !queen_->ready()) return;
    const std::int32_t tick=session.tick();
    // Units in slot order; for each unit queens (placement order), then the
    // owner bases, then markers. TEMPORARY POLICY: retail reaches the handlers
    // from the per-unit collision walk whose object order is the effect list.
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=session.units().slot_id(slot);
        const UnitRecord* unit=session.units().get(id);
        if (unit==nullptr || !unit->alive) continue;
        const queen_world::UnitContact contact{.unit=id,.owner=unit->owner,.event_code=0U,.contact_kind=0U,
                                               .collector_blocked=false};
        for (const queen_world::QueenHandle queen : queen_handles_) {
            queen_world::QueenView view{};
            if (!queen_->view(queen,view)) continue;
            if (view.state==queen_world::QueenState::carried) continue;
            if (overlaps(*unit,view.x,view.z,queen_world::queen_half_extent))
                static_cast<void>(queen_->touch_queen(queen,contact,tick));
        }
        for (std::size_t owner=0U; owner<owner_capacity; ++owner) {
            const queen_world::BaseHandle base=queen_->base_of(owner);
            queen_world::BaseView view{};
            if (!base.valid() || !queen_->view(base,view)) continue;
            if (overlaps(*unit,view.x,view.z,queen_world::base_half_extent))
                static_cast<void>(queen_->touch_base(base,contact,tick));
        }
        for (const queen_world::MarkerHandle marker : marker_handles_) {
            queen_world::MarkerView view{};
            if (!queen_->view(marker,view)) continue;
            if (overlaps(*unit,view.x,view.z,static_cast<float>(view.extent)))
                static_cast<void>(queen_->touch_marker(marker,contact,tick));
        }
    }
    route_queen_events(session,report);
    mark(report,Phase::queen_step);
    static_cast<void>(queen_->step(tick));
    route_queen_events(session,report);
}

void Bridge::route_queen_events(BattleSession& session,TickReport& report) {
    if (!queen_) return;
    const std::size_t count=queen_->pending_events();
    for (std::size_t i=0U; i<count; ++i) {
        const QueenEvent& event=queen_->event(i);
        std::optional<ObjectiveEventStatus> status;
        switch (event.kind) {
        case QueenEventKind::taken_from_base: status=session.queen_taken(event.holder_team,event.team); break;
        case QueenEventKind::stored:
        case QueenEventKind::returned: status=session.queen_stored(event.team); break;
        case QueenEventKind::rescue_contact: status=session.queen_contact(event.owner); break;
        case QueenEventKind::marker_reached: status=session.marker_contact(event.owner); break;
        case QueenEventKind::picked_up:
        case QueenEventKind::dropped: break;
        }
        if (status) {
            bump(report.queen_events_routed);
            if (*status!=ObjectiveEventStatus::applied) bump(report.objective_calls_rejected);
        } else {
            bump(report.queen_events_local);
        }
        routed_.push_back(event);
    }
    queen_->clear_events();
}

ObjectiveEventStatus Bridge::self_destruct(BattleSession& session,std::uint8_t owner) {
    return session.self_destruct(owner);
}

// ---- Placed objects ----------------------------------------------------------

placed::UpdateContext Bridge::placed_context(BattleSession& session,
                                             std::array<TurretTarget,unit_capacity>& targets) const {
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const UnitRecord* unit=session.units().get(session.units().slot_id(slot));
        // Index in the span is the retail unit id (unit_kinds.hpp TurretTarget).
        targets[slot]=TurretTarget{.unit=unit,.targetable=unit!=nullptr && unit->alive,.ai_state_two=false};
    }
    placed::UpdateContext context;
    context.angles=&angles_;
    context.targets=std::span<const TurretTarget>(targets.data(),targets.size());
    context.owner_configs=std::span<const TurretOwnerConfig>(config_.turret_owners.data(),config_.turret_owners.size());
    context.tick=session.tick();
    context.frame_delta=1.0F;
    context.search_range=turret_search_range;
    context.rng=&session.rng();
    return context;
}

std::size_t Bridge::splash_units(BattleSession& session,float x,float z,float radius,std::int32_t amount,
                                 std::uint8_t attacker,TickReport& report) {
    // TEMPORARY POLICY: square radius test on unit centres, slot order, indirect
    // damage. Retail message 7 goes through notify_objects_near_point.
    std::size_t hits=0U;
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=session.units().slot_id(slot);
        const UnitRecord* unit=session.units().get(id);
        if (unit==nullptr || !unit->alive || !inside(x,z,radius,unit->x,unit->z)) continue;
        if (session.apply_damage(id,amount,attacker,false)==DamageResult::ignored) continue;
        ++hits;
        bump(report.splash_hits);
    }
    return hits;
}

void Bridge::run_placed(BattleSession& session,std::uint16_t clock,TickReport& report) {
    mark(report,Phase::placed_contacts);
    // The session owner table is read-only; turret score awards land in this
    // copy and are discarded (request R5).
    std::array<OwnerRecord,owner_capacity> owners=session.owners();
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=session.units().slot_id(slot);
        const UnitRecord* unit=session.units().get(id);
        if (unit==nullptr || !unit->alive) continue;
        if (contact_shadow_[slot].unit!=id) contact_shadow_[slot]=ContactShadow{.unit=id,.field_3d0=0.0F,.field_3d4=0.0F};
        const placed::Hit hit{.source_kind=0U,.damage=0,.source_owner=unit->owner,
                              .heading=degrees_to_retail_angle(unit->heading_degrees),.shielded=false,.flame_forced=false};
        const std::size_t first=placed_.events().size();
        for (const EntityId object : placed_.at_point(unit->x,unit->z)) {
            if (placed_.hit(object,hit,owners,session.tick(),config_.game_mode)==placed::MessageResult::handled)
                bump(report.placed_hits);
        }
        apply_contact_replies(*unit,slot,first,report);
    }
    run_turret_bullets(session,report);
    mark(report,Phase::placed_update);
    std::array<TurretTarget,unit_capacity> targets{};
    placed_.update(placed_context(session,targets));
    for (const placed::Event& event : placed_.events()) {
        switch (event.kind) {
        case placed::EventKind::shot_fired:
            fire_turret_shot(session,event,report);
            break;
        case placed::EventKind::splash:
            static_cast<void>(splash_units(session,event.x,event.z,battletanx::placed_splash_radius,
                                           static_cast<std::int32_t>(battletanx::placed_splash_value),
                                           event.owner,report));
            break;
        case placed::EventKind::explosion:
            if (event.value>=0 && event.value<=0xFF) {
                const effects::SpawnResult spawned=effects_.spawn_script(static_cast<std::uint8_t>(event.value),
                    event.x,0.0F,event.z,clock);
                if (spawned.status==effects::SpawnStatus::spawned) bump(report.effect_spawns);
            }
            break;
        default: break;
        }
    }
    report.placed_events_overflowed=placed_.events_overflowed();
    placed_.clear_events();
}

// update_tank_drive.c:618-627 replies for the unit whose contacts produced
// events [first, end). The unit fields +0x3D0/+0x3D4 have no UnitRecord
// member (request R7), so the bridge keeps per-slot shadows that the session
// drive does not read (TEMPORARY POLICY).
void Bridge::apply_contact_replies(const UnitRecord& unit,std::size_t slot,std::size_t first,TickReport& report) {
    if (slot>=unit_capacity) return;
    const std::vector<placed::Event>& events=placed_.events();
    for (std::size_t i=first; i<events.size(); ++i) {
        const placed::Event& event=events[i];
        ContactShadow& shadow=contact_shadow_[slot];
        if (event.kind==placed::EventKind::unit_shaken) {
            shadow.field_3d0-=contact_crush_amount;
            shadow.field_3d4+=static_cast<float>(event.value);
            bump(report.contact_replies);
        } else if (event.kind==placed::EventKind::contact_reply) {
            const auto reply=static_cast<placed::ContactReply>(static_cast<std::uint8_t>(event.value&0xFF));
            if (reply==placed::ContactReply::crush) {
                shadow.field_3d0-=contact_crush_amount;
                bump(report.contact_replies);
            } else if (reply==placed::ContactReply::scrape) {
                const float speed_sq=unit.velocity_x*unit.velocity_x+unit.velocity_z*unit.velocity_z;
                if (speed_sq>contact_moving_speed*contact_moving_speed) {
                    shadow.field_3d0-=contact_scrape_amount;
                    bump(report.contact_replies);
                }
            }
        }
    }
}

// Bullet (type 0..2) or beam (type 3) from a turret event.
void Bridge::fire_turret_shot(BattleSession& session,const placed::Event& event,TickReport& report) {
    if (!event.shot) return;
    const TurretShot& shot=*event.shot;
    // update_turret.c: muzzle y = D_80125FD0.y * D_80126038[type] (visual).
    float y=turret_muzzle_y;
    if (const placed::Entry* entry=placed_.get(event.entity)) {
        if (entry->kind==placed::Kind::turret && entry->turret.type<turret_scale.size())
            y=turret_muzzle_y*turret_scale[entry->turret.type];
    }
    // u8 damage argument (owner +0x338 is s16; the call truncates).
    const auto damage=static_cast<std::uint8_t>(static_cast<std::uint16_t>(shot.effect_kind)&0xFFU);
    if (shot.projectile==turret_beam_projectile) {
        // spawn_beam_effect: instant. TEMPORARY POLICY: first unit hit along
        // 4000 units, clipped by the static segment; ricochet/forks unported.
        bump(report.turret_shots_fired);
        const float ex=shot.x+angles_.sine(shot.heading)*turret_beam_length;
        const float ez=shot.z+angles_.cosine(shot.heading)*turret_beam_length;
        const std::optional<UnitSegmentHit> unit_hit=find_unit_segment_hit(session.units(),angles_,
            shot.x,shot.z,ex,ez,no_entity);
        if (!unit_hit) return;
        if (config_.world.segment) {
            const std::optional<SegmentHit> wall=config_.world.segment(shot.x,shot.z,ex,ez);
            if (wall && wall->distance<unit_hit->distance) return;
        }
        if (session.apply_damage(unit_hit->unit,damage,event.owner,false)!=DamageResult::ignored)
            bump(report.turret_shots_applied);
        return;
    }
    if (bullets_.size()>=turret_bullet_capacity) { bump(report.turret_shots_dropped); return; }
    TurretBullet bullet;
    bullet.x=shot.x;
    bullet.y=y;
    bullet.z=shot.z;
    bullet.velocity_x=shot.velocity_x;
    bullet.velocity_z=shot.velocity_z;
    bullet.speed=static_cast<float>(shot.speed);
    bullet.heading=shot.heading;
    bullet.bullet_kind=static_cast<std::uint8_t>(shot.projectile&0xFF);
    bullet.trail=shot.projectile==0 ? turret_bullet_trail : std::uint8_t{0U};
    bullet.damage=damage;
    bullet.attacker=event.owner;
    bullet.turret=event.entity;
    bullet.range=retail::shell_unbounded_range;
    if (config_.world.segment) {
        const float ex=shot.x+angles_.sine(shot.heading)*turret_bullet_range_probe;
        const float ez=shot.z+angles_.cosine(shot.heading)*turret_bullet_range_probe;
        if (const std::optional<SegmentHit> wall=config_.world.segment(shot.x,shot.z,ex,ez))
            bullet.range=wall->distance;
    }
    bullets_.push_back(bullet);
    bump(report.turret_shots_fired);
}

// update_bullet_effect (func_8008E480) as ported by projectile_runtime
// update_ballistic: life, then a unit test before the probed range and a
// static test past it. Response codes, ricochets, sounds and trails are not
// reproduced (TEMPORARY POLICY; projectile response is session-internal).
void Bridge::run_turret_bullets(BattleSession& session,TickReport& report) {
    mark(report,Phase::turret_bullets);
    std::vector<TurretBullet> kept;
    kept.reserve(bullets_.size());
    for (TurretBullet bullet : bullets_) {
        if (--bullet.life<=0) continue;
        const float ex=bullet.x+bullet.velocity_x;
        const float ez=bullet.z+bullet.velocity_z;
        bullet.travelled+=bullet.speed;
        const bool static_phase=bullet.travelled>bullet.range;
        if (!static_phase) {
            const std::optional<UnitSegmentHit> hit=find_unit_segment_hit(session.units(),angles_,
                bullet.x,bullet.z,ex,ez,no_entity);
            if (hit) {
                if (session.apply_damage(hit->unit,bullet.damage,bullet.attacker,false)!=DamageResult::ignored)
                    bump(report.turret_shots_applied);
                continue;
            }
        } else if (config_.world.segment && config_.world.segment(bullet.x,bullet.z,ex,ez)) {
            continue;
        }
        bullet.x=ex;
        bullet.z=ez;
        if (config_.world.inside && !config_.world.inside(ex,ez)) continue;
        kept.push_back(bullet);
    }
    bullets_=std::move(kept);
}

// ---- Spawners ----------------------------------------------------------------

void Bridge::run_spawners(BattleSession& session,TickReport& report) {
    mark(report,Phase::spawners);
    // Owner member slots from the session pool (squad_slot), slot ids as unit ids.
    for (std::size_t owner=0U; owner<spawner_runtime::owner_count; ++owner) {
        spawner_owners_[owner].unit_ids.fill(0U);
        spawner_owners_[owner].unit_active.fill(false);
    }
    // TEMPORARY POLICY (request R4): BattleSession::spawn_unit never writes
    // squad_slot. A unit keeps the member slot it held on earlier ticks
    // (member_ids_, keyed by EntityId) so spawner bookkeeping stays stable as
    // other units die; a new unit takes its squad_slot when free, else the
    // first free one; units beyond member_slots per owner are not mapped.
    std::array<std::array<bool,spawner_runtime::member_slots>,spawner_runtime::owner_count> taken{};
    std::array<std::array<EntityId,spawner_runtime::member_slots>,spawner_runtime::owner_count> next_ids{};
    std::array<bool,unit_capacity> placed_slot{};
    const auto assign=[&](std::size_t slot,const UnitRecord& unit,std::size_t member) {
        taken[unit.owner][member]=true;
        placed_slot[slot]=true;
        next_ids[unit.owner][member]=session.units().slot_id(slot);
        spawner_owners_[unit.owner].unit_ids[member]=static_cast<std::uint16_t>(slot);
        spawner_owners_[unit.owner].unit_active[member]=unit.alive;
    };
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=session.units().slot_id(slot);
        const UnitRecord* unit=session.units().get(id);
        if (unit==nullptr || unit->owner>=spawner_runtime::owner_count) continue;
        for (std::size_t k=0U; k<spawner_runtime::member_slots; ++k) {
            if (member_ids_[unit->owner][k]==id && !taken[unit->owner][k]) {
                assign(slot,*unit,k);
                break;
            }
        }
    }
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        if (placed_slot[slot]) continue;
        const UnitRecord* unit=session.units().get(session.units().slot_id(slot));
        if (unit==nullptr || unit->owner>=spawner_runtime::owner_count) continue;
        std::size_t member=unit->squad_slot;
        if (member>=spawner_runtime::member_slots || taken[unit->owner][member]) {
            member=spawner_runtime::member_slots;
            for (std::size_t k=0U; k<spawner_runtime::member_slots; ++k) {
                if (!taken[unit->owner][k]) { member=k; break; }
            }
            if (member==spawner_runtime::member_slots) continue;
        }
        assign(slot,*unit,member);
    }
    member_ids_=next_ids;
    const spawner_runtime::AreaQuery default_query=[&session](const spawner_runtime::AreaBox& box) {
        spawner_runtime::AreaQueryResult result;
        for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
            const UnitRecord* unit=session.units().get(session.units().slot_id(slot));
            if (unit==nullptr || !unit->alive) continue;
            if (!inside(static_cast<float>(box.x),static_cast<float>(box.z),static_cast<float>(box.radius),
                        unit->x,unit->z)) continue;
            result.found=true;
            result.hits.push_back(spawner_runtime::AreaHit{.reserved_handle=false,.kind=0,
                .object_token=static_cast<std::uint32_t>(slot),.is_linked_effect=false});
        }
        return result;
    };
    const std::function<std::optional<LinkedEffectView>(std::size_t)> no_link=
        [](std::size_t) -> std::optional<LinkedEffectView> { return std::nullopt; };
    const spawner_runtime::AreaQuery* query=&default_query;
    if (config_.area_query) query=&config_.area_query;
    const std::function<std::optional<LinkedEffectView>(std::size_t)>* link=&no_link;
    if (config_.spawner_link) link=&config_.spawner_link;
    static_cast<void>(spawners_.tick(config_.game_kind,session.tick(),spawner_owners_,session.rng(),
        *query,*link,orders_));
    for (const spawner_runtime::SpawnOrder& order : orders_.drain()) {
        bump(report.spawns_requested);
        const std::optional<EntityId> id=session.spawn_unit(UnitSpawn{.kind=order.kind,.owner=order.owner,
            .x=order.x,.z=order.z,.heading=order.heading,.flags=order.flags});
        if (!id) {
            // The spawner marked the member live when it queued the order;
            // release it so the slot is retried (static ledger #7).
            bump(report.spawns_failed);
            if (order.owner<spawner_runtime::owner_count && order.member_slot<spawner_runtime::member_slots)
                spawner_owners_[order.owner].unit_active[order.member_slot]=false;
            continue;
        }
        // squad_slot is not a spawn_unit parameter (request R4); written through
        // the public mutable pool so the next spawner tick sees the member.
        if (UnitRecord* unit=session.units().get(*id)) unit->squad_slot=order.member_slot;
        if (order.owner<spawner_runtime::owner_count && order.member_slot<spawner_runtime::member_slots)
            member_ids_[order.owner][order.member_slot]=*id;
    }
}

// ---- Deployables -------------------------------------------------------------

void Bridge::refresh_squad_context(BattleSession& session) {
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const UnitRecord* unit=session.units().get(session.units().slot_id(slot));
        squad_targets_[slot]=TurretTarget{.unit=unit,.targetable=unit!=nullptr && unit->alive,.ai_state_two=false};
    }
    squad_context_=placed::UpdateContext{};
    squad_context_.angles=&angles_;
    squad_context_.targets=std::span<const TurretTarget>(squad_targets_.data(),squad_targets_.size());
    squad_context_.owner_configs=std::span<const TurretOwnerConfig>(config_.turret_owners.data(),
                                                                   config_.turret_owners.size());
    squad_context_.tick=session.tick();
    squad_context_.frame_delta=1.0F;
    squad_context_.search_range=turret_search_range;
    squad_context_.rng=&session.rng();
    // func_80107A14 for the buddy area check: native mines block (W5d
    // DeployablesState::mine_blocks_area). TEMPORARY POLICY: static shapes,
    // units and other placed objects are not queried.
    squad_context_.area_clear=[this](float x,float z,float half) {
        return !deployables_.mine_blocks_area(x,z,half);
    };
}

deployables::DeployablesWorld Bridge::deployables_world(BattleSession& session,TickReport* report) {
    refresh_squad_context(session);
    deployables::DeployablesWorld world;
    world.placement_blocked=[](float,float,float,std::int16_t) { return false; };
    world.splash_units=[this,&session,report](float x,float z,float radius,std::int32_t amount,std::uint8_t flag,
                                               EntityId) -> std::size_t {
        TickReport scratch;
        return splash_units(session,x,z,radius,amount,owner_of_tag(flag),report!=nullptr ? *report : scratch);
    };
    // Gun buddies are effect-27 turret entities (squad_units.hpp).
    world.spawn_squad_unit=[this,report](const deployables::SquadSpawnRequest& request) {
        const squad::SpawnOutcome outcome=squad_.spawn(request);
        if (report!=nullptr) {
            if (outcome.ok()) bump(report->buddies_spawned);
            else if (outcome.status==squad::SpawnStatus::blocked) bump(report->buddies_blocked);
        }
        return outcome.ok();
    };
    world.unit_team=[&session](std::int16_t tag) {
        const std::uint8_t owner=owner_of_tag(tag);
        return owner<owner_capacity ? session.owners()[owner].team : std::uint8_t{0U};
    };
    return world;
}

deployables::DeployResult Bridge::fire_deployable(BattleSession& session,EntityId unit_id,std::uint8_t weapon,
                                                  bool powered) {
    const UnitRecord* unit=session.units().get(unit_id);
    if (unit==nullptr || !unit->alive || (weapon!=12U && weapon!=13U)) return deployables::DeployResult{};
    const std::uint16_t heading=degrees_to_retail_angle(unit->heading_degrees);
    // unit->id (+0x04) is the owner index (fire_unit_weapon.c:450).
    const deployables::DeployerUnit deployer{.id=static_cast<std::int16_t>(unit->owner),
        .unit_kind=static_cast<std::uint8_t>(unit->kind),
        .team=unit->owner<owner_capacity ? session.owners()[unit->owner].team : unit->team,
        .powered=powered,.x=unit->x,.z=unit->z,
        // STRONG INFERENCE: hull forward = (sin, cos) of the u16 heading.
        .hull_forward_x=angles_.sine(heading),.hull_forward_z=angles_.cosine(heading),.heading=heading,
        .heading_degrees=unit->heading_degrees};
    const deployables::DeployablesWorld world=deployables_world(session,nullptr);
    deployables::DeployablesContext context{deployables_,deployable_events_,session.rng(),world,session.tick()};
    const deployables::DeployResult result=weapon==12U ? deployables::fire_mine_weapon(context,deployer)
                                                         : deployables::fire_gun_buddy_weapon(context,deployer);
    reset_squad_context();
    return result;
}

// Drop session pointers so a later direct squad_ use reports no_context
// instead of reading a stale session (squad_units.cpp no_context guard).
void Bridge::reset_squad_context() noexcept {
    squad_context_.rng=nullptr;
    squad_context_.targets={};
}

void Bridge::run_deployables(BattleSession& session,TickReport& report) {
    mark(report,Phase::deployables);
    // TEMPORARY POLICY (ledger #27): deployables draw from session.rng(), the
    // RetailRandom the session steps, so draw order follows the bridge phase
    // order (A4 before B), not retail's single interleaved effect walk.
    const deployables::DeployablesWorld world=deployables_world(session,&report);
    deployables::DeployablesContext context{deployables_,deployable_events_,session.rng(),world,session.tick()};
    // Unit contacts, units in slot order, mines in slot order.
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=session.units().slot_id(slot);
        const UnitRecord* unit=session.units().get(id);
        if (unit==nullptr || !unit->alive) continue;
        std::vector<EntityId> touched;
        deployables_.mines.for_each([&](EntityId mine,const deployables::MineRecord& record) {
            if (overlaps(*unit,record.x,record.z,deployables::mine_place_half_extent)) touched.push_back(mine);
        });
        for (const EntityId mine : touched) {
            const deployables::ContactResult contact=deployables::contact_mine(context,mine,
                                                                              deployables::ContactSender::unit);
            if (contact.report!=deployables::ContactReport::damage) continue;
            bump(report.mine_contacts);
            // The tank +damage wear (amount * 0.2) has no session field (request R7).
            static_cast<void>(session.apply_damage(id,contact.amount,owner_of_tag(contact.attacker),false));
            const UnitRecord* after=session.units().get(id);
            if (after==nullptr || !after->alive) break;
        }
    }
    deployables::update_deployables(context);
    reset_squad_context();
    report.deployable_events_dropped=static_cast<std::uint32_t>(deployable_events_.dropped());
}

// ---- Session events and effects ----------------------------------------------

void Bridge::consume_session_events(BattleSession& session,std::uint16_t clock,std::uint16_t game_tick,
                                    TickReport& report) {
    mark(report,Phase::session_events);
    const std::span<const GameplayEvent> events=session.gameplay_events();
    if (session_event_cursor_>events.size()) session_event_cursor_=0U;   // cleared outside the bridge
    for (std::size_t i=session_event_cursor_; i<events.size(); ++i) {
        const GameplayEvent event=events[i];
        bump(report.session_events_consumed);
        effects::SpawnResult spawned{.status=effects::SpawnStatus::unsupported_variant,.slot=effects::no_link};
        switch (event.kind) {
        case GameplayEventKind::unit_destroyed: {
            if (queen_) static_cast<void>(queen_->carrier_destroyed(event.subject,event.x,event.z,event.tick));
            // Prefer the live record; fall back to the kind snapshotted before
            // session.step in case the unit was already released (ledger #20).
            const UnitRecord* unit=session.units().get(event.subject);
            std::uint8_t type=unit!=nullptr ? static_cast<std::uint8_t>(unit->kind) : std::uint8_t{0U};
            if (unit==nullptr) {
                for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
                    if (pre_step_ids_[slot]==event.subject) { type=pre_step_kinds_[slot]; break; }
                }
            }
            spawned=effects_.spawn_unit_destruction(type,event.x,event.y,event.z,clock);
            break;
        }
        // Projectile visuals are supplied by BattleSession::effect_events(),
        // which retains the weapon-specific variant and exact muzzle point.
        case GameplayEventKind::projectile_impact:
        case GameplayEventKind::weapon_fired:
            break;
        default: break;
        }
        if (spawned.status==effects::SpawnStatus::spawned) bump(report.effect_spawns);
    }
    session_event_cursor_=events.size();
    report.session_events_at_capacity=events.size()>=session_event_capacity;
    route_queen_events(session,report);
    mark(report,Phase::effects_update);
    // Existing effects update before effects allocated by this tick, matching
    // the retail pending-list rule that newly spawned effects wait one frame.
    effects_.update(clock,game_tick);
    effects_.tick_status();
    const std::span<const EffectEvent> effect_events=session.effect_events();
    if (effect_event_cursor_>effect_events.size()) effect_event_cursor_=0U;
    for (std::size_t i=effect_event_cursor_; i<effect_events.size(); ++i) {
        if (effects_.consume(effect_events[i],clock)==effects::EventMapping::spawned)
            bump(report.effect_spawns);
    }
    effect_event_cursor_=effect_events.size();
}

void Bridge::clear_events(BattleSession& session) noexcept {
    session.clear_events();
    session_event_cursor_=0U;
    effect_event_cursor_=0U;
    deployable_events_.clear();
}

std::vector<queen_world::QueenView> Bridge::queen_views() const {
    std::vector<queen_world::QueenView> views;
    if (!queen_) return views;
    views.reserve(queen_handles_.size());
    for (const queen_world::QueenHandle handle : queen_handles_) {
        queen_world::QueenView view{};
        if (queen_->view(handle,view)) views.push_back(view);
    }
    return views;
}

TickReport Bridge::tick(BattleSession& session,const SessionInput& input) {
    TickReport report;
    routed_.clear();
    effects_.clear_transient_visuals();
    if (session_event_cursor_>session.gameplay_events().size()) session_event_cursor_=0U;
    // One effect clock per tick (CONFIRMED derivation, effect_runtime.hpp
    // effect_clock::for_game_tick), sampled before session.step and shared by
    // every effect call site; the game tick goes separately to the oscillators.
    const std::uint32_t game_tick=static_cast<std::uint32_t>(session.tick());
    const std::uint16_t clock=effects::effect_clock::for_game_tick(game_tick);
    const std::uint16_t game_tick_u16=static_cast<std::uint16_t>(game_tick&0xFFFFU);
    run_queen_contacts(session,report);
    run_placed(session,clock,report);
    run_spawners(session,report);
    run_deployables(session,report);
    mark(report,Phase::session_step);
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=session.units().slot_id(slot);
        const UnitRecord* unit=session.units().get(id);
        pre_step_ids_[slot]=unit!=nullptr ? id : no_entity;
        pre_step_kinds_[slot]=unit!=nullptr ? static_cast<std::uint8_t>(unit->kind) : std::uint8_t{0U};
    }
    // The context lives through session.step so fire_weapon can apply the same
    // cooldown, ammo, sound and event bookkeeping as every other weapon.
    const deployables::DeployablesWorld weapon_world=deployables_world(session,&report);
    deployables::DeployablesContext weapon_deployables{
        deployables_,deployable_events_,session.rng(),weapon_world,session.tick()};
    session.step(input,&weapon_deployables);
    reset_squad_context();
    consume_session_events(session,clock,game_tick_u16,report);
    ++ticks_;
    return report;
}

namespace {
// TEMPORARY POLICY: state counts are u16 on the wire; an oversized container is a
// runtime defect, so fail loudly instead of writing a truncated, mismatched count.
std::uint16_t bridge_state_count(std::size_t count) {
    if (count>0xFFFFU) throw std::length_error("campaign bridge state count exceeds u16");
    return static_cast<std::uint16_t>(count);
}
static_assert(turret_bullet_capacity<=0xFFFFU);
}

void Bridge::serialize(StateWriter& writer) const {
    writer.u32(ticks_);
    writer.u8(static_cast<std::uint8_t>(error_));
    writer.boolean(queen_.has_value());
    if (queen_) queen_->serialize(writer);
    writer.u16(bridge_state_count(queen_handles_.size()));
    for (const queen_world::QueenHandle handle : queen_handles_) writer.id(handle.id);
    writer.u16(bridge_state_count(marker_handles_.size()));
    for (const queen_world::MarkerHandle handle : marker_handles_) writer.id(handle.id);
    placed_.serialize(writer);
    spawners_.serialize(writer);
    orders_.serialize(writer);
    for (const spawner_runtime::OwnerSpawnRecord& owner : spawner_owners_) owner.serialize(writer);
    for (const auto& owner : member_ids_) {
        for (const EntityId id : owner) writer.id(id);
    }
    deployables_.serialize(writer);
    effects_.serialize(writer);
    writer.u16(bridge_state_count(bullets_.size()));
    for (const TurretBullet& bullet : bullets_) bullet.serialize(writer);
    squad_.serialize(writer);
    for (const ContactShadow& shadow : contact_shadow_) {
        writer.id(shadow.unit);
        writer.f32(shadow.field_3d0);
        writer.f32(shadow.field_3d4);
    }
    // The session event cursor is transient (reset by clear_events) and the
    // routed-event log is a per-tick report; neither is simulation state.
    // Caveat: restoring mid-tick with uncleared session events re-consumes
    // them (the cursor restarts at 0); snapshot only after clear_events.
}

} // namespace battletanx::campaign_bridge
