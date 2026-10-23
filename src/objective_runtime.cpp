#include "battletanx/objective_runtime.hpp"

namespace battletanx {
namespace {

[[nodiscard]] bool valid_owner(std::uint8_t owner) noexcept { return owner<owner_capacity; }

[[nodiscard]] OwnerResult playing_result(bool present) noexcept {
    return present ? OwnerResult::playing : OwnerResult::eliminated;
}

} // namespace

ObjectiveMechanism ObjectiveRuntime::mechanism_for(std::uint32_t game_kind) noexcept {
    switch (game_kind) {
    case 0U: case 3U: case 6U: return ObjectiveMechanism::queen_capture;
    case 4U: return ObjectiveMechanism::queen_rescue;
    case 2U: case 5U: case 7U: return ObjectiveMechanism::elimination;
    case 9U: return ObjectiveMechanism::kill_race;
    default: return ObjectiveMechanism::none;
    }
}

ObjectiveCreateResult ObjectiveRuntime::create(const ObjectiveConfig& config) {
    ObjectiveCreateResult out;
    if (config.game_kind>9U) { out.error=ObjectiveConfigError::unknown_game_kind; return out; }
    if (!config.owners[0].present) { out.error=ObjectiveConfigError::owner0_missing; return out; }
    std::uint32_t bases=0U;
    for (const ObjectiveOwner& owner : config.owners) {
        if (!owner.present) continue;
        if (owner.team>=owner_capacity) { out.error=ObjectiveConfigError::team_out_of_range; return out; }
        if (owner.bases>1U) { out.error=ObjectiveConfigError::base_count_invalid; return out; }
        bases+=owner.bases;
    }
    const ObjectiveMechanism mechanism=mechanism_for(config.game_kind);
    if (mechanism==ObjectiveMechanism::queen_capture && bases==0U) {
        out.error=ObjectiveConfigError::no_capture_bases;
        return out;
    }
    if (mechanism==ObjectiveMechanism::kill_race && config.kill_target<=0) {
        out.error=ObjectiveConfigError::kill_target_invalid;
        return out;
    }
    if (config.result_frames<=0) { out.error=ObjectiveConfigError::result_frames_invalid; return out; }
    out.runtime=ObjectiveRuntime(config);
    return out;
}

ObjectiveRuntime::ObjectiveRuntime(const ObjectiveConfig& config) : config_(config) {
    mechanism_=mechanism_for(config.game_kind);
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        const ObjectiveOwner& owner=config.owners[i];
        results_[i]=playing_result(owner.present);   // initialize_player_record.c:297
        if (!owner.present) continue;
        // recount_team_active_entities (func_800A66C8), run by
        // apply_mode_display_rows for kinds 0, 3, 4 and 6.
        total_bases_=static_cast<std::uint8_t>(total_bases_+owner.bases);
        team_queens_[owner.team]=static_cast<std::uint8_t>(team_queens_[owner.team]+owner.bases);
    }
    if (mechanism_==ObjectiveMechanism::none) {
        phase_=ObjectivePhase::unsupported;
        status_=ObjectiveStatus::unsupported;
        reason_=config.game_kind==8U ? UnsupportedReason::demo_playback : UnsupportedReason::no_live_objective;
    }
}

ObjectiveEventStatus ObjectiveRuntime::on_event(const GameplayEvent& event) {
    if (event.kind!=GameplayEventKind::unit_destroyed) return ObjectiveEventStatus::ignored;
    if (mechanism_!=ObjectiveMechanism::kill_race) return ObjectiveEventStatus::ignored;
    if (!accepting_events()) return ObjectiveEventStatus::frozen;
    // destroy_unit.c:135-137: credit only a killer owner (< 4) on another team.
    if (event.value<0 || event.value>=static_cast<std::int32_t>(owner_capacity)) return ObjectiveEventStatus::ignored;
    if (!valid_owner(event.owner)) return ObjectiveEventStatus::invalid_owner;
    const auto killer=static_cast<std::uint8_t>(event.value);
    if (!config_.owners[killer].present || !config_.owners[event.owner].present) return ObjectiveEventStatus::invalid_owner;
    if (config_.owners[killer].team==config_.owners[event.owner].team) return ObjectiveEventStatus::ignored;
    add_counter(killer,1);
    return ObjectiveEventStatus::applied;
}

ObjectiveEventStatus ObjectiveRuntime::on_self_destruct(std::uint8_t owner) {
    if (mechanism_!=ObjectiveMechanism::kill_race) return ObjectiveEventStatus::ignored;
    if (!accepting_events()) return ObjectiveEventStatus::frozen;
    if (!valid_owner(owner) || !config_.owners[owner].present) return ObjectiveEventStatus::invalid_owner;
    add_counter(owner,-1);
    return ObjectiveEventStatus::applied;
}

// add_team_counter (func_800A5B6C) in kind 9: the delta applies to the owner
// and every teammate, each clamped at 0; reaching the target marks it won.
void ObjectiveRuntime::add_counter(std::uint8_t owner,std::int32_t delta) {
    const std::uint8_t team=config_.owners[owner].team;
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        if (!config_.owners[i].present || config_.owners[i].team!=team) continue;
        counters_[i]+=delta;
        if (counters_[i]<0) counters_[i]=0;
        if (counters_[i]>=config_.kill_target) results_[i]=OwnerResult::won;
    }
}

ObjectiveEventStatus ObjectiveRuntime::on_queen_taken(std::uint8_t holder_team,std::uint8_t collector_team) {
    if (total_bases_==0U) return ObjectiveEventStatus::ignored;
    if (!accepting_events()) return ObjectiveEventStatus::frozen;
    if (holder_team>=owner_capacity || collector_team>=owner_capacity) return ObjectiveEventStatus::invalid_team;
    if (holder_team==collector_team) return ObjectiveEventStatus::ignored;   // collect_pickup.c:77
    if (team_queens_[holder_team]==0U) return ObjectiveEventStatus::counter_underflow;
    --team_queens_[holder_team];
    return ObjectiveEventStatus::applied;
}

ObjectiveEventStatus ObjectiveRuntime::on_queen_stored(std::uint8_t base_team) {
    if (total_bases_==0U) return ObjectiveEventStatus::ignored;
    if (!accepting_events()) return ObjectiveEventStatus::frozen;
    if (base_team>=owner_capacity) return ObjectiveEventStatus::invalid_team;
    if (team_queens_[base_team]==0xFFU) return ObjectiveEventStatus::counter_overflow;
    ++team_queens_[base_team];
    return ObjectiveEventStatus::applied;
}

ObjectiveEventStatus ObjectiveRuntime::on_queen_contact(std::uint8_t owner) {
    // unit_contact_game_state_check.c:30: result 9 only in kind 4 for owner 0.
    if (mechanism_!=ObjectiveMechanism::queen_rescue) return ObjectiveEventStatus::ignored;
    if (!accepting_events()) return ObjectiveEventStatus::frozen;
    if (!valid_owner(owner)) return ObjectiveEventStatus::invalid_owner;
    if (owner!=0U) return ObjectiveEventStatus::ignored;
    results_[0]=OwnerResult::won;   // update_tank_drive.c:637-638
    return ObjectiveEventStatus::applied;
}

ObjectiveEventStatus ObjectiveRuntime::on_marker_contact(std::uint8_t owner) {
    if (phase_==ObjectivePhase::unsupported) return ObjectiveEventStatus::ignored;
    if (!accepting_events()) return ObjectiveEventStatus::frozen;
    if (!valid_owner(owner) || !config_.owners[owner].present) return ObjectiveEventStatus::invalid_owner;
    results_[owner]=OwnerResult::won;   // func_800A7614 result 9, any game kind
    return ObjectiveEventStatus::applied;
}

// mark_entities_for_matching_slot (func_800A6798): first team whose bases hold
// every queen.
std::optional<std::uint8_t> ObjectiveRuntime::matching_team() const noexcept {
    for (std::uint8_t team=0U; team<owner_capacity; ++team) {
        if (team_queens_[team]==total_bases_) return team;
    }
    return std::nullopt;
}

void ObjectiveRuntime::finish() noexcept {
    phase_=ObjectivePhase::finished;
    status_=results_[0]==OwnerResult::won ? ObjectiveStatus::succeeded : ObjectiveStatus::failed;
}

ObjectiveResult ObjectiveRuntime::snapshot(bool decided,MatchState next) const noexcept {
    ObjectiveResult out;
    out.status=status_;
    out.reason=reason_;
    out.decided_this_step=decided;
    out.next_state=next;
    return out;
}

ObjectiveResult ObjectiveRuntime::step(const ObjectiveInput& input) {
    switch (phase_) {
    case ObjectivePhase::unsupported:
    case ObjectivePhase::finished:
        return snapshot(false,MatchState::live);
    case ObjectivePhase::resolving: {
        if (input.match!=MatchState::result_delay) return snapshot(false,MatchState::result_delay);
        last_tick_=input.tick;
        // update_squad_state keeps running in state 5 (update_match_state.c:290-294).
        for (std::size_t i=0U; i<owner_capacity; ++i) {
            if (config_.owners[i].present && !input.squad_alive[i]) results_[i]=OwnerResult::eliminated;
        }
        if (--frames_>0) return snapshot(false,MatchState::result_delay);
        // update_match_state.c:296-308: still-playing owners become won (kind 9: eliminated).
        for (std::size_t i=0U; i<owner_capacity; ++i) {
            if (results_[i]!=OwnerResult::playing) continue;
            results_[i]=mechanism_==ObjectiveMechanism::kill_race ? OwnerResult::eliminated : OwnerResult::won;
        }
        finish();
        return snapshot(true,MatchState::result_banner);
    }
    case ObjectivePhase::live:
        break;
    }
    if (input.match!=MatchState::live) return snapshot(false,MatchState::live);   // paused or not started
    last_tick_=input.tick;

    std::array<bool,owner_capacity> teams{};
    std::int32_t team_count=0;
    std::int32_t human_alive=0;
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        const ObjectiveOwner& owner=config_.owners[i];
        if (!owner.present) continue;
        if (!input.squad_alive[i]) { results_[i]=OwnerResult::eliminated; continue; }   // update_squad_state.c:190-192
        if (!owner.computer) ++human_alive;
        if (!teams[owner.team]) { teams[owner.team]=true; ++team_count; }
    }

    const auto resolve=[this,&input](MatchState next) {
        decided_tick_=input.tick;
        if (next==MatchState::result_banner) { finish(); return snapshot(true,next); }
        phase_=ObjectivePhase::resolving;
        frames_=config_.result_frames;
        return snapshot(true,next);
    };

    switch (mechanism_) {
    case ObjectiveMechanism::queen_capture: {
        const std::optional<std::uint8_t> team=matching_team();
        const bool owner0_down=config_.game_kind!=0U && !input.squad_alive[0];
        if (!team && !owner0_down) break;
        if (team) {
            capture_reached_=true;
            for (std::size_t i=0U; i<owner_capacity; ++i) {
                if (!config_.owners[i].present) continue;
                results_[i]=config_.owners[i].team==*team ? OwnerResult::won : OwnerResult::eliminated;
            }
        }
        // update_match_state.c:249-262 (kinds 3/6 only): one team left relabels by squad liveness.
        if (config_.game_kind!=0U && team_count==1) {
            for (std::size_t i=0U; i<owner_capacity; ++i) {
                if (!config_.owners[i].present) continue;
                results_[i]=input.squad_alive[i] ? OwnerResult::won : OwnerResult::eliminated;
            }
        }
        return resolve(MatchState::result_banner);
    }
    case ObjectiveMechanism::queen_rescue:
        // update_match_state.c:234-245: no return after the win, so a wiped
        // team 0 in the same frame turns it into a loss.
        if (!teams[0]) {   // retail tests team index 0, not owner 0's team
            results_[0]=OwnerResult::eliminated;
            return resolve(MatchState::result_delay);
        }
        if (results_[0]==OwnerResult::won) return resolve(MatchState::result_banner);
        break;
    case ObjectiveMechanism::elimination:
        if (team_count<2 || human_alive==0) return resolve(MatchState::result_delay);
        break;
    case ObjectiveMechanism::kill_race:
        for (std::size_t i=0U; i<owner_capacity; ++i) {
            if (config_.owners[i].present && results_[i]==OwnerResult::won) return resolve(MatchState::result_delay);
        }
        break;
    case ObjectiveMechanism::none:
        break;
    }
    return snapshot(false,MatchState::live);
}

void ObjectiveRuntime::serialize(StateWriter& writer) const {
    writer.u32(config_.game_kind);
    for (const ObjectiveOwner& owner : config_.owners) {
        writer.boolean(owner.present);
        writer.boolean(owner.computer);
        writer.u8(owner.team);
        writer.u8(owner.bases);
    }
    writer.s32(config_.kill_target);
    writer.s32(config_.result_frames);
    writer.u8(static_cast<std::uint8_t>(mechanism_));
    writer.u8(static_cast<std::uint8_t>(phase_));
    writer.u8(static_cast<std::uint8_t>(status_));
    writer.u8(static_cast<std::uint8_t>(reason_));
    for (OwnerResult result : results_) writer.u8(static_cast<std::uint8_t>(result));
    for (std::uint8_t queens : team_queens_) writer.u8(queens);
    for (std::int32_t counter : counters_) writer.s32(counter);
    writer.u8(total_bases_);
    writer.s32(frames_);
    writer.s32(last_tick_);
    writer.s32(decided_tick_);
    writer.boolean(capture_reached_);
}

MissionFeed feed_mission_state(const ObjectiveRuntime& objective,MissionState& mission) {
    const std::uint32_t kind=mission.config().game_kind;
    if (objective.mechanism()==ObjectiveMechanism::elimination) return MissionFeed::nothing_to_feed;
    if ((kind==3U || kind==6U) && objective.mechanism()==ObjectiveMechanism::queen_capture) {
        if (!objective.capture_reached()) return MissionFeed::nothing_to_feed;
        mission.set_objective_reached();
        return MissionFeed::objective_signalled;
    }
    if (objective.result(0)==OwnerResult::won || objective.phase()!=ObjectivePhase::live) {
        return MissionFeed::not_representable;
    }
    return MissionFeed::nothing_to_feed;
}

std::string_view to_string(ObjectiveConfigError error) noexcept {
    switch (error) {
    case ObjectiveConfigError::none: return "none";
    case ObjectiveConfigError::unknown_game_kind: return "unknown game kind";
    case ObjectiveConfigError::owner0_missing: return "owner 0 missing";
    case ObjectiveConfigError::team_out_of_range: return "team out of range";
    case ObjectiveConfigError::base_count_invalid: return "base count above 1";
    case ObjectiveConfigError::no_capture_bases: return "queen capture without bases";
    case ObjectiveConfigError::kill_target_invalid: return "kill target not positive";
    case ObjectiveConfigError::result_frames_invalid: return "result frames not positive";
    }
    return "unknown";
}

} // namespace battletanx
