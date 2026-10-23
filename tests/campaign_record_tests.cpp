#include "battletanx/campaign_record.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;

namespace {
void put32(ControllerPakRecord& r,std::size_t at,std::uint32_t v) {
    r.bytes[at]=static_cast<std::uint8_t>(v>>24U); r.bytes[at+1U]=static_cast<std::uint8_t>(v>>16U);
    r.bytes[at+2U]=static_cast<std::uint8_t>(v>>8U); r.bytes[at+3U]=static_cast<std::uint8_t>(v);
}
void seal(ControllerPakRecord& r) { put32(r,0U,controller_pak_checksum(r)); }

// Synthetic record: patterned reserved bytes plus valid known fields.
ControllerPakRecord synthetic() {
    ControllerPakRecord r;
    for (std::size_t i=0U; i<r.bytes.size(); ++i) r.bytes[i]=static_cast<std::uint8_t>(i*7U+3U);
    put32(r,0x04U,0x11223344U);
    r.bytes[0x08]=5U;
    put32(r,0x0CU,1234U); put32(r,0x10U,3U);
    r.bytes[0x14]=0x00U; r.bytes[0x15]=0x2AU;
    r.bytes[0x16]=1U; r.bytes[0x17]=1U; r.bytes[0x18]=0U; r.bytes[0x19]=2U;
    r.bytes[0x1A]=1U; r.bytes[0x1B]=0U;
    put32(r,0x1CU,6U); put32(r,0x20U,4U);
    for (std::size_t i=0U; i<17U; ++i) put32(r,0x24U+i*4U,0xA0000000U+static_cast<std::uint32_t>(i));
    seal(r);
    return r;
}
} // namespace

RUNTIME_TEST(parses_valid_synthetic_record) {
    const auto r=synthetic();
    CampaignRecordError e{CampaignRecordErrorCode::checksum_mismatch,99U};
    const auto p=parse_campaign_record(r,&e);
    REQUIRE(p.has_value());
    REQUIRE(e.code==CampaignRecordErrorCode::none);
    REQUIRE(p->header==0x11223344U && p->mission_id==5U && p->carry==1234 && p->unit_type==3);
    REQUIRE(p->bonus==42 && p->difficulty==CampaignDifficulty::normal);
    REQUIRE(p->powerups.enabled() && !p->powerup_regen.enabled() && p->control_scheme==2U);
    REQUIRE(p->base_defense.enabled() && !p->unlimited_ammo.enabled());
    REQUIRE(p->sfx_volume==6 && p->music_volume==4);
    REQUIRE(p->bindings[0]==0xA0000000U && p->bindings[16]==0xA0000010U);
    REQUIRE(p->checksum==controller_pak_checksum(r));
    return true;
}

RUNTIME_TEST(rejects_checksum_mismatch) {
    auto r=synthetic();
    r.bytes[0xF0]^=0x01U;
    CampaignRecordError e;
    REQUIRE(!parse_campaign_record(r,&e));
    REQUIRE(e.code==CampaignRecordErrorCode::checksum_mismatch && e.offset==0U);
    return true;
}

RUNTIME_TEST(round_trip_is_byte_identical) {
    const auto r=synthetic();
    const auto p=parse_campaign_record(r);
    REQUIRE(p.has_value());
    REQUIRE(serialize_campaign_record(*p).bytes==r.bytes);
    return true;
}

RUNTIME_TEST(non_binary_flag_bytes_round_trip) {
    auto r=synthetic();
    r.bytes[0x17]=0x80U; r.bytes[0x1B]=0xFFU;
    seal(r);
    const auto p=parse_campaign_record(r);
    REQUIRE(p.has_value() && p->powerups.enabled() && p->unlimited_ammo.raw==0xFFU);
    REQUIRE(serialize_campaign_record(*p).bytes==r.bytes);
    return true;
}

RUNTIME_TEST(edit_preserves_unknown_bytes) {
    const auto r=synthetic();
    auto p=parse_campaign_record(r);
    REQUIRE(p.has_value());
    p->mission_id=9U;
    const auto out=serialize_campaign_record(*p);
    REQUIRE(out.bytes[0x08]==9U);
    for (std::size_t i=0x09U; i<0x0CU; ++i) REQUIRE(out.bytes[i]==r.bytes[i]);
    for (std::size_t i=0x68U; i<0x100U; ++i) REQUIRE(out.bytes[i]==r.bytes[i]);
    for (std::size_t i=0x04U; i<0x100U; ++i) if (i!=0x08U) REQUIRE(out.bytes[i]==r.bytes[i]);
    return true;
}

RUNTIME_TEST(serialize_recomputes_checksum) {
    auto p=parse_campaign_record(synthetic());
    REQUIRE(p.has_value());
    p->checksum=0xDEADBEEFU;
    p->carry=0x01020304;
    const auto out=serialize_campaign_record(*p);
    REQUIRE(controller_pak_checksum_valid(out));
    std::uint32_t sum=0U;
    for (std::size_t i=4U; i<0x100U; ++i) sum+=out.bytes[i];
    REQUIRE(out.bytes[0]==static_cast<std::uint8_t>(sum>>24U) && out.bytes[1]==static_cast<std::uint8_t>(sum>>16U));
    REQUIRE(out.bytes[2]==static_cast<std::uint8_t>(sum>>8U) && out.bytes[3]==static_cast<std::uint8_t>(sum));
    return true;
}

RUNTIME_TEST(rejects_difficulty_above_hard) {
    auto r=synthetic(); r.bytes[0x16]=3U; seal(r);
    CampaignRecordError e;
    REQUIRE(!parse_campaign_record(r,&e));
    REQUIRE(e.code==CampaignRecordErrorCode::difficulty_out_of_range && e.offset==0x16U);
    r.bytes[0x16]=2U; seal(r);
    REQUIRE(parse_campaign_record(r).has_value());
    return true;
}

RUNTIME_TEST(rejects_sfx_volume_out_of_range) {
    auto r=synthetic(); put32(r,0x1CU,8U); seal(r);
    CampaignRecordError e;
    REQUIRE(!parse_campaign_record(r,&e));
    REQUIRE(e.code==CampaignRecordErrorCode::sfx_volume_out_of_range && e.offset==0x1CU);
    put32(r,0x1CU,0xFFFFFFFFU); seal(r);
    REQUIRE(!parse_campaign_record(r));
    put32(r,0x1CU,0U); seal(r);
    REQUIRE(parse_campaign_record(r).has_value());
    return true;
}

RUNTIME_TEST(rejects_music_volume_out_of_range) {
    auto r=synthetic(); put32(r,0x20U,0x80000000U); seal(r);
    CampaignRecordError e;
    REQUIRE(!parse_campaign_record(r,&e));
    REQUIRE(e.code==CampaignRecordErrorCode::music_volume_out_of_range && e.offset==0x20U);
    put32(r,0x20U,7U); seal(r);
    REQUIRE(parse_campaign_record(r).has_value());
    return true;
}

RUNTIME_TEST(mission_id_is_not_range_checked) {
    auto r=synthetic(); r.bytes[0x08]=0xFFU; seal(r);
    const auto p=parse_campaign_record(r);
    REQUIRE(p.has_value() && p->mission_id==0xFFU);
    return true;
}

RUNTIME_TEST(defaults_match_session_reset) {
    const auto d=default_campaign_record();
    REQUIRE(d.header==0x42AU && d.mission_id==1U && d.carry==0 && d.unit_type==9);
    REQUIRE(d.bonus==0 && d.difficulty==CampaignDifficulty::easy && d.control_scheme==0U);
    const auto out=serialize_campaign_record(d);
    REQUIRE(out.bytes[0x04]==0U && out.bytes[0x07]==0x2AU && out.bytes[0x06]==0x04U);
    REQUIRE(out.bytes[0x08]==1U && out.bytes[0x13]==9U);
    REQUIRE(controller_pak_checksum_valid(out) && out.bytes==d.raw.bytes);
    REQUIRE(d.checksum==0x04U+0x2AU+1U+9U);
    REQUIRE(parse_campaign_record(out).has_value());
    return true;
}

RUNTIME_TEST(big_endian_field_placement) {
    CampaignRecord c;
    c.header=0x01020304U; c.mission_id=0x08U; c.carry=0x0C0D0E0F; c.unit_type=0x10111213;
    c.bonus=0x1415; c.difficulty=CampaignDifficulty::hard;
    c.powerups.raw=0x17U; c.powerup_regen.raw=0x18U; c.control_scheme=0x19U;
    c.base_defense.raw=0x1AU; c.unlimited_ammo.raw=0x1BU;
    c.sfx_volume=0x1C1D1E1F; c.music_volume=0x20212223;
    for (std::size_t i=0U; i<17U; ++i) c.bindings[i]=0x24252627U+static_cast<std::uint32_t>(i)*0x04040404U;
    const auto out=serialize_campaign_record(c);
    for (std::size_t i=4U; i<0x68U; ++i) {
        if (i>=0x09U && i<0x0CU) { REQUIRE(out.bytes[i]==0U); continue; }
        if (i==0x16U) { REQUIRE(out.bytes[i]==2U); continue; }
        REQUIRE(out.bytes[i]==static_cast<std::uint8_t>(i<0x08U ? i-3U : i));
    }
    return true;
}

RUNTIME_TEST(serializes_negative_signed_fields) {
    auto p=parse_campaign_record(synthetic());
    REQUIRE(p.has_value());
    p->carry=-2; p->unit_type=-1; p->bonus=-300;
    const auto out=serialize_campaign_record(*p);
    REQUIRE(out.bytes[0x0C]==0xFFU && out.bytes[0x0F]==0xFEU);
    REQUIRE(out.bytes[0x10]==0xFFU && out.bytes[0x13]==0xFFU);
    REQUIRE(out.bytes[0x14]==0xFEU && out.bytes[0x15]==0xD4U);
    const auto back=parse_campaign_record(out);
    REQUIRE(back && back->carry==-2 && back->unit_type==-1 && back->bonus==-300);
    return true;
}

RUNTIME_TEST(wave_clear_updates_progress_fields_only) {
    const auto r=synthetic();
    auto p=parse_campaign_record(r);
    REQUIRE(p.has_value());
    CampaignWaveClear w;
    w.mission_id=6U; w.carry=500; w.unit_type=4; w.bonus=-1; w.control_scheme=1U;
    w.difficulty=CampaignDifficulty::hard; w.powerups=0U; w.powerup_regen=1U;
    w.base_defense=0U; w.unlimited_ammo=1U; w.sfx_volume=3; w.music_volume=2;
    w.bindings.fill(0x0BADF00DU);
    apply_campaign_wave_clear(*p,w);
    const auto out=serialize_campaign_record(*p);
    REQUIRE(out.bytes[0x08]==6U && out.bytes[0x16]==2U && out.bytes[0x1B]==1U && out.bytes[0x67]==0x0DU);
    REQUIRE(p->header==0x11223344U);
    for (std::size_t i=0x68U; i<0x100U; ++i) REQUIRE(out.bytes[i]==r.bytes[i]);
    REQUIRE(controller_pak_checksum_valid(out));
    return true;
}

RUNTIME_TEST_MAIN()
