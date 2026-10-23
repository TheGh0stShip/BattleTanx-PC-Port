#include "battletanx/campaign_save_transaction.hpp"

namespace battletanx::campaign_save {
namespace {

constexpr std::uint16_t button_accept=0x20U;
constexpr std::uint16_t button_decline=0x10U;

void write_clear(StateWriter& w,const CampaignWaveClear& c) {
    w.u8(c.mission_id);
    w.s32(c.carry);
    w.s32(c.unit_type);
    w.s16(c.bonus);
    w.u8(c.control_scheme);
    w.u8(static_cast<std::uint8_t>(c.difficulty));
    w.u8(c.powerups);
    w.u8(c.powerup_regen);
    w.u8(c.base_defense);
    w.u8(c.unlimited_ammo);
    w.s32(c.sfx_volume);
    w.s32(c.music_volume);
    for (const std::uint32_t word : c.bindings) w.u32(word);
}

void write_bytes(StateWriter& w,const ControllerPakRecord& r) {
    for (const std::uint8_t b : r.bytes) w.u8(b);
}

} // namespace

bool evaluate_save_prompt(const SavePromptInput& in) noexcept {
    bool flag=in.current_flag;
    std::uint16_t buttons=0U;
    if (in.mode_kind7) {
        if (in.player_status==2U) {
            buttons=static_cast<std::uint16_t>(in.pressed&(button_accept|button_decline));
        } else {
            if (in.player_status!=0U) return flag;
            buttons=static_cast<std::uint16_t>(in.pressed&button_decline);
            if ((in.pressed&button_accept)!=0U) flag=true;
        }
    } else if (in.player_status==2U) {
        // Mode id 16 never offers the save.
        if (in.mode_id!=prompt_suppressed_mode_id && (in.pressed&button_accept)!=0U) flag=true;
        buttons=static_cast<std::uint16_t>(in.pressed&button_decline);
    } else {
        if (in.player_status!=0U) return flag;
        if ((in.pressed&button_decline)!=0U) flag=false;
        buttons=static_cast<std::uint16_t>(in.pressed&button_accept);
    }
    if (buttons!=0U) flag=false;
    return flag;
}

WaveDecision decide_wave_step(const WaveEnd& end) noexcept {
    WaveDecision out;
    out.next_wave_index=end.wave_index;
    // Reject before incrementing: 255+1 would wrap to 0 in u8.
    if (end.wave_index>=save_wave_limit) { out.step=WaveStep::invalid_wave; return out; }
    if (end.outcome==WaveOutcome::quit) { out.step=WaveStep::quit; return out; }
    if (end.outcome==WaveOutcome::lost && end.wave_kind!=loss_advances_wave_kind) {
        out.step=WaveStep::retry;
        return out;
    }
    out.next_wave_index=static_cast<std::uint8_t>(end.wave_index+1U);
    if (out.next_wave_index>=save_wave_limit) {
        // CONFIRMED: the final wave (index 16, mission 16, kind 3) never
        // saves. update_match_state.c:397-405 withholds the 0x20 save for
        // value04==0x10, and every exit from that dialog clears D_8012579B
        // (:403-404 via buttons, :410 on loss), so run_campaign_waves.c:124
        // never reaches the gOpponentWaves[17] read. save_flag is ignored.
        out.next_wave_index=save_wave_limit;
        out.step=WaveStep::complete;
        out.save=false;
        return out;
    }
    out.save=end.save_flag;
    out.step=end.save_flag ? WaveStep::advance_save : WaveStep::advance;
    return out;
}

CampaignWaveClear wave_clear_from(const WaveEnd& end) noexcept {
    CampaignWaveClear c;
    c.mission_id=end.next_wave_id;
    c.carry=end.carry;
    c.unit_type=end.unit_type;
    c.bonus=end.bonus;
    c.control_scheme=end.control_scheme;
    c.difficulty=end.difficulty;
    c.powerups=end.powerups;
    c.powerup_regen=end.powerup_regen;
    c.base_defense=end.base_defense;
    c.unlimited_ammo=end.unlimited_ammo;
    c.sfx_volume=end.sfx_volume;
    c.music_volume=end.music_volume;
    c.bindings=end.bindings;
    return c;
}

SaveTransaction::SaveTransaction(const CampaignRecord& authoritative) : record_(authoritative) {}

RequestResult SaveTransaction::request(const WaveEnd& end) {
    const WaveDecision decision=decide_wave_step(end);
    if (!decision.save) return RequestResult::not_saving;
    return request(decision.next_wave_index,wave_clear_from(end));
}

RequestResult SaveTransaction::request(std::uint8_t next_wave_index,const CampaignWaveClear& clear) {
    if (next_wave_index>max_save_next_wave_index) return RequestResult::invalid_wave_index;
    if (pending_.has_value()) {
        if (failed_attempts_==0U) return RequestResult::busy;
        // Replace a failed pending request (documented in the header).
        pending_.reset();
        failed_attempts_=0U;
    }
    SaveRequest req;
    req.sequence=next_sequence_;
    next_sequence_=next_sequence_==0xFFFFFFFFU ? 1U : next_sequence_+1U;
    req.next_wave_index=next_wave_index;
    req.clear=clear;
    req.record=record_;   // copy: header and unknown bytes carried in raw
    apply_campaign_wave_clear(req.record,clear);
    req.bytes=serialize_campaign_record(req.record);
    req.record.raw=req.bytes;
    req.record.checksum=controller_pak_checksum(req.bytes);
    pending_=req;
    return RequestResult::issued;
}

AckResult SaveTransaction::acknowledge(std::uint32_t sequence,bool write_succeeded) {
    if (!pending_.has_value()) return AckResult::no_pending;
    if (pending_->sequence!=sequence) return AckResult::sequence_mismatch;
    if (!write_succeeded) {
        ++failed_attempts_;
        return AckResult::write_failed;
    }
    record_=pending_->record;
    pending_.reset();
    failed_attempts_=0U;
    return AckResult::committed;
}

void SaveTransaction::serialize(StateWriter& w) const {
    w.u32(save_serial_version);
    write_bytes(w,serialize_campaign_record(record_));
    w.u32(next_sequence_);
    w.u32(failed_attempts_);
    w.boolean(pending_.has_value());
    if (pending_.has_value()) {
        w.u32(pending_->sequence);
        w.u8(pending_->next_wave_index);
        write_clear(w,pending_->clear);
        // pending_->record is intentionally not written: bytes already equal
        // serialize_campaign_record(pending record), so it is fully derivable.
        write_bytes(w,pending_->bytes);
    }
}

} // namespace battletanx::campaign_save
