#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/enemy_control.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace battletanx {

// ---------------------------------------------------------------------------
// Retail constants (USA Rev 0). ROM offsets are main-image VRAM - 0x80070000.
namespace campaign_ai_retail {
// ai_select_best_scored_unit (func_800E3CF0): threshold epsilon D_80074630
// and link-metric scale D_80074634.
inline constexpr float score_epsilon=1.0e-10F;
inline constexpr float score_metric_scale=4.0e-5F;
// func_800E925C: 0.2 per record in the candidate's chain.
inline constexpr float chain_step=0.2F;
// initialize_unit_ai_state (func_800E4104): skill = owner*0.6 + avg_pair*0.4
// (D_8007463C, D_80074640).
inline constexpr float skill_owner_weight=0.6F;
inline constexpr float skill_noise_weight=0.4F;
// func_800A31C0: owner +0x330/+0x334 = profile skill * D_80072DA0.
inline constexpr float owner_skill_scale=100.0F;
// func_800E7628: close range = (lo + (hi-lo)*(D_8007470C - skill)) * D_80074710.
inline constexpr float close_range_one=1.0F;
inline constexpr float close_range_scale=1800.0F;
// func_800E7490 / func_800E7560: health clamp D_800746FC.
inline constexpr float health_clamp=1000.0F;
// func_800EAD9C weights D_800749A0/D_800749A4; func_800EAE30 D_800749A8.
inline constexpr float profile2_lo=0.25F;
inline constexpr float profile2_hi=0.75F;
inline constexpr float profile3_scale=0.1F;
// func_800E21E4 cooldown: owner rate * D_80074504 / D_80074508 compared with
// ai_normalized_elapsed_time (func_800E95A8: elapsed/D_8007488C, 1.0 past 9000).
inline constexpr float fire_rate_scale=0.003333F;
inline constexpr float fire_rate_divisor=16.0F;
inline constexpr std::int32_t elapsed_normalizer=9000;
// ai_update_goal (func_800E5FA8): far = dist / 16000; arrival radii for
// modes 6/12 (500, 300) and mode 9 (40).
inline constexpr float goal_far_distance=16000.0F;
inline constexpr float goal_point_outer=500.0F;
inline constexpr float goal_point_inner=300.0F;
inline constexpr float goal_mode9_arrive=40.0F;
// ai_update_units_round_robin (func_800E63F4): 20 units; stuck timeout
// D_800746B0 = 20 seconds.
inline constexpr std::size_t round_robin_units=20U;
inline constexpr float stuck_seconds=20.0F;
// Retail tick rate used with seconds_to_game_ticks (30 ticks per second, as
// enemy_retail::ticks_per_minute).
inline constexpr float ticks_per_second=30.0F;
// Computer-player profile records inside an opponent wave record (gOpponentWaves
// D_80142DE8, ROM 0xD2DE8, 0x220 bytes each): func_800A31C0 reads
// wave + 0x1C + 0x90*difficulty(+0x217, D_80134E64 option 0..2) + 0x24*row.
inline constexpr std::uint32_t profile_table_offset=0x1CU;
inline constexpr std::uint32_t profile_difficulty_stride=0x90U;
inline constexpr std::uint32_t profile_bytes=0x24U;
inline constexpr std::uint8_t difficulty_levels=3U;
inline constexpr std::uint16_t profile_rows=4U;
} // namespace campaign_ai_retail

// ---------------------------------------------------------------------------
// Difficulty.

// One 0x24-byte computer-player profile record (big-endian).
struct AiProfile {
    float skill_a{};             // +0x00 -> owner +0x190
    float skill_b{};             // +0x04 -> owner +0x194
    std::uint8_t speed{};        // +0x1C -> owner +0x31C
    std::uint8_t rate_primary{}; // +0x1D -> owner +0x320
    std::uint8_t rate_class4{};  // +0x1E -> owner +0x324
    std::uint8_t rate_kind2{};   // +0x1F -> owner +0x328
    std::uint8_t value_32c{};    // +0x20 -> owner +0x32C (consumer unresolved)
};

// Byte offset of a profile inside a wave record; nullopt when out of range.
[[nodiscard]] std::optional<std::uint32_t> ai_profile_offset(std::uint8_t difficulty,
                                                             std::uint16_t row) noexcept;
[[nodiscard]] AiProfile decode_ai_profile(std::span<const std::uint8_t,campaign_ai_retail::profile_bytes> bytes) noexcept;

// Owner difficulty fields as written by func_800A31C0.
struct OwnerDifficulty {
    float skill_a{};
    float skill_b{};
    float skill_a_scaled{};      // +0x330
    float skill_b_scaled{};      // +0x334
    float speed{16.0F};          // +0x31C
    float rate_primary{16.0F};   // +0x320
    float rate_class4{16.0F};    // +0x324
    float rate_kind2{16.0F};     // +0x328
    float value_32c{};           // +0x32C

    void serialize(StateWriter& writer) const;
};

[[nodiscard]] OwnerDifficulty owner_difficulty_from_profile(const AiProfile& profile) noexcept;

// Per-unit skill roll (func_800E4104 slots 0/1): owner*0.6 + random_unit_average_pair*0.4.
// Consumes two generator steps.
[[nodiscard]] float roll_unit_skill(float owner_skill,RetailRandom& rng) noexcept;

// Smallest elapsed tick count at which func_800E21E4 stops cooling for an owner
// rate field; INT32_MAX when the rate can never cool.
[[nodiscard]] std::int32_t fire_cooldown_ticks(float rate) noexcept;

// Engagement profiles chosen by play 4 (states 2..6).
enum class EngagementProfile : std::uint8_t { state2=2U, state3=3U, state4=4U, state5=5U, state6=6U };

struct UnitThresholds {
    std::uint16_t health_threshold{}; // unit +0x35C (consumer unresolved)
    std::uint16_t close_range{};      // unit +0x35E (drive park radius)
};

// func_800EAD9C / func_800EAE30 / func_800EAEA8 / func_800EAF14 / func_800EAF6C.
[[nodiscard]] UnitThresholds apply_engagement_profile(EngagementProfile profile,float skill,
                                                      std::int32_t health,
                                                      std::int32_t max_health) noexcept;

// ---------------------------------------------------------------------------
// Layer 1: perception, as plain values.

struct CandidatePerception {
    EntityId unit{};
    bool active{};       // unit +0x01
    bool targetable{};   // entity_is_targetable
    bool linked{};       // pairwise link record +0x05
    bool idle_mode{};    // unit +0x350 == 1
    bool class4{};       // unit flags & 4
    std::uint8_t chain_length{};
    float link_metric{}; // pairwise link record +0x08
};

enum class PerceptionPolicy : std::uint8_t {
    // The pairwise link table (D_8033AAE8/D_8033B450) is not ported: every
    // opposing unit is linked, the metric is XZ distance, the chain is empty,
    // and candidates are listed in slot order.
    opposing_units_linked_temporary,
};

[[nodiscard]] std::size_t perceive_candidates(const UnitPool& units,EntityId self,std::int32_t tick,
                                              PerceptionPolicy policy,
                                              std::span<CandidatePerception> out);

// Parameters read by the play monitors through ai_get_parameter (func_800E9720).
// Provider-backed keys whose providers are not ported are supplied by the
// caller per member slot and default to 0. Keys computed by this module
// (0x1000..0x1007, 0x1104..0x1107, 0x111B, 0x8100) are not listed here.
struct ExternalParameters {
    float p1003_hold{};  // member slot 3
    float p1100{};
    float p110A{};
    float p110B{};
    float p110D{};
    float p110E{};
    float p1116{};
    float p1117{};
    float p2111{};
    float p8103{};
    // Plays 8/13/19..23 (provider table gAiMemberParameterProviders, ROM 0xC5980).
    float p1102{};       // func_800E8D64 route progress ratio (unit +0x4E4/+0x4E0)
    float p1103{};       // func_800E8DAC 1 - progress ratio
    float p1108{};       // func_800E8F84 linked unit behind (ai_measure_relative_point)
    float p1109{};       // func_800E9008 linked unit ahead
    float p1110{};       // func_800E9288 member link-chain weight (0.2 per record)
    float p1111{};       // func_800E92B8 inverse chain weight
    float p1113{};       // func_800E9318 member metric
    // Owner providers (gAiOwnerParameterProviders, ROM 0xC5920).
    float p210E{};       // func_800E8768 owner timer progress
    float p2113{};       // func_800E8B6C objective chain-state weight
    float p2116{};       // func_800E89B8 owner chain-length weight
    float p2117{};       // func_800E8A48 1 - chain weight
    // Member facts read outside ai_get_parameter.
    std::uint8_t unit_class{};  // unit +0x357 ('A'..'F' = 0x41..0x46), func_800EA2A4
    bool status_one{};          // func_800E3470: skips the unit (func_800E63F4) and picks root 11
    // Route segment (unit +0x4D0 origin, +0x4D8 direction, +0x4E0 length) read by
    // ai_select_owner_target_member (func_800E6D58).
    float segment_x{}, segment_z{}, segment_dx{}, segment_dz{}, segment_length{};
};

// ---------------------------------------------------------------------------
// Layer 2: target scoring.

struct ScoreWeights {
    std::array<float,6> values{};
};
// D_80135878 (ROM 0xC5878), used by ai_step_select_target_unit.
inline constexpr ScoreWeights target_score_weights{{1000.0F,1000.0F,100.0F,200.0F,10.0F,8.0F}};
// D_80135860 (ROM 0xC5860), used by func_800E69D4.
inline constexpr ScoreWeights secondary_score_weights{{0.0F,2.0F,1000.0F,400.0F,40.0F,1.0F}};

[[nodiscard]] float candidate_chain_weight(std::uint8_t chain_length) noexcept;
[[nodiscard]] float score_candidate(const CandidatePerception& candidate,const ScoreWeights& weights,
                                    bool owner_computer) noexcept;

struct ScoredTarget {
    EntityId unit{};
    float score{};
    bool found{};
};

// Highest score above weights[0]-1e-10 wins; ties keep the earlier candidate.
[[nodiscard]] ScoredTarget select_best_scored(std::span<const CandidatePerception> candidates,
                                              const ScoreWeights& weights,bool owner_computer) noexcept;

// ---------------------------------------------------------------------------
// Goals (unit +0x9C move goal, +0xB4 aim goal).

// object_list (2) is the aim-goal form written by func_800E3980 with
// enabled=0 (ai_dispatch_owner_position, func_800E6BBC): aim at an owner
// record's position. It never requests a route.
enum class GoalMode : std::uint8_t {
    none=0U, unit=1U, object_list=2U, chase=5U, point=6U, point_route=7U, follow=8U, path=9U, evasion=10U, point_alt=12U,
};

struct AiGoal {
    GoalMode mode{GoalMode::none};
    EntityId target{};
    float x{}, z{};
    bool has_point{};
    void serialize(StateWriter& writer) const;
};

// ---------------------------------------------------------------------------
// Layer 3: play stack (ai_play_stack_update, func_800EA478).

inline constexpr std::size_t play_stack_capacity=8U;

struct PlayFrame {
    std::uint8_t play{};
    std::int8_t state{};
};

enum class PlayStatus : std::uint8_t {
    running,
    unsupported_play,  // the play's script is not ported
    stack_overflow,
};

struct PlayStack {
    std::array<PlayFrame,play_stack_capacity> frames{};
    std::uint8_t depth{};
    void serialize(StateWriter& writer) const;
};

// Root play for a member (func_800EA2A4, context kind 2).
[[nodiscard]] std::uint8_t select_member_root_play(std::uint8_t unit_class,bool class4,bool status_one) noexcept;
// True for every play this member play runner runs.
[[nodiscard]] bool play_supported(std::uint8_t play) noexcept;

// Which context runs a retail play (script table D_80137888, plays 1..28).
// member: reachable from a member root (func_800EA2A4 kind 2), from a role
//   table (gAiPlaySlotTables, ROM 0xC5CF8) or pushed by such a play.
// owner: reached only from owner roots (kind 1); ported in campaign_owner_plays.
// unreachable: neither a root, a role-table entry, nor pushed by any script step.
enum class PlayScope : std::uint8_t { invalid, member, owner, unreachable };
static_assert(std::is_same_v<std::underlying_type_t<PlayScope>,std::uint8_t>);
[[nodiscard]] PlayScope play_scope(std::uint8_t play) noexcept;

// ---------------------------------------------------------------------------
// Layer 4: route request / response. Planning lives in another module.

struct RouteRequest {
    EntityId unit{};
    GoalMode mode{GoalMode::none};
    float from_x{}, from_z{};
    float to_x{}, to_z{};
    std::uint32_t serial{};
};

struct RouteResponse {
    EntityId unit{};
    std::uint32_t serial{};
    bool reachable{};
    float waypoint_x{}, waypoint_z{};  // current steering point
    float nav_distance{};              // remaining route length
    std::uint8_t nav_state{};          // 1 = final leg (modes 6/12 arrival)
};

// ---------------------------------------------------------------------------
// Layer 5 and controller.

enum class MoveGoalPolicy : std::uint8_t {
    // Member move goals are assigned by owner plays (unported): with a unit
    // aim goal and no move goal, chase the aim target (mode 5).
    chase_aim_target_temporary,
    // Leave move goals to assign_move_goal().
    external_only,
};

enum class RouteFallback : std::uint8_t {
    // Retail does not drive until a route exists (func_800E2DE8 gate).
    hold_until_route,
    // Steer straight at the goal until a route response arrives.
    direct_line_temporary,
};

enum class CampaignAiStatus : std::uint8_t {
    ok,
    owner_difficulty_missing,  // ran with OwnerDifficulty defaults
    unsupported_play,
    stack_overflow,
};

struct CampaignAiCommand {
    EnemyCommand command{};
    CampaignAiStatus status{CampaignAiStatus::ok};
    std::uint8_t top_play{};
    std::uint8_t unsupported_play{};
    bool self_destruct{};        // stuck timer expiry (ai_award_self_kill_credit)
    bool owner_event{};          // func_800EB440 emitted the 0x1003 owner event
    bool owner_command_cleared{};// func_800EB110
    // ai_update_random_metric (func_800E7168) results.
    std::uint8_t ammo_grant_slot{0xFFU};   // unit +0x480 slot gaining 4 (0xFF none)
    bool marker_request{};                 // metric 8: spawn_effect_kind_17 marker
    // release_target (func_800E4A18): D_8033AAC8[owner] = 1.
    bool owner_release_request{};
    // ai_clear_linked_owner_value (func_800E73A4): owner +0x4FC -> +0x18 = 0.
    bool owner_linked_value_cleared{};
};

struct CampaignAiConfig {
    PerceptionPolicy perception{PerceptionPolicy::opposing_units_linked_temporary};
    MoveGoalPolicy move_goal{MoveGoalPolicy::chase_aim_target_temporary};
    RouteFallback route_fallback{RouteFallback::direct_line_temporary};
    // gAiDistanceScale (func_800E1ED0) is set at runtime and not recovered.
    float distance_scale{1000.0F};
    // Mission length used by the fire ramp (level +0x21C, not recovered).
    float mission_minutes{10.0F};
    // Retail "kind == 2" selects owner +0x328; its port kind is unresolved.
    std::optional<retail::UnitKind> retail_kind2{};
    // get_difficulty_level (D_80134E64 option 0..2); func_800E7168 does nothing at >= 2.
    std::uint8_t difficulty_level{};
    // Level record +0x214/+0x215/+0x216: chance*255 for metrics 8/3/2.
    std::array<std::uint8_t,3> metric_levels{};
};

// Owner-record fields written or read by member plays. The owner-play module
// (campaign_owner_plays) owns role (+0x14C) and objective (+0x1A8); the caller
// mirrors the objective here each tick with set_owner_objective.
struct MemberOwnerFields {
    std::uint8_t objective{0xFFU};       // owner +0x1A8 (owner record index, 0xFF none)
    std::optional<std::array<float,2>> objective_point{}; // func_800E377C(objective)
    std::optional<std::array<float,2>> anchor{};          // func_800E377C(this owner)
    std::uint8_t target_member{0xFFU};   // owner +0x1AA (func_800E6D58), unit slot
    float action_value{};                // owner +0x1A0 (func_800E7064)
    bool flag_active{};                  // owner +0x1A4 (func_800E70B0/func_800E70C4)
    bool flag_requested{};               // owner +0x1A5
    std::int32_t timer_start{};          // owner +0x198 (func_800E70DC)
    std::int32_t timer_end{};            // owner +0x19C
    bool release_pending{};              // D_8033AAC8[owner]

    void serialize(StateWriter& writer) const;
};

enum class PlayAssignStatus : std::uint8_t { ok, unchanged, unknown_unit, not_member_play };
static_assert(std::is_same_v<std::underlying_type_t<PlayAssignStatus>,std::uint8_t>);
// force_member_root_play_status result. pending_update: the slot holds a live
// entity that update() has not adopted yet (retry after the next update()).
enum class ForceRootPlayStatus : std::uint8_t { ok, invalid_play, unknown_unit, pending_update };
static_assert(std::is_same_v<std::underlying_type_t<ForceRootPlayStatus>,std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<GoalMode>,std::uint8_t>);

struct CampaignAiUnitState {
    EntityId unit{};
    float skill0{};               // slot 0x1000
    float skill1{};               // slot 0x1001
    float contact{};              // slot 0x1002
    float move_near{}, move_far{1.0F}, aim_near{}, aim_far{1.0F}; // 0x1004..0x1007
    UnitThresholds thresholds{};
    AiGoal move{};
    AiGoal aim{};
    PlayStack stack{};
    EntityId owner_suggestion{};
    DriveTracker tracker{};
    std::int32_t last_fire_tick{retail::never_fired_tick};
    std::int32_t stuck_deadline{};
    bool had_contact{};
    std::int32_t deadline{};      // unit +0x360 (func_800E73E0 / func_800E96C0)
    bool flag_2ba{};              // unit +0x2BA (func_800E7380 / func_800E7394)
    std::uint8_t last_metric{};   // unit +0x474 (func_800E7168)
    std::optional<RouteResponse> route{};
    std::uint32_t pending_serial{};

    void serialize(StateWriter& writer) const;
};

struct CampaignAiFrame {
    std::int32_t tick{};
    bool route_budget{};          // the retail *budget passed to func_800E63F4
    std::span<const ExternalParameters> parameters{}; // by slot; missing = zeros
};

struct CampaignAiOutput {
    std::size_t commands{};
    std::size_t routes{};
};

class CampaignAiController {
public:
    explicit CampaignAiController(CampaignAiConfig config={}) noexcept : config_(config) {}

    void set_owner_difficulty(std::uint8_t owner,const OwnerDifficulty& difficulty) noexcept;
    // Owner-level objective (modes 6/7/8/9/12, or 5 on a unit).
    bool assign_move_goal(EntityId unit,const AiGoal& goal) noexcept;
    // Accepts a response for the last request of that unit (serial must match).
    bool apply_route_response(const RouteResponse& response) noexcept;

    // Owner-play hooks. assign_member_play is ai_assign_member_play_slots'
    // per-member step: unwind the member stack to depth 0 and push play,
    // skipped when frames[0] already holds that play.
    PlayAssignStatus assign_member_play(EntityId unit,std::uint8_t play) noexcept;
    // Unconditional form used by campaign_owner_plays for AssignResult.changes:
    // ai_play_stack_unwind(member,0) + ai_play_stack_push(stack,play). Accepts
    // any play 1..28; a play this runner does not run is reported as
    // unsupported_play on the next update. False for an unknown unit or play 0/>28.
    // ORDERING: a unit is known only after update() has seen its slot, so run
    // CampaignAiController::update before update_owner_plays in each tick. A
    // unit spawned this tick returns false (pending_update) and the caller must
    // re-issue the change after the next update(); it is never silently kept.
    bool force_member_root_play(EntityId unit,std::uint8_t play) noexcept;
    // Same operation with an explicit result the caller can check and retry on.
    [[nodiscard]] ForceRootPlayStatus force_member_root_play_status(EntityId unit,std::uint8_t play) noexcept;
    // Retail unit +0x9C (move-goal mode) for owner_plays::MemberView::state.
    [[nodiscard]] std::uint8_t member_goal_mode(EntityId unit) const noexcept;
    // frames[0].play, or 0 for an empty stack / unknown unit.
    [[nodiscard]] std::uint8_t member_root_play(EntityId unit) const noexcept;
    void set_owner_objective(std::uint8_t owner,std::uint8_t objective,
                             std::optional<std::array<float,2>> objective_point) noexcept;
    void set_owner_anchor(std::uint8_t owner,std::optional<std::array<float,2>> anchor) noexcept;
    [[nodiscard]] const MemberOwnerFields* owner_fields(std::uint8_t owner) const noexcept {
        return owner<owner_capacity ? &owner_fields_[owner] : nullptr;
    }
    MemberOwnerFields* owner_fields(std::uint8_t owner) noexcept {
        return owner<owner_capacity ? &owner_fields_[owner] : nullptr;
    }

    CampaignAiOutput update(const UnitPool& units,const std::array<OwnerRecord,owner_capacity>& owners,
                            const AngleTable& angles,RetailRandom& rng,const CampaignAiFrame& frame,
                            std::span<CampaignAiCommand> commands,std::span<RouteRequest> routes);

    [[nodiscard]] const CampaignAiUnitState* state(std::size_t slot) const noexcept {
        return slot<unit_capacity ? &state_[slot] : nullptr;
    }
    [[nodiscard]] std::uint8_t round_robin_unit() const noexcept { return round_robin_; }
    [[nodiscard]] const CampaignAiConfig& config() const noexcept { return config_; }

    void serialize(StateWriter& writer) const;

private:
    CampaignAiConfig config_;
    std::array<CampaignAiUnitState,unit_capacity> state_{};
    std::array<std::optional<OwnerDifficulty>,owner_capacity> owners_{};
    std::array<MemberOwnerFields,owner_capacity> owner_fields_{};
    std::uint8_t round_robin_{};
    std::uint32_t serial_{};
};

} // namespace battletanx
