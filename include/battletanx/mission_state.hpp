#pragma once

#include "battletanx/runtime_events.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstdint>

namespace battletanx {

// update_match_state (func_8007F2A0) state values held in D_8012578C.
enum class MatchState : std::uint8_t {
    leave=0U,
    live=1U,
    paused=2U,
    title=3U,
    countdown=4U,
    result_delay=5U,
    result_banner=6U,
    round_over=7U,
};

enum class MissionPhase : std::uint8_t { preparing, active, paused, succeeded, failed };

// Owner record +0x10 result values.
enum class OwnerResult : std::uint8_t { eliminated=0U, playing=1U, won=2U };

struct MissionConfig {
    std::uint32_t game_kind{5U};      // wave kind; the first campaign wave is kind 5
    std::uint8_t countdown_unit{24U}; // D_801B4AC0: 24 for one view, 18 for split screen
    std::int32_t result_frames{90};   // states 5 and 6
    std::int32_t round_over_frames{2000};
    std::int32_t respawn_delay_ticks{30}; // owner +0x304 initial value
    // BattleSession enables this when ObjectiveRuntime owns the retail
    // game-kind result rules. MissionState then retains presentation timing,
    // pause, squad liveness and respawns without evaluating a second outcome.
    bool external_objective{};
};

struct RespawnRequest {
    std::uint8_t owner{};
    std::uint8_t squad_slot{};
};

class MissionState {
public:
    explicit MissionState(MissionConfig config={});

    // One frame of the match-state machine. `start_pressed` is the owner-0
    // pause toggle (Start, flags & 0x1000) and `advance` dismisses the title.
    void step(std::array<OwnerRecord,owner_capacity>& owners,const UnitPool& units,
              std::int32_t tick,bool start_pressed,bool advance,GameplayEventQueue& events);

    // Respawn requests that became due this frame, in owner order.
    [[nodiscard]] std::span<const RespawnRequest> respawns() const noexcept;
    void clear_respawns() noexcept { respawn_count_=0U; }

    [[nodiscard]] MatchState state() const noexcept { return state_; }
    [[nodiscard]] MissionPhase phase() const noexcept;
    [[nodiscard]] std::int32_t frames_remaining() const noexcept { return frames_; }
    [[nodiscard]] std::int32_t live_tick() const noexcept { return live_tick_; }
    [[nodiscard]] std::int32_t elapsed_ticks() const noexcept { return elapsed_ticks_; }
    [[nodiscard]] OwnerResult result(std::size_t owner) const noexcept { return results_[owner]; }
    [[nodiscard]] bool squad_alive(std::size_t owner) const noexcept { return squad_alive_[owner]; }
    [[nodiscard]] std::int32_t respawn_due_tick(std::size_t owner) const noexcept { return respawn_at_[owner]; }
    [[nodiscard]] const MissionConfig& config() const noexcept { return config_; }

    // Simulation of units/effects runs in these states (squads update in all
    // non-zero states; effects in 1, 2, 5, 6, 7).
    [[nodiscard]] bool simulation_running() const noexcept;
    void force_state(MatchState state,std::int32_t frames) noexcept { state_=state; frames_=frames; }
    // Kinds 3 and 6 end when func_800A6798 finds a goal slot; the writer of
    // that goal (D_802DF340/D_802DF344) is not recovered, so it is signalled.
    void set_objective_reached() noexcept { objective_reached_=true; }
    // Applies an ObjectiveRuntime decision and copies its authoritative owner
    // results. This is deliberately value-based to keep mission timing
    // independent of the objective implementation.
    void apply_external_objective(MatchState next,
                                  const std::array<OwnerResult,owner_capacity>& results,
                                  std::int32_t tick,GameplayEventQueue& events);

private:
    void advance_state(std::array<OwnerRecord,owner_capacity>& owners,const UnitPool& units,
                       std::int32_t tick,bool start_pressed,bool advance);
    bool update_squad(std::size_t index,OwnerRecord& owner,const UnitPool& units,std::int32_t tick);
    void enter_result(MatchState next,std::int32_t frames) noexcept { state_=next; frames_=frames; }

    MissionConfig config_;
    MatchState state_{MatchState::title};
    std::int32_t frames_{};
    std::int32_t live_tick_{};
    std::int32_t elapsed_ticks_{};
    std::array<OwnerResult,owner_capacity> results_{};
    std::array<bool,owner_capacity> squad_alive_{};
    std::array<std::int32_t,owner_capacity> respawn_at_{};
    std::array<RespawnRequest,owner_capacity> respawn_queue_{};
    std::size_t respawn_count_{};
    bool terminal_reported_{};
    bool objective_reached_{};
};

} // namespace battletanx
