#include "battletanx/effect_runtime.hpp"

#include "runtime_test_support.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>

namespace {
using namespace battletanx;
using namespace battletanx::effects;

std::unique_ptr<EffectRuntime> make() { return std::make_unique<EffectRuntime>(); }

std::uint16_t count_kind(const EffectRuntime& fx,EffectKind kind) {
    std::uint16_t count=0U;
    for (std::int16_t i=0; i<static_cast<std::int16_t>(pool_capacity); ++i)
        if (fx.node(i).in_use && fx.node(i).kind==kind) ++count;
    return count;
}

std::int16_t follow(const EffectRuntime& fx,std::int16_t slot,int steps) {
    for (int i=0; i<steps && slot!=no_link; ++i) slot=fx.node(slot).next;
    return slot;
}

} // namespace

RUNTIME_TEST(variant_table_matches_retail_script_map) {
    REQUIRE(script_for_variant(28U)==nullptr);
    REQUIRE((*script_for_variant(variant::impact_cannon))[0].op==ScriptOp::spawn_particle);
    REQUIRE((*script_for_variant(variant::impact_cannon))[1].arg1==-64);
    REQUIRE((*script_for_variant(1U))[0].op==ScriptOp::end); // empty script 2
    REQUIRE((*script_for_variant(variant::tank_destroyed))[1].op==ScriptOp::wait);
    REQUIRE((*script_for_variant(variant::tank_destroyed))[1].arg0==128);
    REQUIRE(particle_def(muzzle_flash_def)->duration==96U);
    REQUIRE(particle_def(0U)->scale_from==20);
    REQUIRE(particle_def(0U)->scale_to==80);
    REQUIRE(runtime_test::near(particle_render_scale(*particle_def(0U),0U),20.0F/1024.0F));
    REQUIRE(runtime_test::near(particle_render_scale(*particle_def(0U),64U),50.0F/1024.0F));
    REQUIRE(runtime_test::near(particle_render_scale(*particle_def(0U),128U),80.0F/1024.0F));
    REQUIRE(runtime_test::near(color_ramp_render_scale(*particle_def(18U),0U),40.0F));
    REQUIRE(particle_def(63U)==nullptr);
    return true;
}

RUNTIME_TEST(render_queue_uses_retail_particle_models_and_interpolated_scale) {
    auto fx=make();
    REQUIRE(fx->spawn_particle(EffectKind::particle,0U,1.0F,2.0F,3.0F,0U).status==SpawnStatus::spawned);
    REQUIRE(fx->spawn_particle(EffectKind::ramp,18U,4.0F,5.0F,6.0F,0U).status==SpawnStatus::spawned);
    fx->update(64U);
    EffectRenderQueue queue(8U);
    fx->collect(queue);
    REQUIRE(queue.events().size()==2U);
    REQUIRE(queue.events()[0].model_slot==models::scripted_particle);
    REQUIRE(runtime_test::near(queue.events()[0].scale,50.0F/1024.0F));
    REQUIRE(queue.events()[1].model_slot==models::color_ramp_particle);
    REQUIRE(runtime_test::near(queue.events()[1].scale,172.0F));
    return true;
}

RUNTIME_TEST(script_retires_and_particle_chain_boundaries) {
    auto fx=make();
    REQUIRE(fx->spawn_script(variant::impact_cannon,0.0F,0.0F,0.0F,0U).status==SpawnStatus::spawned);
    fx->update(0U); // script 13: two def-9 particles, then retires
    REQUIRE(count_kind(*fx,EffectKind::script)==0U);
    REQUIRE(count_kind(*fx,EffectKind::particle)==2U);
    const std::int16_t first=fx->walked_head();
    REQUIRE(fx->node(follow(*fx,first,1)).x==-64.0F);
    fx->update(192U);                       // elapsed == duration: still def 9
    REQUIRE(fx->node(first).index==9U);
    fx->update(193U);                       // past duration, flag 0x100: chain to def 10
    REQUIRE(fx->node(first).index==10U);
    REQUIRE(fx->node(first).cycle==1U);
    REQUIRE(fx->node(first).start==193U);
    fx->update(193U+512U);
    REQUIRE(fx->node(first).index==10U);
    fx->update(193U+513U);                  // def 11 (256, no chain)
    REQUIRE(fx->node(first).index==11U);
    fx->update(706U+256U);
    REQUIRE(fx->live_count()==2U);
    fx->update(706U+257U);
    REQUIRE(fx->live_count()==0U);
    return true;
}

RUNTIME_TEST(spawns_are_pending_until_merged_and_batches_keep_allocation_order) {
    auto fx=make();
    const std::int16_t a=fx->spawn_particle(EffectKind::particle,9U,0,0,0,0U).slot;
    const std::int16_t b=fx->spawn_particle(EffectKind::particle,9U,0,0,0,0U).slot;
    const std::int16_t c=fx->spawn_particle(EffectKind::particle,9U,0,0,0,0U).slot;
    EffectRenderQueue before(16U);
    fx->collect(before);
    REQUIRE(before.events().empty());      // pending nodes are not on an active list
    fx->update(0U);
    REQUIRE(fx->walked_head()==a);
    REQUIRE(follow(*fx,a,1)==b);
    REQUIRE(follow(*fx,a,2)==c);
    const std::int16_t d=fx->spawn_particle(EffectKind::particle,9U,0,0,0,0U).slot;
    const std::int16_t e=fx->spawn_particle(EffectKind::particle,9U,0,0,0,0U).slot;
    fx->update(1U);
    REQUIRE(fx->walked_head()==d);
    REQUIRE(follow(*fx,d,1)==e);
    REQUIRE(follow(*fx,d,2)==a);
    return true;
}

RUNTIME_TEST(children_spawned_during_walk_are_first_updated_next_frame) {
    auto fx=make();
    fx->spawn_script(variant::tank_destroyed,0,0,0,0U);   // script 0
    fx->update(0U);                                        // P(9) then blocks on W(128)
    REQUIRE(count_kind(*fx,EffectKind::particle)==1U);
    const std::int16_t child=fx->walked_head();
    REQUIRE(fx->node(child).kind==EffectKind::particle);
    REQUIRE(fx->node(child).elapsed==0U);
    fx->update(127U);
    REQUIRE(count_kind(*fx,EffectKind::particle)==1U);
    fx->update(128U);                                      // W(128) passes, P(9,-48,0,-48), blocks W(192)
    REQUIRE(count_kind(*fx,EffectKind::particle)==2U);
    const std::int16_t second=fx->walked_head();
    REQUIRE(fx->node(second).start==128U);
    REQUIRE(fx->node(second).elapsed==0U);                 // not walked in the frame it was born
    REQUIRE(fx->node(child).elapsed==128U);
    fx->update(2000U);                                     // remaining waits pass at once
    REQUIRE(count_kind(*fx,EffectKind::script)==0U);
    return true;
}

RUNTIME_TEST(pool_exhaustion_is_counted_and_script_still_retires) {
    auto fx=make();
    for (std::size_t i=0U; i<pool_capacity-1U; ++i)
        REQUIRE(fx->spawn_particle(EffectKind::particle,2U,0,0,0,0U).status==SpawnStatus::spawned);
    REQUIRE(fx->spawn_script(variant::impact_cannon,0,0,0,0U).status==SpawnStatus::spawned);
    REQUIRE(fx->spawn_script(variant::impact_cannon,0,0,0,0U).status==SpawnStatus::pool_exhausted);
    REQUIRE(fx->failed_spawns()==1U);
    fx->update(0U);                     // both children fail, script still retires (handler returns 1)
    REQUIRE(fx->failed_spawns()==3U);   // the script's own slot is freed only after its update
    REQUIRE(count_kind(*fx,EffectKind::script)==0U);
    return true;
}

RUNTIME_TEST(free_list_is_lifo) {
    auto fx=make();
    const std::int16_t a=fx->spawn_particle(EffectKind::particle,38U,0,0,0,0U).slot; // 64, no chain
    const std::int16_t b=fx->spawn_particle(EffectKind::particle,2U,0,0,0,0U).slot;  // 768
    REQUIRE(a==0 && b==1);
    fx->update(65U);
    REQUIRE(!fx->node(a).in_use);
    REQUIRE(fx->spawn_particle(EffectKind::particle,2U,0,0,0,0U).slot==a);
    REQUIRE(fx->node(a).serial==2U);
    return true;
}

RUNTIME_TEST(rotating_variant_and_unsupported_variant) {
    auto fx=make();
    for (std::uint8_t i=0U; i<14U; ++i) {
        const SpawnResult r=fx->spawn_script(variant_rotating,0,0,0,0U);
        REQUIRE(fx->node(r.slot).index==static_cast<std::uint8_t>(i%rotating_variant_count));
    }
    REQUIRE(fx->spawn_script(29U,0,0,0,0U).status==SpawnStatus::unsupported_variant);
    REQUIRE(fx->spawn_particle(EffectKind::particle,63U,0,0,0,0U).status==SpawnStatus::unsupported_def);
    return true;
}

RUNTIME_TEST(beam_event_is_a_bounded_transient_render_item) {
    EffectRuntime fx;
    EffectEvent beam;
    beam.kind=EffectEventKind::beam;
    beam.variant=8U;
    beam.owner=2U;
    beam.x=10.0F; beam.y=20.0F; beam.z=30.0F;
    beam.angle=0x4000U;
    beam.length=1234.0F;
    REQUIRE(fx.consume(beam,0U)==EventMapping::spawned);
    EffectRenderQueue queue(4U);
    fx.collect(queue);
    REQUIRE(queue.events().size()==1U);
    const auto& item=queue.events().front();
    REQUIRE(item.item==RenderItemKind::beam);
    REQUIRE(item.model_slot==models::beam);
    REQUIRE(item.index==8U && item.owner==2U);
    REQUIRE(item.x==10.0F && item.y==20.0F && item.z==30.0F);
    REQUIRE(item.angle==0x4000U && item.length==1234.0F);
    fx.clear_transient_visuals();
    queue.clear();
    fx.collect(queue);
    REQUIRE(queue.events().empty());
    return true;
}

RUNTIME_TEST(decal_queue_evicts_oldest_and_stops_its_smoke) {
    auto fx=make();
    std::int16_t first=no_link;
    for (int i=0; i<10; ++i) {
        const SpawnResult r=fx->spawn_decal(DecalParams{.x=static_cast<float>(i),.y=0.0F,.z=0.0F,.normal_x=0.0F,.normal_z=0.0F,.surface=EntityId{},.model_slot=models::impact_decal});
        if (i==0) first=r.slot;
    }
    fx->update(0U);
    REQUIRE(fx->decal_count()==10U);
    REQUIRE(fx->live_count()==20U);
    const std::int16_t smoke=fx->node(first).child;
    REQUIRE(fx->node(smoke).lifetime==-1);
    fx->spawn_decal(DecalParams{.x=10.0F,.y=0.0F,.z=0.0F,.normal_x=0.0F,.normal_z=0.0F,.surface=EntityId{},.model_slot=models::impact_decal});
    REQUIRE(fx->decal_count()==10U);
    REQUIRE(fx->live_count()==22U);   // evicted record waits on the walked pending list
    fx->update(1U);                   // decal update zeroes smoke lifetime; smoke retires in the same walk
    REQUIRE(!fx->node(first).in_use);
    REQUIRE(!fx->node(smoke).in_use);
    REQUIRE(fx->live_count()==20U);
    return true;
}

RUNTIME_TEST(weapon_events_map_to_pool_effects) {
    auto fx=make();
    EffectEventQueue queue(8U);
    queue.push(EffectEvent{.kind=EffectEventKind::muzzle_flash,.tick=0,.source=EntityId{1U,1U},.other=EntityId{},.variant=0U,.owner=0U,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F});
    queue.push(EffectEvent{.kind=EffectEventKind::trail,.tick=0,.source=EntityId{},.other=EntityId{},.variant=0U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F});
    queue.push(EffectEvent{.kind=EffectEventKind::impact_explosion,.tick=0,.source=EntityId{},.other=EntityId{},.variant=variant::impact_small,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F});
    queue.push(EffectEvent{.kind=EffectEventKind::impact_decal,.tick=0,.source=EntityId{},.other=EntityId{},.variant=0U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F});
    queue.push(EffectEvent{.kind=EffectEventKind::projectile_removed,.tick=0,.source=EntityId{},.other=EntityId{},.variant=0U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F});
    REQUIRE(fx->consume(queue.events()[1],0U)==EventMapping::no_visual);
    REQUIRE(fx->consume(queue.events()[4],0U)==EventMapping::no_visual);
    REQUIRE(fx->consume(EffectEvent{.kind=EffectEventKind::trail,.tick=0,.source=EntityId{},.other=EntityId{},.variant=40U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F},0U)==EventMapping::unsupported);
    fx->consume_all(queue,0U);
    REQUIRE(count_kind(*fx,EffectKind::muzzle_flash)==1U);
    REQUIRE(count_kind(*fx,EffectKind::script)==1U);
    REQUIRE(count_kind(*fx,EffectKind::decal)==1U);
    REQUIRE(count_kind(*fx,EffectKind::smoke)==1U);
    return true;
}

RUNTIME_TEST(muzzle_flash_chain_36_37_38) {
    auto fx=make();
    const std::int16_t s=fx->spawn_muzzle_flash(EntityId{3U,1U},2U,0,0,0,0U).slot;
    fx->update(96U);  REQUIRE(fx->node(s).index==36U);
    fx->update(97U);  REQUIRE(fx->node(s).index==37U);
    fx->update(161U); REQUIRE(fx->node(s).index==37U);
    fx->update(162U); REQUIRE(fx->node(s).index==38U);
    fx->update(226U); REQUIRE(fx->node(s).in_use);
    fx->update(227U); REQUIRE(!fx->node(s).in_use);
    return true;
}

RUNTIME_TEST(clock_wraps_in_sixteen_bits) {
    auto fx=make();
    const std::int16_t s=fx->spawn_particle(EffectKind::particle,38U,0,0,0,65530U).slot;
    fx->update(5U);
    REQUIRE(fx->node(s).elapsed==11U);
    return true;
}

RUNTIME_TEST(status_visuals_expire_and_bound_model_slot) {
    auto fx=make();
    REQUIRE(!fx->set_status(StatusVisual{.active=false,.kind=StatusVisualKind::base_marker,.subject=EntityId{},.owner=no_owner,.model_slot=model_slot_count,.remaining=0,.x=0.0F,.y=0.0F,.z=0.0F}));
    REQUIRE(fx->set_status(StatusVisual{.active=false,.kind=StatusVisualKind::shield,.subject=EntityId{1U,1U},.owner=no_owner,.model_slot=0U,.remaining=2,.x=0.0F,.y=0.0F,.z=0.0F}));
    REQUIRE(fx->set_status(StatusVisual{.active=false,.kind=StatusVisualKind::queen_marker,.subject=EntityId{2U,1U},.owner=no_owner,.model_slot=0U,.remaining=-1,.x=0.0F,.y=0.0F,.z=0.0F}));
    EffectRenderQueue q(8U);
    fx->collect(q);
    REQUIRE(q.events().size()==2U);
    fx->tick_status();
    fx->tick_status();
    q.clear();
    fx->collect(q);
    REQUIRE(q.events().size()==1U);
    REQUIRE(q.events()[0].status==StatusVisualKind::queen_marker);
    fx->clear_status(EntityId{2U,1U},StatusVisualKind::queen_marker);
    q.clear();
    fx->collect(q);
    REQUIRE(q.events().empty());
    return true;
}

RUNTIME_TEST(serialization_is_deterministic_and_state_sensitive) {
    auto a=make();
    auto b=make();
    for (EffectRuntime* fx : {a.get(),b.get()}) {
        fx->spawn_unit_destruction(0U,1.0F,2.0F,3.0F,0U);
        fx->spawn_decal(DecalParams{.x=4.0F,.y=0.0F,.z=0.0F,.normal_x=0.0F,.normal_z=0.0F,.surface=EntityId{},.model_slot=models::impact_decal});
        fx->update(0U);
    }
    StateWriter wa, wb;
    a->serialize(wa);
    b->serialize(wb);
    REQUIRE(wa.hex()==wb.hex());
    b->update(128U);
    StateWriter wc;
    b->serialize(wc);
    REQUIRE(wa.hex()!=wc.hex());
    return true;
}

RUNTIME_TEST(effect_clock_converts_game_ticks) {
    static_assert(effect_clock::counts_per_game_tick==1'562'500U);
    REQUIRE(effect_clock::for_game_tick(0U)==0U);
    REQUIRE(effect_clock::for_game_tick(1U)==23U);   // 1,562,500 >> 16
    REQUIRE(effect_clock::for_game_tick(30U)==715U); // one second
    REQUIRE(effect_clock::for_game_tick(2749U)==5U); // low word wrapped: 345,204 >> 16
    REQUIRE(effect_clock::for_game_tick(0U,0x10000U)==1U);
    return true;
}

RUNTIME_TEST(group_enable_table_gates_updates) {
    REQUIRE(kind_enabled(UpdateGroup::normal,0x2BU));
    REQUIRE(kind_enabled(UpdateGroup::paused,7U));
    REQUIRE(kind_enabled(UpdateGroup::paused,0x25U));
    REQUIRE(!kind_enabled(UpdateGroup::paused,0x26U));
    REQUIRE(!kind_enabled(UpdateGroup::match_end,0U));
    REQUIRE(kind_enabled(UpdateGroup::match_end,0x26U));
    REQUIRE(!kind_enabled(UpdateGroup::normal,kind_count));
    auto fx=make();
    const SpawnResult r=fx->spawn_script(variant::impact_cannon,0.0F,0.0F,0.0F,0U);
    fx->update(0U,UpdateGroup::paused);  // script kind disabled: kept, not run
    REQUIRE(fx->node(r.slot).in_use);
    REQUIRE(count_kind(*fx,EffectKind::particle)==0U);
    fx->update(0U);
    REQUIRE(!fx->node(r.slot).in_use);
    REQUIRE(count_kind(*fx,EffectKind::particle)==2U);
    return true;
}

RUNTIME_TEST(decal_params_are_stored_negated) {
    auto fx=make();
    const EntityId wall{.slot=5U,.generation=1U};
    const SpawnResult r=fx->spawn_decal(DecalParams{.x=1.0F,.y=0.0F,.z=0.0F,.normal_x=0.5F,.normal_z=-0.25F,.surface=wall,.model_slot=models::impact_decal});
    REQUIRE(fx->node(r.slot).normal_x==-0.5F);
    REQUIRE(fx->node(r.slot).normal_z==0.25F);
    REQUIRE(fx->node(r.slot).surface==wall);
    REQUIRE(fx->node(r.slot).model_slot==models::impact_decal);
    const EffectEvent hit{.kind=EffectEventKind::impact_decal,.tick=0,.source=EntityId{},.other=EntityId{},.variant=0U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=7.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F};
    REQUIRE(fx->consume(hit,DecalParams{.x=0.0F,.y=0.0F,.z=0.0F,.normal_x=1.0F,.normal_z=0.0F,.surface=EntityId{},.model_slot=models::impact_decal},0U)==EventMapping::spawned);
    fx->update(0U);
    const std::int16_t head=follow(*fx,fx->persistent_head(),1); // batch kept in allocation order: r, then hit
    REQUIRE(fx->node(head).x==7.0F);
    REQUIRE(fx->node(head).normal_x==-1.0F);
    const EffectEvent ricochet{.kind=EffectEventKind::ricochet_spawned,.tick=0,.source=EntityId{},.other=EntityId{},.variant=0U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=0.0F,.y=0.0F,.z=0.0F,.angle=0U,.length=0.0F,.normal_x=0.0F,.normal_z=0.0F};
    REQUIRE(fx->consume(ricochet,0U)==EventMapping::no_visual);
    return true;
}

RUNTIME_TEST(model_slot_tables) {
    REQUIRE(models::for_pickup(11U).value()==90U);
    REQUIRE(models::for_pickup(0U).value()==1U);
    REQUIRE(!models::for_pickup(models::pickup_kind_count).has_value());
    REQUIRE(models::for_wreck(1,1U).value()==0x48U);
    REQUIRE(models::for_wreck(1,0U).value()==0x23U);
    REQUIRE(!models::for_wreck(3,0U).has_value());
    REQUIRE(models::for_point_marker(2U,false).value()==0x5DU);
    REQUIRE(models::for_point_marker(0U,true).value()==0xB3U);
    return true;
}

RUNTIME_TEST(debris_emitters_count_down_and_aim) {
    DebrisEmitterState m{.crate_kind=0U,.part_count=6U,.team=2U,.x=0.0F,.z=0.0F,.timer=100};
    REQUIRE(!step_debris_emitter(m,0.0F,100.0F,40).has_value());
    REQUIRE(m.timer==60);
    const auto req=step_debris_emitter(m,0.0F,100.0F,60);
    REQUIRE(req.has_value());
    REQUIRE(m.timer==0);
    REQUIRE(req->model_slot==models::crate);
    REQUIRE(req->part_count==6U);
    REQUIRE(req->team==2U);
    REQUIRE(req->angle==49152U); // cosine -0/100 -> 16384, negated because -dz < 0
    DebrisEmitterState distant{.crate_kind=0U,.part_count=0U,.team=0U,.x=0.0F,.z=0.0F,.timer=100};
    REQUIRE(!step_distance_scaled_debris_emitter(distant,5000.0F,0.0F).has_value()); // rate 50
    REQUIRE(distant.timer==50);
    DebrisEmitterState nearby{.crate_kind=0U,.part_count=0U,.team=0U,.x=0.0F,.z=0.0F,.timer=100};
    const auto n=step_distance_scaled_debris_emitter(nearby,0.0F,0.0F); // rate 100, zero vector
    REQUIRE(n.has_value() && n->angle==0U);
    DebrisEmitterState wrap{.crate_kind=0U,.part_count=0U,.team=0U,.x=0.0F,.z=0.0F,.timer=-32768};
    REQUIRE(!step_debris_emitter(wrap,0.0F,0.0F,1).has_value());
    REQUIRE(wrap.timer==32767);
    DebrisEmitterState bad{.crate_kind=models::crate_kind_count,.part_count=0U,.team=0U,.x=0.0F,.z=0.0F,.timer=0};
    REQUIRE(!step_debris_emitter(bad,0.0F,0.0F,0).has_value());
    REQUIRE(bad.timer==0);
    return true;
}

RUNTIME_TEST(script_17_runs_into_script_18_like_retail) {
    // gEffectScripts[17] fills all 16 commands; retail continues at [18][0]
    // (0x801387BC) and stops at flat step 24.
    REQUIRE(script_step(variant::debris_landed,15U)->op==ScriptOp::spawn_ramp);
    REQUIRE(script_step(variant::debris_landed,15U)->arg0==19);
    REQUIRE(script_step(variant::debris_landed,16U)->op==ScriptOp::spawn_particle);
    REQUIRE(script_step(variant::debris_landed,16U)->arg0==12);
    REQUIRE(script_step(variant::debris_landed,24U)->op==ScriptOp::end);
    REQUIRE(script_step(variant::area_marker,static_cast<std::uint16_t>(16U*script_length))==nullptr); // past table end
    auto fx=make();
    REQUIRE(fx->spawn_script(variant::debris_landed,0.0F,0.0F,0.0F,0U).status==SpawnStatus::spawned);
    fx->update(1000U); // every wait (max 288) passes in one walk
    REQUIRE(count_kind(*fx,EffectKind::script)==0U);
    REQUIRE(count_kind(*fx,EffectKind::particle)==16U); // 8 from script 17 + 8 from script 18
    REQUIRE(count_kind(*fx,EffectKind::ramp)==2U);      // R(18), R(19)
    return true;
}

RUNTIME_TEST(oscillator_runs_on_game_ticks_with_u32_duration) {
    REQUIRE(oscillator_duration_for(60U)==600U);
    REQUIRE(oscillator_duration_for(62U)==600U);
    REQUIRE(!oscillator_duration_for(59U));
    REQUIRE(!oscillator_duration_for(63U));
    auto fx=make();
    REQUIRE(fx->spawn_particle(EffectKind::oscillator,9U,0,0,0,0U).status==SpawnStatus::unsupported_def);
    REQUIRE(fx->spawn_script(variant::area_marker,0.0F,0.0F,0.0F,0U).status==SpawnStatus::spawned);
    fx->update(2048U,10U); // W(2048) passes; three oscillators stamped with game tick 10
    REQUIRE(count_kind(*fx,EffectKind::oscillator)==3U);
    std::int16_t osc=no_link;
    for (std::int16_t i=0; i<static_cast<std::int16_t>(pool_capacity); ++i)
        if (fx->node(i).in_use && fx->node(i).kind==EffectKind::oscillator) { osc=i; break; }
    REQUIRE(fx->node(osc).start==10U);
    fx->update(40000U,610U); // effect clock is ignored; elapsed 600 == duration: keep
    REQUIRE(fx->node(osc).elapsed==600U);
    REQUIRE(fx->node(osc).cycle==0U);
    fx->update(0U,611U);     // 601 > 600: restart the sweep
    REQUIRE(fx->node(osc).cycle==1U);
    REQUIRE(fx->node(osc).start==611U);
    REQUIRE(fx->node(osc).elapsed==0U);
    REQUIRE(count_kind(*fx,EffectKind::oscillator)==3U); // never retires
    return true;
}

RUNTIME_TEST(decal_records_its_smoke_serial) {
    auto fx=make();
    const std::int16_t d=fx->spawn_decal(DecalParams{}).slot;
    const std::int16_t s=fx->node(d).child;
    REQUIRE(s!=no_link);
    REQUIRE(fx->node(d).child_serial==fx->node(s).serial);
    REQUIRE(fx->node(d).child_serial==1U); // decal took serial 0
    return true;
}

RUNTIME_TEST(serialization_starts_with_version) {
    auto fx=make();
    StateWriter w;
    fx->serialize(w);
    REQUIRE(!w.bytes().empty() && w.bytes()[0]==3U);
    return true;
}

RUNTIME_TEST(impact_decal_event_carries_normals_and_model_slot) {
    auto fx=make();
    const EffectEvent event{.kind=EffectEventKind::impact_decal,.tick=0,.source=EntityId{},.other=EntityId{4U,2U},.variant=0U,.owner=no_owner,.reason=RemovalReason{},.gameplay_index=0U,.x=12.0F,.y=3.0F,.z=-8.0F,.angle=0U,.length=0.0F,.normal_x=0.5F,.normal_z=-0.75F};
    REQUIRE(fx->consume(event,0U)==EventMapping::spawned);
    REQUIRE(count_kind(*fx,EffectKind::decal)==1U);
    std::int16_t d=no_link;
    for (std::int16_t i=0; i<static_cast<std::int16_t>(pool_capacity); ++i)
        if (fx->node(i).in_use && fx->node(i).kind==EffectKind::decal) d=i;
    REQUIRE(d!=no_link);
    REQUIRE(fx->node(d).normal_x==-0.5F);  // record stores the hit normal negated
    REQUIRE(fx->node(d).normal_z==0.75F);
    REQUIRE(fx->node(d).model_slot==models::impact_decal);
    REQUIRE(fx->node(d).surface==(EntityId{4U,2U}));
    REQUIRE(fx->node(d).x==12.0F && fx->node(d).y==3.0F && fx->node(d).z==-8.0F);
    return true;
}

RUNTIME_TEST_MAIN()
