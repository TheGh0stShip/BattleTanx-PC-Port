#include "battletanx/effect_runtime.hpp"

#include "battletanx/runtime_math.hpp"

#include <cmath>
#include <initializer_list>

namespace battletanx::effects {
namespace {

using Script=std::array<ScriptStep,script_length>;

constexpr ScriptStep P(std::int16_t d,std::int16_t x=0,std::int16_t y=0,std::int16_t z=0) {
    return ScriptStep{.op=ScriptOp::spawn_particle,.arg0=d,.arg1=x,.arg2=y,.arg3=z};
}
constexpr ScriptStep R(std::int16_t d,std::int16_t x=0,std::int16_t y=0,std::int16_t z=0) {
    return ScriptStep{.op=ScriptOp::spawn_ramp,.arg0=d,.arg1=x,.arg2=y,.arg3=z};
}
constexpr ScriptStep O(std::int16_t d,std::int16_t x=0,std::int16_t y=0,std::int16_t z=0) {
    return ScriptStep{.op=ScriptOp::spawn_oscillator,.arg0=d,.arg1=x,.arg2=y,.arg3=z};
}
constexpr ScriptStep W(std::int16_t t) { return ScriptStep{.op=ScriptOp::wait,.arg0=t,.arg1=0,.arg2=0,.arg3=0}; }

// gEffectScripts[28][16] (effect_script_data.c, 0x80137A3C..0x80138F3B).
// Handlers: spawn_scripted_particle func_800EDB20 -> P, func_800EE000 -> R,
// func_800EE4D4 -> O, sound_duration_elapsed func_800EDA98 -> W.
constexpr std::array<Script,script_count> scripts{{
    /*  0 */ {P(9),W(128),P(9,-48,0,-48),W(192),P(9,48,0,-48),W(256),P(12,48,0,48),W(512),P(15),W(576),
              P(15,-128,0,-128),W(832),P(15,128,0,-128),P(15,128,0,128)},
    /*  1 */ {R(18),P(15,0,64,0),W(96),P(15,-128,64,-128),W(192),P(15,128,64,-128),W(288),P(15,128,64,128)},
    /*  2 */ {},
    /*  3 */ {R(21),P(15)},
    /*  4 */ {P(3)},
    /*  5 */ {P(6)},
    /*  6 */ {P(9)},
    /*  7 */ {P(12)},
    /*  8 */ {P(15)},
    /*  9 */ {P(23)},
    /* 10 */ {P(42)},
    /* 11 */ {P(3)},
    /* 12 */ {P(29)},
    /* 13 */ {P(9),P(9,-64,0,-64)},
    /* 14 */ {P(12),P(12,-128,0,-128)},
    /* 15 */ {P(26),P(26,-128,0,-128)},
    /* 16 */ {P(40),P(40,-128,0,-128)},
    /* 17 */ {W(32),P(12),W(64),P(9,0,64,0),W(96),P(9,0,128,0),W(128),P(9,0,192,0),W(256),R(18),
              P(15,0,256,0),P(15,-128,256,-128),P(15,128,256,-128),P(15,128,256,128),W(288),R(19,0,112,0)},
    /* 18 */ {P(12),P(9,0,64,0),P(9,0,128,0),P(9,0,192,0),P(15,0,256,0),P(15,-128,256,-128),
              P(15,128,256,-128),P(15,128,256,128)},
    /* 19 */ {W(128),R(43)},
    /* 20 */ {W(32),P(12),W(64),R(44,0,10,0),W(96),P(9,0,64,0),P(9,0,128,0),P(9,0,192,0),W(128),
              P(15,0,256,0),P(15,128,256,128),P(15,128,256,-128),P(15,-128,256,-128)},
    /* 21 */ {P(45)},
    /* 22 */ {P(48)},
    /* 23 */ {P(51)},
    /* 24 */ {P(53),P(53,-128,0,-128)},
    /* 25 */ {P(55)},
    /* 26 */ {P(12),W(64),P(9,0,16,0),P(9,0,32,0),P(9,0,48,0)},
    /* 27 */ {W(2048),O(60,0,53,0),O(61,0,53,0),O(62,0,53,0)},
}};

// gEffectScriptTable D_80138F3C: variant -> gEffectScripts index.
constexpr std::array<std::uint8_t,script_count> variant_script{
    17,2,4,5,6,7,8,9,17,18,3,13,14,15,0,11,12,16,10,19,20,21,22,23,24,25,26,27};

// gEffectParticleDefTable D_80137940 resolved to
// {def+0x04 u16, def+0x08 s16, def+0x0A s16, def+0x0C u16}.
constexpr std::array<ParticleDef,particle_def_count> defs{{
    {128,20,80,0x010A},{256,80,250,0x010A},{768,250,75,0x000A},
    {192,50,25,0x010A},{256,25,45,0x010A},{128,45,45,0x000A},
    {192,10,50,0x010A},{4096,50,75,0x010A},{256,75,75,0x000A},
    {192,50,150,0x010A},{512,150,150,0x010A},{256,150,150,0x000A},
    {384,50,300,0x010A},{1024,300,450,0x010A},{256,450,450,0x000A},
    {384,50,600,0x010A},{1024,600,900,0x010A},{256,900,900,0x000A},
    {3072,50,8000,0x0010},{3072,50,8000,0x0000},{3072,50,8000,0x0000},
    {3072,50,8000,0x0000},{3072,50,8000,0x0000},{192,50,150,0x010A},
    {512,150,150,0x010A},{256,150,150,0x000A},{384,50,300,0x010A},
    {256,300,450,0x010A},{256,450,450,0x000A},{192,10,25,0x010A},
    {256,25,37,0x010A},{128,37,37,0x000A},{384,1,4,0x010B},
    {128,4,2,0x010B},{128,2,2,0x000B},{1536,8,8,0x000B},
    {96,10,75,0x0182},{64,75,115,0x0102},{64,115,115,0x0002},
    {192,50,300,0x010A},{1536,300,525,0x010A},{256,525,525,0x000A},
    {448,20,150,0x000A},{1536,50,2000,0x0000},{240,50,600,0x0010},
    {192,10,25,0x010A},{256,25,37,0x010A},{128,37,37,0x000A},
    {384,50,300,0x010A},{1024,300,450,0x010A},{256,450,450,0x000A},
    {256,10,18,0x010A},{128,18,18,0x000A},{1536,300,525,0x010A},
    {256,525,525,0x000A},{192,5,10,0x010A},{256,10,15,0x010A},
    {128,15,15,0x000A},{2816,15,270,0x0102},{256,270,270,0x000A},
    {0,95,250,0x0008},{0,200,100,0x0008},{0,150,150,0x0008},
    // 60..62 are ParticleSweepDefs: +0x04 is a u32 (see oscillator_duration).
}};

constexpr std::uint16_t flag_chain=0x0100U;
constexpr std::uint8_t status_keep=0U, status_free=2U; // update_active_effect_list results
// Hash/layout discriminator; there is no deserializer.
constexpr std::uint8_t effect_serialize_version=3U; // 3: transient beam visuals

bool drawn(EffectKind kind) noexcept { return kind!=EffectKind::script; }

} // namespace

const Script* script_for_variant(std::uint8_t v) noexcept {
    return v<script_count ? &scripts[variant_script[v]] : nullptr;
}

const ScriptStep* script_step(std::uint8_t v,std::uint16_t step) noexcept {
    if (v>=script_count) return nullptr;
    const std::uint32_t flat=static_cast<std::uint32_t>(variant_script[v])*script_length+step;
    if (flat>=script_table_steps) return nullptr;
    return &scripts[flat/script_length][flat%script_length];
}

const ParticleDef* particle_def(std::uint8_t index) noexcept {
    return index<particle_def_count ? &defs[index] : nullptr;
}

namespace {
std::int32_t interpolated_particle_value(const ParticleDef& def,std::uint16_t elapsed) noexcept {
    if (def.duration==0U) return def.scale_from;
    const std::uint16_t clamped=std::min(elapsed,def.duration);
    const std::int32_t delta=static_cast<std::int32_t>(def.scale_to)-def.scale_from;
    const float rate=static_cast<float>(delta)/static_cast<float>(def.duration);
    return static_cast<std::int32_t>(static_cast<float>(def.scale_from)+
                                     static_cast<float>(clamped)*rate);
}
} // namespace

float particle_render_scale(const ParticleDef& def,std::uint16_t elapsed) noexcept {
    return static_cast<float>(interpolated_particle_value(def,elapsed))/1024.0F;
}

float color_ramp_render_scale(const ParticleDef& def,std::uint16_t elapsed) noexcept {
    return static_cast<float>(interpolated_particle_value(def,elapsed))*0.8F;
}

void EffectRuntime::reset() noexcept {
    for (std::uint16_t i=0U; i<pool_capacity; ++i) {
        nodes_[i]=EffectNode{};
        nodes_[i].next=i+1U<pool_capacity ? static_cast<std::int16_t>(i+1U) : no_link;
    }
    free_head_=0;
    walked_pending_=no_link;
    walked_active_=no_link;
    persistent_pending_=no_link;
    persistent_active_=no_link;
    serial_=0U;
    rotating_variant_=0U;
    game_tick_=0U;
    decals_.fill(no_link);
    decal_count_=0U;
    failed_spawns_=0U;
    status_.fill(StatusVisual{});
    beams_.fill(EffectRenderItem{});
    beam_count_=0U;
}

// allocate_effect func_8008AD10: pop the free head, push onto the group's
// pending list head, stamp kind and serial.
std::int16_t EffectRuntime::allocate(bool persistent,EffectKind kind) noexcept {
    if (free_head_==no_link) { ++failed_spawns_; return no_link; }
    const std::int16_t slot=free_head_;
    EffectNode& e=nodes_[static_cast<std::uint16_t>(slot)];
    free_head_=e.next;
    e=EffectNode{};
    std::int16_t& pending=persistent ? persistent_pending_ : walked_pending_;
    e.next=pending;
    pending=slot;
    e.in_use=true;
    e.persistent_group=persistent;
    e.kind=kind;
    e.serial=serial_++;
    return slot;
}

// retire_effect_group_lists func_8008B1F0. Popping the LIFO pending list onto
// the active head leaves the batch in allocation order ahead of older nodes.
void EffectRuntime::merge_pending() noexcept {
    while (walked_pending_!=no_link) {
        const std::int16_t slot=walked_pending_;
        EffectNode& e=nodes_[static_cast<std::uint16_t>(slot)];
        walked_pending_=e.next;
        e.next=walked_active_;
        walked_active_=slot;
    }
    while (persistent_pending_!=no_link) {
        const std::int16_t slot=persistent_pending_;
        EffectNode& e=nodes_[static_cast<std::uint16_t>(slot)];
        persistent_pending_=e.next;
        e.next=persistent_active_;
        persistent_active_=slot;
    }
}

SpawnResult EffectRuntime::spawn_script(std::uint8_t v,float x,float y,float z,std::uint16_t clock) noexcept {
    if (v==variant_rotating) {
        v=rotating_variant_;
        rotating_variant_=static_cast<std::uint8_t>((v+1U)%rotating_variant_count);
    }
    if (v>=script_count) return SpawnResult{.status=SpawnStatus::unsupported_variant,.slot=no_link};
    const std::int16_t slot=allocate(false,EffectKind::script);
    if (slot==no_link) return SpawnResult{.status=SpawnStatus::pool_exhausted,.slot=no_link};
    EffectNode& e=nodes_[static_cast<std::uint16_t>(slot)];
    e.index=v;
    e.step=0U;
    e.start=clock;
    e.now=clock;
    e.x=x; e.y=y; e.z=z;
    return SpawnResult{.status=SpawnStatus::spawned,.slot=slot};
}

SpawnResult EffectRuntime::spawn_particle(EffectKind kind,std::uint8_t def,float x,float y,float z,
                                          std::uint16_t clock) noexcept {
    if (def>=particle_def_count) return SpawnResult{.status=SpawnStatus::unsupported_def,.slot=no_link};
    // UNRESOLVED: retail spawn_effect_kind_29_at_offset draws next_random_u16 (func_800F38F4) x6 per
    // oscillator (visual phase/rates) and sets D_8033B630/1 by pos.z sign; not drawn here, so a shared
    // RetailRandom stream would desync after variant 0x1B.
    const bool oscillator=kind==EffectKind::oscillator;
    if (oscillator && !oscillator_duration_for(def)) return SpawnResult{.status=SpawnStatus::unsupported_def,.slot=no_link};
    const std::int16_t slot=allocate(false,kind);
    if (slot==no_link) return SpawnResult{.status=SpawnStatus::pool_exhausted,.slot=no_link};
    EffectNode& e=nodes_[static_cast<std::uint16_t>(slot)];
    e.index=def;
    // spawn_effect_kind_29_at_offset stamps get_elapsed_game_ticks, not the effect clock.
    const std::uint16_t t=oscillator ? game_tick_ : clock;
    e.start=t;
    e.now=t;
    e.x=x; e.y=y; e.z=z;
    return SpawnResult{.status=SpawnStatus::spawned,.slot=slot};
}

SpawnResult EffectRuntime::spawn_muzzle_flash(EntityId unit,std::uint8_t owner,float x,float y,float z,
                                              std::uint16_t clock) noexcept {
    SpawnResult result=spawn_particle(EffectKind::muzzle_flash,muzzle_flash_def,x,y,z,clock);
    if (result.status==SpawnStatus::spawned) {
        EffectNode& e=nodes_[static_cast<std::uint16_t>(result.slot)];
        e.anchor=unit;
        e.owner=owner;
    }
    return result;
}

// spawn_queued_effect_kind_2b func_80093D50: kind-4 record in group 0 plus a
// kind-0x2B smoke (lifetime -1); the eleventh queued record releases the oldest.
SpawnResult EffectRuntime::spawn_decal(const DecalParams& p) noexcept {
    const std::int16_t slot=allocate(true,EffectKind::decal);
    if (slot==no_link) return SpawnResult{.status=SpawnStatus::pool_exhausted,.slot=no_link};
    {
        EffectNode& e=nodes_[static_cast<std::uint16_t>(slot)];
        e.x=p.x; e.y=p.y; e.z=p.z;
        e.normal_x=-p.normal_x; // entry->value0C = -a
        e.normal_z=-p.normal_z; // entry->value10 = -b
        e.surface=p.surface;
        e.model_slot=p.model_slot;
    }
    const std::int16_t smoke=allocate(false,EffectKind::smoke);
    if (smoke!=no_link) {
        EffectNode& s=nodes_[static_cast<std::uint16_t>(smoke)];
        s.x=p.x; s.y=p.y; s.z=p.z;
        s.lifetime=-1;
    }
    nodes_[static_cast<std::uint16_t>(slot)].child=smoke;
    if (smoke!=no_link) nodes_[static_cast<std::uint16_t>(slot)].child_serial=nodes_[static_cast<std::uint16_t>(smoke)].serial;
    decals_[decal_count_]=slot;
    ++decal_count_;
    if (decal_count_>decal_queue_capacity) {
        release(decals_[0]);
        for (std::uint8_t i=1U; i<decal_count_; ++i) decals_[i-1U]=decals_[i];
        --decal_count_;
        decals_[decal_count_]=no_link;
    }
    return SpawnResult{.status=SpawnStatus::spawned,.slot=slot};
}

// release_effect func_8008B89C: unlink from the group-0 active list (else its
// pending list) and push onto the walked pending list, where the kind's update
// retires it. Not found: no change.
void EffectRuntime::release(std::int16_t slot) noexcept {
    if (slot<0 || static_cast<std::uint16_t>(slot)>=pool_capacity) return;
    for (std::int16_t* head : {&persistent_active_,&persistent_pending_}) {
        std::int16_t prev=no_link;
        for (std::int16_t i=*head; i!=no_link; i=nodes_[static_cast<std::uint16_t>(i)].next) {
            if (i!=slot) { prev=i; continue; }
            EffectNode& e=nodes_[static_cast<std::uint16_t>(i)];
            if (prev==no_link) *head=e.next;
            else nodes_[static_cast<std::uint16_t>(prev)].next=e.next;
            e.next=walked_pending_;
            walked_pending_=slot;
            return;
        }
    }
}

EventMapping EffectRuntime::map(SpawnResult result) const noexcept {
    switch (result.status) {
    case SpawnStatus::spawned: return EventMapping::spawned;
    case SpawnStatus::pool_exhausted: return EventMapping::pool_exhausted;
    default: return EventMapping::unsupported;
    }
}

EventMapping EffectRuntime::consume(const EffectEvent& event,const DecalParams& decal,std::uint16_t clock) noexcept {
    if (event.kind!=EffectEventKind::impact_decal) return consume(event,clock);
    DecalParams p=decal;
    p.x=event.x; p.y=event.y; p.z=event.z;
    return map(spawn_decal(p));
}

EventMapping EffectRuntime::consume(const EffectEvent& event,std::uint16_t clock) noexcept {
    SpawnResult result;
    switch (event.kind) {
    case EffectEventKind::muzzle_flash:
        result=spawn_muzzle_flash(event.source,event.owner,event.x,event.y,event.z,clock);
        break;
    case EffectEventKind::trail:
        if (event.variant==0U) return EventMapping::no_visual; // retail: if (e->trail != 0)
        result=spawn_script(event.variant,event.x,event.y,event.z,clock);
        break;
    case EffectEventKind::impact_explosion:
    case EffectEventKind::blast_armed:
    case EffectEventKind::ring_burst:
        result=spawn_script(event.variant,event.x,event.y,event.z,clock);
        break;
    case EffectEventKind::impact_decal:
        // EffectEvent carries the hit normal (projectile_runtime.hpp EffectEvent
        // normal_x/normal_z; 0,0 when the hit has none) and the surface in other.
        result=spawn_decal(DecalParams{.x=event.x,.y=event.y,.z=event.z,.normal_x=event.normal_x,
                                       .normal_z=event.normal_z,.surface=event.other,
                                       .model_slot=models::impact_decal});
        break;
    case EffectEventKind::beam:
        if (beam_count_>=beams_.size()) return EventMapping::pool_exhausted;
        beams_[beam_count_++]=EffectRenderItem{
            .item=RenderItemKind::beam,.kind={},.status={},.slot=no_link,.serial=0U,
            .index=event.variant,.elapsed=0U,.duration=0U,.flags=0U,
            .model_slot=models::beam,.anchor=event.source,.owner=event.owner,
            .x=event.x,.y=event.y,.z=event.z,.normal_x=0.0F,.normal_z=0.0F,
            .angle=event.angle,.length=event.length,.scale=1.0F};
        return EventMapping::spawned;
    case EffectEventKind::bounce:
        // TEMPORARY POLICY: nuke/grenade bounces spawn no pool visual here; the
        // landing visual arrives as its own trail/impact event.
        return EventMapping::no_visual;
    default: // ricochet_*: update_shell_effect response 7 spawns no pool visual (sound 3 only);
             // projectile_removed/splash/shield_raised/repaired: no pool visual
        return EventMapping::no_visual;
    }
    return map(result);
}

void EffectRuntime::consume_all(const EffectEventQueue& queue,std::uint16_t clock) noexcept {
    for (const EffectEvent& event : queue.events()) static_cast<void>(consume(event,clock));
}

// run_effect_script func_800ED994. Spawn handlers return 1 even when the pool
// is exhausted (the child is skipped); wait blocks until elapsed >= arg0.
// Steps index the flat [28][16] table from the script start with no per-script
// bound, so script 17 continues into script 18 (CONFIRMED by ROM, 0x801387BC).
// TEMPORARY POLICY: stop at the table end instead of reading past it (no
// retail script reaches it: script 27 ends at step 4).
static_assert(script_length<=0xFFU,"EffectNode::step must address a full script");
std::uint8_t EffectRuntime::run_script(EffectNode& e,std::uint16_t clock) noexcept {
    if (script_for_variant(e.index)==nullptr) return status_free;
    e.now=clock;
    e.elapsed=static_cast<std::uint16_t>(clock-e.start);
    const float x=e.x, y=e.y, z=e.z;
    for (const ScriptStep* p=script_step(e.index,e.step); p!=nullptr && p->op!=ScriptOp::end;
         p=script_step(e.index,e.step)) {
        const ScriptStep& s=*p;
        const std::uint8_t def=static_cast<std::uint8_t>(s.arg0);
        const float px=x+static_cast<float>(s.arg1), py=y+static_cast<float>(s.arg2), pz=z+static_cast<float>(s.arg3);
        switch (s.op) {
        case ScriptOp::wait:
            if (e.elapsed<static_cast<std::uint16_t>(s.arg0)) return status_keep;
            break;
        case ScriptOp::spawn_particle: static_cast<void>(spawn_particle(EffectKind::particle,def,px,py,pz,clock)); break;
        case ScriptOp::spawn_ramp: static_cast<void>(spawn_particle(EffectKind::ramp,def,px,py,pz,clock)); break;
        case ScriptOp::spawn_oscillator: static_cast<void>(spawn_particle(EffectKind::oscillator,def,px,py,pz,clock)); break;
        case ScriptOp::end: break;
        }
        // EffectNode::step is u8 while script_step walks a u16 flat index. No
        // retail run-on passes flat step 24; stop rather than wrap if a
        // data-driven script ever reaches the u8 limit.
        if (e.step==0xFFU) return status_free;
        ++e.step;
    }
    return status_free;
}

// func_800EDC64 (0x27/0x2A) and func_800EE164 (0x28): live while
// elapsed <= duration; past it, chain to def+1 (flag 0x100) or retire.
std::uint8_t EffectRuntime::update_particle(EffectNode& e,std::uint16_t clock) noexcept {
    const ParticleDef* def=particle_def(e.index);
    if (def==nullptr) return status_free;
    e.now=clock;
    e.elapsed=static_cast<std::uint16_t>(clock-e.start);
    if (e.elapsed<=def->duration) return status_keep;
    if (e.kind==EffectKind::ramp || (def->flags & flag_chain)==0U) return status_free;
    // TEMPORARY POLICY: a chain past the last def retires instead of reading out of range.
    if (e.index+1U>=particle_def_count) return status_free;
    e.elapsed=0U;
    ++e.cycle;
    ++e.index;
    e.start=e.now;
    return status_keep;
}

std::uint8_t EffectRuntime::update_node(EffectNode& e,std::uint16_t clock) noexcept {
    switch (e.kind) {
    case EffectKind::script: return run_script(e,clock);
    case EffectKind::particle:
    case EffectKind::muzzle_flash:
    case EffectKind::ramp: return update_particle(e,clock);
    case EffectKind::oscillator: return update_oscillator(e);
    case EffectKind::smoke: // func_8010BEFC lifetime countdown
        if (e.lifetime==-1) return status_keep;
        if (e.lifetime==0) return status_free;
        --e.lifetime;
        return status_keep;
    case EffectKind::decal: // func_80093EF8: stop the smoke (func_8010CB4C), retire
        if (e.child!=no_link) {
            EffectNode& s=nodes_[static_cast<std::uint16_t>(e.child)];
            // Retail holds a raw pointer; the serial check skips a reused slot.
            if (s.in_use && s.kind==EffectKind::smoke && s.serial==e.child_serial) s.lifetime=0;
        }
        return status_free;
    }
    return status_free;
}

// update_effect_particle_oscillation func_800EE75C. Never reports a status.
// Runs on get_elapsed_game_ticks (u16), not the effect clock; keeps while
// duration (u32) >= elapsed, else restarts the sweep (direction flip and the
// delay countdown are visual only and not modeled; cycle counts restarts).
std::uint8_t EffectRuntime::update_oscillator(EffectNode& e) noexcept {
    const std::optional<std::uint32_t> duration=oscillator_duration_for(e.index);
    e.now=game_tick_;
    e.elapsed=static_cast<std::uint16_t>(game_tick_-e.start);
    if (duration && *duration<e.elapsed) { e.elapsed=0U; e.start=e.now; ++e.cycle; }
    return status_keep;
}

// update_active_effect_list func_8008B2C8 (result 1, the move to the group-0
// list, is not produced by any kind simulated here).
void EffectRuntime::update(std::uint16_t clock,std::uint16_t game_tick,UpdateGroup group) noexcept {
    game_tick_=game_tick;
    merge_pending();
    std::int16_t prev=no_link;
    std::int16_t i=walked_active_;
    while (i!=no_link) {
        EffectNode& e=nodes_[static_cast<std::uint16_t>(i)];
        if (!kind_enabled(group,static_cast<std::uint8_t>(e.kind))) { prev=i; i=e.next; continue; }
        const std::uint8_t status=update_node(e,clock);
        const std::int16_t next=e.next;
        if (status==status_free) {
            if (prev==no_link) walked_active_=next;
            else nodes_[static_cast<std::uint16_t>(prev)].next=next;
            e=EffectNode{};
            e.next=free_head_;
            free_head_=i;
        } else {
            prev=i;
        }
        i=next;
    }
    merge_pending();
}

bool EffectRuntime::set_status(const StatusVisual& visual) noexcept {
    if (visual.model_slot>=model_slot_count) return false;
    StatusVisual* empty=nullptr;
    for (StatusVisual& s : status_) {
        if (s.active && s.subject==visual.subject && s.kind==visual.kind) { s=visual; s.active=true; return true; }
        if (!s.active && empty==nullptr) empty=&s;
    }
    if (empty==nullptr) return false;
    *empty=visual;
    empty->active=true;
    return true;
}

void EffectRuntime::clear_status(EntityId subject,StatusVisualKind kind) noexcept {
    for (StatusVisual& s : status_)
        if (s.active && s.subject==subject && s.kind==kind) s=StatusVisual{};
}

void EffectRuntime::tick_status() noexcept {
    for (StatusVisual& s : status_) {
        if (!s.active || s.remaining<0) continue;
        if (s.remaining<=1) s=StatusVisual{};
        else --s.remaining;
    }
}

void EffectRuntime::collect(EffectRenderQueue& out) const {
    for (const std::int16_t head : {walked_active_,persistent_active_}) {
        for (std::int16_t i=head; i!=no_link; i=nodes_[static_cast<std::uint16_t>(i)].next) {
            const EffectNode& e=nodes_[static_cast<std::uint16_t>(i)];
            if (!drawn(e.kind)) continue;
            const ParticleDef* def=(e.kind==EffectKind::decal || e.kind==EffectKind::smoke) ? nullptr : particle_def(e.index);
            const std::optional<std::uint32_t> sweep=e.kind==EffectKind::oscillator ? oscillator_duration_for(e.index)
                                                                                     : std::nullopt;
            const bool ordinary=e.kind==EffectKind::particle;
            const bool ramp=e.kind==EffectKind::ramp;
            const std::uint16_t model=ordinary ? models::scripted_particle :
                ramp ? models::color_ramp_particle : e.model_slot;
            const float scale=def==nullptr ? 1.0F : ordinary ? particle_render_scale(*def,e.elapsed) :
                ramp ? color_ramp_render_scale(*def,e.elapsed) : 1.0F;
            out.push(EffectRenderItem{.item=RenderItemKind::pool_effect,.kind=e.kind,.status={},.slot=i,.serial=e.serial,
                                      .index=e.index,.elapsed=e.elapsed,
                                      .duration=sweep ? static_cast<std::uint16_t>(*sweep)
                                               : def!=nullptr ? def->duration : std::uint16_t{},
                                      .flags=def!=nullptr ? def->flags : std::uint16_t{},
                                      .model_slot=model,.anchor=e.anchor,.owner=e.owner,
                                      .x=e.x,.y=e.y,.z=e.z,.normal_x=e.normal_x,.normal_z=e.normal_z,
                                      .angle=0U,.length=0.0F,.scale=scale});
        }
    }
    for (const StatusVisual& s : status_) {
        if (!s.active) continue;
        out.push(EffectRenderItem{.item=RenderItemKind::status,.kind={},.status=s.kind,.slot=no_link,.serial=0U,.index=0U,.elapsed=0U,.duration=0U,.flags=0U,.model_slot=s.model_slot,
                                  .anchor=s.subject,.owner=s.owner,.x=s.x,.y=s.y,.z=s.z,.normal_x=0.0F,.normal_z=0.0F,
                                  .angle=0U,.length=0.0F,.scale=1.0F});
    }
    for (std::uint8_t i=0U; i<beam_count_; ++i) out.push(beams_[i]);
}

std::uint16_t EffectRuntime::live_count() const noexcept {
    std::uint16_t count=0U;
    for (const EffectNode& e : nodes_) if (e.in_use) ++count;
    return count;
}


void EffectRuntime::serialize(StateWriter& w) const {
    w.u8(effect_serialize_version);
    w.s16(free_head_);
    w.s16(walked_pending_);
    w.s16(walked_active_);
    w.s16(persistent_pending_);
    w.s16(persistent_active_);
    w.u32(serial_);
    w.u8(rotating_variant_);
    w.u16(game_tick_);
    w.u8(decal_count_);
    for (const std::int16_t d : decals_) w.s16(d);
    w.u32(failed_spawns_);
    w.u8(beam_count_);
    for (std::uint8_t i=0U; i<beam_count_; ++i) {
        const EffectRenderItem& beam=beams_[i];
        w.u8(beam.index); w.u8(beam.owner);
        w.f32(beam.x); w.f32(beam.y); w.f32(beam.z);
        w.u16(beam.angle); w.f32(beam.length);
    }
    for (const EffectNode& e : nodes_) {
        w.s16(e.next);
        w.boolean(e.in_use);
        if (!e.in_use) continue;
        w.boolean(e.persistent_group);
        w.u32(e.serial);
        w.u8(static_cast<std::uint8_t>(e.kind));
        w.u8(e.index);
        w.u8(e.step);
        w.u8(e.cycle);
        w.u16(e.start);
        w.u16(e.now);
        w.u16(e.elapsed);
        w.s32(e.lifetime);
        w.s16(e.child);
        w.u32(e.child_serial);
        w.f32(e.normal_x); w.f32(e.normal_z);
        w.id(e.surface);
        w.u16(e.model_slot);
        w.f32(e.x); w.f32(e.y); w.f32(e.z);
        w.id(e.anchor);
        w.u8(e.owner);
    }
    for (const StatusVisual& s : status_) {
        w.boolean(s.active);
        if (!s.active) continue;
        w.u8(static_cast<std::uint8_t>(s.kind));
        w.id(s.subject);
        w.u8(s.owner);
        w.u16(s.model_slot);
        w.s32(s.remaining);
        w.f32(s.x); w.f32(s.y); w.f32(s.z);
    }
}

namespace {

// Shared tail of func_800A2A44 / func_800A2C38. STRONG INFERENCE: retail angle: dz/dx are
// target - emitter, cosine = -dx/len, negated when -dz < 0, via
// radians_to_angle (func_800F3BE4, an arccos to binary angle as used by
// xz_vector_heading). That is the heading of (-dz, -dx) in the forward =
// (sin a, cos a) convention, computed here with direction_angle (polynomial;
// may differ from the retail arccos by +-1 binary-angle unit; visual only).
std::optional<DebrisRequest> fire_debris(DebrisEmitterState& m,float dx,float dz) noexcept {
    if (m.timer>0) return std::nullopt;
    m.timer=0;
    if (m.crate_kind>=models::crate_kind_count) return std::nullopt; // TEMPORARY POLICY: out-of-table kind
    return DebrisRequest{.model_slot=models::crate,.x=m.x,.z=m.z,.part_count=m.part_count,
                         .angle=(dz!=0.0F || dx!=0.0F) ? direction_angle(-dz,-dx) : std::uint16_t{},
                         .team=m.team};
}

std::int16_t sub16(std::int16_t timer,std::int32_t amount) noexcept {
    // s16 field updated through a 32-bit register then stored with sh.
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(static_cast<std::int32_t>(timer))-static_cast<std::uint32_t>(amount)));
}

} // namespace

std::optional<DebrisRequest> step_debris_emitter(DebrisEmitterState& m,float tx,float tz,std::int32_t elapsed) noexcept {
    m.timer=sub16(m.timer,elapsed);
    return fire_debris(m,tx-m.x,tz-m.z);
}

std::optional<DebrisRequest> step_distance_scaled_debris_emitter(DebrisEmitterState& m,float tx,float tz) noexcept {
    const float dz=tz-m.z, dx=tx-m.x;
    const float dist=std::sqrt(dz*dz+dx*dx);
    const std::int32_t rate=debris_distance_limit<dist ? debris_rate_base
        : static_cast<std::int32_t>((1.0F-dist/debris_distance_limit)*50.0F)+debris_rate_base;
    m.timer=sub16(m.timer,rate);
    return fire_debris(m,dx,dz);
}

void serialize(StateWriter& w,const DebrisEmitterState& m) {
    w.u8(m.crate_kind);
    w.u16(m.part_count);
    w.u8(m.team);
    w.f32(m.x); w.f32(m.z);
    w.s16(m.timer);
}

} // namespace battletanx::effects
