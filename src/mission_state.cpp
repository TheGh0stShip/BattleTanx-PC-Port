#include "battletanx/mission_state.hpp"

namespace battletanx {

MissionState::MissionState(MissionConfig config) : config_(config) {
    frames_=0xFF;
    results_.fill(OwnerResult::playing);
}

std::span<const RespawnRequest> MissionState::respawns() const noexcept {
    return {respawn_queue_.data(),respawn_count_};
}

MissionPhase MissionState::phase() const noexcept {
    switch (state_) {
    case MatchState::title:
    case MatchState::countdown: return MissionPhase::preparing;
    case MatchState::live:
    case MatchState::result_delay: return MissionPhase::active;
    case MatchState::paused: return MissionPhase::paused;
    case MatchState::result_banner:
    case MatchState::round_over:
    case MatchState::leave:
        return results_[0]==OwnerResult::won ? MissionPhase::succeeded : MissionPhase::failed;
    }
    return MissionPhase::preparing;
}

bool MissionState::simulation_running() const noexcept {
    switch (state_) {
    case MatchState::live:
    case MatchState::result_delay:
    case MatchState::result_banner:
    case MatchState::round_over: return true;
    default: return false;
    }
}

bool MissionState::update_squad(std::size_t index,OwnerRecord& owner,const UnitPool& units,
                                std::int32_t tick) {
    (void)units;
    const bool can_respawn=owner.live_units<owner.squad_cap &&
                           (owner.respawn_bank[0]>0 || owner.respawn_enabled);
    if (can_respawn) {
        if (respawn_at_[index]==0) {
            respawn_at_[index]=tick+config_.respawn_delay_ticks;
        } else if (respawn_at_[index]<tick && respawn_count_<respawn_queue_.size()) {
            respawn_at_[index]=0;
            if (owner.respawn_bank[0]>0) --owner.respawn_bank[0];
            respawn_queue_[respawn_count_++]=RespawnRequest{static_cast<std::uint8_t>(index),0U};
        }
    } else {
        respawn_at_[index]=0;
    }
    const bool alive=owner.live_units>0U || can_respawn;
    if (!alive) results_[index]=OwnerResult::eliminated;
    return alive;
}

void MissionState::step(std::array<OwnerRecord,owner_capacity>& owners,const UnitPool& units,
                        std::int32_t tick,bool start_pressed,bool advance,GameplayEventQueue& events) {
    advance_state(owners,units,tick,start_pressed,advance);
    if (state_==MatchState::result_banner && !terminal_reported_) {
        terminal_reported_=true;
        GameplayEvent event;
        event.kind=results_[0]==OwnerResult::won ? GameplayEventKind::mission_succeeded
                                                 : GameplayEventKind::mission_failed;
        event.tick=tick;
        event.owner=0U;
        events.push(event);
    }
}

void MissionState::apply_external_objective(
    MatchState next,const std::array<OwnerResult,owner_capacity>& results,
    std::int32_t tick,GameplayEventQueue& events) {
    results_=results;
    if (next==MatchState::live || next==state_) return;
    state_=next;
    if (next==MatchState::result_banner) {
        frames_=config_.result_frames;
        if (!terminal_reported_) {
            terminal_reported_=true;
            GameplayEvent event;
            event.kind=results_[0]==OwnerResult::won ? GameplayEventKind::mission_succeeded
                                                     : GameplayEventKind::mission_failed;
            event.tick=tick;
            event.owner=0U;
            events.push(event);
        }
    } else if (next==MatchState::result_delay) {
        frames_=config_.result_frames;
    }
}

void MissionState::advance_state(std::array<OwnerRecord,owner_capacity>& owners,const UnitPool& units,
                                 std::int32_t tick,bool start_pressed,bool advance) {
    switch (state_) {
    case MatchState::leave:
        return;
    case MatchState::title:
        frames_=frames_>=4 ? frames_-4 : 0;
        if (advance) { state_=MatchState::countdown; frames_=config_.countdown_unit*5; }
        return;
    case MatchState::countdown:
        if (--frames_<=0) {
            state_=MatchState::live;
            live_tick_=tick;
            if (config_.external_objective) {
                for (std::size_t i=0U; i<owner_capacity; ++i) {
                    if (!owners[i].present) continue;
                    squad_alive_[i]=update_squad(i,owners[i],units,tick);
                }
            }
        }
        return;
    case MatchState::paused:
        if (start_pressed) state_=MatchState::live;
        return;
    case MatchState::result_delay:
        if (config_.external_objective) {
            for (std::size_t i=0U; i<owner_capacity; ++i) {
                if (!owners[i].present) continue;
                squad_alive_[i]=update_squad(i,owners[i],units,tick);
            }
            return;
        }
        if (--frames_<=0) {
            for (std::size_t i=0U; i<owner_capacity; ++i) {
                if (!owners[i].present || results_[i]!=OwnerResult::playing) continue;
                results_[i]=config_.game_kind==9U ? OwnerResult::eliminated : OwnerResult::won;
            }
            enter_result(MatchState::result_banner,config_.result_frames);
        }
        return;
    case MatchState::result_banner:
        if (--frames_<=0) {
            elapsed_ticks_=tick-live_tick_;
            enter_result(MatchState::round_over,config_.round_over_frames);
        }
        return;
    case MatchState::round_over:
        if (frames_>0) --frames_;
        return;
    case MatchState::live:
        break;
    }

    std::array<bool,owner_capacity> teams{};
    std::int32_t team_count=0;
    std::int32_t human_alive=0;
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        OwnerRecord& owner=owners[i];
        if (!owner.present) continue;
        squad_alive_[i]=update_squad(i,owner,units,tick);
        if (!squad_alive_[i]) continue;
        if (!owner.computer) ++human_alive;
        const std::size_t team=owner.team<owner_capacity ? owner.team : 0U;
        if (!teams[team]) { teams[team]=true; ++team_count; }
    }
    if (start_pressed) { state_=MatchState::paused; return; }

    if (config_.external_objective) return;

    const std::uint32_t kind=config_.game_kind;
    if (kind==3U || kind==6U) {
        if (objective_reached_ || !squad_alive_[0]) {
            for (std::size_t i=0U; i<owner_capacity; ++i) {
                if (!owners[i].present) continue;
                if (team_count==1) results_[i]=squad_alive_[i] ? OwnerResult::won : OwnerResult::eliminated;
            }
            enter_result(MatchState::result_banner,config_.result_frames);
        }
        return;
    }
    if (kind==9U || kind==4U) {
        if (kind==9U) {
            for (std::size_t i=0U; i<owner_capacity; ++i) {
                if (owners[i].present && results_[i]==OwnerResult::won) {
                    enter_result(MatchState::result_delay,config_.result_frames);
                }
            }
        }
        if (results_[0]==OwnerResult::won) { enter_result(MatchState::result_banner,config_.result_frames); return; }
        if (!teams[owners[0].team<owner_capacity ? owners[0].team : 0U]) {
            results_[0]=OwnerResult::eliminated;
            enter_result(MatchState::result_delay,config_.result_frames);
            return;
        }
    }
    if (kind==2U || kind==4U || kind==5U || kind==7U || kind==9U) {
        if (team_count<2 || human_alive==0) enter_result(MatchState::result_delay,config_.result_frames);
    }
}

} // namespace battletanx
