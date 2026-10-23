#pragma once

// Owner (squad commander) plays for computer-controlled and human owners.
//
// Retail source: ai_update_owner_play_stack (func_800EA89C) runs the shared
// play-stack runner (ai_play_stack_update, func_800EA478) with a kind-1
// context. Owner plays set the owner role (owner +0x14C) and the owner
// objective (owner +0x1A8), then hand squad members their root plays through
// ai_assign_member_play_slots (func_800E9E5C).
//
// Ported plays: 2, 10, 12, 14 (child of 15), 15, 25, 26. Scripts come from
// gAiScriptData (D_80137888 script table), role tables from gAiPlaySlotTables
// (D_80135CF8), weighted policies from ai_select_weighted_action
// (func_800EA93C). ROM offsets are main-image VRAM - 0x80070000.
//
// Member stacks belong to the member controller (campaign_ai). This module does
// not touch them: it returns MemberPlayAssignment records ("unwind the member
// stack to depth 0, then push play"), which the caller applies.

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace battletanx::owner_plays {

// ---------------------------------------------------------------------------
// Retail constants.
namespace retail_values {
inline constexpr std::size_t squad_members=5U;          // owner +0x00 s16[5]
inline constexpr std::size_t owner_records=4U;
inline constexpr std::size_t stack_capacity=4U;         // owner plays nest at most 2 deep
inline constexpr std::uint8_t no_objective=0xFFU;       // owner +0x1A8 "none"
inline constexpr std::uint8_t objective_limit=5U;       // func_800E6FF8: id >= 5 -> 0xFF
inline constexpr std::uint8_t excluded_unit_class=0x42U; // ai_assign_member_play_slots skip
inline constexpr std::int32_t excluded_member_state=8;  // unit +0x9C == 8 skip
inline constexpr std::size_t policy_option_limit=5U;    // func_800EA93C reads 5 options
inline constexpr float term_epsilon=1.0e-4F;            // 0.0001 requirement terms
// Provider weights (ROM 0x472C..0x47F0, all f32).
inline constexpr float chain_node_weight=0.2F;    // D_8007472C/34/3C/44/4C/50/54, D_800747D0/D8
inline constexpr float flagged_member_weight=1.0F;// D_80074730/38/40/48, D_800747C4/C8/CC
inline constexpr float chain_state0_weight=0.5F;  // D_800747E8
inline constexpr float chain_state1_weight=0.4F;  // D_800747EC
inline constexpr float inverse_base=1.0F;         // D_800747DC, D_800747F0
inline constexpr float random_range=1.0F;         // D_80074720 (func_800E7734)
inline constexpr std::int32_t elapsed_normalizer=9000; // D_8007488C (func_800E95A8)
// Scorer constants (ai_play_scorers.c).
inline constexpr float objective_near=1000.0F;
inline constexpr float objective_scale=2000.0F;
inline constexpr float origin_scale=16000.0F;
} // namespace retail_values

// ---------------------------------------------------------------------------
// Root play (ai_select_root_play, func_800EA2A4, kind 1). CONFIRMED.
// computer = owner +0x1A6; game_mode = *get_game_mode().
[[nodiscard]] std::uint8_t select_owner_root_play(std::int32_t game_mode,bool computer) noexcept;
[[nodiscard]] bool owner_play_supported(std::uint8_t play) noexcept;

// ---------------------------------------------------------------------------
// Role tables (gAiPlaySlotTables, ROM 0xC5CF8; slot runs at ROM 0xC5A50..0xC5D24).

enum class SlotScorer : std::uint8_t {
    first_candidate,       // NULL scorer: first remaining member wins
    on_slot_play,          // func_800E9D80
    keep_current,          // func_800E9A30
    current_or_class4,     // func_800E9A54 (member flags & 4)
    member_mode,           // func_800E9A9C
    objective_distance,    // func_800E9C24
    on_play_status_two,    // func_800E9D30
    origin_proximity,      // func_800E9DA4
};
static_assert(std::is_same_v<std::underlying_type_t<SlotScorer>,std::uint8_t>);

struct RoleSlot {
    std::uint8_t play{};
    SlotScorer scorer{SlotScorer::first_candidate};
};

inline constexpr std::uint8_t role_count=11U; // roles 0..10; role 0 has no table
// Empty span for role 0 or out-of-range roles.
[[nodiscard]] std::span<const RoleSlot> role_slots(std::uint8_t role) noexcept;

// What the scorers read from one squad member.
struct MemberView {
    EntityId unit{};
    bool in_use{};               // unit +0x01
    std::uint8_t unit_class{};   // unit +0x357
    std::int32_t state{};        // unit +0x9C
    std::uint32_t mode{};        // unit +0x18
    std::uint16_t flags{};       // unit +0x14
    float x{}, z{};              // unit +0x24, +0x2C
    std::uint8_t root_play{};    // member stack frames[0].play, 0 = empty
    bool status_one{};           // func_800E3470
    bool status_two{};           // func_800E3460
};

// Axis convention (UNRESOLVED): x is the world x that retail pairs with the
// member's +0x24 (ai_member_horizontal_distance, func_800E1A20, reads it
// against point[1]); z pairs with +0x2C and point[0]. ai_find_object_position
// fills point[1] from owner +0x1B4 and point[0] from +0x1B0, which the decomp
// names "z" and "x" (likely swapped). Producers should build {p.x,p.z} under
// this convention until the +0x1B0/+0x1B4 semantics are confirmed.
struct Point2 {
    float x{}, z{};
};

struct ScoreInput {
    const MemberView* member{};
    std::uint8_t slot_play{};
    // ai_find_object_position(owner objective, ..., 1); nullopt = not found.
    std::optional<Point2> objective_point{};
};

[[nodiscard]] float score_member(SlotScorer scorer,const ScoreInput& input) noexcept;

struct MemberPlayAssignment {
    EntityId unit{};
    std::uint8_t member_slot{};
    std::uint8_t play{};
};

enum class AssignStatus : std::uint8_t {
    ok,
    owner_inactive,   // owner +0x18 == 0: retail returns without work
    no_members,
    role_without_table, // role 0 / >10: retail dereferences NULL (not reproduced)
    stalled,          // no member chosen and the table cannot advance (retail spins)
};

struct AssignResult {
    AssignStatus status{AssignStatus::ok};
    std::array<MemberPlayAssignment,retail_values::squad_members> changes{};
    std::uint8_t change_count{};   // members whose root play differs (stack replaced)
    std::uint8_t matched{};        // members consumed by the table walk
};

// ai_assign_member_play_slots. Deterministic: members in squad-slot order,
// strict ">" so ties keep the earlier member, break at score >= 1.0.
[[nodiscard]] AssignResult assign_member_plays(std::uint8_t role,bool owner_active,
                                               std::span<const MemberView,retail_values::squad_members> members,
                                               std::optional<Point2> objective_point) noexcept;

// ---------------------------------------------------------------------------
// Parameters (ai_get_parameter, func_800E9720) read by owner-play policies.

// Per owner-record facts the providers read. The link chain (owner +0x1B8
// list, +0x1AF flag) is not ported elsewhere, so the caller supplies counts.
struct OwnerFacts {
    bool chain_active{};                     // owner +0x1AF
    std::uint8_t chain_nodes{};              // nodes in owner +0x1B8 ->+0x10 list
    std::optional<std::int32_t> chain_state{}; // owner +0x1B8 -> +0x18 (nullopt = no list)
    bool has_flagged_member{};               // any in-use member with unit +0x98 != 0
};

struct OwnerWorld {
    std::uint8_t owner_count{};              // get_entity_capacity (func_8007AAB0)
    std::array<OwnerFacts,retail_values::owner_records> owners{};
    // Team record D_8033A980[team] (0x52 bytes): u16 list, count at halfword 3.
    // STRONG INFERENCE: the owner indices on this owner's team.
    std::array<std::uint16_t,3> team_list{};
    std::uint8_t team_list_count{};
    // func_800E8934 (0x2111): link records owned by another owner. UNRESOLVED producer.
    std::uint8_t foreign_links{};
    std::int32_t elapsed_ticks{};            // func_800E95A8 input
    std::array<std::optional<Point2>,retail_values::owner_records> objective_points{};
};

enum class ParameterKey : std::uint16_t {
    lacks_team_0=0x2104, lacks_team_1=0x2105, lacks_team_2=0x2106, lacks_team_3=0x2107,
    objective_is_0=0x2108, objective_is_1=0x2109, objective_is_2=0x210A, objective_is_3=0x210B,
    squad_flagged=0x210F, squad_unflagged=0x2110, foreign_links=0x2111,
    objective_chain_state=0x2113, objective_chain_free=0x2117,
    random=0x8100, elapsed=0x8101,
    presence_0=0x810C, presence_1=0x810D, presence_2=0x810E, presence_3=0x810F,
    chain_0=0x8110, chain_1=0x8111, chain_2=0x8112, owner1_active=0x8113,
};
static_assert(std::is_same_v<std::underlying_type_t<ParameterKey>,std::uint16_t>);

struct ParameterContext {
    std::uint8_t owner{};
    std::uint8_t objective{retail_values::no_objective};
    const OwnerWorld* world{};
    RetailRandom* rng{};    // 0x8100 consumes one step per read
};

// nullopt = key not ported for owner contexts.
[[nodiscard]] std::optional<float> owner_parameter(const ParameterContext& context,std::uint16_t key) noexcept;
[[nodiscard]] float normalized_elapsed_time(std::int32_t elapsed_ticks) noexcept;

// ---------------------------------------------------------------------------
// Owner play state and update.

struct OwnerPlayFrame {
    std::uint8_t play{};
    std::int8_t state{};
};

struct OwnerPlayStack {
    std::array<OwnerPlayFrame,retail_values::stack_capacity> frames{};
    std::uint8_t depth{};
    void serialize(StateWriter& writer) const;
};

struct OwnerPlayState {
    OwnerPlayStack stack{};
    std::uint8_t role{};                              // owner +0x14C
    std::uint8_t objective{retail_values::no_objective}; // owner +0x1A8
    void serialize(StateWriter& writer) const;
};

enum class OwnerPlayStatus : std::uint8_t {
    ok,
    inactive,           // owner +0x18 == 0: ai_update_owner_play_stack skips
    unsupported_play,   // root or pushed play outside the ported set
    stack_overflow,
    parameter_unported, // a policy read a key this module does not provide (read as 0)
};
static_assert(std::is_same_v<std::underlying_type_t<OwnerPlayStatus>,std::uint8_t>);

struct OwnerPlayInput {
    std::uint8_t owner{};
    bool active{};                       // owner +0x18 != 0
    bool computer{};                     // owner +0x1A6
    std::int32_t game_mode{};
    std::array<MemberView,retail_values::squad_members> members{};
    const OwnerWorld* world{};
};

struct OwnerPlayResult {
    OwnerPlayStatus status{OwnerPlayStatus::ok};
    AssignResult assignment{};
    bool assigned{};             // ai_assign_member_play_slots ran this update
    bool role_changed{};
    bool objective_changed{};
    bool release_target{};       // func_800E6C5C / func_800E4A18 requested (owner target id)
    bool clear_word_pair{};      // state 4 of play 15 (clear_word_pair)
    std::uint8_t unsupported_play{};
};

// One ai_update_owner_play_stack call.
[[nodiscard]] OwnerPlayResult update_owner_plays(OwnerPlayState& state,const OwnerPlayInput& input,
                                                 RetailRandom& rng) noexcept;

// ---------------------------------------------------------------------------
// Tuning from the wave record (CampaignSkillProfile, record +0x1C).

struct OwnerPlayTuning {
    bool computer{};
    std::int32_t team{};
    float skill_a{};   // owner +0x190 = parameter 0x2000 (aggressiveness)
    float skill_b{};   // owner +0x194 = parameter 0x2001 (intelligence)
};

// nullopt for owner >= wave.owner_count or group >= campaign_skill_groups.
[[nodiscard]] std::optional<OwnerPlayTuning> owner_tuning_from_wave(const CampaignWaveDefinition& wave,
                                                                    std::uint8_t skill_group,
                                                                    std::uint8_t owner) noexcept;

} // namespace battletanx::owner_plays
