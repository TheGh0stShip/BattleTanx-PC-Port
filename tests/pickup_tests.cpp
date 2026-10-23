#include "battletanx/pickup_runtime.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;

namespace {

UnitRecord tank_at(float x,float z) {
    return make_unit(retail::UnitKind::tank,0U,0U,x,z,0U,0,UnitControl::player);
}

PickupRecord pickup_at(std::uint8_t kind,float x,float z) {
    PickupRecord pickup;
    pickup.id=EntityId{3U,1U};
    pickup.kind=kind;
    pickup.x=x;
    pickup.z=z;
    return pickup;
}

struct Fixture {
    GameplayEventQueue events;
    SoundEventQueue sounds;
    PickupOptions options;
};

PickupCollector collector_for(UnitRecord& unit,std::uint8_t owner_class=0U,
                              std::uint8_t state=0U) {
    return PickupCollector{EntityId{1U,1U},&unit,owner_class,state};
}

} // namespace

RUNTIME_TEST(pickup_collected_with_events) {
    Fixture f;
    UnitRecord unit=tank_at(0.0F,0.0F);
    unit.health=50;
    std::array<PickupRecord,1> pickups{pickup_at(0U,30.0F,0.0F)};
    std::array<PickupCollector,1> collectors{collector_for(unit)};
    const auto result=collect_pickups(pickups,collectors,77,f.options,f.events,f.sounds);
    REQUIRE(result.collected==1U);
    REQUIRE(pickups[0].consumed);
    REQUIRE(!pickups[0].active);
    REQUIRE(unit.health==80);
    REQUIRE(f.sounds.events().size()==1U);
    REQUIRE(f.sounds.events()[0].sound_id==0x2BU);
    REQUIRE(f.events.events().size()==1U);
    REQUIRE(f.events.events()[0].kind==GameplayEventKind::pickup_collected);
    REQUIRE(f.events.events()[0].value==0);
    REQUIRE(f.events.events()[0].tick==77);
    REQUIRE(f.events.events()[0].subject==(EntityId{3U,1U}));
    return true;
}

RUNTIME_TEST(pickup_out_of_range) {
    Fixture f;
    UnitRecord unit=tank_at(0.0F,0.0F);
    // Tank footprint x -35..35: the pickup box starts at 400-35.
    std::array<PickupRecord,1> pickups{pickup_at(0U,400.0F,0.0F)};
    std::array<PickupCollector,1> collectors{collector_for(unit)};
    REQUIRE(collect_pickups(pickups,collectors,1,f.options,f.events,f.sounds).collected==0U);
    REQUIRE(!pickup_overlaps(pickup_at(0U,70.5F,0.0F),unit));
    REQUIRE(pickup_overlaps(pickup_at(0U,69.9F,0.0F),unit));
    REQUIRE(f.events.events().empty());
    return true;
}

RUNTIME_TEST(pickup_inactive_or_consumed) {
    Fixture f;
    UnitRecord unit=tank_at(0.0F,0.0F);
    std::array<PickupRecord,2> pickups{pickup_at(0U,0.0F,0.0F),pickup_at(0U,0.0F,0.0F)};
    pickups[0].active=false;
    pickups[1].consumed=true;
    std::array<PickupCollector,1> collectors{collector_for(unit)};
    REQUIRE(collect_pickups(pickups,collectors,1,f.options,f.events,f.sounds).collected==0U);
    return true;
}

RUNTIME_TEST(pickup_health_clamp) {
    PickupOptions options;
    UnitRecord unit=tank_at(0.0F,0.0F);
    unit.health=90;
    REQUIRE(apply_pickup_effect(unit,0U,options));
    REQUIRE(unit.health==100);
    unit.health=100;
    REQUIRE(apply_pickup_effect(unit,0U,options));
    REQUIRE(unit.health==100);
    unit.health=95;
    REQUIRE(apply_pickup_effect(unit,11U,options));
    REQUIRE(unit.health==100);
    UnitRecord bike=make_unit(retail::UnitKind::bike,0U,0U,0.0F,0.0F,0U,0,UnitControl::player);
    bike.health=1;
    REQUIRE(apply_pickup_effect(bike,0U,options));
    REQUIRE(bike.health==10);  // (int)(30*0.3f) = 9
    return true;
}

RUNTIME_TEST(pickup_ammo_add) {
    PickupOptions options;
    UnitRecord unit=tank_at(0.0F,0.0F);       // weapon 1, 70 rounds
    REQUIRE(apply_pickup_effect(unit,5U,options));
    REQUIRE(unit.ammo[1]==100);
    unit.primary_weapon.kind=2U;
    REQUIRE(apply_pickup_effect(unit,5U,options));
    REQUIRE(unit.ammo[2]==70);
    unit.health=50;
    REQUIRE(apply_pickup_effect(unit,11U,options));
    REQUIRE(unit.health==70);
    REQUIRE(unit.ammo[2]==105);
    unit.primary_weapon.kind=3U;
    REQUIRE(apply_pickup_effect(unit,11U,options));
    REQUIRE(unit.ammo[3]==15);
    options.ammo_with_crate_disabled=true;
    REQUIRE(apply_pickup_effect(unit,11U,options));
    REQUIRE(unit.ammo[3]==15);
    return true;
}

RUNTIME_TEST(pickup_eligibility_rules) {
    Fixture f;
    UnitRecord unit=tank_at(0.0F,0.0F);
    unit.health=50;
    std::array<PickupRecord,1> dropped{pickup_at(11U,0.0F,0.0F)};
    dropped[0].owner_class=2;                  // dropped by owner class 2
    std::array<PickupCollector,1> same_class{collector_for(unit,2U)};
    REQUIRE(collect_pickups(dropped,same_class,1,f.options,f.events,f.sounds).collected==0U);
    std::array<PickupCollector,1> ai_state{collector_for(unit,0U,2U)};
    REQUIRE(collect_pickups(dropped,ai_state,1,f.options,f.events,f.sounds).collected==0U);
    std::array<PickupCollector,1> other{collector_for(unit,1U,1U)};
    REQUIRE(collect_pickups(dropped,other,1,f.options,f.events,f.sounds).collected==1U);
    REQUIRE(unit.health==70);
    return true;
}

RUNTIME_TEST(pickup_unsupported_kind_reported) {
    // 13 and 14 need the owner record; 6 has no retail case.
    for (const std::uint8_t kind : {std::uint8_t{6U},std::uint8_t{13U},std::uint8_t{14U}}) {
        Fixture f;
        UnitRecord unit=tank_at(0.0F,0.0F);
        const UnitRecord before=unit;
        std::array<PickupRecord,1> pickups{pickup_at(kind,0.0F,0.0F)};
        std::array<PickupCollector,1> collectors{collector_for(unit)};
        const auto result=collect_pickups(pickups,collectors,1,f.options,f.events,f.sounds);
        REQUIRE(result.collected==1U);
        REQUIRE(result.unsupported==1U);
        REQUIRE(f.events.events()[0].value==(kind|pickup_unsupported_flag));
        REQUIRE(unit.health==before.health);
        REQUIRE(unit.ammo==before.ammo);
    }
    return true;
}

RUNTIME_TEST(pickup_unit_only_kinds_now_applied) {
    PickupOptions options;
    UnitRecord unit=tank_at(0.0F,0.0F);
    REQUIRE(apply_pickup_effect(unit,7U,options));
    REQUIRE(unit.ammo[11]==5 && unit.secondary_weapon.kind==11U);
    REQUIRE(apply_pickup_effect(unit,4U,options));
    REQUIRE((unit.flags&0x80U)!=0U);
    REQUIRE(apply_pickup_effect(unit,8U,options));
    REQUIRE(unit.shield_strength==100);
    REQUIRE(apply_pickup_effect(unit,18U,options));
    REQUIRE(unit.shield_strength==125);         // positive strength = live link
    REQUIRE(apply_pickup_effect(unit,12U,options));
    REQUIRE(unit.ammo[15]==1);                  // no cloak option here
    unit.health=unit.max_health;
    REQUIRE(apply_pickup_effect(unit,0U,options));
    REQUIRE(unit.ammo[16]==1);                  // stored pack at full health
    REQUIRE(!apply_pickup_effect(unit,13U,options));
    REQUIRE(!apply_pickup_effect(unit,22U,options));
    return true;
}

RUNTIME_TEST(pickup_collector_major_order) {
    Fixture f;
    UnitRecord first=tank_at(0.0F,0.0F);
    UnitRecord second=tank_at(0.0F,0.0F);
    first.health=10;
    second.health=10;
    std::array<PickupRecord,2> pickups{pickup_at(0U,0.0F,0.0F),pickup_at(0U,0.0F,0.0F)};
    pickups[1].id=EntityId{4U,1U};
    std::array<PickupCollector,2> collectors{collector_for(first),collector_for(second)};
    collectors[1].id=EntityId{2U,1U};
    REQUIRE(collect_pickups(pickups,collectors,1,f.options,f.events,f.sounds).collected==2U);
    // The first unit's update takes both overlapping pickups.
    REQUIRE(first.health==70 && second.health==10);
    REQUIRE(f.events.events()[0].other==(EntityId{1U,1U}));
    REQUIRE(f.events.events()[1].other==(EntityId{1U,1U}));
    return true;
}

RUNTIME_TEST(pickup_zero_weight_total_keeps_kind) {
    PickupOptions options;
    std::array<PickupRecord,1> pickups{pickup_at(9U,0.0F,0.0F)};
    pickups[0].active=false;
    pickups[0].respawn_tick=5;
    PickupWeights empty;
    RetailRandom rng(7U);
    update_pickups(pickups,6,options,rng,&empty);
    REQUIRE(pickups[0].active && pickups[0].kind==9U);
    REQUIRE(rng.seed()==7U);
    return true;
}

RUNTIME_TEST(pickup_respawn_timing_fixed_seed) {
    PickupOptions options;
    options.game_mode=0;
    std::array<PickupRecord,1> pickups{pickup_at(0U,0.0F,0.0F)};
    pickups[0].consumed=true;
    pickups[0].active=false;
    RetailRandom rng(0xBEEFU);
    RetailRandom mirror(0xBEEFU);
    const std::int32_t expected=1000+static_cast<std::int32_t>(mirror.next_u32()%3600U)+1800;
    update_pickups(pickups,1000,options,rng,nullptr);
    REQUIRE(!pickups[0].removed);
    REQUIRE(!pickups[0].consumed);
    REQUIRE(pickups[0].respawn_tick==expected);
    REQUIRE(expected>=2800 && expected<6400);
    update_pickups(pickups,expected,options,rng,nullptr);
    REQUIRE(!pickups[0].active);               // strict compare
    PickupWeights weights;
    weights.register_permanent(4U);
    weights.register_permanent(1U);            // kind 1 never weighted
    REQUIRE(weights.total==1U);
    update_pickups(pickups,expected+1,options,rng,&weights);
    REQUIRE(pickups[0].active);
    REQUIRE(pickups[0].kind==4U);
    REQUIRE(pickups[0].respawn_tick==0);
    return true;
}

RUNTIME_TEST(pickup_campaign_permanent_removal) {
    PickupOptions options;
    options.game_mode=5;
    std::array<PickupRecord,2> pickups{pickup_at(0U,0.0F,0.0F),pickup_at(5U,0.0F,0.0F)};
    pickups[0].consumed=true;
    RetailRandom rng(1U);
    update_pickups(pickups,10,options,rng,nullptr);
    REQUIRE(pickups[0].removed);
    REQUIRE(!pickups[1].removed);
    REQUIRE(rng.seed()==1U);
    // Timed drop expires even outside campaign.
    options.game_mode=0;
    std::array<PickupRecord,1> timed{pickup_at(11U,0.0F,0.0F)};
    timed[0].expire_tick=210;
    update_pickups(timed,209,options,rng,nullptr);
    REQUIRE(!timed[0].removed);
    update_pickups(timed,210,options,rng,nullptr);
    REQUIRE(timed[0].removed);
    return true;
}

RUNTIME_TEST(pickup_creation_gate) {
    PickupOptions options;
    options.extra_pickups_enabled=false;
    REQUIRE(pickup_kind_allowed(0U,options));
    REQUIRE(pickup_kind_allowed(5U,options));
    REQUIRE(!pickup_kind_allowed(11U,options));
    options.ammo_with_crate_disabled=true;
    REQUIRE(!pickup_kind_allowed(5U,options));
    return true;
}

RUNTIME_TEST_MAIN()
