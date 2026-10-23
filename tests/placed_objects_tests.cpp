#include "battletanx/placed_objects.hpp"
#include "battletanx/runtime_math.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <initializer_list>
#include <vector>

using namespace battletanx;
using namespace battletanx::placed;

namespace {

struct Fixture {
    AngleTable angles{reference_angle_table()};
    RetailRandom rng{7U};
    std::array<TurretOwnerConfig,4> configs{};
    std::array<std::uint8_t,4> effect_counts{1U,1U,1U,1U};
    std::vector<UnitRecord> units;
    std::vector<TurretTarget> targets;
    std::vector<OwnerRecord> owners;
    World world;

    Fixture() {
        for (TurretOwnerConfig& config : configs) {
            config.fire_delay={10,10,10,10};
            config.effect_kind={2,2,2,2};
        }
        owners.resize(4U);
        for (std::size_t i=0U; i<owners.size(); ++i) {
            owners[i].present=true;
            owners[i].team=static_cast<std::uint8_t>(i<2U ? 0U : 1U);
        }
    }
    void add_unit(std::uint8_t team,float x,float z) {
        UnitRecord unit{};
        unit.team=team;
        unit.alive=true;
        unit.x=x;
        unit.z=z;
        units.push_back(unit);
    }
    UpdateContext context(std::int32_t tick) {
        targets.clear();
        for (const UnitRecord& unit : units) targets.push_back(TurretTarget{.unit=&unit,.targetable=true,.ai_state_two=false});
        return UpdateContext{.angles=&angles,.targets=targets,.owner_configs=configs,
                             .owner_effect_counts=effect_counts,.tick=tick,.frame_delta=1.0F,
                             .search_range=turret_search_range,.rng=&rng,.area_clear={},.line_of_sight={}};
    }
    std::vector<std::int32_t> run(std::int32_t from,std::int32_t to) {
        std::vector<std::int32_t> shot_ticks;
        for (std::int32_t tick=from; tick<to; ++tick) {
            world.clear_events();
            world.update(context(tick));
            for (const Event& e : world.events()) {
                if (e.kind==EventKind::shot_fired) shot_ticks.push_back(tick);
            }
        }
        return shot_ticks;
    }
};

bool has_event(const World& world,EventKind kind,std::int32_t value) {
    for (const Event& e : world.events()) {
        if (e.kind==kind && e.value==value) return true;
    }
    return false;
}

} // namespace

RUNTIME_TEST(placed_turret_subtypes_use_retail_tables) {
    Fixture f;
    const std::array<std::int16_t,4> health{25,50,100,100};
    for (std::uint8_t type=0U; type<4U; ++type) {
        const auto id=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=type,.heading=0U,
                                                      .x=type*1000.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0));
        REQUIRE(id);
        const Entry* entry=f.world.get(*id);
        REQUIRE(entry!=nullptr && entry->kind==Kind::turret);
        REQUIRE(entry->turret.health==health[type]);
        REQUIRE(entry->serial==type);
        REQUIRE(runtime_test::near(entry->bounds.max_x-entry->bounds.min_x,2.0F*turret_scale[type]*45.0F));
    }
    REQUIRE(!f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=4U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0)));
    REQUIRE(f.world.next_serial()==4U);
    return true;
}

RUNTIME_TEST(placed_turret_area_check_blocks_spawn) {
    Fixture f;
    UpdateContext context=f.context(0);
    context.area_clear=[](float,float,float) { return false; };
    REQUIRE(!f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=true,.scaled=false},context));
    REQUIRE(f.world.size()==0U);
    return true;
}

RUNTIME_TEST(placed_turret_ignores_friends_and_fires_on_enemies) {
    Fixture f;
    f.add_unit(1U,0.0F,500.0F);   // friend
    const auto id=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0));
    REQUIRE(id);
    REQUIRE(f.world.get(*id)->turret.target==no_turret_target);
    REQUIRE(f.run(1,80).empty());

    Fixture g;
    g.add_unit(1U,0.0F,500.0F);
    g.add_unit(0U,0.0F,600.0F);   // enemy, index 1
    const auto enemy_turret=g.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},g.context(0));
    REQUIRE(enemy_turret);
    REQUIRE(g.world.get(*enemy_turret)->turret.target==1U);
    REQUIRE(!g.run(1,80).empty());
    return true;
}

RUNTIME_TEST(placed_turret_respects_cooldown) {
    Fixture f;
    f.add_unit(0U,0.0F,600.0F);
    REQUIRE(f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0)));
    const std::vector<std::int32_t> shots=f.run(1,200);
    REQUIRE(shots.size()>=2U);
    for (std::size_t i=1U; i<shots.size(); ++i) REQUIRE(shots[i]-shots[i-1U]>10);
    return true;
}

RUNTIME_TEST(placed_turret_stops_on_target_loss) {
    Fixture f;
    f.add_unit(0U,0.0F,600.0F);
    REQUIRE(f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0)));
    REQUIRE(!f.run(1,80).empty());
    f.units[0].alive=false;
    REQUIRE(f.run(80,200).empty());
    return true;
}

RUNTIME_TEST(placed_turret_destruction_scores_enemy_only) {
    Fixture f;
    const auto id=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=0U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0));
    REQUIRE(id);
    // Friendly owner 3 (team 1): no score.
    REQUIRE(f.world.hit(*id,Hit{.source_kind=2U,.damage=30,.source_owner=3U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,0,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::destroyed,0));
    REQUIRE(!has_event(f.world,EventKind::score_awarded,100));

    const auto other=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=0U,.heading=0U,.x=2000.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0));
    REQUIRE(other);
    f.world.clear_events();
    REQUIRE(f.world.hit(*other,Hit{.source_kind=12U,.damage=30,.source_owner=0U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,0,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::score_awarded,100));
    REQUIRE(has_event(f.world,EventKind::explosion,turret_explosion_variant));
    REQUIRE(f.owners[0].score==100);

    f.run(1,40);   // fade-in (0xFF, -16 per tick) precedes the health check
    REQUIRE(f.world.size()==0U);
    REQUIRE(f.world.hit(*id,Hit{.source_kind=2U,.damage=1,.source_owner=0xFFU,.heading=0U,.shielded=false,.flame_forced=false},f.owners,0,0)==MessageResult::stale);
    return true;
}

RUNTIME_TEST(placed_turret_return_fire_and_query) {
    Fixture f;
    const auto id=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=2U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0));
    REQUIRE(id);
    REQUIRE(f.world.hit(*id,Hit{.source_kind=2U,.damage=5,.source_owner=0U,.heading=0x1000U,.shielded=false,.flame_forced=false},f.owners,9,0)==MessageResult::handled);
    REQUIRE(f.world.get(*id)->turret.aim_time==9);
    REQUIRE(f.world.get(*id)->turret.aim_heading==0x9000U);
    REQUIRE(f.world.answers_query(*id,6U));
    REQUIRE(f.world.answers_query(*id,37U));
    REQUIRE(!f.world.answers_query(*id,5U));
    return true;
}

RUNTIME_TEST(placed_turret_blast_falloff_and_rebuild) {
    static_assert(turret_blast_policy);   // exercises the TEMPORARY POLICY path
    Fixture f;
    const auto id=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=2U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=true},f.context(0));
    REQUIRE(id);
    REQUIRE(f.world.blast(*id,0.0F,8000.0F,f.owners,0,f.angles)==MessageResult::handled);
    REQUIRE(f.world.get(*id)->turret.health==50);
    REQUIRE(f.world.blast(*id,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::handled);
    REQUIRE(f.world.get(*id)->turret.health==0);
    f.run(1,40);
    REQUIRE(f.world.size()==1U);
    REQUIRE((f.world.get(*id)->turret.flags&turret_rebuilding)!=0U);
    return true;
}

RUNTIME_TEST(placed_staged_object_changes_stage) {
    Fixture f;
    const PlacedObjectRecord record=make_placed_object(PlacedObjectClass::staged,5U,0U,3U,10.0F,20.0F);
    const auto id=f.world.spawn_placed(record,rotated_bounds(10.0F,20.0F,50.0F,30.0F,0x4000U,f.angles));
    REQUIRE(id);
    REQUIRE(f.world.hit(*id,Hit{.source_kind=2U,.damage=0x11,.source_owner=0xFFU,.heading=0U,.shielded=false,.flame_forced=false},f.owners,0,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::stage_lost,0));
    f.run(0,placed_collapse_ticks);
    const std::vector<Snapshot> snapshots=f.world.snapshots();
    REQUIRE(snapshots.size()==1U);
    REQUIRE(snapshots[0].stage==1U);
    REQUIRE(snapshots[0].stage_count==3U);
    REQUIRE(!snapshots[0].destroyed);
    REQUIRE(runtime_test::near(snapshots[0].bounds.max_x-snapshots[0].bounds.min_x,60.0F,0.5));
    return true;
}

RUNTIME_TEST(placed_knockable_and_crushable) {
    Fixture f;
    const auto knock=f.world.spawn_placed(make_placed_object(PlacedObjectClass::knockable,3U,0U,1U,0.0F,0.0F),Bounds{});
    const auto crush=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,0.0F,0.0F),Bounds{});
    REQUIRE(knock && crush);
    REQUIRE(f.world.blast(*knock,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::handled);
    REQUIRE(f.world.get(*knock)->object.knocked);
    REQUIRE(f.world.hit(*crush,Hit{.source_kind=44U,.damage=1,.source_owner=0xFFU,.heading=0U,.shielded=false,.flame_forced=false},f.owners,0,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::flattened,0));
    // Flattened (flag 4) objects ignore message 3.
    REQUIRE(f.world.blast(*crush,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::ignored);
    return true;
}

RUNTIME_TEST(placed_cosine_to_angle_matches_table) {
    const AngleTable angles=reference_angle_table();
    REQUIRE(retail_cosine_to_angle(1.0F,angles)==0U);
    REQUIRE(retail_cosine_to_angle(-1.0F,angles)==0x8000U);
    const std::uint16_t quarter=retail_cosine_to_angle(0.0F,angles);
    REQUIRE(quarter>=0x3F00U && quarter<=0x4100U);
    const auto right=fling_heading(10.0F,0.0F,0.0F,0.0F,angles);
    const auto left=fling_heading(-10.0F,0.0F,0.0F,0.0F,angles);
    REQUIRE(right && left);
    REQUIRE(*right==0U && *left==0x8000U);   // dz==0: sign branch not taken
    const auto north=fling_heading(0.0F,10.0F,0.0F,0.0F,angles);
    const auto south=fling_heading(0.0F,-10.0F,0.0F,0.0F,angles);
    REQUIRE(north && south);
    REQUIRE(*north>=0x3F00U && *north<=0x4100U);
    REQUIRE(*south>=0xBF00U && *south<=0xC100U);
    REQUIRE(static_cast<std::uint16_t>(*north+*south)==0U);
    REQUIRE(!fling_heading(3.0F,4.0F,3.0F,4.0F,angles));
    return true;
}

RUNTIME_TEST(placed_breakable_unit_contact_replies_scrape) {
    Fixture f;
    const auto id=f.world.spawn_breakable(BreakableRecord{.flags=0U,.debris=0U,.heading=0U,.x=5,.z=6,.released=false},Bounds{});
    REQUIRE(id);
    Hit contact{};
    contact.source_kind=0U;
    contact.source_owner=1U;
    REQUIRE(f.world.hit(*id,contact,f.owners,0,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::scrape)));
    return true;
}

RUNTIME_TEST(placed_staged_blast_drops_one_stage) {
    Fixture f;
    const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::staged,5U,0U,3U,0.0F,0.0F),Bounds{});
    REQUIRE(id);
    REQUIRE(f.world.blast(*id,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::handled);
    REQUIRE(f.world.get(*id)->object.stage==0U);   // released; stage moves on the update
    REQUIRE((f.world.get(*id)->object.flags&staged_blasted)!=0U);
    REQUIRE((f.world.get(*id)->object.flags&0x80U)==0U);
    f.run(0,1);
    REQUIRE(f.world.get(*id)->object.stage==1U);
    REQUIRE(f.world.direct_damage(*id,5,0.0F,0.0F,f.angles)==MessageResult::handled);
    f.run(1,2);
    REQUIRE(f.world.get(*id)->object.stage==2U);
    REQUIRE(has_event(f.world,EventKind::destroyed,2));
    REQUIRE(f.world.blast(*id,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::ignored);
    return true;
}

RUNTIME_TEST(placed_staged_animated_blast_collapses_without_0x80) {
    // Flag 1 set, health > 0: 60-tick collapse, one stage, flags &= 0x6F.
    Fixture f;
    PlacedObjectRecord object=make_placed_object(PlacedObjectClass::staged,5U,1U,4U,0.0F,0.0F);
    object.health=10;
    const auto id=f.world.spawn_placed(object,Bounds{});
    REQUIRE(id);
    REQUIRE(f.world.blast(*id,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::handled);
    REQUIRE((f.world.get(*id)->object.flags&0x80U)==0U);
    f.run(0,placed_collapse_ticks-1);
    REQUIRE(f.world.get(*id)->object.stage==0U);
    f.run(placed_collapse_ticks-1,placed_collapse_ticks);
    REQUIRE(f.world.get(*id)->object.stage==1U);
    REQUIRE((f.world.get(*id)->object.flags&staged_blasted)==0U);   // cleared by 0x6F
    REQUIRE(!f.world.get(*id)->object.updating);
    return true;
}

RUNTIME_TEST(placed_crushable_blast_flings_debris) {
    Fixture f;
    const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,100.0F,0.0F),Bounds{});
    REQUIRE(id);
    REQUIRE(f.world.blast(*id,0.0F,0.0F,f.owners,0,f.angles)==MessageResult::handled);
    const std::uint16_t heading=f.world.get(*id)->crushable.heading;
    f.world.clear_events();
    f.world.update(f.context(1));
    REQUIRE(has_event(f.world,EventKind::debris,heading));
    REQUIRE(f.world.size()==0U);
    return true;
}

RUNTIME_TEST(placed_crushable_unit_contact) {
    Fixture f;
    const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,0.0F,0.0F),Bounds{});
    REQUIRE(id);
    for (std::uint16_t i=0U; i<crushable_shake_ticks; ++i) {
        REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,i,0)==MessageResult::handled);
    }
    REQUIRE(has_event(f.world,EventKind::collision_cleared,crushable_motion_flag));
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,50,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::crush)));
    REQUIRE(has_event(f.world,EventKind::flattened,0));
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,55,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::pass)));
    return true;
}

RUNTIME_TEST(placed_event_queue_is_capped_and_serialized) {
    Fixture f;
    const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,0.0F,0.0F),Bounds{});
    REQUIRE(id);
    StateWriter pending;
    f.world.serialize(pending);
    f.world.clear_events();
    StateWriter cleared;
    f.world.serialize(cleared);
    REQUIRE(pending.hex()!=cleared.hex());
    for (std::int32_t tick=0; tick<static_cast<std::int32_t>(max_pending_events)+16; ++tick) {
        f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,tick,0);
    }
    REQUIRE(f.world.events().size()==max_pending_events);
    REQUIRE(f.world.events_overflowed());
    f.world.clear_events();
    REQUIRE(!f.world.events_overflowed());
    return true;
}

RUNTIME_TEST(placed_inactive_entries_are_inhibited) {
    Fixture f;
    f.add_unit(0U,0.0F,600.0F);
    const auto id=f.world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f.context(0));
    REQUIRE(id && f.world.set_active(*id,false));
    REQUIRE(f.run(1,80).empty());
    REQUIRE(f.world.hit(*id,Hit{.source_kind=2U,.damage=100,.source_owner=0xFFU,.heading=0U,.shielded=false,.flame_forced=false},f.owners,0,0)==MessageResult::inactive);
    return true;
}

RUNTIME_TEST(placed_world_serialization_is_deterministic) {
    Fixture a;
    Fixture b;
    for (Fixture* f : {&a,&b}) {
        f->add_unit(0U,0.0F,600.0F);
        f->world.spawn_turret(TurretSpawn{.team=1U,.owner=2U,.type=3U,.heading=0U,.x=0.0F,.z=0.0F,.check_area=false,.scaled=false},f->context(0));
        f->world.spawn_breakable(BreakableRecord{.flags=0U,.debris=0U,.heading=0U,.x=5,.z=6,.released=false},Bounds{});
        f->run(1,40);
    }
    StateWriter wa;
    StateWriter wb;
    a.world.serialize(wa);
    b.world.serialize(wb);
    REQUIRE(wa.hex()==wb.hex());
    a.world.hit(EntityId{0U,1U},Hit{.source_kind=2U,.damage=1,.source_owner=0U,.heading=0U,.shielded=false,.flame_forced=false},a.owners,40,0);
    StateWriter wc;
    a.world.serialize(wc);
    REQUIRE(wc.hex()!=wb.hex());
    return true;
}

RUNTIME_TEST(placed_crushable_contact_sequence_shakes_then_crushes) {
    // pulse_effect_handle_message source kind 0: ten shakes, then replies.
    Fixture f;
    const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,0.0F,0.0F),Bounds{});
    REQUIRE(id);
    for (std::uint16_t i=0U; i<crushable_shake_ticks; ++i) {
        f.world.clear_events();
        REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,i,0)==MessageResult::handled);
        const std::vector<Event>& events=f.world.events();
        const bool last=i+1U==crushable_shake_ticks;
        REQUIRE(events.size()==(last ? 3U : 2U));
        REQUIRE(events[0].kind==EventKind::unit_shaken);
        REQUIRE(events[0].owner==1U);
        // shake counts down from 10: even -> +2, odd -> -2.
        REQUIRE(events[0].value==((i&1U)==0U ? 2 : -2));
        if (last) {
            REQUIRE(events[1].kind==EventKind::collision_cleared);
            REQUIRE(events[1].value==crushable_motion_flag);
        }
        REQUIRE(events.back().kind==EventKind::contact_reply);
        REQUIRE(events.back().value==static_cast<std::int32_t>(ContactReply::none));
        REQUIRE(f.world.get(*id)->crushable.shake==crushable_shake_ticks-i-1U);
    }
    // Stage 0: crush (5) and flatten once.
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,50,0)==MessageResult::handled);
    REQUIRE(!has_event(f.world,EventKind::unit_shaken,2) && !has_event(f.world,EventKind::unit_shaken,-2));
    REQUIRE(has_event(f.world,EventKind::flattened,0));
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::crush)));
    REQUIRE(f.world.get(*id)->crushable.last_contact==50);
    REQUIRE((f.world.get(*id)->object.flags&crushable_flattened)!=0U);
    // Within 10 ticks (50+10<60 is false): pass, last contact unchanged.
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,60,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::pass)));
    REQUIRE(f.world.get(*id)->crushable.last_contact==50);
    // Tick 61: crush again, no second flattened event.
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=1U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,61,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::crush)));
    REQUIRE(!has_event(f.world,EventKind::flattened,0));
    return true;
}

RUNTIME_TEST(placed_crushable_contact_scrapes_at_later_stage) {
    Fixture f;
    PlacedObjectRecord object=make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,0.0F,0.0F);
    object.stage=1U;
    const auto id=f.world.spawn_placed(object,Bounds{});
    REQUIRE(id);
    for (std::uint16_t i=0U; i<crushable_shake_ticks; ++i) {
        REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=2U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,i,0)==MessageResult::handled);
    }
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=2U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,20,0)==MessageResult::handled);
    REQUIRE(f.world.events().size()==1U);
    REQUIRE(f.world.events()[0].kind==EventKind::contact_reply);
    REQUIRE(f.world.events()[0].owner==2U);
    REQUIRE(f.world.events()[0].value==static_cast<std::int32_t>(ContactReply::scrape));
    REQUIRE((f.world.get(*id)->object.flags&crushable_flattened)==0U);
    REQUIRE(!f.world.get(*id)->crushable.released);
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=2U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,25,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::pass)));
    f.world.clear_events();
    REQUIRE(f.world.hit(*id,Hit{.source_kind=0U,.damage=0,.source_owner=2U,.heading=0U,.shielded=false,.flame_forced=false},f.owners,31,0)==MessageResult::handled);
    REQUIRE(has_event(f.world,EventKind::contact_reply,static_cast<std::int32_t>(ContactReply::scrape)));
    // Not released: an update leaves it in place.
    f.world.update(f.context(32));
    REQUIRE(f.world.size()==1U);
    return true;
}

RUNTIME_TEST(placed_crushable_messages_3_and_7_fling_then_remove) {
    for (int message=0; message<2; ++message) {
        Fixture f;
        const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,100.0F,0.0F),Bounds{});
        REQUIRE(id);
        const MessageResult result=message==0 ? f.world.blast(*id,0.0F,0.0F,f.owners,0,f.angles)
                                              : f.world.direct_damage(*id,5,0.0F,0.0F,f.angles);
        REQUIRE(result==MessageResult::handled);
        const Entry* entry=f.world.get(*id);
        REQUIRE(entry!=nullptr);
        REQUIRE((entry->object.flags&crushable_blast_flags)==crushable_blast_flags);
        REQUIRE((entry->object.flags&crushable_oriented)!=0U);
        REQUIRE(entry->crushable.released);
        const auto expected=fling_heading(100.0F,0.0F,0.0F,0.0F,f.angles);
        REQUIRE(expected && *expected==0U && entry->crushable.heading==*expected);
        const std::int32_t heading=entry->crushable.heading;
        REQUIRE(f.world.size()==1U);   // removal waits for the update
        f.world.clear_events();
        f.world.update(f.context(1));
        REQUIRE(f.world.events().size()==2U);
        REQUIRE(f.world.events()[0].kind==EventKind::debris);
        REQUIRE(f.world.events()[0].value==heading);
        REQUIRE(f.world.events()[1].kind==EventKind::removed);
        REQUIRE(f.world.size()==0U);
        REQUIRE(f.world.get(*id)==nullptr);
    }
    return true;
}

RUNTIME_TEST(placed_crushable_fling_from_own_position_is_unoriented) {
    for (int message=0; message<2; ++message) {
        Fixture f;
        const auto id=f.world.spawn_placed(make_placed_object(PlacedObjectClass::crushable,6U,0U,2U,40.0F,40.0F),Bounds{});
        REQUIRE(id);
        const MessageResult result=message==0 ? f.world.blast(*id,40.0F,40.0F,f.owners,0,f.angles)
                                              : f.world.direct_damage(*id,5,40.0F,40.0F,f.angles);
        REQUIRE(result==MessageResult::handled);
        REQUIRE((f.world.get(*id)->object.flags&crushable_oriented)==0U);
        f.world.clear_events();
        f.world.update(f.context(1));
        REQUIRE(has_event(f.world,EventKind::debris,-1));
        REQUIRE(has_event(f.world,EventKind::removed,0));
        REQUIRE(f.world.size()==0U);
    }
    return true;
}

RUNTIME_TEST_MAIN()
