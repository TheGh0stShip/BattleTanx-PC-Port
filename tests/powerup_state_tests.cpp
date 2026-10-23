#include "battletanx/powerup_state.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;
using namespace battletanx::powerup;

namespace {

UnitRecord tank() {
    return make_unit(retail::UnitKind::tank,0U,0U,0.0F,0.0F,0U,0,UnitControl::player);
}

} // namespace

RUNTIME_TEST(every_world_kind_has_a_source_and_effect_path) {
    for (std::uint8_t kind=0U; kind<23U; ++kind) {
        const bool counted=world_placed_kind_count[kind]!=0U ||
                           (kind<world_distributed_kind_count.size() && world_distributed_kind_count[kind]!=0U);
        if (counted) REQUIRE(pickup_kind_source(kind)==KindSource::world_item);
        if (counted && kind!=22U) {
            UnitRecord unit=tank();
            OwnerRecord owner;
            TimedPowerupState timed;
            PickupEffectOptions options;
            options.announce=false;
            const auto outcome=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=kind,.announce=false},unit,owner,timed,options);
            REQUIRE(outcome.result.status!=PickupEffectStatus::no_retail_case);
        }
    }
    REQUIRE(pickup_kind_source(11U)==KindSource::wreck_drop);
    REQUIRE(pickup_kind_source(6U)==KindSource::unused);
    REQUIRE(pickup_kind_source(18U)==KindSource::spawn_grant_only);
    return true;
}

RUNTIME_TEST(contact_eligibility) {
    ContactQuery q{.pickup_consumed=false,.contact_is_unit=true,.collector_class=1U,
                   .pickup_owner_class=-1,.unit_mode=unit_mode_player};
    REQUIRE(pickup_contact_eligible(q));
    q.unit_mode=unit_mode_computer_leader;
    REQUIRE(pickup_contact_eligible(q));
    q.unit_mode=unit_mode_follower;
    REQUIRE(!pickup_contact_eligible(q));
    q.unit_mode=0U;
    q.pickup_owner_class=1;
    REQUIRE(!pickup_contact_eligible(q));
    q.pickup_owner_class=-1;
    q.contact_is_unit=false;
    REQUIRE(!pickup_contact_eligible(q));
    q.contact_is_unit=true;
    q.pickup_consumed=true;
    REQUIRE(!pickup_contact_eligible(q));
    REQUIRE(retail_unit_mode(UnitControl::computer,false)==1U);
    REQUIRE(retail_unit_mode(UnitControl::player,true)==2U);
    return true;
}

RUNTIME_TEST(targetability_cloak_mine_carry) {
    TargetabilityInput in{.tick=1000,.value10=0,.carrying_pickup=false,.last_mine_tick=retail::never_fired_tick,
                          .cloak_end_tick=0,.last_fire_tick=retail::never_fired_tick};
    REQUIRE(unit_targetable(in));
    in.value10=1;
    REQUIRE(!unit_targetable(in));
    in.value10=0;
    in.last_mine_tick=911;                      // 1000-90 < 911
    REQUIRE(!unit_targetable(in));
    in.carrying_pickup=true;
    REQUIRE(unit_targetable(in));
    in.carrying_pickup=false;
    in.last_mine_tick=910;
    in.cloak_end_tick=1500;
    REQUIRE(!unit_targetable(in));
    in.last_fire_tick=956;                      // fired within 45 ticks
    REQUIRE(unit_targetable(in));
    in.last_fire_tick=955;
    REQUIRE(!unit_targetable(in));
    return true;
}

RUNTIME_TEST(shield_absorbs_and_depletes) {
    UnitRecord unit=tank();
    TimedPowerupState timed;
    REQUIRE(absorb_shield_damage(timed,unit,30).remaining_damage==30);
    timed.shield_live=true;
    unit.shield_strength=25;
    ShieldAbsorb r=absorb_shield_damage(timed,unit,10);
    REQUIRE(r.remaining_damage==0 && r.absorbed==10 && unit.shield_strength==15 && !r.depleted);
    r=absorb_shield_damage(timed,unit,40);
    REQUIRE(r.remaining_damage==25 && r.absorbed==15 && unit.shield_strength==0 && r.depleted);
    return true;
}

RUNTIME_TEST(unlimited_ammo_rules) {
    AmmoRules rules;
    REQUIRE(weapon_consumes_ammo(1U,0U,false,rules));
    REQUIRE(!weapon_consumes_ammo(1U,2U,false,rules));
    REQUIRE(!weapon_consumes_ammo(5U,2U,false,rules));
    rules.unlimited_ammo=true;
    REQUIRE(!weapon_consumes_ammo(1U,0U,false,rules));
    REQUIRE(weapon_consumes_ammo(4U,0U,false,rules));
    rules.unlimited_ammo=false;
    REQUIRE(!weapon_consumes_ammo(2U,0U,true,rules));
    rules.cloak_pickups=true;
    REQUIRE(!weapon_consumes_ammo(1U,0U,false,rules));
    REQUIRE(weapon_consumes_ammo(4U,2U,false,rules));
    return true;
}

RUNTIME_TEST(weight_roll_and_wrap) {
    PickupWeights w;
    RetailRandom rng(3U);
    REQUIRE(!roll_weighted_kind(w,rng).rng_drawn && rng.seed()==3U);
    w.register_permanent(2U);
    w.register_permanent(2U);
    w.register_permanent(9U);
    RetailRandom mirror(3U);
    const std::uint32_t roll=mirror.next_u32()%3U;
    const WeightRoll r=roll_weighted_kind(w,rng);
    REQUIRE(r.kind && *r.kind==(roll<2U ? 2U : 9U));
    for (int i=0; i<253; ++i) w.register_permanent(0U);
    REQUIRE(w.total==0U);                       // u8 wrap
    REQUIRE(!roll_weighted_kind(w,rng).kind);
    reset_weights(w);
    REQUIRE(w.total==0U && w.weights[2]==0U);
    return true;
}

RUNTIME_TEST(distribution_interleaves_reroll) {
    std::array<PickupPlacement,4> spots{};
    for (std::uint32_t i=0U; i<4U; ++i) spots[i]=PickupPlacement{i,static_cast<float>(i),0.0F,22U};
    std::array<std::int32_t,pickup_spawn_count_kinds> counts{};
    counts[5]=2;
    counts[0]=1;
    RetailRandom rng(11U);
    RetailRandom mirror(11U);
    const DistributionResult r=distribute_pickups_interleaved(spots,counts,0,true,rng);
    REQUIRE(r.status==DistributionStatus::placed && r.placed.size()==3U);
    std::array<bool,4> used{};
    std::uint32_t spot=0U;
    do { spot=mirror.next_u32()%4U; } while (used[spot]);
    used[spot]=true;
    REQUIRE(r.placed[0].placement_index==spot && r.placed[0].kind==0U);  // kind 0 not re-rolled
    do { spot=mirror.next_u32()%4U; } while (used[spot]);
    used[spot]=true;
    const std::uint8_t k=pick_effect_kind(5U,true,mirror);
    REQUIRE(r.placed[1].placement_index==spot && r.placed[1].kind==k);
    REQUIRE(distribute_pickups_interleaved(spots,counts,4,true,rng).status==DistributionStatus::campaign_mode);
    counts[1]=2;
    REQUIRE(distribute_pickups_interleaved(spots,counts,0,true,rng).status==DistributionStatus::not_enough_spots);
    counts.fill(0x7FFFFFFF);
    const DistributionResult big=distribute_pickups_interleaved(spots,counts,0,false,rng);
    REQUIRE(big.status==DistributionStatus::not_enough_spots && big.total==22LL*0x7FFFFFFFLL);
    counts.fill(0);
    counts[3]=-1;
    REQUIRE(distribute_pickups_interleaved(spots,counts,0,false,rng).status==DistributionStatus::negative_count);
    return true;
}

RUNTIME_TEST(lifecycle_per_game_mode_and_wreck) {
    PickupOptions options;
    PickupRecord placed;
    for (std::int32_t mode : {0,1,2,7,8,9}) {
        options.game_mode=mode;
        REQUIRE(pickup_lifecycle(placed,options)==PickupLifecycle::respawns);
    }
    for (std::int32_t mode : {3,4,5,6}) {
        options.game_mode=mode;
        REQUIRE(pickup_lifecycle(placed,options)==PickupLifecycle::removed_on_collect);
    }
    options.game_mode=0;
    options.respawn_enabled=false;
    REQUIRE(pickup_lifecycle(placed,options)==PickupLifecycle::removed_on_collect);
    const auto drop=make_wreck_drop(EntityId{5U,1U},1.0F,2.0F,100,3U,options);
    REQUIRE(drop && drop->kind==11U && drop->expire_tick==310 && drop->owner_class==3);
    REQUIRE(pickup_lifecycle(*drop,options)==PickupLifecycle::timed);
    options.extra_pickups_enabled=false;
    REQUIRE(!make_wreck_drop(EntityId{5U,1U},1.0F,2.0F,100,3U,options));
    return true;
}

RUNTIME_TEST(effect_request_deltas) {
    UnitRecord unit=tank();
    unit.health=50;
    OwnerRecord owner;
    owner.present=true;
    owner.score=49950;
    TimedPowerupState timed;
    PickupEffectOptions options;
    options.game_mode=5;
    EffectOutcome o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=11U,.announce=true},unit,owner,timed,options);
    REQUIRE(o.unit.health==20 && o.unit.ammo[1]==15 && o.play_sound);
    REQUIRE(o.owner.score==100 && o.owner.lives==1U && o.owner.respawn_bank0==1 && o.owner.timed_event==22);
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=8U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.unit.shield==100 && o.unit.shield_created && !o.play_sound && o.owner.score==0);
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=4U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.unit.flags_set==0x80U);
    // Campaign kind 12: the cloak option is cleared by run_campaign_waves, so it stocks.
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=12U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.unit.ammo[15]==1 && o.unit.secondary_selected && *o.unit.secondary_selected==15U);
    options.cloak_pickups=true;
    options.game_mode=0;
    options.tick=40;
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=12U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.unit.cloak_end_tick_delta==940 && o.unit.cloak_flash_set==240);
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=12U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.unit.cloak_end_tick_delta==900);  // stacks
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=14U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.owner.score==900 && o.owner.lives==0U);  // mode 0: no life
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=13U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.owner.lives==1U && o.owner.respawn_bank0==1);
    options.game_mode=7;
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=13U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.owner.lives==1U && o.owner.respawn_bank0==0);
    owner.computer=true;
    o=apply_effect_request(EffectRequest{.pickup=EntityId{},.collector=EntityId{},.kind=13U,.announce=false},unit,owner,timed,options);
    REQUIRE(o.owner.lives==0U);
    return true;
}

RUNTIME_TEST(collect_with_effects_order_and_events) {
    UnitRecord a=tank();
    UnitRecord b=tank();
    OwnerRecord oa, ob;
    TimedPowerupState ta, tb;
    std::array<PickupRecord,1> pickups{};
    pickups[0].id=EntityId{9U,1U};
    pickups[0].kind=14U;
    std::array<Collector,2> collectors{
        Collector{EntityId{1U,1U},&a,&oa,&ta,0U,unit_mode_follower},
        Collector{EntityId{2U,1U},&b,&ob,&tb,1U,unit_mode_player}};
    GameplayEventQueue events;
    SoundEventQueue sounds;
    PickupEffectOptions options;
    options.tick=12;
    const auto out=collect_with_effects(pickups,collectors,options,events,sounds);
    REQUIRE(out.size()==1U && out[0].request.collector==(EntityId{2U,1U}));
    REQUIRE(ob.score==1000 && oa.score==0);    // 100 announce + 900
    REQUIRE(pickups[0].consumed && events.events().size()==1U && sounds.events().size()==1U);
    REQUIRE(events.events()[0].tick==12 && events.events()[0].value==14);
    return true;
}

RUNTIME_TEST_MAIN()
