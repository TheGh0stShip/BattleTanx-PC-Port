#include "battletanx/campaign_save_transaction.hpp"

#include "runtime_test_support.hpp"

#include <cstddef>
#include <cstdint>

using namespace battletanx;
using namespace battletanx::campaign_save;

namespace {

CampaignRecord synthetic_record() {
    CampaignRecord base=default_campaign_record();
    ControllerPakRecord raw=base.raw;
    raw.bytes[0x09U]=0xA5U;                     // reserved between fields
    for (std::size_t i=0x68U; i<ControllerPakRecord::size; ++i) {
        raw.bytes[i]=static_cast<std::uint8_t>(i*7U);
    }
    base.raw=raw;
    base.sfx_volume=3;
    base.music_volume=2;
    base.control_scheme=1U;
    for (std::size_t i=0U; i<campaign_binding_words; ++i) base.bindings[i]=0x01020300U+static_cast<std::uint32_t>(i);
    const ControllerPakRecord bytes=serialize_campaign_record(base);
    return *parse_campaign_record(bytes);
}

WaveEnd wave_end(const CampaignRecord& r,std::uint8_t index) {
    WaveEnd e;
    e.wave_index=index;
    e.wave_kind=0U;
    e.outcome=WaveOutcome::won;
    e.save_flag=true;
    e.next_wave_id=static_cast<std::uint8_t>(index+2U);
    e.carry=1234;
    e.unit_type=r.unit_type;
    e.bonus=55;
    e.control_scheme=r.control_scheme;
    e.difficulty=r.difficulty;
    e.sfx_volume=r.sfx_volume;
    e.music_volume=r.music_volume;
    e.bindings=r.bindings;
    return e;
}

bool same_bytes(const ControllerPakRecord& a,const ControllerPakRecord& b) { return a.bytes==b.bytes; }

} // namespace

RUNTIME_TEST(successful_wave_commits_only_after_ack) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    REQUIRE(tx.request(wave_end(start,3U))==RequestResult::issued);
    REQUIRE(tx.pending().has_value());
    REQUIRE(same_bytes(serialize_campaign_record(tx.authoritative()),serialize_campaign_record(start)));
    const SaveRequest& req=*tx.pending();
    REQUIRE(req.next_wave_index==4U);
    REQUIRE(controller_pak_checksum_valid(req.bytes));
    const auto parsed=parse_campaign_record(req.bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->mission_id==5U);
    REQUIRE(parsed->carry==1234);
    REQUIRE(parsed->bonus==55);
    REQUIRE(tx.acknowledge(req.sequence,true)==AckResult::committed);
    REQUIRE(!tx.pending().has_value());
    REQUIRE(tx.authoritative().mission_id==5U);
    REQUIRE(tx.authoritative().carry==1234);
    return true;
}

RUNTIME_TEST(unknown_bytes_options_and_bindings_preserved) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    REQUIRE(tx.request(wave_end(start,0U))==RequestResult::issued);
    const ControllerPakRecord& out=tx.pending()->bytes;
    REQUIRE(out.bytes[0x09U]==0xA5U);
    for (std::size_t i=0x68U; i<ControllerPakRecord::size; ++i) {
        REQUIRE(out.bytes[i]==static_cast<std::uint8_t>(i*7U));
    }
    for (std::size_t i=0x04U; i<0x08U; ++i) REQUIRE(out.bytes[i]==start.raw.bytes[i]);
    const auto parsed=parse_campaign_record(out);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->bindings==start.bindings);
    REQUIRE(parsed->sfx_volume==3);
    REQUIRE(parsed->music_volume==2);
    REQUIRE(parsed->control_scheme==1U);
    return true;
}

RUNTIME_TEST(write_failure_keeps_record_and_retries) {
    const CampaignRecord start=synthetic_record();
    const ControllerPakRecord before=serialize_campaign_record(start);
    SaveTransaction tx(start);
    REQUIRE(tx.request(wave_end(start,1U))==RequestResult::issued);
    const std::uint32_t seq=tx.pending()->sequence;
    REQUIRE(tx.acknowledge(seq,false)==AckResult::write_failed);
    REQUIRE(tx.failed_attempts()==1U);
    REQUIRE(tx.pending().has_value());
    REQUIRE(same_bytes(serialize_campaign_record(tx.authoritative()),before));
    REQUIRE(tx.acknowledge(seq+1U,true)==AckResult::sequence_mismatch);
    REQUIRE(tx.acknowledge(seq,true)==AckResult::committed);
    REQUIRE(tx.failed_attempts()==0U);
    REQUIRE(tx.acknowledge(seq,true)==AckResult::no_pending);
    return true;
}

RUNTIME_TEST(retry_and_quit_do_not_save) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    WaveEnd lost=wave_end(start,4U);
    lost.outcome=WaveOutcome::lost;
    REQUIRE(decide_wave_step(lost).step==WaveStep::retry);
    REQUIRE(decide_wave_step(lost).next_wave_index==4U);
    REQUIRE(tx.request(lost)==RequestResult::not_saving);
    WaveEnd quit=wave_end(start,4U);
    quit.outcome=WaveOutcome::quit;
    REQUIRE(tx.request(quit)==RequestResult::not_saving);
    REQUIRE(!tx.pending().has_value());
    REQUIRE(same_bytes(serialize_campaign_record(tx.authoritative()),serialize_campaign_record(start)));
    return true;
}

RUNTIME_TEST(kind7_loss_advances_and_saves) {
    const CampaignRecord start=synthetic_record();
    WaveEnd e=wave_end(start,6U);
    e.outcome=WaveOutcome::lost;
    e.wave_kind=loss_advances_wave_kind;
    const WaveDecision d=decide_wave_step(e);
    REQUIRE(d.step==WaveStep::advance_save);
    REQUIRE(d.next_wave_index==7U);
    e.save_flag=false;
    REQUIRE(decide_wave_step(e).step==WaveStep::advance);
    return true;
}

RUNTIME_TEST(busy_until_failure_then_replaced) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    REQUIRE(tx.request(wave_end(start,1U))==RequestResult::issued);
    const std::uint32_t first=tx.pending()->sequence;
    REQUIRE(tx.request(wave_end(start,2U))==RequestResult::busy);
    REQUIRE(tx.pending()->sequence==first);
    REQUIRE(tx.acknowledge(first,false)==AckResult::write_failed);
    REQUIRE(tx.request(wave_end(start,2U))==RequestResult::issued);
    const std::uint32_t second=tx.pending()->sequence;
    REQUIRE(second!=first);
    REQUIRE(tx.pending()->next_wave_index==3U);
    REQUIRE(tx.failed_attempts()==0U);
    REQUIRE(tx.acknowledge(first,true)==AckResult::sequence_mismatch);
    REQUIRE(tx.acknowledge(second,true)==AckResult::committed);
    REQUIRE(tx.authoritative().mission_id==4U);
    return true;
}

RUNTIME_TEST(abandon_clears_pending_and_failures) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    REQUIRE(tx.request(wave_end(start,1U))==RequestResult::issued);
    REQUIRE(tx.acknowledge(tx.pending()->sequence,false)==AckResult::write_failed);
    tx.abandon();
    REQUIRE(!tx.pending().has_value());
    REQUIRE(tx.failed_attempts()==0U);
    REQUIRE(tx.request(wave_end(start,2U))==RequestResult::issued);
    return true;
}

RUNTIME_TEST(final_wave_completes_without_save) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    WaveEnd e=wave_end(start,16U);
    e.save_flag=true;    // ignored: retail clears the flag on every exit
    const WaveDecision d=decide_wave_step(e);
    REQUIRE(d.step==WaveStep::complete);
    REQUIRE(!d.save);
    REQUIRE(d.next_wave_index==save_wave_limit);
    REQUIRE(tx.request(e)==RequestResult::not_saving);
    REQUIRE(!tx.pending().has_value());
    e.save_flag=false;
    REQUIRE(decide_wave_step(e).step==WaveStep::complete);
    REQUIRE(!decide_wave_step(e).save);
    return true;
}

RUNTIME_TEST(out_of_range_wave_index_is_invalid_not_wrapped) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    for (const std::uint8_t index : {std::uint8_t{17U},std::uint8_t{18U},std::uint8_t{0xFFU}}) {
        WaveEnd e=wave_end(start,index);
        const WaveDecision d=decide_wave_step(e);
        REQUIRE(d.step==WaveStep::invalid_wave);
        REQUIRE(!d.save);
        REQUIRE(d.next_wave_index==index);
        REQUIRE(tx.request(e)==RequestResult::not_saving);
    }
    REQUIRE(!tx.pending().has_value());
    return true;
}

RUNTIME_TEST(explicit_request_rejects_index_past_final_save) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    const CampaignWaveClear clear=wave_clear_from(wave_end(start,0U));
    REQUIRE(tx.request(save_wave_limit,clear)==RequestResult::invalid_wave_index);
    REQUIRE(tx.request(18U,clear)==RequestResult::invalid_wave_index);
    REQUIRE(tx.request(0xFFU,clear)==RequestResult::invalid_wave_index);
    REQUIRE(!tx.pending().has_value());
    REQUIRE(tx.request(max_save_next_wave_index,clear)==RequestResult::issued);
    REQUIRE(tx.pending()->next_wave_index==16U);
    return true;
}

RUNTIME_TEST(carry_bonus_unit_changes_round_trip) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction tx(start);
    WaveEnd e=wave_end(start,2U);
    e.carry=-7;
    e.bonus=-300;
    e.unit_type=4;
    e.powerups=1U;
    e.unlimited_ammo=1U;
    e.difficulty=CampaignDifficulty::hard;
    REQUIRE(tx.request(e)==RequestResult::issued);
    REQUIRE(tx.acknowledge(tx.pending()->sequence,true)==AckResult::committed);
    const auto parsed=parse_campaign_record(serialize_campaign_record(tx.authoritative()));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->carry==-7);
    REQUIRE(parsed->bonus==-300);
    REQUIRE(parsed->unit_type==4);
    REQUIRE(parsed->powerups.enabled());
    REQUIRE(parsed->unlimited_ammo.enabled());
    REQUIRE(parsed->difficulty==CampaignDifficulty::hard);
    return true;
}

RUNTIME_TEST(save_prompt_rules) {
    SavePromptInput in;
    in.player_status=2U;
    in.pressed=0x20U;
    REQUIRE(evaluate_save_prompt(in));
    in.mode_id=prompt_suppressed_mode_id;
    REQUIRE(!evaluate_save_prompt(in));
    in.mode_id=0U;
    in.pressed=0x30U;
    REQUIRE(!evaluate_save_prompt(in));
    SavePromptInput k7;
    k7.mode_kind7=true;
    k7.pressed=0x20U;
    REQUIRE(evaluate_save_prompt(k7));
    k7.player_status=1U;
    k7.current_flag=true;
    k7.pressed=0x10U;
    REQUIRE(evaluate_save_prompt(k7));
    SavePromptInput alive;
    alive.current_flag=true;
    alive.pressed=0x10U;
    REQUIRE(!evaluate_save_prompt(alive));
    return true;
}

RUNTIME_TEST(serialize_is_deterministic) {
    const CampaignRecord start=synthetic_record();
    SaveTransaction a(start);
    SaveTransaction b(start);
    (void)a.request(wave_end(start,1U));
    (void)b.request(wave_end(start,1U));
    StateWriter wa;
    StateWriter wb;
    a.serialize(wa);
    b.serialize(wb);
    REQUIRE(wa.bytes()==wb.bytes());
    return true;
}

RUNTIME_TEST_MAIN()
