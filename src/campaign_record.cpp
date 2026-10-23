#include "battletanx/campaign_record.hpp"

namespace battletanx {
namespace {
namespace off=campaign_offsets;

std::uint32_t read_u32(const ControllerPakRecord& r,std::size_t at) noexcept {
    return static_cast<std::uint32_t>(r.bytes[at])<<24U |
           static_cast<std::uint32_t>(r.bytes[at+1U])<<16U |
           static_cast<std::uint32_t>(r.bytes[at+2U])<<8U |
           static_cast<std::uint32_t>(r.bytes[at+3U]);
}

std::uint16_t read_u16(const ControllerPakRecord& r,std::size_t at) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(r.bytes[at])<<8U|r.bytes[at+1U]);
}

void write_u32(ControllerPakRecord& r,std::size_t at,std::uint32_t v) noexcept {
    r.bytes[at]=static_cast<std::uint8_t>(v>>24U);
    r.bytes[at+1U]=static_cast<std::uint8_t>(v>>16U);
    r.bytes[at+2U]=static_cast<std::uint8_t>(v>>8U);
    r.bytes[at+3U]=static_cast<std::uint8_t>(v);
}

void write_u16(ControllerPakRecord& r,std::size_t at,std::uint16_t v) noexcept {
    r.bytes[at]=static_cast<std::uint8_t>(v>>8U);
    r.bytes[at+1U]=static_cast<std::uint8_t>(v);
}

bool volume_valid(std::int32_t v) noexcept { return v>=0 && v<=7; }

std::optional<CampaignRecord> fail(CampaignRecordError* error,CampaignRecordErrorCode code,
                                   std::size_t at) {
    if (error!=nullptr) *error=CampaignRecordError{code,at};
    return std::nullopt;
}
} // namespace

std::optional<CampaignRecord> parse_campaign_record(const ControllerPakRecord& record,
                                                    CampaignRecordError* error) {
    using Code=CampaignRecordErrorCode;
    if (!controller_pak_checksum_valid(record)) return fail(error,Code::checksum_mismatch,off::checksum);
    const std::uint8_t difficulty=record.bytes[off::difficulty];
    if (difficulty>2U) return fail(error,Code::difficulty_out_of_range,off::difficulty);
    const auto sfx=static_cast<std::int32_t>(read_u32(record,off::sfx_volume));
    if (!volume_valid(sfx)) return fail(error,Code::sfx_volume_out_of_range,off::sfx_volume);
    const auto music=static_cast<std::int32_t>(read_u32(record,off::music_volume));
    if (!volume_valid(music)) return fail(error,Code::music_volume_out_of_range,off::music_volume);

    CampaignRecord out;
    out.raw=record;
    out.checksum=read_u32(record,off::checksum);
    out.header=read_u32(record,off::header);
    out.mission_id=record.bytes[off::mission_id];
    out.carry=static_cast<std::int32_t>(read_u32(record,off::carry));
    out.unit_type=static_cast<std::int32_t>(read_u32(record,off::unit_type));
    out.bonus=static_cast<std::int16_t>(read_u16(record,off::bonus));
    out.difficulty=static_cast<CampaignDifficulty>(difficulty);
    out.powerups.raw=record.bytes[off::powerups];
    out.powerup_regen.raw=record.bytes[off::powerup_regen];
    out.control_scheme=record.bytes[off::control_scheme];
    out.base_defense.raw=record.bytes[off::base_defense];
    out.unlimited_ammo.raw=record.bytes[off::unlimited_ammo];
    out.sfx_volume=sfx;
    out.music_volume=music;
    for (std::size_t i=0U; i<campaign_binding_words; ++i) {
        out.bindings[i]=read_u32(record,off::bindings+i*4U);
    }
    if (error!=nullptr) *error=CampaignRecordError{};
    return out;
}

ControllerPakRecord serialize_campaign_record(const CampaignRecord& record) {
    ControllerPakRecord out=record.raw;
    write_u32(out,off::header,record.header);
    out.bytes[off::mission_id]=record.mission_id;
    write_u32(out,off::carry,static_cast<std::uint32_t>(record.carry));
    write_u32(out,off::unit_type,static_cast<std::uint32_t>(record.unit_type));
    write_u16(out,off::bonus,static_cast<std::uint16_t>(record.bonus));
    out.bytes[off::difficulty]=static_cast<std::uint8_t>(record.difficulty);
    out.bytes[off::powerups]=record.powerups.raw;
    out.bytes[off::powerup_regen]=record.powerup_regen.raw;
    out.bytes[off::control_scheme]=record.control_scheme;
    out.bytes[off::base_defense]=record.base_defense.raw;
    out.bytes[off::unlimited_ammo]=record.unlimited_ammo.raw;
    write_u32(out,off::sfx_volume,static_cast<std::uint32_t>(record.sfx_volume));
    write_u32(out,off::music_volume,static_cast<std::uint32_t>(record.music_volume));
    for (std::size_t i=0U; i<campaign_binding_words; ++i) {
        write_u32(out,off::bindings+i*4U,record.bindings[i]);
    }
    write_u32(out,off::checksum,controller_pak_checksum(out));
    return out;
}

CampaignRecord default_campaign_record() {
    CampaignRecord out;
    out.header=campaign_header_default;
    out.mission_id=1U;
    out.carry=0;
    out.unit_type=9;
    out.bonus=0;
    out.difficulty=CampaignDifficulty::easy;
    out.control_scheme=0U;
    out.sfx_volume=7;
    out.music_volume=5;
    out.raw=serialize_campaign_record(out);
    out.checksum=read_u32(out.raw,off::checksum);
    return out;
}

void apply_campaign_wave_clear(CampaignRecord& record,const CampaignWaveClear& clear) noexcept {
    record.mission_id=clear.mission_id;
    record.difficulty=clear.difficulty;
    record.carry=clear.carry;
    record.unit_type=clear.unit_type;
    record.bonus=clear.bonus;
    record.powerups.raw=clear.powerups;
    record.sfx_volume=clear.sfx_volume;
    record.music_volume=clear.music_volume;
    record.powerup_regen.raw=clear.powerup_regen;
    record.base_defense.raw=clear.base_defense;
    record.unlimited_ammo.raw=clear.unlimited_ammo;
    record.control_scheme=clear.control_scheme;
    record.bindings=clear.bindings;
}

} // namespace battletanx
