#pragma once

// Producers for the campaign_ai ExternalParameters keys that the play
// parameter evaluator reads but does not compute itself (provider tables
// gAiMemberParameterProviders ROM 0xC5980 and gAiOwnerParameterProviders
// ROM 0xC5920). Main image ROM = VRAM - 0x80070000.
//
//   0x1102 func_800E8D64  rail travel / length (unit +0x4E4 / +0x4E0), owner +0x1A5 reverses
//   0x1103 func_800E8DAC  1 - the 0x1102 value
//   0x1108 func_800E8F84  1.0 when the owner +0x1AA unit's heading-relative angle is negative
//   0x1109 func_800E9008  1.0 otherwise (both 0 when +0x1AA == 0xFF)
//   0x1110 func_800E9288  0.2 per queen in the member's carried list (unit +0x98)
//   0x1111 func_800E92B8  1 - the 0x1110 value
//   0x1113 func_800E9318  goal-mode route reachability (modes 5/6/7)
//   0x210E func_800E8768  get_range_progress(owner +0x198..+0x19C, get_frame_time func_8007B1DC)
//   0x2113 func_800E8B6C  1 - {0.5,0.4} by objective base record state
//   0x2116 func_800E89B8  0.2 per queen stored in the objective owner's base
//   0x2117 func_800E8A48  1 - the 0x2116 value
//
// The provider is stateless: every value is recomputed from its inputs each
// tick, so there is nothing to serialize. Evidence classes are in the .cpp.

#include "battletanx/angle.hpp"
#include "battletanx/campaign_ai.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/owner_links.hpp"
#include "battletanx/queen_world.hpp"
#include "battletanx/unit_kinds.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace battletanx::campaign_ai_parameters {

namespace retail_values {
inline constexpr float one=1.0F;                    // D_80074810/814/818/830/834/864/868, D_800747DC/7F0, D_80071100
inline constexpr float owner_chain_weight=0.2F;     // D_800747D4 / D_800747D8 (ROM 0x47D4 / 0x47D8)
inline constexpr float member_chain_weight=0.2F;    // literal in func_800E9288 / func_800E92B8
inline constexpr float base_state0_weight=0.5F;     // D_800747E8 (ROM 0x47E8)
inline constexpr float base_state1_weight=0.4F;     // D_800747EC (ROM 0x47EC)
inline constexpr float near_point_length=0.01F;     // ai_measure_relative_point literal
inline constexpr std::uint8_t no_unit=0xFFU;        // owner +0x1AA / +0x1A8 "none"
} // namespace retail_values

// Missing-data reasons, reported per member so callers can audit policy use.
enum class Gap : std::uint8_t {
    none=0U,
    no_rail=1U,             // 0x1102/0x1103: unit has no rail state (TEMPORARY POLICY ratio 0)
    zero_rail_length=2U,    // 0x1102/0x1103: length <= 0 (retail divides by zero)
    linked_unit_missing=4U, // 0x1108/0x1109: +0x1AA names a slot with no live unit
    no_owner_fields=8U,     // unit.owner >= owner_capacity
    no_snapshot=16U,        // owner_links snapshot absent: 0x2113/0x2116/0x2117 use empty base
    objective_out_of_range=32U, // 0x2113: objective >= owner slots (retail reads past the table)
};
static_assert(std::is_same_v<std::underlying_type_t<Gap>,std::uint8_t>);

// ---------------------------------------------------------------------------
// Per-key formulas (pure).

// func_800E8D64. `rail` null or length <= 0 -> ratio 0 (TEMPORARY POLICY).
[[nodiscard]] float route_progress(const RailState* rail,bool reversed) noexcept;
// func_800E8DAC.
[[nodiscard]] float route_progress_inverse(const RailState* rail,bool reversed) noexcept;

// ai_measure_relative_point (func_800E28DC) head delta (+0x400 heading) with
// the retail 0xFFFF wrap. Positions are (x, z). The angle is the retail
// cosine_to_angle(dz/len) search over `angles`, negated when dx < 0.
[[nodiscard]] std::int32_t relative_head_delta(float self_x,float self_z,float heading_degrees,
                                               float point_x,float point_z,const AngleTable& angles) noexcept;
struct AheadBehind { float behind{}; float ahead{}; };
// func_800E8F84 / func_800E9008. nullopt linked = owner +0x1AA == 0xFF.
// Sign of the heading-relative angle (retail names); not a front/back test.
[[nodiscard]] AheadBehind linked_side(const UnitRecord& member,const UnitRecord* linked,const AngleTable& angles) noexcept;

// 0.2 accumulated once per record, in float, as the retail loops do.
[[nodiscard]] float chain_weight(std::size_t records,float step) noexcept;

// get_range_progress (func_8007B208) with s32 wrapping arithmetic.
[[nodiscard]] float range_progress(std::int32_t start,std::int32_t end,std::int32_t now) noexcept;

// func_800E8B6C. `base` null = no record / not present.
[[nodiscard]] float objective_state_weight(const owner_links::BaseSample* base) noexcept;

// func_800E9318 inputs. Mode is unit +0x9C, goal unit +0xA4.
struct ReachInput {
    std::uint8_t mode{};
    float x{}, z{};             // member +0x24/+0x2C
    float goal_x{}, goal_z{};
    // Mode 5: unit +0xB0 -> +0x05 byte. Meaning UNRESOLVED; caller supplied.
    bool chase_flag{};
    std::int32_t range_source{}; // D_80125858
    const owner_links::RouteProbe* route{}; // nullptr = every probe fails
};
[[nodiscard]] float member_reachability(const ReachInput& input) noexcept;

// ---------------------------------------------------------------------------
// Bulk provider.

struct ProviderInput {
    const UnitPool* units{};
    const CampaignAiController* controller{};      // owner fields + goal modes
    const queen_world::QueenWorld* queens{};       // carried lists (unit +0x98)
    const owner_links::OwnerSnapshot* snapshot{};  // bases (+0x1AF/+0x1B8) and points
    const owner_links::RouteProbe* route{};
    std::span<const std::optional<RailState>> rails{};   // by unit slot; missing = none
    std::span<const bool> chase_flags{};                 // by unit slot; missing = false
    std::int32_t range_source{};
    std::int32_t tick{};  // get_frame_time (func_8007B1DC); must be the same tick passed to CampaignAiController::update
    const AngleTable* angles{};  // ROM cosine table; nullptr = reference_angle_table()
};

struct ProviderReport {
    std::array<std::uint8_t,unit_capacity> gaps{}; // OR of Gap bits per unit slot
    std::uint8_t filled{};                           // live units written
};

// Writes the keys listed at the top plus segment_x/z/dx/dz/length (unit
// +0x4D0..+0x4E0, the same rail record) for each live unit, indexed by slot.
// Every other ExternalParameters field is left as given. Dead or absent
// slots are reset to the zero defaults for the owned fields.
ProviderReport fill(const ProviderInput& input,std::span<ExternalParameters,unit_capacity> out) noexcept;

// Owner objective and anchor points (func_800E377C results mirrored into
// MemberOwnerFields). Snapshot point first, then the owner's base position
// from queen_world (TEMPORARY POLICY), else nullopt.
struct OwnerPoints {
    std::optional<std::array<float,2>> objective_point{};
    std::optional<std::array<float,2>> anchor{};
};
[[nodiscard]] std::optional<std::array<float,2>> owner_point(const owner_links::OwnerSnapshot* snapshot,
                                                             const queen_world::QueenWorld* queens,
                                                             std::uint8_t owner) noexcept;
[[nodiscard]] OwnerPoints owner_points(const owner_links::OwnerSnapshot* snapshot,
                                       const queen_world::QueenWorld* queens,std::uint8_t owner,
                                       std::uint8_t objective) noexcept;
// Applies owner_points to every owner known to the controller, keeping its
// current objective id.
void apply_owner_points(CampaignAiController& controller,const owner_links::OwnerSnapshot* snapshot,
                        const queen_world::QueenWorld* queens) noexcept;

} // namespace battletanx::campaign_ai_parameters
