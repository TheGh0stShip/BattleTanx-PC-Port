#pragma once

// Typed catalog of the 17 retail campaign waves.
//
// Source table: gOpponentWaves (D_80142DE8, ROM 0x0D2DE8), 17 records of
// 0x220 bytes, big-endian. run_campaign_waves (func_8007B280) copies one
// record per wave, patches a few fields and hands it to the match code.
// Every record word is kept as a numeric constant (campaign_wave_raw_words);
// the typed fields below are decoded from those words at compile time.
// Field-by-field evidence and confidence are listed next to each member.

#include "battletanx/campaign_record.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battletanx {

inline constexpr std::size_t campaign_wave_count=17U;
inline constexpr std::size_t campaign_wave_record_size=0x220U;
inline constexpr std::size_t campaign_wave_record_words=campaign_wave_record_size/4U;
inline constexpr std::size_t campaign_owner_slots=4U;
inline constexpr std::size_t campaign_skill_groups=3U;
inline constexpr std::size_t campaign_skill_profile_size=0x24U;
inline constexpr std::uint32_t campaign_wave_table_vram=0x80142DE8U;
inline constexpr std::uint32_t campaign_wave_table_rom=0x0D2DE8U;
// Gun damage / fire-rate tables the skill profiles point at (s16[4] each).
inline constexpr std::uint32_t campaign_gun_table_vram=0x80142CF8U;
inline constexpr std::uint32_t campaign_gun_table_rom=0x0D2CF8U;
inline constexpr std::size_t campaign_gun_table_count=28U;
// Briefing image index table read as D_801416F8[level] by
// load_level_support_assets (ROM 0x0D16F8).
inline constexpr std::uint32_t campaign_briefing_index_rom=0x0D16F8U;
// Main program image: ROM offset = VRAM - 0x80070000.
inline constexpr std::uint32_t campaign_program_vram_bias=0x80070000U;

using CampaignRawWave=std::array<std::uint32_t,campaign_wave_record_words>;

// Win rule applied by update_match_state (func_8007F2A0, state 1) per kind.
enum class CampaignObjectiveKind : std::uint8_t {
    // kinds 2/5: ends when fewer than two teams or no human squad remain.
    last_team_standing,
    // kinds 3/6: ends when mark_entities_for_matching_slot (func_800A6798)
    // finds a goal slot, or player 0 loses its squad. Who fills the goal
    // counters (D_802DF344 / D_802DF340) is UNRESOLVED.
    goal_slot,
    // kind 4: won when player 0's result flag becomes 2 (setter UNRESOLVED);
    // lost when team 0 has no living squad.
    player_win_flag,
    // kind 7: last-team-standing rule inside the endurance repeat loop
    // (play_endurance_wave); the campaign advances whatever the result.
    endurance,
    unresolved,
};

[[nodiscard]] constexpr CampaignObjectiveKind objective_for_game_kind(std::int32_t kind) noexcept {
    switch (kind) {
    case 2: case 5: return CampaignObjectiveKind::last_team_standing;
    case 3: case 6: return CampaignObjectiveKind::goal_slot;
    case 4: return CampaignObjectiveKind::player_win_flag;
    case 7: return CampaignObjectiveKind::endurance;
    default: return CampaignObjectiveKind::unresolved;
    }
}

// AiSkillProfile, record +0x1C + (group*4 + owner)*0x24. Member names follow
// apply_team_tuning (func_8007CCD0); the group index is record +0x217, which
// run_campaign_waves overwrites with D_80134E64 (STRONG INFERENCE: the
// selected difficulty).
struct CampaignSkillProfile {
    float aggressiveness=0.0F;        // +0x00 f32
    float intelligence=0.0F;          // +0x04 f32
    std::int32_t offense=0;           // +0x08, copied to limit[0] for computer owners
    std::int32_t defense=0;           // +0x0C, limit[1]
    std::int32_t gun_resurrection=0;  // +0x10, limit[2]
    std::uint32_t gun_damage_vram=0U; // +0x14 pointer to s16[4], 0 = none
    std::uint32_t gun_fire_rate_vram=0U; // +0x18 pointer to s16[4], 0 = none
    std::array<std::uint8_t,5> tint{}; // +0x1C computer speed, computer fire, rail fire, off-rail fire, rail speed
};

struct CampaignOwnerSetup {
    std::int32_t unit_kind=0;     // +0x0C + 4*owner: controller-roster index
    std::int32_t team=0;          // +0x1D4 + 4*owner
    bool computer_controlled=false; // setup_players mode 1: owner 0 human, others computer-driven
    std::int32_t initial_score=0; // +0x1E4 + 4*owner (record +0x210)
    std::int32_t reserve=0;       // +0x1F4 + 4*owner (record +0x208); -1 raw, meaning UNRESOLVED
    std::array<CampaignSkillProfile,campaign_skill_groups> skills{};
};

struct CampaignWaveDefinition {
    std::uint8_t wave_index=0U;          // position in gOpponentWaves
    std::int32_t game_kind=0;            // +0x000
    std::int32_t mission_id=0;           // +0x004: lookup key and world stream index
    std::int32_t raw_field_08=0;         // +0x008, equals wave_index in retail; UNRESOLVED
    std::int32_t player_unit_kind=0;     // +0x00C roster index; replaced by selection unless kind 7
    bool player_unit_from_selection=false;
    std::uint8_t world_stream=0U;        // = mission_id (play_campaign_wave -> func_80081AD0)
    bool navigation_available=false;     // a navigation stream exists for world_stream
    CampaignObjectiveKind objective=CampaignObjectiveKind::unresolved;
    std::int32_t view_mode=0;            // +0x1CC, 1 = single view
    std::uint8_t countdown_unit=0U;      // apply_mode_display_rows: 24 if view_mode==1 else 18
    bool resets_team_counters=false;     // apply_mode_display_rows kinds 0/3/4/6
    std::int32_t owner_count=0;          // +0x1D0
    std::array<CampaignOwnerSetup,campaign_owner_slots> owners{};
    std::int32_t raw_field_204=0;        // +0x204, 2 in every wave; UNRESOLVED
    std::int32_t endurance_kills=0;      // +0x208, kind 7 starting kill target ("waves")
    std::array<std::uint16_t,4> player_bounds{}; // +0x20C, owner 0 record +0x3A/+0x3E/+0x40/+0x42
    std::array<std::uint8_t,3> scene_rgb{}; // +0x214, preserved across retries; use UNRESOLVED
    std::int32_t par_time_seconds=0;     // +0x218, get_match_time_bonus (func_8007D384)
    std::uint32_t raw_field_21C=0U;      // +0x21C, f32 bits (30.0 in all waves); UNRESOLVED
    std::int8_t music_transition=-1;     // select_screen_transition(mission_id), -1 = stop
    std::int32_t briefing_image=-1;      // D_801416F8[mission_id] -> D_80141768 index
    std::optional<std::uint8_t> chapter_sequence; // run_state_loop_after_sequence_reset arg
    std::optional<std::uint8_t> next_wave_index;
    std::uint8_t saved_mission_id_after_clear=0U; // byte func_800D0828 stores
    bool advances_on_failure=false;      // kind 7 advances even when lost
    bool save_prompt_after_win=true;     // state 7 "save" button disabled when id == 16
};

[[nodiscard]] std::span<const CampaignRawWave,campaign_wave_count> campaign_wave_raw_words() noexcept;
[[nodiscard]] std::span<const std::array<std::int16_t,4>,campaign_gun_table_count> campaign_gun_tables() noexcept;
// Briefing image indices for world streams 0..17 (D_801416F8[0..17]).
[[nodiscard]] std::span<const std::int32_t,18> campaign_briefing_indices() noexcept;

[[nodiscard]] std::span<const CampaignWaveDefinition,campaign_wave_count> campaign_wave_definitions() noexcept;
// run_campaign_waves lookup: the last entry whose id matches wins.
[[nodiscard]] const CampaignWaveDefinition* find_campaign_wave(std::int32_t mission_id) noexcept;
[[nodiscard]] const CampaignWaveDefinition* next_campaign_wave(const CampaignWaveDefinition& wave) noexcept;
// Mission id stored at save offset 0x08 -> wave to resume (nullptr = no wave;
// the original returns to the menu in that case).
[[nodiscard]] const CampaignWaveDefinition* campaign_wave_for_record(const CampaignRecord& record) noexcept;
[[nodiscard]] std::optional<std::array<std::int16_t,4>> campaign_gun_table(std::uint32_t vram) noexcept;

// Endurance (kind 7) per-owner split from setup_players for owners 1..3.
struct CampaignOwnerPopulation {
    std::int32_t lives=0;
    std::int32_t reserve=0;
};
[[nodiscard]] CampaignOwnerPopulation endurance_owner_population(std::int32_t kills,std::uint8_t owner) noexcept;

// The record run_campaign_waves builds before play_endurance_wave.
struct CampaignWaveLaunch {
    std::uint8_t wave_index=0U;
    std::int32_t game_kind=0;
    std::int32_t world_stream=0;
    std::int32_t player_unit_kind=0;     // controller-roster index; name kept for API stability
    std::uint8_t skill_group=0U;
    std::uint16_t player_bonus=0U;   // +0x20C after += bonus
    std::int32_t player_score=0;     // +0x1E4 after += carry
    void serialize(StateWriter& out) const;
};
[[nodiscard]] CampaignWaveLaunch make_campaign_wave_launch(const CampaignWaveDefinition& wave,
    std::int32_t carry,std::int16_t bonus,std::uint8_t skill_group,std::int32_t selected_unit_kind) noexcept;

enum class CampaignCatalogIssueCode : std::uint8_t {
    wrong_wave_count,
    duplicate_mission_id,
    wave_index_mismatch,
    world_stream_out_of_range,
    navigation_missing,
    transition_outside_catalog,
    objective_unresolved,
    owner_count_out_of_range,
    gun_table_missing,
    briefing_image_out_of_range,
};

struct CampaignCatalogIssue {
    CampaignCatalogIssueCode code=CampaignCatalogIssueCode::wrong_wave_count;
    std::uint8_t wave_index=0U;
};

struct CampaignCatalogAudit {
    std::size_t wave_count=0U;
    std::size_t unique_mission_ids=0U;
    std::size_t chapter_starts=0U;
    std::size_t endurance_waves=0U;
    std::size_t resolved_objectives=0U;
    std::vector<CampaignCatalogIssue> issues;
    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
};

[[nodiscard]] CampaignCatalogAudit audit_campaign_catalog();

} // namespace battletanx
