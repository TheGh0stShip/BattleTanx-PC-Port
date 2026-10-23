#pragma once

// Owner/unit link tracker: the inputs owner plays read and nothing else
// produces.
//
// Retail sources (main image, ROM = VRAM - 0x80070000):
//   * Unit pair records D_8033AAE8 (12 bytes each, count D_8033B448) and the
//     20x20 index table D_8033B450, built by build_unit_pair_records
//     (func_800E5328), read by get_link_record (func_800E52A0), cleared by
//     clear_link_record (func_800E566C) / clear_unit_link_records (func_800E4094).
//   * Four-phase rotor ai_run_update_phase (func_800E6754, effect kind 11
//     update, one phase per call): 0 update_unit_pairings(1,0);
//     1 update_unit_pairings(0,1) + tracked kind-0x1B sweep; 2 func_800E4AA4;
//     3 round robin (func_800E63F4) with the D_8033B44C 4-call flag.
//     Retail first does `if (task->state==2) *state=task->state;`; that task
//     state passthrough is done by the effect-kind-11 caller, not here.
//   * update_unit_pair_link (func_800E56B0) and resolve_unit_pair
//     (func_800E1F14) act on unit +0x9C (action kind), +0xA0 (action point),
//     +0xB0 (followed unit), +0x344, +0x348, +0x34A.
//   * Owner +0x1AF = base point count (assign_group_object_points,
//     func_800A4B2C), +0x1B8 = base record (create_entity_point_record,
//     func_800A6AB4); base record +0x10 = stored queen list, +0x18 = state.
//   * Unit +0x98 = carried queen list head (collect_pickup, func_800A70E0).
//   * Parameter 0x2111 (func_800E8934): 0.2 per effect record whose +0x28
//     source has +0x08 == 0 (a unit body, unit +0x504) and whose
//     (source+0x0C)->+0x04 differs from the query owner record's u16 +0x1A.
//     What +0x1A holds is UNRESOLVED (it is not the +0x1AE team byte).
//
// Queens and bases themselves are owned by queen_world; this module reads a
// snapshot of them. Unit records are owned by the unit pool; the tracker owns
// only the link fields that have no other port home (+0x18, +0x9C..+0xB0,
// +0x344..+0x34A, +0x358, +0x364).

#include "battletanx/campaign_owner_plays.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace battletanx::owner_links {

namespace retail_values {
inline constexpr std::size_t unit_slots=20U;            // get_unit ids 0..19
inline constexpr std::size_t owner_slots=4U;
inline constexpr std::size_t squad_members=5U;          // unit id = owner*5 + slot
// 20*19/2 distinct pairs. Retail D_8033AAE8 holds 200; 190 suffices only
// because duplicated-id self pairs are skipped (see BuildStatus::duplicate_id).
inline constexpr std::size_t pair_capacity=190U;
inline constexpr std::size_t set_members=15U;           // LinkGroup ids[15]
inline constexpr std::uint8_t no_pair=0xFFU;            // D_8033B450 empty entry
inline constexpr float link_distance=400.0F;            // update_unit_pair_link literal
inline constexpr float resolve_follow_distance=250.0F;  // D_800744EC (ROM 0x44EC)
inline constexpr float resolve_credit_distance=250.0F;  // D_800744F0 (ROM 0x44F0)
inline constexpr float resolve_release_distance=250.0F; // D_800744F4 (ROM 0x44F4)
inline constexpr float range_threshold=1800.0F;         // D_80074390 (ROM 0x4390)
inline constexpr float range_default=2000.0F;           // D_80074394 (ROM 0x4394)
inline constexpr float range_scale=0.95F;               // D_80074398 (ROM 0x4398)
inline constexpr std::int16_t toggle_period=500;        // D_8033AAE0 reload
inline constexpr std::int16_t round_robin_period=3;     // D_8033B44C reload
// Documentation only (the sweep and command consumers use the literal meaning).
inline constexpr std::uint32_t tracked_object_kind=0x1BU;
inline constexpr std::uint16_t restart_parameter=0x1002U; // func_800E986C key
// The 0.2 chain/foreign weight (D_800747D0) lives in owner_plays::retail_values.
} // namespace retail_values

// Unit +0x9C action kind values the link code tests. Other values pass through.
namespace action_kind {
inline constexpr std::int32_t none=0;
inline constexpr std::int32_t follow_unit=8;   // init_unit_link_command (func_800E36FC)
inline constexpr std::int32_t move_point=9;    // update_action_goal (func_800E360C) fallback
} // namespace action_kind

// Retail flag bit tested by the pair code (unit +0x14 & 4).
inline constexpr std::uint16_t unit_flag_excluded=0x0004U;

// ---------------------------------------------------------------------------
// Link fields per unit. CONFIRMED offsets; see evidence.md for writers.
struct UnitLinkFields {
    EntityId unit{};                 // host handle bound to retail id
    std::uint32_t mode{};            // +0x18 (member_mode scorer)
    std::int32_t action{};           // +0x9C
    bool action_point{};             // +0xA0 != 0
    float point_x{}, point_z{};      // the +0xA0 point
    std::uint8_t follow{retail_values::no_pair}; // +0xB0 followed unit id, no_pair = none
    std::uint16_t value344{};        // +0x344 (resolve_unit_pair: follow allowed)
    std::uint8_t value348{};         // +0x348
    bool pair_eligible{};            // +0x34A
    bool tracking{};                 // +0x358
    std::int32_t track_start{};      // +0x364
    void serialize(StateWriter& writer) const;
};

// What the pair update reads from the unit pool each call.
struct UnitSample {
    bool in_use{};                   // +0x01
    std::uint8_t owner{0xFFU};       // +0x04
    std::uint16_t flags{};           // +0x14
    float x{}, z{};                  // +0x24, +0x2C
    bool status_one{};               // func_800E3470
    bool status_two{};               // func_800E3460
    bool carrying{};                 // +0x98 != 0 (carried queen list)
    // +0x34C tracked object: kind 0x1B owner index, nullopt = none / other kind.
    std::optional<std::uint8_t> tracked_kind1b_owner{};
};

// Route probe used for cross-set pairs (func_800DE664 with a fresh search
// buffer). Implemented by the navigation layer. Must be pure and
// deterministic: the tracker's state hash depends on its answers.
class RouteProbe {
public:
    virtual ~RouteProbe()=default;
    [[nodiscard]] virtual bool route_clear(float from_x,float from_z,float to_x,float to_z) const=0;
};

// One LinkSet: friendly ids (a) and opposing ids (b).
struct UnitLinkSet {
    std::array<std::uint8_t,retail_values::set_members> friends{};
    std::uint8_t friend_count{};
    std::array<std::uint8_t,retail_values::set_members> others{};
    std::uint8_t other_count{};
};

// UnitPairing / UnitPair record (12 bytes in retail).
struct PairRecord {
    std::uint8_t first{};       // +0x00, lower id
    std::uint8_t second{};      // +0x01
    bool same_set{};            // +0x02 (1 = friendly pair)
    std::uint8_t value03{};     // +0x03
    bool checked{};             // +0x04
    bool linked{};              // +0x05
    bool in_range{};            // +0x06
    bool value07{};             // +0x07 (both excluded / both status one)
    float distance{};           // +0x08
    void serialize(StateWriter& writer) const;
};

enum class CommandKind : std::uint8_t {
    clear_action,        // func_800E349C: +0x9C,+0xA0,+0x344,+0x348,+0x34A = 0
    self_kill_credit,    // ai_award_self_kill_credit (func_800E4494)
    release_follow,      // +0xA0 = 0, +0x34A = 0
    follow_unit,         // func_800E36FC applied in tracker; caller runs update_linked_unit (func_800E5F30); read final +0x9C/+0xA0 from unit()
    restart_parameter,   // func_800E986C(unit, 0x1002, value) (+ +0x364 = tick)
    clear_tracked,       // unit +0x34C = 0 (tracked object no longer owned)
    run_phase_two,       // caller runs func_800E4AA4
    run_round_robin,     // caller runs func_800E63F4 with `value` as the flag
};
static_assert(std::is_same_v<std::underlying_type_t<CommandKind>,std::uint8_t>);

struct Command {
    CommandKind kind{};
    std::uint8_t unit{};
    std::uint8_t other{};
    float value{};
};

inline constexpr std::size_t command_capacity=512U;

struct UpdateResult {
    std::uint8_t phase{};             // rotor phase this call ran
    bool toggled{};                   // D_8033B44A flipped this call
    std::array<Command,command_capacity> commands{};
    std::uint16_t command_count{};
    bool commands_truncated{};
};

struct UpdateInput {
    std::array<UnitSample,retail_values::unit_slots> units{};
    std::int32_t range_source{};      // D_80125858 (get_scaled_difficulty input)
    std::int32_t tick{};              // get_frame_time
    const RouteProbe* route{};        // nullptr = every probe fails
};

// Errors (id_out_of_range, too_many_members) are detected before any state
// changes; the previous table survives. duplicate_id: a set listed one id
// twice; retail records a lo==hi self pair, the port skips it (TEMPORARY POLICY).
enum class BuildStatus : std::uint8_t { ok, id_out_of_range, too_many_members, pair_overflow, duplicate_id };
static_assert(std::is_same_v<std::underlying_type_t<BuildStatus>,std::uint8_t>);

[[nodiscard]] float scaled_link_range(std::int32_t range_source) noexcept;

class LinkTracker {
public:
    LinkTracker() noexcept;

    // build_unit_pair_records. Resets records, index table and rotor phase;
    // toggle_timer_/round_robin_ are kept (UNRESOLVED whether retail resets
    // D_8033AAE0/D_8033B44C here). id_out_of_range from add_pair is a guard
    // that build() never reaches (it validates ids first).
    BuildStatus build(std::span<const UnitLinkSet> sets) noexcept;
    void bind_unit(std::uint8_t id,EntityId unit) noexcept;

    // Writers of fields with no other port home.
    void set_mode(std::uint8_t id,std::uint32_t mode) noexcept;   // ai_set_unit_mode store
    void set_action(std::uint8_t id,std::int32_t action,std::optional<float> point_x,
                    std::optional<float> point_z) noexcept;
    // init_unit_link_command (func_800E36FC) + update_action_goal (func_800E360C).
    // The source ends as follow_unit (point from the target's 6/7/8 action
    // point), move_point (target +0x344 == 0 and target action 6/7/8: point
    // from the target action point), none with +0x344/+0x348/+0x34A untouched
    // when the target is dead, or cleared when the target is in state 0/9/other.
    void follow(std::uint8_t source,std::uint8_t target,const UpdateInput& input) noexcept;
    // func_800E349C. Leaves +0xB0 (follow) untouched, as retail does.
    void clear_action(std::uint8_t id) noexcept;
    void set_follow_allowed(std::uint8_t id,std::uint16_t value344) noexcept;
    void clear_unit_links(std::uint8_t id) noexcept;               // func_800E4094

    // One ai_run_update_phase call.
    UpdateResult update(const UpdateInput& input) noexcept;

    [[nodiscard]] const PairRecord* pair(std::uint8_t a,std::uint8_t b) const noexcept;
    [[nodiscard]] std::uint16_t pair_count() const noexcept { return pair_count_; }
    [[nodiscard]] const UnitLinkFields* unit(std::uint8_t id) const noexcept;
    [[nodiscard]] std::uint8_t phase() const noexcept { return phase_; }
    [[nodiscard]] bool toggle() const noexcept { return toggle_; }

    void serialize(StateWriter& writer) const;

private:
    void add_pair(std::uint8_t a,std::uint8_t b,bool same_set,BuildStatus& status) noexcept;
    void update_pairings(bool friendly,bool cross,const UpdateInput& input,UpdateResult& out) noexcept;
    void update_pair_link(PairRecord& pair,const UpdateInput& input,UpdateResult& out) noexcept;
    void resolve(std::uint8_t first,std::uint8_t second,float amount,const UpdateInput& input,
                 UpdateResult& out) noexcept;
    void track_kind1b(const UpdateInput& input,UpdateResult& out) noexcept;

    std::array<PairRecord,retail_values::pair_capacity> pairs_{};
    std::uint16_t pair_count_{};
    std::array<std::array<std::uint8_t,retail_values::unit_slots>,retail_values::unit_slots> index_{};
    std::array<UnitLinkFields,retail_values::unit_slots> units_{};
    std::uint8_t phase_{};            // D_8033B5E0
    std::int16_t toggle_timer_{};     // D_8033AAE0
    bool toggle_{};                   // D_8033B44A
    std::int16_t round_robin_{};      // D_8033B44C
};

// ---------------------------------------------------------------------------
// Owner-play inputs.

enum class QueenPlace : std::uint8_t { in_base, carried, dropped, absent };
static_assert(std::is_same_v<std::underlying_type_t<QueenPlace>,std::uint8_t>);

// TEMPORARY POLICY: retail walks every effect record; the port samples one
// queen per owner slot (owner_slots entries).
struct QueenSample {
    QueenPlace place{QueenPlace::absent};
    std::uint16_t carrier_group{0xFFFFU}; // carrier unit +0x04, widened to compare with owner +0x1A
};

struct BaseSample {
    bool present{};                       // owner +0x1AF
    std::uint8_t queens_held{};           // +0x1B8 ->+0x10 list length
    // +0x1B8 ->+0x18. Initialised 0 (CONFIRMED); later writers UNRESOLVED.
    std::int32_t state{};
    bool has_record{};                    // +0x1B8 != NULL (chain_state source)
};

struct OwnerSnapshot {
    std::uint8_t owner_count{};
    std::array<BaseSample,retail_values::owner_slots> bases{};
    std::array<QueenSample,retail_values::owner_slots> queens{};
    std::array<std::uint8_t,retail_values::owner_slots> owner_team{};
    // Owner record u16 +0x1A, the func_800E8934 query key. UNRESOLVED meaning;
    // TEMPORARY POLICY default = owner index until a +0x1A writer is found.
    std::array<std::uint16_t,retail_values::owner_slots> owner_group{0U,1U,2U,3U};
    std::array<bool,retail_values::owner_slots> owner_present{};
    std::int32_t elapsed_ticks{};
    // Built as {p.x,p.z} under the owner_plays::Point2 axis convention
    // (x pairs with unit +0x24, z with +0x2C).
    std::array<std::optional<owner_plays::Point2>,retail_values::owner_slots> objective_points{};
};

// func_800E8934 count (queens carried by a unit whose +0x04 differs from
// query_group, the owner record +0x1A). Weight 0.2 is applied by owner_parameter.
[[nodiscard]] std::uint8_t foreign_link_count(const OwnerSnapshot& snapshot,std::uint16_t query_group) noexcept;

// OwnerWorld for one owner context (team list and foreign links are per owner).
[[nodiscard]] owner_plays::OwnerWorld owner_world(const OwnerSnapshot& snapshot,
                                                  std::span<const UnitSample,retail_values::unit_slots> units,
                                                  std::uint8_t owner) noexcept;

// Fills MemberView.state (+0x9C) and mode (+0x18) from the tracker.
void apply_link_fields(const LinkTracker& tracker,std::uint8_t owner,
                       std::span<owner_plays::MemberView,retail_values::squad_members> members) noexcept;

} // namespace battletanx::owner_links
