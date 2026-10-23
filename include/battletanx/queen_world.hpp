#pragma once

#include "battletanx/entity_id.hpp"
#include "battletanx/objective_runtime.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// World entities behind the queen objectives (game kinds 0, 3, 4, 6) and the
// effect-31 capture marker. See docs/OBJECTIVE_ENTITIES.md for evidence.
//
// Retail objects:
//   base   = effect kind 20 (create_entity_point_record, func_800A6AB4), one per
//            owner, created from level objects of kind 11..14 by
//            assign_group_object_points (func_800A4B2C); contact handler
//            release_matching_owner_effects (func_800A74AC).
//   queen  = effect kind 21 (spawn_effect_kind_15, func_800A6958); contact
//            handler func_800A739C -> collect_pickup (func_800A70E0); update
//            expire_effect_after_timer (func_800A6900).
//   marker = effect kind 31 (spawn_effect_marker, func_800A7550) from bank
//            entry kind 24 (spawn_bank_record_entries, single view only);
//            handler set_result_nine_if_unblocked (func_800A7614): result 9
//            when an owner-0 unit touches it, no game-kind check.
//   Kind-4 waves 1, 3, 9, 13 have no queen at all: their objective is one
//   bank type-24 marker (extent 600). Wave 5 is the only kind-4 wave with an
//   effect-21 queen (placed type 12). append_entity_point_record
//   (func_800A4A64) is dead code (no caller, no pointer in ROM).
//   counters D_802DF340 (bases) and D_802DF344[team] (recount func_800A66C8,
//            +1 per attach func_800A6E30, -1 per take from a base).
// Units are never owned here: a carrier is referenced by the caller's EntityId.
namespace battletanx::queen_world {

inline constexpr std::size_t queen_capacity=owner_capacity;   // one queen per base
inline constexpr std::size_t base_capacity=owner_capacity;    // func_800A4B2C: pointCount != 1
inline constexpr std::size_t marker_capacity=16U;             // TEMPORARY POLICY: retail limit is the effect allocator
inline constexpr std::size_t event_capacity=32U;
inline constexpr std::size_t base_slot_count=4U;              // D_801266D0 meaningful entries

inline constexpr std::uint8_t base_object_kind_first=11U;     // assign_group_object_points case 11..14
inline constexpr std::uint8_t base_object_kind_last=14U;
inline constexpr std::uint8_t effect_kind_base=20U;
inline constexpr std::uint8_t effect_kind_queen=21U;
inline constexpr std::uint8_t effect_kind_marker=31U;
inline constexpr std::uint8_t bank_entry_kind_marker=24U;     // spawn_bank_record_entries case 24

inline constexpr std::int32_t drop_return_ticks=0x78;         // start_effect_slot_motion: expire = tick + 0x78
inline constexpr std::int32_t take_progress=1000;             // collect_pickup: add_entity_progress(self, 1000)
inline constexpr std::int32_t store_progress=5000;            // attach_effect_to_owner_group: 5000
inline constexpr float queen_half_extent=20.0F;               // motion bounds +-0x14; D_80073050 (ROM 0x3050) = 20.0
inline constexpr float base_half_extent=195.0F;               // create_entity_point_record motion bounds +-0xC3
inline constexpr std::uint32_t rescue_game_kind=4U;           // unit_contact_game_state_check GAME_STATE_4
inline constexpr std::uint32_t queenless_owner0_game_kind=6U; // create_entity_point_record: kind 6, index 0

// D_801266D0 (ROM 0xB66D0) stored as {first, second}; the decomp names them
// {y, x} and adds `second` to x and `first` to the planar y (world z).
// Values CONFIRMED, world axis STRONG INFERENCE.
struct SlotOffset { float dx; float dz; };
inline constexpr std::array<SlotOffset,base_slot_count> slot_offsets{{
    {0.0F,0.0F},{0.0F,60.0F},{60.0F,0.0F},{0.0F,-60.0F},
}};

// Handles are generation-checked: a slot's generation changes on release, so a
// stale handle never resolves to a later occupant.
struct QueenHandle {
    EntityId id{};
    [[nodiscard]] constexpr bool valid() const noexcept { return id.valid(); }
    [[nodiscard]] friend constexpr bool operator==(QueenHandle,QueenHandle) noexcept=default;
};
struct BaseHandle {
    EntityId id{};
    [[nodiscard]] constexpr bool valid() const noexcept { return id.valid(); }
    [[nodiscard]] friend constexpr bool operator==(BaseHandle,BaseHandle) noexcept=default;
};
struct MarkerHandle {
    EntityId id{};
    [[nodiscard]] constexpr bool valid() const noexcept { return id.valid(); }
    [[nodiscard]] friend constexpr bool operator==(MarkerHandle,MarkerHandle) noexcept=default;
};

// Native view of a queen. Retail effect +0x10 holds 0 (in a base list),
// 1 (carried) or 2 (dropped); at_base/stored split state 0 by how the queen got
// there, and rescued overlays a queen touched by owner 0 in kind 4.
enum class QueenState : std::uint8_t {
    at_base=0U,   // state 0, still in the list it was spawned into
    carried=1U,   // state 1, in the carrier's +0x98 list
    stored=2U,    // state 0 after attach_effect_to_owner_group
    dropped=3U,   // state 2, lying on the ground with a return timer
    rescued=4U,   // kind 4 with a queen (wave 5 only): touched by owner 0 (result 9); waves 1/3/9/13 have no queen
};
static_assert(sizeof(QueenState)==1U);

enum class QueenEventKind : std::uint8_t {
    taken_from_base=0U,   // collect from state 0; D_802DF344[holder]--   -> on_queen_taken
    picked_up=1U,         // collect from state 2; no counter change     -> local only
    stored=2U,            // carrier delivered at a same-team base       -> on_queen_stored
    returned=3U,          // drop timer / carrier link returned it home  -> on_queen_stored
    dropped=4U,           // carrier destroyed                            -> local only
    rescue_contact=5U,    // kind 4, owner 0 touched a queen (wave 5 only) -> on_queen_contact
    marker_reached=6U,    // effect 31: owner-0 unit contact (result 9)   -> on_marker_contact
                          // (kind-4 waves 1/3/9/13 objective; no queen; idempotent)
};
static_assert(sizeof(QueenEventKind)==1U);

enum class ReturnReason : std::uint8_t { none=0U, drop_expired=1U, carrier_invalidated=2U };
static_assert(sizeof(ReturnReason)==1U);

using battletanx::no_owner;   // unit_runtime.hpp

struct QueenEvent {
    std::uint32_t sequence{};
    QueenEventKind kind{};
    ReturnReason reason{};
    QueenState from{};
    QueenHandle queen{};
    BaseHandle base{};
    MarkerHandle marker{};
    EntityId unit{};
    std::uint8_t owner{no_owner};         // unit owner; base owner for stored/returned
    std::uint8_t team{no_owner};          // collector team or base team
    std::uint8_t holder_team{no_owner};   // taken_from_base: team that lost the queen
    std::uint8_t previous_home{no_owner}; // queen +0xC before the transition
    std::uint8_t slot{};                  // D_801266D0 slot used (stored/returned/dropped)
    std::int32_t progress{};              // add_entity_progress amount, 0 if none
    std::int32_t tick{};
};

enum class Status : std::uint8_t {
    applied=0U,
    ignored=1U,            // retail early return; nothing changed
    stale_handle=2U,
    invalid_owner=3U,
    not_ready=4U,          // placement not finished
    placement_closed=5U,
    pool_exhausted=6U,
    event_queue_full=7U,   // nothing changed
    slot_overflow=8U,      // a base would hold more than four queens
    home_base_missing=9U,
    counter_underflow=10U,
    counter_overflow=11U,
};
static_assert(sizeof(Status)==1U);

enum class PlaceOutcome : std::uint8_t {
    placed=0U,
    not_base_kind=1U,        // object kind outside 11..14
    inactive=2U,             // object +active == 0
    kind_has_no_bases=3U,    // apply_mode_display_rows: only kinds 0, 3, 4, 6
    owner_out_of_range=4U,   // gSlotRank[kind-11] >= get_entity_capacity()
    duplicate_base=5U,       // pointCount == 1 already
    split_screen=6U,         // marker: get_view_count() != 1
    pool_exhausted=7U,
    placement_closed=8U,
    not_marker_kind=9U,      // bank entry kind != 24
};
static_assert(sizeof(PlaceOutcome)==1U);

enum class ConfigError : std::uint8_t {
    none=0U, owner_count_invalid=1U, team_out_of_range=2U, slot_rank_invalid=3U,
};
static_assert(sizeof(ConfigError)==1U);

struct OwnerInfo {
    bool present{};
    std::uint8_t team{};   // owner +0x1AE
};

struct Policies {
    // D_801266F0 (ROM 0xB66F0 = 1, no writer found): attach rewrites the
    // queen's home owner (+0xC) to the storing base owner.
    bool store_rehomes_queen{true};
};

struct Config {
    std::uint32_t game_kind{};
    std::uint8_t owner_count{};                    // get_entity_capacity (func_8007AAB0)
    std::array<OwnerInfo,owner_capacity> owners{};
    // gSlotRank (D_801260A4): object kind 11+i belongs to owner slot_rank[i].
    // randomize_slot_order fills it; clear_four_runtime_pairs sets it to 0..3.
    std::array<std::uint8_t,owner_capacity> slot_rank{0U,1U,2U,3U};
    Policies policies{};
};

// One level object, in scan order (groups, then entries).
struct LevelObject {
    std::uint8_t kind{};
    bool active{};
    float x{};
    float z{};
};

struct PlaceResult {
    PlaceOutcome outcome{};
    BaseHandle base{};
    QueenHandle queen{};   // invalid when kind 6 owner 0
    std::uint8_t owner{no_owner};
};

// One world-bank item (spawn_bank_record_entries input, record 0 only). Only
// kind 24 creates a marker: position = item f32 x (+0xC) / z (+0x10), extent =
// (s16) of the u16 param half at +0x4. Retail type-24 entries carry no owner or
// team. Bank flags 0x10/0x40 mode filtering is the caller's job.
struct MarkerBankEntry {
    std::uint8_t kind{};
    float x{};
    float z{};
    std::uint16_t param{};
};

// (s16)param, as func_80088670 passes it to func_800A7550.
[[nodiscard]] constexpr std::int16_t marker_extent(std::uint16_t param) noexcept {
    return static_cast<std::int16_t>(param);
}

struct MarkerPlaceResult {
    PlaceOutcome outcome{};
    MarkerHandle marker{};
};

// Contact delivered to an effect handler (object, contact, event, data, result).
struct UnitContact {
    EntityId unit{};
    std::uint8_t owner{};          // unit +0x4
    std::uint8_t event_code{};     // handler argument 3; acts only on 0
    std::uint8_t contact_kind{};   // contact +0x8; acts only on 0
    // collect_pickup early returns: collector flags & 4, or a human owner whose
    // func_800E3460 status is 2. Meaning UNRESOLVED; the caller combines them.
    bool collector_blocked{};
};

struct QueenView {
    QueenState state{};
    std::uint8_t retail_state{};   // effect +0x10
    std::uint8_t home_owner{};     // +0xC
    std::uint8_t spawn_owner{};    // +0xD
    std::uint8_t holder_owner{};   // +0x14 (meaningful in state 0)
    std::uint8_t slot{};
    EntityId carrier{};
    std::uint8_t carrier_owner{no_owner};
    float x{};
    float z{};
    std::int32_t expire_tick{};
};

struct BaseView {
    std::uint8_t owner{};
    std::uint8_t team{};
    float x{};
    float z{};
    std::uint8_t queens_held{};
};

struct MarkerView {
    float x{};
    float z{};
    std::int16_t extent{};   // bank entry param, used as +-extent
    // Contact box |ux-x|<=extent && |uz-z|<=extent (STRONG INFERENCE). The
    // inclusive edge is TEMPORARY POLICY: motion-box/contact-grid inclusivity
    // was not traced. The caller performs the test before touch_marker.
};


struct CreateResult;

class QueenWorld {
public:
    [[nodiscard]] static CreateResult create(const Config& config);

    // assign_group_object_points for one level object.
    PlaceResult place_level_object(const LevelObject& object);
    // spawn_bank_record_entries case 24 -> spawn_effect_marker. view_count != 1
    // creates no marker (split_screen). Unreachable in campaign: view count =
    // human players and >=2 players forces non-campaign mode
    // (update_game_setup_menu_input.c:55-80); with no marker the wave can only
    // end by a team-0 loss.
    MarkerPlaceResult place_marker(float x,float z,std::int16_t extent,std::uint8_t view_count);
    // Same, from a bank entry; kinds other than 24 return not_marker_kind.
    MarkerPlaceResult place_bank_entry(const MarkerBankEntry& entry,std::uint8_t view_count);
    // recount_team_active_entities; contacts are accepted afterwards.
    void finish_placement() noexcept;

    Status touch_queen(QueenHandle queen,const UnitContact& contact,std::int32_t tick);
    Status touch_base(BaseHandle base,const UnitContact& contact,std::int32_t tick);
    Status touch_marker(MarkerHandle marker,const UnitContact& contact,std::int32_t tick);
    // destroy_unit: carried queens dropped head first, slot i++.
    Status carrier_destroyed(EntityId unit,float x,float z,std::int32_t tick);
    // draw_marker_entity carrier-link test (meaning UNRESOLVED): return home.
    Status carrier_invalidated(EntityId unit,std::int32_t tick);
    // expire_effect_after_timer for every dropped queen, in slot order.
    Status step(std::int32_t tick);
    // Releases every entity; all handles become stale. Placement reopens.
    void reset() noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] std::uint8_t total_bases() const noexcept { return total_bases_; }
    [[nodiscard]] std::uint8_t team_count(std::size_t team) const noexcept {
        return team<owner_capacity ? team_counts_[team] : std::uint8_t{0U};
    }
    [[nodiscard]] bool has_base(std::size_t owner) const noexcept;
    [[nodiscard]] BaseHandle base_of(std::size_t owner) const noexcept;
    [[nodiscard]] bool view(QueenHandle queen,QueenView& out) const noexcept;
    [[nodiscard]] bool view(BaseHandle base,BaseView& out) const noexcept;
    [[nodiscard]] bool view(MarkerHandle marker,MarkerView& out) const noexcept;
    // Queens carried by `unit`, head first; returns the count written.
    std::size_t carried_by(EntityId unit,std::array<QueenHandle,queen_capacity>& out) const noexcept;
    // func_800A6798: first team whose count equals the total, or no_owner.
    [[nodiscard]] std::uint8_t matching_team() const noexcept;

    [[nodiscard]] std::size_t pending_events() const noexcept { return event_count_; }
    [[nodiscard]] const QueenEvent& event(std::size_t index) const noexcept { return events_[index]; }
    void clear_events() noexcept { event_count_=0U; }
    // Events rejected by push() because the queue was full; serialized.
    [[nodiscard]] std::uint32_t dropped_events() const noexcept { return dropped_events_; }

    void serialize(StateWriter& writer) const;

private:
    explicit QueenWorld(const Config& config);

    struct QueenSlot {
        std::uint16_t generation{1U};
        bool live{};
        bool rescued{};
        bool moved{};            // attached at least once (stored vs at_base)
        std::uint8_t retail_state{};
        std::uint8_t home_owner{};
        std::uint8_t spawn_owner{};
        std::uint8_t holder_owner{};
        std::uint8_t slot{};
        EntityId carrier{};
        std::uint8_t carrier_owner{no_owner};
        std::uint32_t link_order{};   // larger = nearer the list head
        float x{};
        float z{};
        std::int32_t expire_tick{};
    };
    struct BaseSlot {
        std::uint16_t generation{1U};
        bool live{};
        std::uint8_t owner{};
        std::uint8_t team{};
        float x{};
        float z{};
    };
    struct MarkerSlot {
        std::uint16_t generation{1U};
        bool live{};
        float x{};
        float z{};
        std::int16_t extent{};
    };

    [[nodiscard]] std::size_t queen_slot(QueenHandle handle) const noexcept;
    [[nodiscard]] std::size_t base_slot(BaseHandle handle) const noexcept;
    [[nodiscard]] std::size_t marker_slot(MarkerHandle handle) const noexcept;
    [[nodiscard]] std::size_t base_index_of(std::uint8_t owner) const noexcept;
    [[nodiscard]] bool owner_ok(std::uint8_t owner) const noexcept;
    [[nodiscard]] std::uint8_t team_of(std::uint8_t owner) const noexcept { return config_.owners[owner].team; }
    [[nodiscard]] std::size_t free_events() const noexcept { return event_capacity-event_count_; }
    [[nodiscard]] std::uint8_t queens_at(std::uint8_t owner) const noexcept;
    [[nodiscard]] QueenState state_of(const QueenSlot& queen) const noexcept;
    [[nodiscard]] QueenHandle queen_handle(std::size_t slot) const noexcept;
    [[nodiscard]] BaseHandle base_handle(std::size_t slot) const noexcept;
    std::size_t carried_slots(EntityId unit,std::array<std::size_t,queen_capacity>& out) const noexcept;
    Status check_attach(std::size_t queen,std::uint8_t base_owner,std::uint8_t extra) const noexcept;
    void attach(std::size_t queen,std::uint8_t base_owner,QueenEventKind kind,ReturnReason reason,
                EntityId unit,std::int32_t tick);
    void push(QueenEvent event) noexcept;

    Config config_;
    bool ready_{};
    std::array<QueenSlot,queen_capacity> queens_{};
    std::array<BaseSlot,base_capacity> bases_{};
    std::array<MarkerSlot,marker_capacity> markers_{};
    std::uint8_t total_bases_{};                              // D_802DF340
    std::array<std::uint8_t,owner_capacity> team_counts_{};   // D_802DF344
    std::uint32_t link_counter_{};
    std::uint32_t event_sequence_{};                          // not cleared by reset()
    std::array<QueenEvent,event_capacity> events_{};
    std::size_t event_count_{};
    std::uint32_t dropped_events_{};                          // not cleared by reset()
};

struct CreateResult {
    std::optional<QueenWorld> world;
    ConfigError error{ConfigError::none};
};

// ObjectiveConfig::owners[i].bases mirrors pointCount (+0x1AF) after placement.
void seed_objective_bases(const QueenWorld& world,ObjectiveConfig& config) noexcept;

// Authority: ObjectiveRuntime's per-team queen counts decide the match
// (win/lose, frozen state). QueenWorld's team_counts_ (D_802DF344) is
// authoritative only for entity placement/state and can diverge when the
// objective returns frozen / ignored / counter_* for a counter event.
struct ForwardSummary {
    std::uint32_t forwarded{};
    std::uint32_t applied{};
    std::uint32_t local_only{};   // picked_up / dropped have no objective call
    std::uint32_t mismatched{};   // queen_taken / queen_stored not applied: counts diverged
    std::array<ObjectiveEventStatus,event_capacity> statuses{};   // per forwarded event, in order
};

// Value-only description of the ObjectiveRuntime / BattleSession entry point
// an event maps to, for callers that hold a BattleSession instead of the
// runtime (BattleSession::queen_taken/queen_stored/queen_contact/marker_contact
// forward 1:1 to ObjectiveRuntime::on_queen_taken/... ). self_destruct is not
// produced by this module.
enum class ObjectiveEntry : std::uint8_t {
    none=0U, queen_taken=1U, queen_stored=2U, queen_contact=3U, marker_contact=4U,
};
static_assert(sizeof(ObjectiveEntry)==1U);

struct ObjectiveCall {
    ObjectiveEntry entry{ObjectiveEntry::none};
    std::uint8_t first{};    // holder_team / base_team / owner
    std::uint8_t second{};   // collector_team (queen_taken only)
};

[[nodiscard]] ObjectiveCall objective_call(const QueenEvent& event) noexcept;

// Drains the event queue into ObjectiveRuntime in sequence order, then clears it.
ForwardSummary forward_events(QueenWorld& world,ObjectiveRuntime& objective);

} // namespace battletanx::queen_world
