#pragma once

// Typed view of the BattleTanx 256-byte Controller Pak save record.
// All multi-byte fields are big-endian, as stored by the original MIPS code.
// Bytes not decoded here are carried verbatim in `raw` and are written back
// unchanged by serialize_campaign_record.

#include "battletanx/controller_pak.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace battletanx {

enum class CampaignDifficulty : std::uint8_t { easy=0U, normal=1U, hard=2U };

enum class CampaignRecordErrorCode : std::uint8_t {
    none=0U,
    checksum_mismatch,
    difficulty_out_of_range,
    sfx_volume_out_of_range,
    music_volume_out_of_range,
};

struct CampaignRecordError {
    CampaignRecordErrorCode code=CampaignRecordErrorCode::none;
    std::size_t offset=0U;
};

namespace campaign_offsets {
inline constexpr std::size_t checksum=0x00U;
inline constexpr std::size_t header=0x04U;
inline constexpr std::size_t mission_id=0x08U;
inline constexpr std::size_t carry=0x0CU;
inline constexpr std::size_t unit_type=0x10U;
inline constexpr std::size_t bonus=0x14U;
inline constexpr std::size_t difficulty=0x16U;
inline constexpr std::size_t powerups=0x17U;
inline constexpr std::size_t powerup_regen=0x18U;
inline constexpr std::size_t control_scheme=0x19U;
inline constexpr std::size_t base_defense=0x1AU;
inline constexpr std::size_t unlimited_ammo=0x1BU;
inline constexpr std::size_t sfx_volume=0x1CU;
inline constexpr std::size_t music_volume=0x20U;
inline constexpr std::size_t bindings=0x24U;
inline constexpr std::size_t reserved_tail=0x68U;
} // namespace campaign_offsets

inline constexpr std::uint32_t campaign_header_default=0x0000042AU;
inline constexpr std::size_t campaign_binding_words=17U;

// On/off option byte. The game stores 0 or 1 but never validates on load, so
// the raw byte is kept: any value round-trips, and non-zero reads as "on".
struct CampaignFlag {
    std::uint8_t raw=0U;
    [[nodiscard]] bool enabled() const noexcept { return raw!=0U; }
    void set(bool on) noexcept { raw=on ? 1U : 0U; }
};

struct CampaignRecord {
    std::uint32_t checksum=0U;     // as read; recomputed on serialize
    std::uint32_t header=0U;       // 0x04, raw; meaning unresolved
    std::uint8_t mission_id=0U;    // 0x08, not range checked (see parse)
    std::int32_t carry=0;          // 0x0C
    std::int32_t unit_type=0;      // 0x10
    std::int16_t bonus=0;          // 0x14
    CampaignDifficulty difficulty=CampaignDifficulty::easy; // 0x16
    CampaignFlag powerups;         // 0x17
    CampaignFlag powerup_regen;    // 0x18
    std::uint8_t control_scheme=0U; // 0x19
    CampaignFlag base_defense;     // 0x1A
    CampaignFlag unlimited_ammo;   // 0x1B
    std::int32_t sfx_volume=0;     // 0x1C, 0..7
    std::int32_t music_volume=0;   // 0x20, 0..7
    std::array<std::uint32_t,campaign_binding_words> bindings{}; // 0x24..0x67
    // Full source record. Reserved bytes 0x09..0x0B and 0x68..0xFF (and any
    // other byte not named above) are written back from here.
    ControllerPakRecord raw{};
};

[[nodiscard]] std::optional<CampaignRecord> parse_campaign_record(
    const ControllerPakRecord& record,CampaignRecordError* error=nullptr);
[[nodiscard]] ControllerPakRecord serialize_campaign_record(const CampaignRecord& record);
// Mirrors reset_session_state_defaults: only the fields that function writes
// are set; everything else is zero (BSS). Bindings are zero because the
// original copies them from a live entity record.
[[nodiscard]] CampaignRecord default_campaign_record();

// Inputs of write_controller_pak_progress (func_800D0828).
struct CampaignWaveClear {
    std::uint8_t mission_id=0U;
    std::int32_t carry=0;
    std::int32_t unit_type=0;
    std::int16_t bonus=0;
    std::uint8_t control_scheme=0U;
    CampaignDifficulty difficulty=CampaignDifficulty::easy;
    std::uint8_t powerups=0U;
    std::uint8_t powerup_regen=0U;
    std::uint8_t base_defense=0U;
    std::uint8_t unlimited_ammo=0U;
    std::int32_t sfx_volume=0;
    std::int32_t music_volume=0;
    std::array<std::uint32_t,campaign_binding_words> bindings{};
};

// Applies every field write_controller_pak_progress stores; header and
// reserved bytes are left as they were.
void apply_campaign_wave_clear(CampaignRecord& record,const CampaignWaveClear& clear) noexcept;

} // namespace battletanx
