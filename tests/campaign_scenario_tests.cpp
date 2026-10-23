// Cross-module campaign scenarios: campaign_flow + campaign_save +
// frontend + music_selection over the catalog and record value layers.
// Synthetic outcomes only; no retail bytes.

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_flow.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/campaign_save_transaction.hpp"
#include "battletanx/frontend_model.hpp"
#include "battletanx/music_selection.hpp"
#include "battletanx/state_hash.hpp"

#include "runtime_test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using namespace battletanx;
namespace flow=battletanx::campaign_flow;
namespace save=battletanx::campaign_save;
namespace fe=battletanx::frontend;
namespace music=battletanx::music_selection;

namespace {

flow::FlowCommand cmd(flow::FlowCommandKind kind) {
    flow::FlowCommand c;
    c.kind=kind;
    return c;
}

flow::FlowCommand with_record(flow::FlowCommandKind kind,const CampaignRecord& record) {
    flow::FlowCommand c=cmd(kind);
    c.record=record;
    return c;
}

flow::FlowCommand battle(bool won,std::int32_t score,std::int16_t bonus) {
    flow::FlowCommand c=cmd(flow::FlowCommandKind::battle_finished);
    c.outcome.won=won;
    c.outcome.player_score=score;
    c.outcome.player_bonus=bonus;
    return c;
}

flow::FlowCommand choose(flow::FlowResultChoice choice) {
    flow::FlowCommand c=cmd(flow::FlowCommandKind::result_choice);
    c.choice=choice;
    return c;
}

const flow::FlowTransition* find_kind(const std::vector<flow::FlowTransition>& out,flow::FlowTransitionKind kind) {
    for (const flow::FlowTransition& t : out) if (t.kind==kind) return &t;
    return nullptr;
}

bool offered(const flow::FlowTransition& result,flow::FlowResultChoice choice) {
    return result.choices[static_cast<std::size_t>(choice)];
}

bool is_endurance(const CampaignWaveDefinition& wave) { return wave.game_kind==7; }

// Synthetic per-wave outcome values.
std::int32_t score_for(std::uint8_t wave) { return 100*(static_cast<std::int32_t>(wave)+1); }
std::int16_t bonus_for(std::uint8_t wave) { return static_cast<std::int16_t>(3*(wave+1U)); }

// Commits a flow save request through the transaction. Returns the ack result
// of the final acknowledgement.
save::AckResult commit(save::SaveTransaction& tx,const flow::FlowTransition& request,bool succeed) {
    if (!request.save) return save::AckResult::no_pending;
    if (tx.request(request.wave_index,request.save->clear)!=save::RequestResult::issued)
        return save::AckResult::no_pending;
    return tx.acknowledge(tx.pending()->sequence,succeed);
}

struct Music {
    music::MusicState state{};
    std::vector<music::AudioCommand> log;
    void apply(music::MusicTransition transition,std::int32_t level,std::uint8_t volume) {
        music::AudioCommandList out;
        music::apply_music_transition(state,transition,
            music::TransitionContext{.level=level,.music_volume=volume,.demo_playback=false},out);
        for (std::size_t i=0U; i<out.size(); ++i) log.push_back(out[i]);
    }
};

// Maps flow transitions to music transitions (host policy, see api.md).
void drive_music(Music& m,const std::vector<flow::FlowTransition>& out,std::uint8_t volume) {
    for (const flow::FlowTransition& t : out) {
        switch (t.kind) {
        case flow::FlowTransitionKind::briefing:
            m.apply(music::MusicTransition::briefing,t.mission_id,volume);
            break;
        case flow::FlowTransitionKind::battle_launch:
            m.apply(music::MusicTransition::wave_loading,t.mission_id,volume);
            m.apply(music::MusicTransition::countdown_finished,t.mission_id,volume);
            break;
        case flow::FlowTransitionKind::retry:
            m.apply(music::MusicTransition::wave_restarted,t.mission_id,volume);
            m.apply(music::MusicTransition::countdown_finished,t.mission_id,volume);
            break;
        case flow::FlowTransitionKind::result:
            m.apply(music::MusicTransition::results,t.mission_id,volume);
            break;
        case flow::FlowTransitionKind::intermission:
            m.apply(music::MusicTransition::intermission,t.mission_id,volume);
            break;
        default:
            break;
        }
    }
}

struct Campaign {
    flow::CampaignFlow flow;
    save::SaveTransaction tx{default_campaign_record()};
    Music music;
    std::vector<std::uint8_t> saved_ids;
    std::vector<std::int32_t> launched_missions;
    bool completed=false;
};

// Plays from the flow's current phase to completion. Normal waves are won
// with save_and_continue when offered. Endurance waves are reported lost so
// the save button is offered (retail offers it only on a kind-7 loss).
// The last wave (mission 16) offers no save and completes without one.
bool play_to_end(Campaign& c,std::vector<flow::FlowTransition> out) {
    const auto volume=static_cast<std::uint8_t>(c.flow.options().music_volume);
    for (int guard=0; guard<200; ++guard) {
        drive_music(c.music,out,volume);
        if (find_kind(out,flow::FlowTransitionKind::completion)) { c.completed=true; return true; }
        if (find_kind(out,flow::FlowTransitionKind::rejected)) return false;
        if (find_kind(out,flow::FlowTransitionKind::return_to_menu)) return false;
        switch (c.flow.phase()) {
        case flow::FlowPhase::chapter_intro: {
            flow::FlowCommand f=cmd(flow::FlowCommandKind::intro_finished);
            f.accepted=true;
            out=c.flow.apply(f);
            break;
        }
        case flow::FlowPhase::briefing:
            out=c.flow.apply(cmd(flow::FlowCommandKind::briefing_finished));
            if (const flow::FlowTransition* l=find_kind(out,flow::FlowTransitionKind::battle_launch))
                c.launched_missions.push_back(l->mission_id);
            break;
        case flow::FlowPhase::battle: {
            const CampaignWaveDefinition* wave=c.flow.current_wave();
            if (!wave) return false;
            const std::uint8_t w=c.flow.wave_index();
            out=c.flow.apply(battle(!is_endurance(*wave),score_for(w),bonus_for(w)));
            break;
        }
        case flow::FlowPhase::result: {
            const CampaignWaveDefinition* wave=c.flow.current_wave();
            if (!wave) return false;
            const flow::FlowTransition* r=find_kind(out,flow::FlowTransitionKind::result);
            if (!r) return false;
            if (offered(*r,flow::FlowResultChoice::save_and_continue)) {
                out=c.flow.apply(choose(flow::FlowResultChoice::save_and_continue));
                break;
            }
            // Final wave (mission 16): retail offers no save button and
            // never writes after it (update_match_state.c:397-405,
            // run_campaign_waves.c:122-126), so completion carries no save.
            out=c.flow.apply(choose(flow::FlowResultChoice::continue_without_save));
            drive_music(c.music,out,volume);
            // A non-final wave without a save prompt is not expected here.
            if (!find_kind(out,flow::FlowTransitionKind::completion)) return false;
            if (find_kind(out,flow::FlowTransitionKind::save_request)) return false;
            if (wave->wave_index+1U!=campaign_wave_count) return false;
            c.completed=true;
            return true;
        }
        case flow::FlowPhase::save_pending: {
            const flow::FlowTransition* s=find_kind(out,flow::FlowTransitionKind::save_request);
            if (!s) return false;
            if (commit(c.tx,*s,true)!=save::AckResult::committed) return false;
            c.saved_ids.push_back(c.tx.authoritative().mission_id);
            flow::FlowCommand f=cmd(flow::FlowCommandKind::save_finished);
            f.accepted=true;
            out=c.flow.apply(f);
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

std::array<std::uint8_t,32> combined_digest(const Campaign& c,const fe::FrontendState* frontend) {
    StateWriter w;
    c.flow.serialize(w);
    c.tx.serialize(w);
    c.music.state.serialize(w);
    if (frontend) fe::serialize(w,*frontend);
    w.u32(static_cast<std::uint32_t>(c.saved_ids.size()));
    for (const std::uint8_t id : c.saved_ids) w.u8(id);
    return w.digest();
}

// Drives the flow from a start/resume to the first result screen of the
// current wave and returns that wave's launch transition.
std::optional<flow::FlowTransition> to_battle(flow::CampaignFlow& f,std::vector<flow::FlowTransition> out) {
    for (int guard=0; guard<4; ++guard) {
        if (const flow::FlowTransition* l=find_kind(out,flow::FlowTransitionKind::battle_launch)) return *l;
        if (f.phase()==flow::FlowPhase::chapter_intro) out=f.apply(cmd(flow::FlowCommandKind::intro_finished));
        else if (f.phase()==flow::FlowPhase::briefing) out=f.apply(cmd(flow::FlowCommandKind::briefing_finished));
        else return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

// 1. Cold start through all 17 waves; a committed save after each of the
//    first 16 waves and none after the final wave (retail behaviour).
RUNTIME_TEST(cold_start_completes_with_save_sequence) {
    Campaign c;
    const CampaignRecord start=default_campaign_record();
    REQUIRE(start.mission_id==1U);
    REQUIRE(play_to_end(c,c.flow.apply(with_record(flow::FlowCommandKind::start_new,start))));
    REQUIRE(c.completed);
    REQUIRE(c.flow.phase()==flow::FlowPhase::completed);
    REQUIRE(c.launched_missions.size()==campaign_wave_count);
    REQUIRE(c.saved_ids.size()+1U==campaign_wave_count);
    const auto waves=campaign_wave_definitions();
    for (std::size_t i=0U; i<campaign_wave_count; ++i) {
        REQUIRE(c.launched_missions[i]==waves[i].mission_id);
        if (i+1U<campaign_wave_count) {
            REQUIRE(c.saved_ids[i]==waves[i].saved_mission_id_after_clear);
            REQUIRE(static_cast<std::int32_t>(c.saved_ids[i])==waves[i+1U].mission_id);
        }
    }
    // Last save is the one after wave 15; it resumes at the final wave.
    REQUIRE(static_cast<std::int32_t>(c.saved_ids.back())==waves[16].mission_id);
    REQUIRE(static_cast<std::int32_t>(c.tx.authoritative().mission_id)==waves[16].mission_id);
    REQUIRE(!c.tx.pending().has_value());
    // Record holds wave 15's synthetic outcome; wave 16's is never written.
    REQUIRE(c.tx.authoritative().carry==score_for(15U));
    REQUIRE(c.tx.authoritative().bonus==bonus_for(15U));
    const CampaignWaveDefinition* resume=campaign_wave_for_record(c.tx.authoritative());
    REQUIRE(resume!=nullptr);
    REQUIRE(resume->wave_index==16U);
    // Header and unknown bytes carried from the default record.
    REQUIRE(c.tx.authoritative().header==campaign_header_default);
    return true;
}

// 2. Resume at every saved mission id; an invalid id returns to the menu.
RUNTIME_TEST(resume_from_each_saved_mission_id) {
    for (const CampaignWaveDefinition& wave : campaign_wave_definitions()) {
        CampaignRecord record=default_campaign_record();
        record.mission_id=static_cast<std::uint8_t>(wave.mission_id);
        record.carry=4321;
        record.bonus=12;
        flow::CampaignFlow f;
        const auto launch=to_battle(f,f.apply(with_record(flow::FlowCommandKind::resume,record)));
        REQUIRE(launch.has_value());
        REQUIRE(launch->wave_index==wave.wave_index);
        REQUIRE(launch->mission_id==wave.mission_id);
        REQUIRE(f.carry()==4321);
        REQUIRE(f.bonus()==12);
        const CampaignWaveLaunch expected=make_campaign_wave_launch(wave,4321,12,
            static_cast<std::uint8_t>(record.difficulty),record.unit_type);
        REQUIRE(launch->launch.player_score==expected.player_score);
        REQUIRE(launch->launch.player_bonus==expected.player_bonus);
        REQUIRE(launch->launch.game_kind==wave.game_kind);
    }
    for (const std::uint8_t bad : {std::uint8_t{0U},std::uint8_t{18U},std::uint8_t{0xFFU}}) {
        CampaignRecord record=default_campaign_record();
        record.mission_id=bad;
        REQUIRE(campaign_wave_for_record(record)==nullptr);
        flow::CampaignFlow f;
        const auto out=f.apply(with_record(flow::FlowCommandKind::resume,record));
        REQUIRE(out.size()==1U);
        REQUIRE(out[0].kind==flow::FlowTransitionKind::return_to_menu);
        REQUIRE(out[0].menu==flow::FlowMenuReason::invalid_resume);
        REQUIRE(out[0].reject==flow::FlowRejectReason::invalid_mission_id);
        REQUIRE(f.phase()==flow::FlowPhase::menu);
    }
    return true;
}

// 3. Non-endurance loss: retry restores the pre-battle carry/bonus; quit
//    leaves the authoritative record untouched.
RUNTIME_TEST(loss_retry_then_quit_keeps_record) {
    const CampaignWaveDefinition* wave=nullptr;
    for (const CampaignWaveDefinition& w : campaign_wave_definitions())
        if (!is_endurance(w)) { wave=&w; break; }
    REQUIRE(wave!=nullptr);
    CampaignRecord record=default_campaign_record();
    record.mission_id=static_cast<std::uint8_t>(wave->mission_id);
    record.carry=900;
    record.bonus=7;
    save::SaveTransaction tx(record);
    StateWriter before;
    tx.serialize(before);

    flow::CampaignFlow f;
    const auto first=to_battle(f,f.apply(with_record(flow::FlowCommandKind::resume,record)));
    REQUIRE(first.has_value());
    auto out=f.apply(battle(false,55555,99));
    const flow::FlowTransition* result=find_kind(out,flow::FlowTransitionKind::result);
    REQUIRE(result!=nullptr);
    REQUIRE(!result->won);
    REQUIRE(offered(*result,flow::FlowResultChoice::retry));
    REQUIRE(offered(*result,flow::FlowResultChoice::quit));
    REQUIRE(!offered(*result,flow::FlowResultChoice::save_and_continue));

    out=f.apply(choose(flow::FlowResultChoice::retry));
    const flow::FlowTransition* retry=find_kind(out,flow::FlowTransitionKind::retry);
    REQUIRE(retry!=nullptr);
    REQUIRE(f.retries()==1U);
    REQUIRE(f.carry()==900);
    REQUIRE(f.bonus()==7);
    REQUIRE(retry->launch.player_score==first->launch.player_score);
    REQUIRE(retry->launch.player_bonus==first->launch.player_bonus);
    REQUIRE(retry->launch.wave_index==first->launch.wave_index);

    out=f.apply(battle(false,1,1));
    REQUIRE(find_kind(out,flow::FlowTransitionKind::result)!=nullptr);
    out=f.apply(choose(flow::FlowResultChoice::quit));
    REQUIRE(out.size()==1U);
    REQUIRE(out[0].kind==flow::FlowTransitionKind::return_to_menu);
    REQUIRE(out[0].menu==flow::FlowMenuReason::wave_lost);
    REQUIRE(find_kind(out,flow::FlowTransitionKind::save_request)==nullptr);
    REQUIRE(f.phase()==flow::FlowPhase::menu);

    StateWriter after;
    tx.serialize(after);
    REQUIRE(before.bytes()==after.bytes());
    REQUIRE(tx.authoritative().mission_id==record.mission_id);
    REQUIRE(tx.authoritative().carry==900);
    // The transaction's own decision layer agrees: a non-kind-7 loss retries.
    save::WaveEnd end{};
    end.wave_index=wave->wave_index;
    end.wave_kind=static_cast<std::uint8_t>(wave->game_kind);
    end.outcome=save::WaveOutcome::lost;
    REQUIRE(save::decide_wave_step(end).step==save::WaveStep::retry);
    REQUIRE(tx.request(end)==save::RequestResult::not_saving);
    REQUIRE(!tx.pending().has_value());
    return true;
}

// 4. Endurance loss advances and saves.
RUNTIME_TEST(endurance_loss_advances_and_saves) {
    const CampaignWaveDefinition* wave=nullptr;
    for (const CampaignWaveDefinition& w : campaign_wave_definitions())
        if (is_endurance(w)) { wave=&w; break; }
    REQUIRE(wave!=nullptr);
    REQUIRE(wave->advances_on_failure);
    REQUIRE(wave->next_wave_index.has_value());
    CampaignRecord record=default_campaign_record();
    record.mission_id=static_cast<std::uint8_t>(wave->mission_id);
    save::SaveTransaction tx(record);
    flow::CampaignFlow f;
    REQUIRE(to_battle(f,f.apply(with_record(flow::FlowCommandKind::resume,record))).has_value());
    auto out=f.apply(battle(false,321,4));
    const flow::FlowTransition* result=find_kind(out,flow::FlowTransitionKind::result);
    REQUIRE(result!=nullptr);
    REQUIRE(!offered(*result,flow::FlowResultChoice::retry));
    REQUIRE(offered(*result,flow::FlowResultChoice::save_and_continue));
    out=f.apply(choose(flow::FlowResultChoice::save_and_continue));
    const flow::FlowTransition* s=find_kind(out,flow::FlowTransitionKind::save_request);
    REQUIRE(s!=nullptr);
    REQUIRE(s->save.has_value());
    REQUIRE(s->save->cleared_wave_index==wave->wave_index);
    REQUIRE(s->wave_index==*wave->next_wave_index);
    REQUIRE(s->save->clear.mission_id==wave->saved_mission_id_after_clear);
    REQUIRE(f.phase()==flow::FlowPhase::save_pending);
    REQUIRE(commit(tx,*s,true)==save::AckResult::committed);
    REQUIRE(tx.authoritative().mission_id==wave->saved_mission_id_after_clear);
    REQUIRE(tx.authoritative().carry==321);
    REQUIRE(tx.authoritative().bonus==4);
    out=f.apply(cmd(flow::FlowCommandKind::save_finished));
    const flow::FlowTransition* next=find_kind(out,flow::FlowTransitionKind::next_wave);
    REQUIRE(next!=nullptr);
    REQUIRE(next->wave_index==*wave->next_wave_index);
    // Cross-check with the transaction's own kind-7 rule.
    save::WaveEnd end{};
    end.wave_index=wave->wave_index;
    end.wave_kind=save::loss_advances_wave_kind;
    end.outcome=save::WaveOutcome::lost;
    end.save_flag=true;
    const save::WaveDecision d=save::decide_wave_step(end);
    REQUIRE(d.step==save::WaveStep::advance_save);
    REQUIRE(d.save);
    REQUIRE(d.next_wave_index==*wave->next_wave_index);
    return true;
}

// 5. Write failure, retry (re-request), success, then continue.
RUNTIME_TEST(save_failure_retry_success_continue) {
    const CampaignRecord start=default_campaign_record();
    save::SaveTransaction tx(start);
    flow::CampaignFlow f;
    REQUIRE(to_battle(f,f.apply(with_record(flow::FlowCommandKind::start_new,start))).has_value());
    REQUIRE(find_kind(f.apply(battle(true,500,6)),flow::FlowTransitionKind::result)!=nullptr);
    auto out=f.apply(choose(flow::FlowResultChoice::save_and_continue));
    const flow::FlowTransition* s=find_kind(out,flow::FlowTransitionKind::save_request);
    REQUIRE(s!=nullptr && s->save.has_value());
    const flow::FlowTransition request=*s;

    REQUIRE(tx.request(request.wave_index,request.save->clear)==save::RequestResult::issued);
    const std::uint32_t first=tx.pending()->sequence;
    REQUIRE(first!=0U);
    REQUIRE(tx.acknowledge(first,false)==save::AckResult::write_failed);
    REQUIRE(tx.failed_attempts()==1U);
    REQUIRE(tx.pending().has_value());
    REQUIRE(tx.authoritative().mission_id==start.mission_id);
    REQUIRE(tx.authoritative().carry==start.carry);
    REQUIRE(f.phase()==flow::FlowPhase::save_pending);

    // Retry: a failed pending request is replaced with a fresh sequence.
    REQUIRE(tx.request(request.wave_index,request.save->clear)==save::RequestResult::issued);
    const std::uint32_t second=tx.pending()->sequence;
    REQUIRE(second!=first);
    REQUIRE(tx.failed_attempts()==0U);
    REQUIRE(tx.acknowledge(first,true)==save::AckResult::sequence_mismatch);
    REQUIRE(tx.authoritative().mission_id==start.mission_id);
    REQUIRE(tx.acknowledge(second,true)==save::AckResult::committed);
    REQUIRE(!tx.pending().has_value());
    REQUIRE(tx.acknowledge(second,true)==save::AckResult::no_pending);
    REQUIRE(tx.authoritative().mission_id==campaign_wave_definitions()[0].saved_mission_id_after_clear);
    REQUIRE(tx.authoritative().carry==500);
    REQUIRE(tx.authoritative().bonus==6);
    REQUIRE(tx.authoritative().header==start.header);

    flow::FlowCommand done=cmd(flow::FlowCommandKind::save_finished);
    done.accepted=true;
    out=f.apply(done);
    REQUIRE(find_kind(out,flow::FlowTransitionKind::next_wave)!=nullptr);
    REQUIRE(f.wave_index()==1U);
    REQUIRE(f.phase()==flow::FlowPhase::chapter_intro || f.phase()==flow::FlowPhase::briefing);
    return true;
}

// 6. Options edited in the frontend reach the record, the flow, and the save.
RUNTIME_TEST(frontend_options_reach_wave_clear) {
    fe::FrontendState ui=fe::make_frontend_state();
    (void)fe::apply_pak_probe(ui,fe::PakProbe::ok_no_save,nullptr);
    REQUIRE(ui.pak==fe::PakDecision::use_defaults);
    auto press=[&ui](std::uint32_t bits) { return fe::update_frontend(ui,fe::MenuInput{.pressed=bits,.raw_pressed=0U}); };
    (void)press(fe::buttons::confirm);           // title -> campaign menu
    REQUIRE(ui.screen==fe::Screen::campaign_menu);
    // Setup menu opens on start game (row 6); load game (5) is skipped
    // to input code (4), then credits (3), then options (2).
    (void)press(fe::buttons::up);
    (void)press(fe::buttons::up);
    (void)press(fe::buttons::up);
    REQUIRE(ui.cursor==2);
    (void)press(fe::buttons::confirm);           // options
    REQUIRE(ui.screen==fe::Screen::options);
    REQUIRE(ui.cursor==0);
    (void)press(fe::buttons::left);              // sfx 7 -> 6
    (void)press(fe::buttons::down);
    (void)press(fe::buttons::left);              // music 5 -> 4
    (void)press(fe::buttons::down);
    (void)press(fe::buttons::right);             // difficulty easy -> normal
    (void)press(fe::buttons::down);
    (void)press(fe::buttons::right);             // powerups on
    (void)press(fe::buttons::down);
    (void)press(fe::buttons::right);             // regen on
    (void)press(fe::buttons::down);
    (void)press(fe::buttons::right);             // control scheme 1
    (void)press(fe::buttons::down);
    (void)press(fe::buttons::right);             // unlimited ammo on
    (void)press(fe::buttons::cancel);            // leave
    REQUIRE(ui.screen==fe::Screen::campaign_menu);
    const CampaignRecord edited=ui.record;
    REQUIRE(edited.sfx_volume==6);
    REQUIRE(edited.music_volume==4);
    REQUIRE(edited.difficulty==CampaignDifficulty::normal);
    REQUIRE(edited.powerups.raw==1U);
    REQUIRE(edited.powerup_regen.raw==1U);
    REQUIRE(edited.control_scheme==1U);
    REQUIRE(edited.unlimited_ammo.raw==1U);

    save::SaveTransaction tx(edited);
    flow::CampaignFlow f;
    const auto launch=to_battle(f,f.apply(with_record(flow::FlowCommandKind::start_new,edited)));
    REQUIRE(launch.has_value());
    REQUIRE(launch->launch.skill_group==static_cast<std::uint8_t>(CampaignDifficulty::normal));
    const flow::FlowOptions expected=flow::options_from_record(edited);
    REQUIRE(f.options().sfx_volume==expected.sfx_volume);
    REQUIRE(f.options().difficulty==expected.difficulty);
    REQUIRE(f.options().unit_type==9); // new campaign forces the default unit
    REQUIRE(find_kind(f.apply(battle(true,77,2)),flow::FlowTransitionKind::result)!=nullptr);
    const auto out=f.apply(choose(flow::FlowResultChoice::save_and_continue));
    const flow::FlowTransition* s=find_kind(out,flow::FlowTransitionKind::save_request);
    REQUIRE(s!=nullptr && s->save.has_value());
    const CampaignWaveClear& clear=s->save->clear;
    REQUIRE(clear.sfx_volume==6);
    REQUIRE(clear.music_volume==4);
    REQUIRE(clear.difficulty==CampaignDifficulty::normal);
    REQUIRE(clear.powerups==1U);
    REQUIRE(clear.powerup_regen==1U);
    REQUIRE(clear.control_scheme==1U);
    REQUIRE(clear.unlimited_ammo==1U);
    REQUIRE(clear.base_defense==edited.base_defense.raw);
    REQUIRE(clear.bindings==edited.bindings);
    REQUIRE(commit(tx,*s,true)==save::AckResult::committed);
    REQUIRE(tx.authoritative().music_volume==4);
    REQUIRE(tx.authoritative().difficulty==CampaignDifficulty::normal);
    REQUIRE(tx.authoritative().unlimited_ammo.raw==1U);
    return true;
}

// 7. Music cues for each wave transition agree with the catalog.
RUNTIME_TEST(music_cues_follow_catalog_per_wave) {
    Campaign c;
    REQUIRE(play_to_end(c,c.flow.apply(with_record(flow::FlowCommandKind::start_new,default_campaign_record()))));
    const auto volume=static_cast<std::uint8_t>(default_campaign_record().music_volume);
    // Each wave: wave_loading -> stop; countdown_finished -> play + volume.
    // briefing / results / intermission emit nothing.
    REQUIRE(c.music.log.size()==3U*campaign_wave_count);
    const auto waves=campaign_wave_definitions();
    for (std::size_t i=0U; i<campaign_wave_count; ++i) {
        const music::AudioCommand& stop=c.music.log[3U*i];
        const music::AudioCommand& play=c.music.log[3U*i+1U];
        const music::AudioCommand& vol=c.music.log[3U*i+2U];
        REQUIRE(stop.kind==music::AudioCommandKind::stop_music);
        REQUIRE(waves[i].music_transition>=0);
        const auto track=music::music_track_for_level(waves[i].mission_id);
        REQUIRE(track.has_value());
        REQUIRE(static_cast<std::int32_t>(*track)==static_cast<std::int32_t>(waves[i].music_transition));
        REQUIRE(play.kind==music::AudioCommandKind::play_music);
        REQUIRE(play.track==*track);
        REQUIRE(vol.kind==music::AudioCommandKind::set_music_volume);
        REQUIRE(vol.music_volume==volume);
    }
    REQUIRE(c.music.state.current_track==static_cast<std::int16_t>(waves[campaign_wave_count-1U].music_transition));
    // Retry restarts the same level track after a stop.
    Music m;
    flow::FlowTransition t;
    t.kind=flow::FlowTransitionKind::retry;
    t.mission_id=waves[0].mission_id;
    drive_music(m,std::vector<flow::FlowTransition>{t},volume);
    REQUIRE(m.log.size()==3U);
    REQUIRE(m.log[0].kind==music::AudioCommandKind::stop_music);
    REQUIRE(m.log[1].track==static_cast<std::uint8_t>(waves[0].music_transition));
    return true;
}

// 8. Two identical runs give identical combined digests.
RUNTIME_TEST(combined_state_checksum_is_deterministic) {
    auto run=[](Campaign& c,fe::FrontendState& ui) {
        ui=fe::make_frontend_state();
        (void)fe::apply_pak_probe(ui,fe::PakProbe::ok_no_save,nullptr);
        return play_to_end(c,c.flow.apply(with_record(flow::FlowCommandKind::start_new,ui.record)));
    };
    Campaign a;
    Campaign b;
    fe::FrontendState ua;
    fe::FrontendState ub;
    REQUIRE(run(a,ua));
    REQUIRE(run(b,ub));
    REQUIRE(a.flow.checksum()==b.flow.checksum());
    REQUIRE(combined_digest(a,&ua)==combined_digest(b,&ub));
    REQUIRE(a.saved_ids==b.saved_ids);
    REQUIRE(a.music.log.size()==b.music.log.size());
    // A different outcome changes the digest.
    Campaign d;
    REQUIRE(play_to_end(d,d.flow.apply(with_record(flow::FlowCommandKind::start_new,ua.record))));
    flow::FlowCommand o=cmd(flow::FlowCommandKind::set_options);
    o.options=d.flow.options();
    o.options.sfx_volume=1;
    (void)d.flow.apply(o);
    REQUIRE(d.flow.checksum()!=a.flow.checksum());
    return true;
}

RUNTIME_TEST_MAIN()
