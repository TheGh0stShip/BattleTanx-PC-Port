#include "battletanx/campaign_catalog.hpp"

#include "runtime_test_support.hpp"


using namespace battletanx;

RUNTIME_TEST(catalog_has_seventeen_ordered_waves) {
    const auto waves=campaign_wave_definitions();
    REQUIRE(waves.size()==17U);
    for (std::size_t i=0U; i<waves.size(); ++i) {
        REQUIRE(waves[i].wave_index==i);
        REQUIRE(waves[i].raw_field_08==static_cast<std::int32_t>(i));
    }
    return true;
}

RUNTIME_TEST(audit_reports_no_issues) {
    const CampaignCatalogAudit audit=audit_campaign_catalog();
    REQUIRE(audit.ok());
    REQUIRE(audit.wave_count==17U);
    REQUIRE(audit.unique_mission_ids==17U);
    REQUIRE(audit.resolved_objectives==17U);
    REQUIRE(audit.chapter_starts==4U);
    REQUIRE(audit.endurance_waves==4U);
    return true;
}

RUNTIME_TEST(objectives_follow_game_kind) {
    REQUIRE(objective_for_game_kind(3)==CampaignObjectiveKind::goal_slot);
    REQUIRE(objective_for_game_kind(6)==CampaignObjectiveKind::goal_slot);
    REQUIRE(objective_for_game_kind(4)==CampaignObjectiveKind::player_win_flag);
    REQUIRE(objective_for_game_kind(5)==CampaignObjectiveKind::last_team_standing);
    REQUIRE(objective_for_game_kind(7)==CampaignObjectiveKind::endurance);
    REQUIRE(objective_for_game_kind(1)==CampaignObjectiveKind::unresolved);
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions())
        REQUIRE(wave.objective==objective_for_game_kind(wave.game_kind));
    return true;
}

RUNTIME_TEST(endurance_waves_are_bonus_streams) {
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
        const bool bonus=wave.mission_id==5 || wave.mission_id==9 || wave.mission_id==13 || wave.mission_id==17;
        REQUIRE((wave.game_kind==7)==bonus);
        if (wave.game_kind==7) {
            REQUIRE(wave.endurance_kills==5);
            REQUIRE(wave.advances_on_failure);
            REQUIRE(!wave.player_unit_from_selection);
            REQUIRE(wave.music_transition==6);
            REQUIRE(wave.briefing_image==1);
        }
    }
    return true;
}

RUNTIME_TEST(find_by_mission_id_and_record) {
    const CampaignWaveDefinition* first=find_campaign_wave(1);
    REQUIRE(first!=nullptr && first->wave_index==0U);
    REQUIRE(find_campaign_wave(17)->wave_index==15U);
    REQUIRE(find_campaign_wave(16)->wave_index==16U);
    REQUIRE(find_campaign_wave(0)==nullptr);
    REQUIRE(find_campaign_wave(18)==nullptr);
    CampaignRecord record;
    record.mission_id=10U;
    REQUIRE(campaign_wave_for_record(record)->wave_index==9U);
    record.mission_id=0xFFU;
    REQUIRE(campaign_wave_for_record(record)==nullptr);
    return true;
}

RUNTIME_TEST(saved_mission_id_zero_is_campaign_complete) {
    CampaignRecord record;
    record.mission_id=0U;
    REQUIRE(campaign_wave_for_record(record)==nullptr);
    const CampaignResume done=campaign_resume_for_record(record);
    REQUIRE(done.status==CampaignResumeStatus::campaign_complete && done.wave==nullptr);
    // TEMPORARY POLICY: retail never writes id 0 (the final wave does not save);
    // it is treated as completed for robustness. The post-final id word is 0.
    const auto waves=campaign_wave_definitions();
    REQUIRE(waves.back().saved_mission_id_after_clear==0U);
    record.mission_id=10U;
    const CampaignResume resume=campaign_resume_for_record(record);
    REQUIRE(resume.status==CampaignResumeStatus::resume_wave && resume.wave->wave_index==9U);
    for (const std::uint8_t id : {std::uint8_t{18U},std::uint8_t{0xFFU}}) {
        record.mission_id=id;
        const CampaignResume bad=campaign_resume_for_record(record);
        REQUIRE(bad.status==CampaignResumeStatus::unknown_mission && bad.wave==nullptr);
    }
    return true;
}

RUNTIME_TEST(next_wave_and_save_advancement) {
    const auto waves=campaign_wave_definitions();
    for (std::size_t i=0U; i+1U<waves.size(); ++i) {
        const CampaignWaveDefinition* next=next_campaign_wave(waves[i]);
        REQUIRE(next==&waves[i+1U]);
        REQUIRE(waves[i].saved_mission_id_after_clear==static_cast<std::uint8_t>(next->mission_id));
    }
    REQUIRE(next_campaign_wave(waves[16])==nullptr);
    REQUIRE(waves[16].saved_mission_id_after_clear==0U); // resumes to no wave
    REQUIRE(find_campaign_wave(waves[16].saved_mission_id_after_clear)==nullptr);
    return true;
}

RUNTIME_TEST(chapter_cut_scenes_at_retail_indices) {
    const auto waves=campaign_wave_definitions();
    for (std::size_t i=0U; i<waves.size(); ++i) {
        const bool chapter=i==0U || i==5U || i==9U || i==13U;
        REQUIRE(waves[i].chapter_sequence.has_value()==chapter);
    }
    REQUIRE(*waves[0].chapter_sequence==15U);
    REQUIRE(*waves[13].chapter_sequence==18U);
    return true;
}

RUNTIME_TEST(owner_setup_and_teams) {
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
        REQUIRE(wave.view_mode==1 && wave.countdown_unit==24U);
        REQUIRE(wave.owner_count>=2 && wave.owner_count<=4);
        REQUIRE(wave.owners[0].team==0 && !wave.owners[0].computer_controlled);
        for (std::size_t o=1U; o<4U; ++o) {
            REQUIRE(wave.owners[o].team==1);
            REQUIRE(wave.owners[o].computer_controlled);
        }
        REQUIRE(wave.raw_field_204==2);
        REQUIRE(wave.raw_field_21C==0x41F00000U);
    }
    const CampaignWaveDefinition& first=campaign_wave_definitions()[0];
    REQUIRE(first.owner_count==2 && first.player_unit_kind==9 && first.par_time_seconds==180);
    return true;
}

RUNTIME_TEST(skill_profiles_decode_and_link_gun_tables) {
    const CampaignWaveDefinition& first=campaign_wave_definitions()[0];
    const CampaignSkillProfile& easy=first.owners[0].skills[0];
    REQUIRE(runtime_test::near(easy.aggressiveness,0.2));
    REQUIRE(easy.gun_damage_vram==0x80142D10U);
    const auto table=campaign_gun_table(easy.gun_damage_vram);
    REQUIRE(table.has_value() && (*table)[0]==2 && (*table)[3]==14);
    REQUIRE(!campaign_gun_table(0x80142D11U).has_value());
    REQUIRE(!campaign_gun_table(0x80142DD8U).has_value());
    const CampaignSkillProfile& hard=first.owners[1].skills[2];
    REQUIRE(runtime_test::near(hard.aggressiveness,0.4));
    REQUIRE(hard.offense==100 && hard.defense==150 && hard.gun_resurrection==250);
    return true;
}

RUNTIME_TEST(endurance_population_split) {
    REQUIRE(endurance_owner_population(5,1).lives==2);
    REQUIRE(endurance_owner_population(5,2).lives==2);
    REQUIRE(endurance_owner_population(5,3).lives==1);
    const CampaignOwnerPopulation big=endurance_owner_population(14,1);
    REQUIRE(big.lives==3 && big.reserve==2);
    REQUIRE(endurance_owner_population(14,0).lives==5);
    return true;
}

RUNTIME_TEST(launch_applies_carry_bonus_and_selection) {
    const auto waves=campaign_wave_definitions();
    const CampaignWaveLaunch launch=make_campaign_wave_launch(waves[6],1000,3,2U,4);
    REQUIRE(launch.player_unit_kind==4);
    REQUIRE(launch.player_bonus==3U);
    REQUIRE(launch.player_score==1000);
    REQUIRE(launch.world_stream==7);
    const CampaignWaveLaunch endurance=make_campaign_wave_launch(waves[4],0,-1,0U,4);
    REQUIRE(endurance.player_unit_kind==12);
    REQUIRE(endurance.player_bonus==0xFFFFU);
    StateWriter a, b;
    launch.serialize(a);
    launch.serialize(b);
    REQUIRE(a.bytes()==b.bytes());
    REQUIRE(a.bytes().size()==1U+4U+4U+4U+1U+2U+4U);
    return true;
}

RUNTIME_TEST(music_and_briefing_metadata) {
    REQUIRE(find_campaign_wave(1)->music_transition==7);
    REQUIRE(find_campaign_wave(3)->music_transition==0);
    REQUIRE(find_campaign_wave(16)->music_transition==0xA);
    REQUIRE(find_campaign_wave(1)->briefing_image==0);
    REQUIRE(find_campaign_wave(2)->briefing_image==13);
    REQUIRE(!find_campaign_wave(16)->save_prompt_after_win);
    REQUIRE(find_campaign_wave(15)->save_prompt_after_win);
    REQUIRE(campaign_wave_raw_words()[0][0]==5U);
    return true;
}

RUNTIME_TEST_MAIN()
