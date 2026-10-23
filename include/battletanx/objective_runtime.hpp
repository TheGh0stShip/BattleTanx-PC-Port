#pragma once

#include "battletanx/mission_state.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/state_hash.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battletanx {

// Objective mechanisms recovered from update_match_state (func_8007F2A0, live
// state 1). Each game kind selects exactly one; nothing falls back to another.
enum class ObjectiveMechanism : std::uint8_t {
    none=0U,
    // Kinds 0, 3, 6: func_800A6798 succeeds when one team's bases hold every
    // queen (D_802DF344[team] == D_802DF340). Kinds 3/6 also end when owner 0's
    // squad is gone.
    queen_capture=1U,
    // Kind 4: owner 0 wins when its unit touches a queen (effect kind 21
    // handler func_800A739C sets contact result 9, update_tank_drive writes
    // owner +0x10 = 2); it loses when no squad of team 0 is alive.
    queen_rescue=2U,
    // Kinds 2, 5, 7: fewer than two teams alive or no human squad alive.
    elimination=3U,
    // Kind 9: add_team_counter (func_800A5B6C) marks an owner won once its
    // kill counter reaches 7; any won owner ends the match.
    kill_race=4U,
};

enum class ObjectiveStatus : std::uint8_t { ongoing=0U, succeeded=1U, failed=2U, unsupported=3U };

enum class ObjectiveConfigError : std::uint8_t {
    none=0U,
    unknown_game_kind,     // above 9
    owner0_missing,        // every evaluated kind reads owner 0
    team_out_of_range,     // D_802DF344 has four team slots
    base_count_invalid,    // assign_group_object_points caps +0x1AF at 1
    no_capture_bases,      // D_802DF340 == 0 would match team 0 on the first live frame
    kill_target_invalid,
    result_frames_invalid,
};

enum class ObjectiveEventStatus : std::uint8_t {
    applied, ignored, frozen, invalid_owner, invalid_team, counter_underflow, counter_overflow,
};

// Why a kind is not driven by this runtime.
enum class UnsupportedReason : std::uint8_t {
    none=0U,
    no_live_objective=1U,   // kind 1: update_match_state has no branch for it
    demo_playback=2U,       // kind 8: match state 8 replays a recording
};

inline constexpr std::int32_t kill_race_target=7;   // add_team_counter: counter >= 7
inline constexpr std::int32_t objective_result_frames=90; // D_801B4AD4 = 90 on entering 5/6

struct ObjectiveOwner {
    bool present{};
    bool computer{};        // +0x1A6
    std::uint8_t team{};    // +0x1AE
    std::uint8_t bases{};   // +0x1AF, queen bases assigned by func_800A4B2C (0 or 1)
};

struct ObjectiveConfig {
    std::uint32_t game_kind{};
    std::array<ObjectiveOwner,owner_capacity> owners{};
    std::int32_t kill_target{kill_race_target};
    std::int32_t result_frames{objective_result_frames};
};

struct ObjectiveInput {
    MatchState match{MatchState::live};
    std::int32_t tick{};
    // func_800A3DE0 result per owner (squad has a unit or a pending respawn).
    std::array<bool,owner_capacity> squad_alive{};
};

struct ObjectiveResult {
    ObjectiveStatus status{ObjectiveStatus::ongoing};
    UnsupportedReason reason{};
    // Retail state the match moves to when the objective decided this step
    // (result_delay or result_banner); live while still ongoing.
    MatchState next_state{MatchState::live};
    bool decided_this_step{};
};

enum class ObjectivePhase : std::uint8_t { live=0U, resolving=1U, finished=2U, unsupported=3U };

struct ObjectiveCreateResult;

class ObjectiveRuntime {
public:
    [[nodiscard]] static ObjectiveCreateResult create(const ObjectiveConfig& config);
    [[nodiscard]] static ObjectiveMechanism mechanism_for(std::uint32_t game_kind) noexcept;

    // unit_destroyed: kill credit for kind 9 (value = killer owner, owner =
    // victim owner). Other events are ignored.
    ObjectiveEventStatus on_event(const GameplayEvent& event);
    // collect_pickup (func_800A70E0) taking a queen from a base of `holder_team`.
    ObjectiveEventStatus on_queen_taken(std::uint8_t holder_team,std::uint8_t collector_team);
    // attach_effect_to_owner_group (func_800A6E30) storing a queen at a base.
    ObjectiveEventStatus on_queen_stored(std::uint8_t base_team);
    // A unit of `owner` touched a queen (effect 21, func_800A739C).
    ObjectiveEventStatus on_queen_contact(std::uint8_t owner);
    // A unit of `owner` touched an effect-31 marker whose readiness test
    // (func_800A7614: not inhibited, not blocked, ready byte 0) passed.
    ObjectiveEventStatus on_marker_contact(std::uint8_t owner);
    // update_unit_control self-destruct in kind 9: counter -1.
    ObjectiveEventStatus on_self_destruct(std::uint8_t owner);

    ObjectiveResult step(const ObjectiveInput& input);

    [[nodiscard]] ObjectivePhase phase() const noexcept { return phase_; }
    [[nodiscard]] ObjectiveStatus status() const noexcept { return status_; }
    [[nodiscard]] ObjectiveMechanism mechanism() const noexcept { return mechanism_; }
    [[nodiscard]] OwnerResult result(std::size_t owner) const noexcept { return results_[owner]; }
    [[nodiscard]] std::uint8_t team_queens(std::size_t team) const noexcept { return team_queens_[team]; }
    [[nodiscard]] std::uint8_t total_bases() const noexcept { return total_bases_; }
    [[nodiscard]] std::int32_t kill_counter(std::size_t owner) const noexcept { return counters_[owner]; }
    [[nodiscard]] bool capture_reached() const noexcept { return capture_reached_; }
    [[nodiscard]] std::int32_t decided_tick() const noexcept { return decided_tick_; }

    void serialize(StateWriter& writer) const;

private:
    explicit ObjectiveRuntime(const ObjectiveConfig& config);
    [[nodiscard]] bool accepting_events() const noexcept { return phase_==ObjectivePhase::live; }
    [[nodiscard]] std::optional<std::uint8_t> matching_team() const noexcept;
    void add_counter(std::uint8_t owner,std::int32_t delta);
    void finish() noexcept;
    ObjectiveResult snapshot(bool decided,MatchState next) const noexcept;

    ObjectiveConfig config_;
    ObjectiveMechanism mechanism_{};
    ObjectivePhase phase_{ObjectivePhase::live};
    ObjectiveStatus status_{ObjectiveStatus::ongoing};
    UnsupportedReason reason_{};
    std::array<OwnerResult,owner_capacity> results_{};
    std::array<std::uint8_t,owner_capacity> team_queens_{};
    std::array<std::int32_t,owner_capacity> counters_{};
    std::uint8_t total_bases_{};
    std::int32_t frames_{};
    std::int32_t last_tick_{};
    std::int32_t decided_tick_{-1};
    bool capture_reached_{};
};

struct ObjectiveCreateResult {
    std::optional<ObjectiveRuntime> runtime;
    ObjectiveConfigError error{ObjectiveConfigError::none};
};

// Narrow MissionState adapter. Kinds 3 and 6 are the only ones MissionState
// takes an external objective signal for; elimination kinds are evaluated by
// MissionState itself, and kinds 4/9 need an owner-result write that its API
// does not expose (reported, not approximated).
enum class MissionFeed : std::uint8_t { nothing_to_feed, objective_signalled, not_representable };

MissionFeed feed_mission_state(const ObjectiveRuntime& objective,MissionState& mission);

[[nodiscard]] std::string_view to_string(ObjectiveConfigError error) noexcept;

} // namespace battletanx
