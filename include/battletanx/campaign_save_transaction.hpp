#pragma once

// Pure save-transaction layer over CampaignRecord / ControllerPakRecord.
//
// Retail flow (run_campaign_waves, func_8007B280): after a wave the index is
// advanced and, only if the save-prompt flag (D_8012579B) is set,
// write_controller_pak_progress (func_800D0828) stores the fields into the
// live save record and writes the Controller Pak. Retail mutates the live
// record before the write and ignores the write result. This layer instead
// builds the new record as a pending request and only replaces the
// authoritative record when the host acknowledges a successful write.
// No file I/O happens here; the host calls write_controller_pak_record.

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace battletanx::campaign_save {

// Wave-table size comes from battletanx::campaign_wave_count
// (campaign_catalog.hpp); this is its 8-bit form for wave indices.
inline constexpr std::uint8_t save_wave_limit=static_cast<std::uint8_t>(battletanx::campaign_wave_count);
static_assert(battletanx::campaign_wave_count==17U);
// Largest next_wave_index a save may carry. Retail never saves after the
// final wave (index 16), so 16 (= save after clearing index 15) is the last.
inline constexpr std::uint8_t max_save_next_wave_index=static_cast<std::uint8_t>(save_wave_limit-1U);
// Version word written first by SaveTransaction::serialize.
inline constexpr std::uint32_t save_serial_version=1U;
// CampaignWave.kind value that advances even on params.result==0.
inline constexpr std::uint8_t loss_advances_wave_kind=7U;
// get_game_mode()->value04 value for which the post-wave save prompt is
// suppressed (update_match_state.c, state 7).
inline constexpr std::uint32_t prompt_suppressed_mode_id=0x10U;

enum class WaveOutcome : std::uint8_t { won=0U, lost=1U, quit=2U };
static_assert(sizeof(WaveOutcome)==1U);

// Mirrors the D_80134E5C==0 branch of update_match_state state 7.
// Button roles depend on the branch (pressed bits 0x20 / 0x10):
//  - non-kind-7, status 2 (won): 0x20 sets the flag (unless mode_id==0x10),
//    then 0x10 clears it.
//  - non-kind-7, status 0: 0x10 clears the flag and 0x20 also clears it
//    (retail buttons=pressed&0x20); neither sets it.
//  - kind 7, status 2: either 0x20 or 0x10 clears the flag.
//  - kind 7, status 0: 0x20 sets the flag, then 0x10 clears it.
//  - any other status: flag unchanged.
// player_status is func_800A42B0(entity 0): 0 = alive/normal, 2 = wave won.
struct SavePromptInput {
    bool current_flag=false;        // D_8012579B before this frame
    bool mode_kind7=false;          // get_game_mode()->kind==7
    std::uint32_t mode_id=0U;       // get_game_mode()->value04
    std::uint8_t player_status=0U;  // func_800A42B0(get_entity_record(0))
    std::uint16_t pressed=0U;       // read_player_menu_directions bits
};
[[nodiscard]] bool evaluate_save_prompt(const SavePromptInput& input) noexcept;

// Everything the wave loop knows when a wave ends.
struct WaveEnd {
    std::uint8_t wave_index=0U;     // D_801B4AD0 before increment
    std::uint8_t wave_kind=0U;      // gOpponentWaves[index].kind
    WaveOutcome outcome=WaveOutcome::won;
    bool save_flag=false;           // D_8012579B
    // gOpponentWaves[index+1].id. Unused for the final wave: retail never
    // saves there, so its one-past-table read is unreachable.
    std::uint8_t next_wave_id=0U;
    std::int32_t carry=0;           // get_entity_record(0)->value210
    std::int32_t unit_type=0;       // D_80134E78->value10 (loadout)
    std::int16_t bonus=0;           // params.value08, accumulated bonus
    std::uint8_t control_scheme=0U; // D_803283B8
    CampaignDifficulty difficulty=CampaignDifficulty::easy; // D_80134E64
    std::uint8_t powerups=0U;       // D_80134E65
    std::uint8_t powerup_regen=0U;  // D_80134E66
    std::uint8_t base_defense=0U;   // D_80134E67
    std::uint8_t unlimited_ammo=0U; // D_80134E69
    std::int32_t sfx_volume=0;      // gSoundVolume D_80125790
    std::int32_t music_volume=0;    // gMusicVolume D_80125794
    std::array<std::uint32_t,campaign_binding_words> bindings{}; // D_803282A8
};

enum class WaveStep : std::uint8_t {
    quit=0U,           // D_80125798 set: leave loop, nothing saved
    retry=1U,          // lost a non-kind-7 wave: leave loop, nothing saved
    advance=2U,        // next wave, no save (prompt declined)
    advance_save=3U,   // next wave and a save request
    complete=4U,       // index reached 17: gFrontendState=19; never saves
    invalid_wave=5U,   // wave_index>=17: not a table row; nothing saved
};
static_assert(sizeof(WaveStep)==1U);

struct WaveDecision {
    WaveStep step=WaveStep::quit;
    std::uint8_t next_wave_index=0U;
    bool save=false;
};
[[nodiscard]] WaveDecision decide_wave_step(const WaveEnd& end) noexcept;

// A write the host must perform and acknowledge. `bytes` is the complete
// 256-byte record with checksum already computed by serialize_campaign_record.
struct SaveRequest {
    std::uint32_t sequence=0U;      // non-zero; matched by acknowledge()
    std::uint8_t next_wave_index=0U;
    CampaignWaveClear clear{};
    CampaignRecord record{};        // candidate authoritative record
    ControllerPakRecord bytes{};    // what to write
};

enum class AckResult : std::uint8_t {
    committed=0U,
    write_failed=1U,     // record unchanged; request stays pending for retry
    no_pending=2U,
    sequence_mismatch=3U,
};
static_assert(sizeof(AckResult)==1U);

enum class RequestResult : std::uint8_t {
    issued=0U,
    not_saving=1U,       // decision does not save
    busy=2U,             // previous request unacknowledged and not yet failed
    invalid_wave_index=3U, // next_wave_index > max_save_next_wave_index (16)
};
static_assert(sizeof(RequestResult)==1U);

[[nodiscard]] CampaignWaveClear wave_clear_from(const WaveEnd& end) noexcept;

class SaveTransaction {
public:
    explicit SaveTransaction(const CampaignRecord& authoritative);

    [[nodiscard]] const CampaignRecord& authoritative() const noexcept { return record_; }
    [[nodiscard]] const std::optional<SaveRequest>& pending() const noexcept { return pending_; }

    // Builds a request when decide_wave_step(end).save; never touches the
    // authoritative record.
    RequestResult request(const WaveEnd& end);
    // Explicit request from a prepared clear (W1 adapter path).
    // next_wave_index may be 0..16; 17+ returns invalid_wave_index because
    // retail never saves after the final wave (see decide_wave_step).
    // Pending policy: a pending request that has never failed makes this
    // return busy. A pending request whose write has failed at least once
    // (failed_attempts()>0) is replaced: the new request gets a fresh
    // sequence, failed_attempts() resets to 0, and acks for the old
    // sequence then return sequence_mismatch.
    RequestResult request(std::uint8_t next_wave_index,const CampaignWaveClear& clear);
    // Host reports the write outcome for `sequence`.
    AckResult acknowledge(std::uint32_t sequence,bool write_succeeded);
    // Drops the pending request (e.g. player gives up retrying); also
    // resets failed_attempts().
    void abandon() noexcept { pending_.reset(); failed_attempts_=0U; }

    [[nodiscard]] std::uint32_t failed_attempts() const noexcept { return failed_attempts_; }
    void serialize(StateWriter& writer) const;

private:
    CampaignRecord record_;
    std::optional<SaveRequest> pending_;
    std::uint32_t next_sequence_=1U;
    std::uint32_t failed_attempts_=0U;
};

} // namespace battletanx::campaign_save
