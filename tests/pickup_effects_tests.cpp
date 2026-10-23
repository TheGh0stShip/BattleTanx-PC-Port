#include "battletanx/pickup_effects.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;

namespace {

struct Setup {
    UnitRecord unit{make_unit(retail::UnitKind::tank,0U,0U,0.0F,0.0F,0U,0,UnitControl::player)};
    OwnerRecord owner{};
    TimedPowerupState timed{};
    PickupEffectOptions options{};
    Setup() { owner.present=true; options.game_mode=5; options.announce=false; }
    PickupEffectResult apply(std::uint8_t kind) {
        return apply_pickup_effect_complete(unit,owner,kind,options,timed);
    }
};

} // namespace

RUNTIME_TEST(health_kind_heals_and_stores_at_full) {
    Setup s;
    s.unit.health=50;
    PickupEffectResult r=s.apply(0U);
    REQUIRE(r.effect==PickupEffectKind::health && s.unit.health==80 && r.health_delta==30);
    s.unit.health=100;
    r=s.apply(0U);
    REQUIRE(r.effect==PickupEffectKind::stored_health && s.unit.ammo[16]==1);
    REQUIRE(s.unit.secondary_weapon.kind==16U && r.selected_secondary);
    return true;
}

RUNTIME_TEST(secondary_stock_table_matches_retail) {
    const std::array<std::array<int,3>,9> rows{{{2,4,5},{3,5,5},{1,6,1},{15,7,5},{7,11,5},
                                                {9,12,5},{10,13,2},{19,13,1},{16,14,3}}};
    for (const auto& row : rows) {
        Setup s;
        const PickupEffectResult r=s.apply(static_cast<std::uint8_t>(row[0]));
        REQUIRE(r.status==PickupEffectStatus::applied);
        REQUIRE(r.effect==PickupEffectKind::secondary_stock);
        REQUIRE(s.unit.ammo[static_cast<std::size_t>(row[1])]==row[2]);
        REQUIRE(s.unit.secondary_weapon.kind==row[1]);
    }
    return true;
}

RUNTIME_TEST(existing_secondary_is_kept) {
    Setup s;
    s.unit.secondary_weapon.kind=7U;
    const PickupEffectResult r=s.apply(2U);
    REQUIRE(s.unit.secondary_weapon.kind==7U && !r.selected_secondary && s.unit.ammo[4]==5);
    return true;
}

RUNTIME_TEST(ammo_and_crate_kinds) {
    Setup s;
    REQUIRE(s.apply(5U).ammo_delta==30 && s.unit.ammo[1]==100);
    s.unit.health=10;
    PickupEffectResult r=s.apply(11U);
    REQUIRE(s.unit.health==30 && s.unit.ammo[1]==115 && r.ammo_delta==15);
    s.options.base.ammo_with_crate_disabled=true;
    r=s.apply(11U);
    REQUIRE(r.ammo_delta==0 && s.unit.ammo[1]==115);
    s.unit.primary_weapon.kind=0U;
    r=s.apply(5U);
    REQUIRE(r.status==PickupEffectStatus::no_change);
    return true;
}

RUNTIME_TEST(map_reveal_sets_flag_0x80) {
    Setup s;
    REQUIRE(s.apply(4U).effect==PickupEffectKind::map_reveal);
    REQUIRE((s.unit.flags&0x80U)!=0U);
    return true;
}

RUNTIME_TEST(shield_creates_then_stacks) {
    Setup s;
    PickupEffectResult r=s.apply(8U);
    REQUIRE(r.effect==PickupEffectKind::shield_created && s.unit.shield_strength==100);
    r=s.apply(18U);
    REQUIRE(r.effect==PickupEffectKind::shield_added && s.unit.shield_strength==125);
    return true;
}

RUNTIME_TEST(cloak_starts_extends_and_expires) {
    Setup s;
    s.options.cloak_pickups=true;
    s.options.tick=100;
    REQUIRE(s.apply(12U).effect==PickupEffectKind::cloak_started);
    REQUIRE(s.timed.cloak_end_tick==1000 && s.timed.cloak_flash==240);
    REQUIRE(s.apply(12U).effect==PickupEffectKind::cloak_extended && s.timed.cloak_end_tick==1900);
    REQUIRE(cloak_hides_unit(s.timed,200));
    s.timed.last_fire_tick=190;
    REQUIRE(!cloak_hides_unit(s.timed,200));
    update_timed_powerups(s.timed,1901);
    REQUIRE(s.timed.cloak_end_tick==0);
    Setup t;
    REQUIRE(t.apply(12U).effect==PickupEffectKind::secondary_stock && t.unit.ammo[15]==1);
    return true;
}

RUNTIME_TEST(extra_life_and_score_thresholds) {
    Setup s;
    PickupEffectResult r=s.apply(13U);
    REQUIRE(r.lives_awarded==1U && s.owner.respawn_bank[0]==1);
    s.options.game_mode=7;
    r=s.apply(13U);
    REQUIRE(r.lives_awarded==1U && s.owner.respawn_bank[0]==1);
    s.options.game_mode=5;
    s.owner.score=49500;
    r=s.apply(14U);
    REQUIRE(s.owner.score==50400 && r.lives_awarded==1U && s.owner.respawn_bank[0]==2);
    s.owner.computer=true;
    REQUIRE(s.apply(13U).status==PickupEffectStatus::no_change);
    return true;
}

RUNTIME_TEST(announce_adds_progress_and_event) {
    Setup s;
    s.options.announce=true;
    const PickupEffectResult r=s.apply(4U);
    REQUIRE(r.announced && r.owner_event==15 && s.owner.score==100);
    s.options.base.replay_active=true;
    REQUIRE(!s.apply(4U).announced && s.owner.score==100);
    return true;
}

RUNTIME_TEST(unknown_kinds_are_reported) {
    for (const std::uint8_t kind : {6U,17U,20U,21U,22U,40U,255U}) {
        Setup s;
        const UnitRecord before=s.unit;
        const PickupEffectResult r=s.apply(kind);
        REQUIRE(r.status==PickupEffectStatus::no_retail_case);
        REQUIRE(s.unit.health==before.health && s.unit.ammo==before.ammo);
    }
    return true;
}

RUNTIME_TEST(spawn_grant_random_table_and_events) {
    REQUIRE(spawn_grant_random_kind(0U)==1U && spawn_grant_random_kind(6U)==2U);
    REQUIRE(spawn_grant_random_kind(12U)==3U && spawn_grant_random_kind(14U)==4U);
    REQUIRE(spawn_grant_random_kind(19U)==7U && spawn_grant_random_kind(21U)==9U);
    REQUIRE(spawn_grant_random_kind(23U)==15U && spawn_grant_random_kind(25U)==12U);
    REQUIRE(spawn_grant_random_kind(29U)==18U && spawn_grant_random_kind(31U)==19U);
    Setup s;
    RetailRandom rng(7U);
    REQUIRE(!grant_spawn_weapon(s.unit,s.owner,23U,s.options,s.timed,rng).granted_kind);
    REQUIRE(rng.seed()==7U);
    const SpawnGrantResult g=grant_spawn_weapon(s.unit,s.owner,2U,s.options,s.timed,rng);
    REQUIRE(g.granted_kind==2U && s.unit.ammo[4]==5 && !g.effect.announced);
    s.options.tick=10;
    s.unit.primary=true;
    REQUIRE(!grant_spawn_weapon(s.unit,s.owner,1U,s.options,s.timed,rng).granted_kind);
    RetailRandom a(99U), b(99U);
    const SpawnGrantResult r=grant_spawn_weapon(s.unit,s.owner,22U,s.options,s.timed,a);
    REQUIRE(r.granted_kind==spawn_grant_random_kind(b.next_u32()));
    return true;
}

RUNTIME_TEST(spawn_grant_mode_rules) {
    REQUIRE(spawn_grant_applies(5,0U,0,false,false));
    REQUIRE(!spawn_grant_applies(5,1U,0,false,false));
    REQUIRE(!spawn_grant_applies(5,0U,9,false,false));
    REQUIRE(!spawn_grant_applies(1,0U,0,true,false));
    REQUIRE(spawn_grant_applies(1,0U,0,true,true));
    REQUIRE(!spawn_grant_applies(7,0U,0,false,false));
    return true;
}

RUNTIME_TEST(bank_reroll_is_gated_and_deterministic) {
    RetailRandom rng(1U);
    REQUIRE(reroll_bank_pickup_kind(5U,false,rng)==5U && rng.seed()==1U);
    REQUIRE(reroll_bank_pickup_kind(0U,true,rng)==0U && rng.seed()==1U);
    RetailRandom a(5U), b(5U);
    const std::uint8_t k=reroll_bank_pickup_kind(14U,true,a);
    const std::array<std::uint8_t,11> table{0U,0U,0U,3U,3U,2U,2U,4U,8U,12U,15U};
    REQUIRE(k==table[b.next_u32()%11U]);
    return true;
}

RUNTIME_TEST(timed_state_serializes_big_endian) {
    TimedPowerupState t;
    t.cloak_end_tick=0x01020304;
    StateWriter w;
    t.serialize(w);
    REQUIRE(w.bytes().size()==17U);
    REQUIRE(w.bytes()[0]==1U && w.bytes()[3]==4U);
    return true;
}

RUNTIME_TEST_MAIN()
