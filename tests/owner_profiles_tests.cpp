#include "battletanx/owner_profiles.hpp"

#include "runtime_test_support.hpp"

using namespace battletanx;
using namespace battletanx::owner_profiles;

RUNTIME_TEST(lookup_rejects_null_and_outside_pool) {
    REQUIRE(find_gun_table(0U).status==LookupStatus::null_pointer);
    REQUIRE(find_gun_table(campaign_gun_table_vram-8U).status==LookupStatus::outside_pool);
    REQUIRE(find_gun_table(campaign_gun_table_vram+2U).status==LookupStatus::outside_pool);
    REQUIRE(find_gun_table(campaign_gun_table_vram+8U*static_cast<std::uint32_t>(campaign_gun_table_count)).status==LookupStatus::outside_pool);
    REQUIRE(find_gun_table(0xFFFFFFF8U).status==LookupStatus::outside_pool);
    const GunTableLookup bad=find_gun_table(campaign_gun_table_vram+1U);
    REQUIRE(bad.values==GunValues{});
    return true;
}

RUNTIME_TEST(lookup_indexes_by_stride) {
    for (std::size_t i=0U;i<campaign_gun_table_count;++i) {
        const GunTableLookup found=find_gun_table(campaign_gun_table_vram+static_cast<std::uint32_t>(i)*8U);
        REQUIRE(found.status==LookupStatus::ok);
        REQUIRE(found.values==campaign_gun_tables()[i]);
    }
    return true;
}

RUNTIME_TEST(rebuild_time_by_difficulty) {
    CampaignSkillProfile profile;
    profile.gun_resurrection=250;
    REQUIRE(rebuild_time(true,profile,0U)==250.0F);
    REQUIRE(rebuild_time(false,profile,0U)==difficulty_rebuild_time[0]);
    REQUIRE(rebuild_time(false,profile,1U)==difficulty_rebuild_time[1]);
    REQUIRE(rebuild_time(false,profile,2U)==difficulty_rebuild_time[2]);
    REQUIRE(rebuild_time(false,profile,7U)==difficulty_rebuild_time[1]);
    return true;
}

RUNTIME_TEST(make_turret_owner_copies_both_tables) {
    CampaignSkillProfile profile;
    profile.gun_damage_vram=campaign_gun_table_vram+3U*8U;
    profile.gun_fire_rate_vram=campaign_gun_table_vram;
    profile.gun_resurrection=10;
    const TurretOwnerResult owner=make_turret_owner(profile,true,1U);
    REQUIRE(owner.damage_status==LookupStatus::ok);
    REQUIRE(owner.fire_delay_status==LookupStatus::ok);
    REQUIRE((owner.config.effect_kind==GunValues{2,7,14,14}));  // D_80142D10
    REQUIRE((owner.config.fire_delay==GunValues{7,95,100,50})); // D_80142CF8
    REQUIRE(owner.config.rebuild_time==10.0F);
    return true;
}

RUNTIME_TEST(make_turret_owner_null_profile_is_zero) {
    const TurretOwnerResult owner=make_turret_owner(CampaignSkillProfile{},false,2U);
    REQUIRE(owner.damage_status==LookupStatus::null_pointer);
    REQUIRE(owner.fire_delay_status==LookupStatus::null_pointer);
    REQUIRE(owner.config.effect_kind==GunValues{});
    REQUIRE(owner.config.fire_delay==never_fire_delay);
    REQUIRE(owner.config.rebuild_time==difficulty_rebuild_time[2]);
    return true;
}

RUNTIME_TEST_MAIN()
