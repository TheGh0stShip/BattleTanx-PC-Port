#include "battletanx/angle.hpp"
#include "battletanx/audio.hpp"
#include "battletanx/battle_session.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/camera.hpp"
#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_briefing.hpp"
#include "battletanx/campaign_battle_bridge.hpp"
#include "battletanx/campaign_flow.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/campaign_save_transaction.hpp"
#include "battletanx/campaign_spawn.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/fixed_tick.hpp"
#include "battletanx/frontend_model.hpp"
#include "battletanx/hud.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/level.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/model.hpp"
#include "battletanx/music_selection.hpp"
#include "battletanx/owner_profiles.hpp"
#include "battletanx/pc_settings.hpp"
#include "battletanx/platform.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/software_renderer.hpp"
#include "battletanx/terrain.hpp"
#include "battletanx/unit_kinds.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {
constexpr std::size_t pickup_model_kind_count=23U;

std::array<std::uint32_t,battletanx::turret_types> load_turret_model_slots(
    const battletanx::Rom& rom) {
    constexpr std::size_t table_offset=0xB5FB0U;
    const auto view=rom.view();
    std::array<std::uint32_t,battletanx::turret_types> slots{};
    for (std::size_t index=0U; index<slots.size(); ++index) {
        slots[index]=view.u32(table_offset+index*4U);
        if (slots[index]>=182U) throw std::runtime_error("turret model slot is invalid");
    }
    return slots;
}

std::array<std::uint32_t,pickup_model_kind_count> load_pickup_model_slots(
    const battletanx::Rom& rom) {
    constexpr std::size_t table_offset=0xB5CE0U;
    const auto bytes=rom.bytes().subspan(table_offset,pickup_model_kind_count*4U);
    std::array<std::uint32_t,pickup_model_kind_count> slots{};
    for (std::size_t index=0U; index<slots.size(); ++index) {
        const std::size_t offset=index*4U;
        slots[index]=static_cast<std::uint32_t>(bytes[offset])<<24U |
                     static_cast<std::uint32_t>(bytes[offset+1U])<<16U |
                     static_cast<std::uint32_t>(bytes[offset+2U])<<8U |
                     static_cast<std::uint32_t>(bytes[offset+3U]);
        if (slots[index]>=182U) throw std::runtime_error("pickup model slot is invalid");
    }
    return slots;
}

enum class WaveRunResult { exit, title, retry, advance, complete, selection_previous, selection_next };

struct PakProbeResult {
    battletanx::frontend::PakProbe probe{battletanx::frontend::PakProbe::ok_no_save};
    std::optional<battletanx::CampaignRecord> record;
};

PakProbeResult probe_controller_pak(const std::filesystem::path& path) {
    try {
        const auto bytes=battletanx::load_controller_pak_record(path);
        if (!bytes) return {};
        const auto record=battletanx::parse_campaign_record(*bytes);
        if (!record) return {.probe=battletanx::frontend::PakProbe::checksum_bad};
        return {.probe=battletanx::frontend::PakProbe::ok_with_save,.record=*record};
    } catch (const std::exception&) {
        return {.probe=battletanx::frontend::PakProbe::damaged};
    }
}

battletanx::frontend::MenuInput frontend_input(const battletanx::PlayerInput& input) {
    using namespace battletanx::frontend;
    std::uint32_t pressed=0U;
    if ((input.pressed&0x0800U)!=0U) pressed|=buttons::up;
    if ((input.pressed&0x0400U)!=0U) pressed|=buttons::down;
    if ((input.pressed&0x0200U)!=0U) pressed|=buttons::left;
    if ((input.pressed&0x0100U)!=0U) pressed|=buttons::right;
    if ((input.pressed&(0x8000U|0x1000U))!=0U) pressed|=buttons::confirm;
    if ((input.pressed&0x4000U)!=0U) pressed|=buttons::cancel;
    if ((input.pressed&0x0020U)!=0U) pressed|=buttons::prev_target;
    if ((input.pressed&0x0010U)!=0U) pressed|=buttons::next_target;
    return {.pressed=pressed,.raw_pressed=input.pressed};
}

std::optional<battletanx::CampaignRecord> run_campaign_frontend(
    battletanx::Platform& platform,const battletanx::HudAssets& hud_assets,
    const battletanx::SoundBank& sounds,const std::filesystem::path& pak_path,
    const PakProbeResult& initial_pak) {
    platform.set_sound_bank(sounds);
    auto state=battletanx::frontend::make_frontend_state();
    const auto* loaded=initial_pak.record ? &*initial_pak.record : nullptr;
    static_cast<void>(battletanx::frontend::apply_pak_probe(
        state,initial_pak.probe,loaded));
    std::array<battletanx::ControllerPad,4> previous{};
    std::array<battletanx::ControllerPad,4> current{};
    std::vector<std::uint32_t> framebuffer(
        static_cast<std::size_t>(battletanx::native_width)*
        static_cast<std::size_t>(battletanx::native_height));
    while (platform.pump(current)) {
        const auto pad=battletanx::translate_player_input(current[0],previous[0]);
        previous=current;
        const auto step=battletanx::frontend::update_frontend(state,frontend_input(pad));
        for (const auto& command:step.commands) {
            using battletanx::frontend::CommandKind;
            switch (command.kind) {
            case CommandKind::write_campaign_record:
                battletanx::write_controller_pak_record(
                    pak_path,battletanx::serialize_campaign_record(state.record));
                state.record_loaded=true;
                state.save_enabled=true;
                break;
            case CommandKind::repair_pak: {
                std::error_code error;
                std::filesystem::remove(pak_path,error);
                if (error) throw std::runtime_error(
                    "failed to replace damaged Controller Pak image: "+error.message());
                battletanx::write_controller_pak_record(
                    pak_path,battletanx::serialize_campaign_record(
                        battletanx::default_campaign_record()));
                break;
            }
            case CommandKind::repoll_pak: {
                const auto refreshed=probe_controller_pak(pak_path);
                const auto* refreshed_record=refreshed.record ? &*refreshed.record : nullptr;
                static_cast<void>(battletanx::frontend::apply_pak_probe(
                    state,refreshed.probe,refreshed_record));
                break;
            }
            case CommandKind::start_mission:
                return state.record;
            default:
                break;
            }
        }
        battletanx::draw_frontend_menu(framebuffer,battletanx::native_width,
            battletanx::native_height,hud_assets,
            battletanx::frontend::make_menu_view(state));
        platform.present(framebuffer,battletanx::native_width,battletanx::native_height);
    }
    return std::nullopt;
}

void apply_flow_state_to_record(battletanx::CampaignRecord& record,
                                const battletanx::campaign_flow::CampaignFlow& flow) {
    battletanx::CampaignWaveClear clear;
    clear.mission_id=flow.current_wave()!=nullptr ?
        static_cast<std::uint8_t>(flow.current_wave()->mission_id) : 0U;
    clear.carry=flow.carry();
    clear.unit_type=flow.options().unit_type;
    clear.bonus=flow.bonus();
    clear.control_scheme=flow.options().control_scheme;
    clear.difficulty=flow.options().difficulty;
    clear.powerups=flow.options().powerups;
    clear.powerup_regen=flow.options().powerup_regen;
    clear.base_defense=flow.options().base_defense;
    clear.unlimited_ammo=flow.options().unlimited_ammo;
    clear.sfx_volume=flow.options().sfx_volume;
    clear.music_volume=flow.options().music_volume;
    clear.bindings=flow.options().bindings;
    battletanx::apply_campaign_wave_clear(record,clear);
}

void launch_flow_battle(battletanx::campaign_flow::CampaignFlow& flow) {
    using namespace battletanx::campaign_flow;
    if (flow.phase()==FlowPhase::chapter_intro) {
        FlowCommand command;
        command.kind=FlowCommandKind::intro_finished;
        const auto transitions=flow.apply(command);
        if (transitions.empty() || transitions.front().kind!=FlowTransitionKind::briefing)
            throw std::runtime_error("campaign intro did not reach the briefing");
    }
    if (flow.phase()==FlowPhase::briefing) {
        FlowCommand command;
        command.kind=FlowCommandKind::briefing_finished;
        const auto transitions=flow.apply(command);
        if (transitions.empty() || transitions.front().kind!=FlowTransitionKind::battle_launch)
            throw std::runtime_error("campaign briefing did not launch the battle");
    }
    if (flow.phase()!=FlowPhase::battle)
        throw std::runtime_error("campaign flow is not ready for battle");
}

const battletanx::CampaignWaveDefinition* resume_campaign(
    battletanx::campaign_flow::CampaignFlow& flow,
    const battletanx::CampaignRecord& record) {
    flow=battletanx::campaign_flow::CampaignFlow{};
    battletanx::campaign_flow::FlowCommand resume;
    resume.kind=battletanx::campaign_flow::FlowCommandKind::resume;
    resume.record=record;
    const auto transitions=flow.apply(resume);
    if (transitions.empty() || flow.current_wave()==nullptr)
        throw std::runtime_error("Controller Pak campaign is complete or has an unknown mission");
    battletanx::campaign_flow::FlowCommand options;
    options.kind=battletanx::campaign_flow::FlowCommandKind::set_options;
    options.options=battletanx::campaign_flow::options_from_record(record);
    static_cast<void>(flow.apply(options));
    launch_flow_battle(flow);
    return flow.current_wave();
}

std::string roster_summary(const battletanx::ControllerRoster& roster) {
    std::array<std::size_t,3> counts{};
    for (std::size_t index=0U; index<roster.count; ++index) ++counts[roster.kinds[index]];
    std::string result="LINEUP";
    constexpr std::array<std::string_view,3> names{"TANK","BIKE","GOLIATH"};
    for (std::size_t kind=0U; kind<counts.size(); ++kind) {
        if (counts[kind]==0U) continue;
        result+=' ';
        result+=std::to_string(counts[kind]);
        result+=names[kind];
        if (counts[kind]!=1U) result+='S';
    }
    return result;
}

WaveRunResult run_platform_diagnostic(battletanx::Platform& platform,
                             const battletanx::WorldLevel& world,
                             const battletanx::NavigationLevel& navigation,
                             const battletanx::AngleTable& angles,
                             const battletanx::InputBindings& bindings,
                             const battletanx::ModelAssets& assets,
                             const battletanx::SoundBank& sounds,
                             const battletanx::MusicTrack& music,
                             const battletanx::HudAssets& hud_assets,
                             const std::optional<battletanx::RgbaImage>& briefing_image,
                             const battletanx::CampaignBriefingPage& briefing_page,
                             const std::array<std::uint32_t,pickup_model_kind_count>& pickup_models,
                             const std::array<std::uint32_t,battletanx::turret_types>& turret_models,
                             std::span<const std::uint8_t> roster_table,
                             const battletanx::CampaignWaveDefinition& wave,
                             std::optional<battletanx::TeamSpawnTable> team_spawns,
                             battletanx::CampaignRecord& campaign,
                             battletanx::campaign_flow::CampaignFlow& campaign_flow,
                             battletanx::campaign_save::SaveTransaction& save_transaction,
                             const battletanx::PcSettings& settings,
                             const std::filesystem::path& pak_path,
                             std::uint64_t smoke_ticks=0U) {
    platform.set_sound_bank(sounds);
    platform.set_music_track(music,settings.master_volume*settings.music_volume*
                                    static_cast<float>(campaign.music_volume)/7.0F);
    battletanx::SoftwareRenderer renderer(
        battletanx::native_width,battletanx::native_height);
    std::array<battletanx::ControllerPad,4> previous{};
    std::array<battletanx::ControllerPad,4> current{};
    constexpr std::uint32_t ticks_per_second=30U;
    const auto clock_start=std::chrono::steady_clock::now();
    const auto terrain=battletanx::TerrainSurface::build(world,assets,angles);
    battletanx::CampaignSpawnInput spawn_input;
    spawn_input.game_kind=wave.game_kind;
    spawn_input.active_player_count=1;
    spawn_input.team_spawns=std::move(team_spawns);
    const auto launch=battletanx::make_campaign_wave_launch(
        wave,campaign.carry,campaign.bonus,static_cast<std::uint8_t>(campaign.difficulty),
        campaign.unit_type);
    const auto selected_roster=battletanx::decode_controller_roster(
        roster_table,launch.player_unit_kind);
    if (!selected_roster) throw std::runtime_error("campaign player roster is invalid");
    const std::string selected_roster_summary=roster_summary(*selected_roster);
    for (std::size_t owner=0U; owner<battletanx::spawn_owner_count; ++owner) {
        auto& setup=spawn_input.owners[owner];
        setup.present=owner<static_cast<std::size_t>(wave.owner_count);
        setup.computer=owner!=0U;
        setup.team=static_cast<std::uint8_t>(wave.owners[owner].team);
        setup.difficulty=static_cast<std::uint8_t>(campaign.difficulty);
        const std::int32_t roster_index=owner==0U ? launch.player_unit_kind
                                                  : wave.owners[owner].unit_kind;
        const auto roster=battletanx::decode_controller_roster(roster_table,roster_index);
        if (!roster) throw std::runtime_error("campaign controller roster is invalid");
        setup.lineup=roster->kinds;
        setup.lineup_count=roster->count;
        setup.lives=std::max(1,wave.owners[owner].reserve);
    }
    constexpr std::uint32_t session_seed=0x42545831U;
    battletanx::RetailRandom spawn_random{session_seed};
    battletanx::SpawnPlanError spawn_error{};
    auto spawn_plan=battletanx::build_campaign_spawn_plan(
        spawn_input,world,&navigation,spawn_random,&spawn_error);
    if (!spawn_plan) {
        throw std::runtime_error("campaign spawn plan failed: "+
                                 std::to_string(static_cast<unsigned>(spawn_error)));
    }
    std::cout << "Recovered wave " << static_cast<unsigned>(wave.wave_index)
              << " population: " << spawn_plan->units.size() << " units, "
              << spawn_plan->spawners.size() << " spawners, "
              << spawn_plan->pickups.size() << " pickups\n";
    battletanx::SessionConfig session_config;
    session_config.rng_seed=spawn_random.seed();
    session_config.mission.game_kind=static_cast<std::uint32_t>(wave.game_kind);
    session_config.mission.countdown_unit=wave.countdown_unit;
    session_config.combat.unlimited_ammo=campaign.unlimited_ammo.enabled();
    const auto world_collision=battletanx::level_world_collision(world,navigation,angles);
    battletanx::campaign_bridge::BridgeConfig bridge_config;
    bridge_config.game_kind=wave.game_kind;
    bridge_config.game_mode=wave.game_kind;
    bridge_config.world=world_collision;
    for (std::size_t owner=0U; owner<battletanx::spawn_owner_count; ++owner) {
        bridge_config.spawner_owners[owner].budget=spawn_plan->owners[owner].reserve;
        bridge_config.spawner_owners[owner].gate_open=true;
        const auto group=std::min<std::size_t>(static_cast<std::size_t>(launch.skill_group),
                                               battletanx::campaign_skill_groups-1U);
        const auto turret_owner=battletanx::owner_profiles::make_turret_owner(
            wave.owners[owner].skills[group],spawn_input.owners[owner].computer,
            static_cast<std::uint32_t>(campaign.difficulty));
        bridge_config.turret_owners[owner]=turret_owner.config;
    }
    if (wave.game_kind==3 || wave.game_kind==4 || wave.game_kind==6) {
        battletanx::queen_world::Config queen_config;
        queen_config.game_kind=static_cast<std::uint32_t>(wave.game_kind);
        queen_config.owner_count=static_cast<std::uint8_t>(wave.owner_count);
        queen_config.slot_rank=spawn_plan->slot_rank;
        for (std::size_t owner=0U; owner<battletanx::spawn_owner_count; ++owner) {
            queen_config.owners[owner].present=spawn_plan->owners[owner].present;
            queen_config.owners[owner].team=spawn_plan->owners[owner].team;
        }
        bridge_config.queen=queen_config;
    }
    battletanx::campaign_bridge::Bridge bridge(std::move(bridge_config),angles);
    if (bridge.error()!=battletanx::campaign_bridge::CreateError::none)
        throw std::runtime_error("campaign bridge objective configuration failed");
    for (const auto& object:battletanx::queen_level_objects(*spawn_plan,world))
        static_cast<void>(bridge.place_queen_object(object));
    const auto objective_markers=battletanx::objective_marker_entries(*spawn_plan,world);
    static_cast<void>(bridge.place_markers(objective_markers,1U));
    bridge.finish_queen_placement();

    battletanx::BattleSession session(
        session_config,angles,battletanx::level_unit_motion(world,navigation,angles),
        world_collision,
        battletanx::level_route_planner(navigation));
    for (std::size_t owner=0U; owner<battletanx::spawn_owner_count; ++owner) {
        if (!spawn_plan->owners[owner].present) continue;
        const auto count=static_cast<std::uint8_t>(std::count_if(
            spawn_plan->units.begin(),spawn_plan->units.end(),
            [owner](const battletanx::PlannedUnit& unit) { return unit.owner==owner; }));
        const float scale=spawn_plan->owners[owner].human_scale.value_or(
            battletanx::retail::human_scale_default);
        const auto group=std::min<std::size_t>(static_cast<std::size_t>(launch.skill_group),
                                               battletanx::campaign_skill_groups-1U);
        const auto& skill=wave.owners[owner].skills[group];
        battletanx::AiProfile profile;
        profile.skill_a=skill.aggressiveness;
        profile.skill_b=skill.intelligence;
        profile.speed=skill.tint[0];
        profile.rate_primary=skill.tint[1];
        profile.rate_class4=skill.tint[2];
        profile.rate_kind2=skill.tint[3];
        profile.value_32c=skill.tint[4];
        session.add_owner(static_cast<std::uint8_t>(owner),battletanx::OwnerSetup{
            spawn_plan->owners[owner].team,spawn_input.owners[owner].computer,
            std::max<std::uint8_t>(count,1U),
            static_cast<std::int16_t>(std::max(spawn_plan->owners[owner].reserve,0)),false,
            scale,scale,bridge.queens()!=nullptr && bridge.queens()->has_base(static_cast<std::uint8_t>(owner)),
            battletanx::owner_difficulty_from_profile(profile),
            owner==0U ? launch.player_score : wave.owners[owner].initial_score});
        if (!spawn_plan->respawn_points[owner].points.empty()) {
            const auto& point=spawn_plan->respawn_points[owner].points.front();
            session.set_respawn_point(static_cast<std::uint8_t>(owner),
                                      {point.x,point.z,point.heading});
        }
    }
    std::optional<battletanx::EntityId> player_id;
    for (const auto& planned:spawn_plan->units) {
        const auto id=session.spawn_unit({planned.kind,planned.owner,planned.x,planned.z,
                                          planned.heading,planned.flags,planned.slot});
        if (!id) throw std::runtime_error("failed to spawn a planned campaign unit");
        if (planned.unit_id==spawn_plan->player.unit_id) player_id=id;
    }
    if (!player_id) throw std::runtime_error("failed to spawn the player tank");
    for (const auto& planned:spawn_plan->spawners) {
        const auto error=bridge.add_spawner(
            planned.definition,planned.definition.linked ? std::optional<std::int32_t>{1} : std::nullopt);
        if (error) {
            throw std::runtime_error("failed to register a planned campaign spawner");
        }
    }
    for (const auto& placement:spawn_plan->owned_objects) {
        if (!bridge.spawn_turret(battletanx::owned_object_turret_spawn(placement),session)) {
            throw std::runtime_error("failed to register a planned campaign turret");
        }
    }
    battletanx::PickupWeights pickup_weights;
    std::uint16_t pickup_slot=0U;
    for (const auto& placement:spawn_plan->pickups) {
        battletanx::PickupRecord pickup;
        pickup.id={pickup_slot++,1U};
        pickup.kind=placement.kind;
        pickup.x=placement.x;
        pickup.z=placement.z;
        if (session.add_pickup(pickup)) pickup_weights.register_permanent(pickup.kind);
    }
    session.set_pickup_weights(pickup_weights);
    auto terrain_sample=terrain.sample(spawn_plan->player.x,spawn_plan->player.z);
    battletanx::PlayerTank tank;
    battletanx::sync_player_tank_from_unit(tank,*session.units().get(*player_id));
    const float initial_player_x=tank.x;
    const float initial_player_z=tank.z;
    const auto initial_primary_ammo=session.units().get(*player_id)->ammo[
        session.units().get(*player_id)->primary_weapon.kind];
    std::cout << "Built native terrain surface: " << terrain.triangle_count()
              << " triangles, player height "
              << (terrain_sample ? terrain_sample->height : 0.0F) << '\n';
    std::uint64_t game_tick=0U;
    bool verified_native_frame=false;
    std::optional<battletanx::campaign_flow::FlowBattleOutcome> pending_outcome;
    std::optional<battletanx::frontend::FrontendState> pause_frontend;
    std::optional<battletanx::frontend::FrontendState> result_frontend;
    std::optional<WaveRunResult> resolved_result;
    std::function<void(std::vector<battletanx::campaign_flow::FlowTransition>)> handle_transitions;
    handle_transitions=[&](std::vector<battletanx::campaign_flow::FlowTransition> transitions) {
        using namespace battletanx::campaign_flow;
        for (const auto& transition:transitions) {
            switch (transition.kind) {
            case FlowTransitionKind::result: {
                if (transition.auto_advanced) break;
                auto state=battletanx::frontend::make_frontend_state();
                state.record=campaign;
                state.record_loaded=true;
                state.save_enabled=true;
                static_cast<void>(battletanx::frontend::enter_result(state,transition));
                result_frontend=std::move(state);
                break;
            }
            case FlowTransitionKind::save_request: {
                if (!transition.save || save_transaction.request(
                        static_cast<std::uint8_t>(transition.save->cleared_wave_index+1U),
                        transition.save->clear)!=battletanx::campaign_save::RequestResult::issued) {
                    throw std::runtime_error("failed to stage campaign progress");
                }
                const auto sequence=save_transaction.pending()->sequence;
                battletanx::write_controller_pak_record(pak_path,save_transaction.pending()->bytes);
                if (save_transaction.acknowledge(sequence,true)!=
                    battletanx::campaign_save::AckResult::committed) {
                    throw std::runtime_error("failed to commit campaign progress");
                }
                campaign=save_transaction.authoritative();
                std::cout << "Saved campaign progress to the Controller Pak\n";
                FlowCommand done;
                done.kind=FlowCommandKind::save_finished;
                done.accepted=true;
                handle_transitions(campaign_flow.apply(done));
                break;
            }
            case FlowTransitionKind::retry:
                resolved_result=WaveRunResult::retry;
                break;
            case FlowTransitionKind::next_wave:
                apply_flow_state_to_record(campaign,campaign_flow);
                resolved_result=WaveRunResult::advance;
                break;
            case FlowTransitionKind::completion:
                apply_flow_state_to_record(campaign,campaign_flow);
                resolved_result=WaveRunResult::complete;
                break;
            case FlowTransitionKind::return_to_menu:
                resolved_result=WaveRunResult::title;
                break;
            case FlowTransitionKind::rejected:
                throw std::runtime_error("campaign flow rejected a host transition");
            default:
                break;
            }
        }
    };
    while (platform.pump(current)) {
        const auto pad=battletanx::translate_player_input(current[0],previous[0]);
        if (session.mission().state()==battletanx::MatchState::title) {
            if ((pad.pressed&0x0200U)!=0U) return WaveRunResult::selection_previous;
            if ((pad.pressed&0x0100U)!=0U) return WaveRunResult::selection_next;
        }
        auto input=battletanx::translate_drive_input(pad,bindings);
        battletanx::SessionInput session_input;
        session_input.drive=input;
        session_input.start_pressed=(pad.pressed&0x1000U)!=0U;
        session_input.advance=(pad.pressed&(0x8000U|0x1000U))!=0U;
        bool resume_from_pause=false;
        if (pause_frontend) {
            session_input.drive={};
            session_input.start_pressed=false;
            session_input.advance=false;
            const auto frontend_step=battletanx::frontend::update_frontend(
                *pause_frontend,frontend_input(pad));
            for (const auto& command:frontend_step.commands) {
                using battletanx::frontend::CommandKind;
                if (command.kind==CommandKind::resume_mission) {
                    resume_from_pause=true;
                    pause_frontend.reset();
                } else if (command.kind==CommandKind::restart_mission ||
                           command.kind==CommandKind::quit_to_title) {
                    battletanx::campaign_flow::FlowCommand finished;
                    finished.kind=battletanx::campaign_flow::FlowCommandKind::battle_finished;
                    finished.outcome.restarted=command.kind==CommandKind::restart_mission;
                    finished.outcome.aborted=true;
                    finished.outcome.player_score=session.owners()[0].score;
                    finished.outcome.player_bonus=campaign.bonus;
                    handle_transitions(campaign_flow.apply(finished));
                }
            }
        }
        if (resolved_result) return *resolved_result;
        if (smoke_ticks!=0U) {
            session_input.advance=game_tick==0U;
            if (game_tick>=130U) {
                session_input.drive.throttle=80.0F;
                session_input.drive.rotation=game_tick<180U ? 18.0F : -12.0F;
                session_input.drive.fire=true;
                session_input.drive.fire_pressed=game_tick==130U;
            }
        }
        if (result_frontend) {
            const auto frontend_step=battletanx::frontend::update_frontend(
                *result_frontend,frontend_input(pad));
            for (const auto& command:frontend_step.commands) {
                using battletanx::frontend::CommandKind;
                if (command.kind==CommandKind::advance_mission) {
                    battletanx::campaign_flow::FlowCommand choice;
                    choice.kind=battletanx::campaign_flow::FlowCommandKind::result_choice;
                    choice.choice=static_cast<battletanx::campaign_flow::FlowResultChoice>(command.value);
                    handle_transitions(campaign_flow.apply(choice));
                } else if (command.kind==CommandKind::restart_mission) {
                    battletanx::campaign_flow::FlowCommand choice;
                    choice.kind=battletanx::campaign_flow::FlowCommandKind::result_choice;
                    choice.choice=battletanx::campaign_flow::FlowResultChoice::retry;
                    handle_transitions(campaign_flow.apply(choice));
                } else if (command.kind==CommandKind::quit_to_title) {
                    battletanx::campaign_flow::FlowCommand choice;
                    choice.kind=battletanx::campaign_flow::FlowCommandKind::result_choice;
                    choice.choice=battletanx::campaign_flow::FlowResultChoice::quit;
                    handle_transitions(campaign_flow.apply(choice));
                }
            }
        }
        previous=current;
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-clock_start).count();
        const auto target_tick=smoke_ticks!=0U ? game_tick+1U : battletanx::fixed_rate_tick(
            static_cast<std::uint64_t>(elapsed),ticks_per_second);
        while (game_tick<target_tick) {
            if (resume_from_pause) {
                session_input.start_pressed=true;
                resume_from_pause=false;
            }
            static_cast<void>(bridge.tick(session,session_input));
            for (const auto& event:session.gameplay_events()) {
                if (event.kind==battletanx::GameplayEventKind::unit_damaged && event.owner==0U) {
                    platform.rumble(0U,std::clamp(static_cast<float>(event.value)/40.0F,0.2F,1.0F),250U);
                }
                if (event.kind==battletanx::GameplayEventKind::unit_destroyed && event.owner==0U)
                    platform.rumble(0U,1.0F,500U);
                if ((event.kind==battletanx::GameplayEventKind::mission_succeeded ||
                     event.kind==battletanx::GameplayEventKind::mission_failed) &&
                    !pending_outcome && !result_frontend && !resolved_result) {
                    pending_outcome=battletanx::campaign_flow::FlowBattleOutcome{
                        .won=event.kind==battletanx::GameplayEventKind::mission_succeeded,
                        .aborted=false,
                        .player_score=session.owners()[0].score,
                        .player_bonus=campaign.bonus,
                        .restarted=false,
                        .result_code=battletanx::campaign_flow::flow_result_code_from_won};
                }
            }
            const auto listener_id=session.owner_unit(0U);
            const auto* listener=listener_id ? session.units().get(*listener_id) : nullptr;
            for (const auto& sound:session.sound_events()) {
                const float pan=listener!=nullptr && sound.positional ?
                    std::clamp((sound.x-listener->x)/2000.0F,-1.0F,1.0F) : 0.0F;
                platform.play_sound(sound.sound_id,pan,
                    settings.master_volume*settings.effects_volume*
                    static_cast<float>(campaign.sfx_volume)/7.0F);
            }
            bridge.clear_events(session);
            ++game_tick;
            session_input.drive.fire_pressed=false;
            session_input.drive.special_pressed=false;
            session_input.start_pressed=false;
            session_input.advance=false;
        }
        if (session.mission().state()==battletanx::MatchState::paused &&
            !pause_frontend && !resolved_result) {
            auto state=battletanx::frontend::make_frontend_state();
            state.record=campaign;
            state.screen=battletanx::frontend::Screen::in_mission;
            battletanx::frontend::enter_pause(state);
            pause_frontend=std::move(state);
        }
        if (pending_outcome) {
            battletanx::campaign_flow::FlowCommand finished;
            finished.kind=battletanx::campaign_flow::FlowCommandKind::battle_finished;
            finished.outcome=*pending_outcome;
            pending_outcome.reset();
            handle_transitions(campaign_flow.apply(finished));
        }
        if (resolved_result) return *resolved_result;
        const auto active_player=session.owner_unit(0U);
        if (active_player) {
            const auto* player=session.units().get(*active_player);
            if (player==nullptr) throw std::runtime_error("the player tank handle became stale");
            battletanx::sync_player_tank_from_unit(tank,*player);
            const auto vehicle_audio=battletanx::vehicle_sound_mix(
                player->velocity_x,player->velocity_z,
                settings.master_volume*settings.effects_volume*
                static_cast<float>(campaign.sfx_volume)/7.0F);
            platform.set_looping_sound(0U,0x10U,0.0F,
                                        vehicle_audio.engine_gain,vehicle_audio.engine_pitch);
            if (vehicle_audio.skid_gain>0.0F) {
                platform.set_looping_sound(1U,0x12U,0.0F,
                                            vehicle_audio.skid_gain,1.0F);
            } else {
                platform.stop_looping_sound(1U);
            }
        } else {
            platform.stop_looping_sound(0U);
            platform.stop_looping_sound(1U);
        }
        terrain_sample=terrain.sample(tank.x,tank.z);
        if (terrain_sample) tank.y=terrain_sample->height;
        battletanx::DrawList draw;
        draw.animation_tick=game_tick;
        const std::array<float,3> ground_normal=terrain_sample ? terrain_sample->normal :
            std::array<float,3>{0.0F,1.0F,0.0F};
        const auto camera=battletanx::build_tank_camera(
            tank,angles,ground_normal,world);
        draw.eye=camera.eye;
        draw.view=battletanx::look_at_matrix(camera.eye,camera.target,camera.up);
        draw.projection=battletanx::perspective_matrix(
            camera.vertical_fov_degrees,camera.aspect_ratio,
            camera.near_plane,camera.far_plane);
        draw.items.reserve(world.items.size()+5U*battletanx::unit_capacity+
                           battletanx::shell_capacity);
        for (const auto& item:world.items) {
            const std::uint8_t layer=item.type==0 ? 0U : item.type==0x1C ? 6U : 2U;
            draw.items.push_back({static_cast<std::uint32_t>(item.shape),
                battletanx::yaw_translation_matrix(item.angle,item.x,0.0F,item.z,angles),
                layer,battletanx::decode_world_animation_phase(item.kind)});
        }
        session.units().for_each([&](battletanx::EntityId,const battletanx::UnitRecord& unit) {
            if (!unit.alive) return;
            const auto unit_ground=terrain.sample(unit.x,unit.z);
            const float unit_y=unit_ground ? unit_ground->height : unit.y;
            const auto normal=unit_ground ? unit_ground->normal :
                std::array<float,3>{0.0F,1.0F,0.0F};
            const auto heading=battletanx::degrees_to_retail_angle(unit.heading_degrees);
            const auto turret=battletanx::degrees_to_retail_angle(unit.turret_degrees);
            const auto body=battletanx::terrain_yaw_translation_matrix(
                heading,unit.x,unit_y,unit.z,normal,angles);
            const auto turret_transform=battletanx::terrain_yaw_translation_matrix(
                turret,unit.x,unit_y,unit.z,normal,angles);
            const bool damaged=unit.health>0 && unit.health<=unit.max_health/2;
            const auto models=battletanx::unit_model_set(
                static_cast<std::uint8_t>(unit.kind),damaged);
            if (!models) return;
            for (std::size_t index=0U; index<models->part_count; ++index) {
                const auto& part=models->parts[index];
                const auto& transform=part.transform==battletanx::UnitModelTransform::turret ?
                    turret_transform : body;
                std::int8_t phase=0;
                if (part.animation==battletanx::UnitModelAnimation::left_track) {
                    phase=static_cast<std::int8_t>(unit.track_phase_left>>4U);
                } else if (part.animation==battletanx::UnitModelAnimation::right_track) {
                    phase=static_cast<std::int8_t>(unit.track_phase_right>>4U);
                }
                draw.items.push_back({assets.set_base()+part.slot,transform,1U,phase});
            }
            draw.items.push_back({assets.set_base()+models->shadow_slot,body,5U,0});
        });
        for (const auto& turret:bridge.placed_world().snapshots()) {
            if (!turret.active || turret.destroyed || turret.kind!=battletanx::placed::Kind::turret ||
                turret.subtype>=turret_models.size()) continue;
            const auto ground=terrain.sample(turret.x,turret.z);
            const float y=ground ? ground->height : 0.0F;
            auto transform=battletanx::yaw_translation_matrix(
                turret.heading,turret.x,y,turret.z,angles);
            const float scale=turret.scale;
            for (std::size_t row=0U; row<3U; ++row) {
                for (std::size_t column=0U; column<3U; ++column) {
                    transform[row*4U+column]*=scale;
                }
            }
            draw.items.push_back({assets.set_base()+turret_models[turret.subtype],transform,3U,0});
        }
        session.projectiles().for_each(
            [&](battletanx::EntityId,const battletanx::WeaponProjectile& shell) {
            // Effect 34's visible gun round uses slot 0x24. Retail cannon and
            // spread rounds are primarily represented by their trail effects;
            // keep the existing cannon fallback until those persistent effects
            // are part of the native renderer. Effect 22 has its own confirmed
            // rotating missile model at slot 0x41.
            const std::optional<std::uint32_t> slot=shell.effect_kind==34U ?
                std::optional<std::uint32_t>{0x24U} :
                shell.effect_kind==22U ? std::optional<std::uint32_t>{0x41U} : std::nullopt;
            if (!slot) return;
            draw.items.push_back({assets.set_base()+*slot,
                battletanx::yaw_translation_matrix(
                    shell.heading,shell.x,shell.y,shell.z,angles),3U,0});
        });
        battletanx::effects::EffectRenderQueue effect_draws;
        bridge.effect_runtime().collect(effect_draws);
        for (const auto& effect:effect_draws.events()) {
            if (effect.model_slot>=battletanx::effects::model_slot_count) continue;
            const std::uint32_t set=assets.set_base()+effect.model_slot;
            if (set>=assets.sets().size() || assets.set(set).lod_count==0U) continue;
            const auto ground=terrain.sample(effect.x,effect.z);
            const float y=effect.y!=0.0F ? effect.y : (ground ? ground->height : 0.0F);
            const auto angle=static_cast<std::uint16_t>(
                game_tick*0x500U+static_cast<std::uint16_t>(effect.index)*0x800U);
            draw.items.push_back({set,
                battletanx::yaw_translation_matrix(angle,effect.x,y,effect.z,angles),3U,0});
        }
        const auto pickup_angle=static_cast<std::uint16_t>(game_tick*0x600U);
        for (const auto& pickup:session.pickups()) {
            if (!pickup.active || pickup.removed || pickup.kind>=pickup_models.size()) continue;
            const std::uint32_t set=assets.set_base()+pickup_models[pickup.kind];
            if (set>=assets.sets().size() || assets.set(set).lod_count==0U) continue;
            const auto pickup_ground=terrain.sample(pickup.x,pickup.z);
            const float pickup_y=pickup_ground ? pickup_ground->height : 0.0F;
            draw.items.push_back({set,
                battletanx::yaw_translation_matrix(
                    pickup_angle,pickup.x,pickup_y,pickup.z,angles),3U,0});
        }
        for (const auto& queen:bridge.queen_views()) {
            if (queen.state==battletanx::queen_world::QueenState::carried ||
                queen.home_owner>=session.owners().size()) continue;
            const std::uint8_t team=session.owners()[queen.home_owner].team;
            if (team>3U) continue;
            const std::uint32_t slot=0x5FU-team;
            const std::uint32_t set=assets.set_base()+slot;
            if (set>=assets.sets().size() || assets.set(set).lod_count==0U) continue;
            const auto queen_ground=terrain.sample(queen.x,queen.z);
            const float queen_y=queen_ground ? queen_ground->height : 0.0F;
            draw.items.push_back({set,
                battletanx::yaw_translation_matrix(
                    pickup_angle,queen.x,queen_y,queen.z,angles),2U,0});
        }
        renderer.render(draw,assets);
        battletanx::HudState hud;
        hud.match_state=session.mission().state();
        hud.briefing_image=briefing_image ? &*briefing_image : nullptr;
        hud.briefing_text=briefing_page.text;
        hud.objective=battletanx::campaign_objective_text(
            static_cast<std::size_t>(wave.wave_index)).value_or(std::string_view{});
        hud.result=battletanx::campaign_result_text(
            static_cast<std::size_t>(wave.wave_index)).value_or(std::string_view{});
        std::string result_prompt;
        const auto* active_frontend=result_frontend ? &*result_frontend :
            pause_frontend ? &*pause_frontend : nullptr;
        if (active_frontend!=nullptr) {
            for (const auto& command:battletanx::frontend::frontend_draw(*active_frontend)) {
                using battletanx::frontend::ItemId;
                std::string_view label;
                switch (command.item) {
                case ItemId::pause_continue: label="CONTINUE"; break;
                case ItemId::pause_restart: label="RESTART"; break;
                case ItemId::pause_quit: label="QUIT"; break;
                case ItemId::result_retry: label="A RETRY"; break;
                case ItemId::result_quit: label="B QUIT"; break;
                case ItemId::result_continue: label="A CONTINUE"; break;
                case ItemId::result_save_continue: label="B SAVE AND CONTINUE"; break;
                default: break;
                }
                if (label.empty()) continue;
                if (!result_prompt.empty()) result_prompt+="   ";
                if (command.selected) result_prompt+="> ";
                result_prompt+=label;
            }
        }
        hud.result_prompt=result_prompt;
        if (wave.player_unit_from_selection) hud.lineup=selected_roster_summary;
        hud.frames_remaining=session.mission().frames_remaining();
        hud.score=session.owners()[0].score;
        session.units().for_each([&](battletanx::EntityId id,
                                     const battletanx::UnitRecord& unit) {
            if (!unit.alive) return;
            hud.markers.push_back({unit.x,unit.z,unit.heading_degrees,unit.team,
                                   active_player && id==*active_player});
        });
        if (active_player) {
            const auto* player=session.units().get(*active_player);
            if (player!=nullptr) {
                hud.health=player->health;
                hud.max_health=player->max_health;
                hud.primary_ammo=player->ammo[player->primary_weapon.kind];
            }
        }
        if (result_frontend) {
            hud.match_state=battletanx::MatchState::result_banner;
            if (result_frontend->result==battletanx::frontend::ResultKind::victory)
                hud.health=std::max(hud.health,1);
            else
                hud.health=0;
        }
        battletanx::draw_player_hud(renderer.overlay_pixels(),
            battletanx::native_width,battletanx::native_height,hud_assets,hud);
        if (!verified_native_frame) {
            const auto pixels=renderer.pixels();
            const auto visible=static_cast<std::size_t>(std::count_if(
                pixels.begin(),pixels.end(),[](std::uint32_t pixel) {
                    return pixel!=0x00101820U;
                }));
            if (visible<100U) throw std::runtime_error("native model frame is empty");
            std::cout << "Rendered native model frame: " << visible
                      << " visible pixels\n";
            verified_native_frame=true;
        }
        platform.present(renderer.pixels(),battletanx::native_width,battletanx::native_height);
        if (smoke_ticks!=0U && game_tick>=smoke_ticks) {
            const auto id=session.owner_unit(0U);
            const auto* player=id ? session.units().get(*id) : nullptr;
            if (player==nullptr || session.mission().state()!=battletanx::MatchState::live)
                throw std::runtime_error("native smoke run did not reach live gameplay");
            if (player->x==initial_player_x && player->z==initial_player_z)
                throw std::runtime_error("native smoke run did not move the player unit");
            if (player->ammo[player->primary_weapon.kind]>=initial_primary_ammo)
                throw std::runtime_error("native smoke run did not fire the primary weapon");
            std::cout << "Native smoke run passed: " << game_tick << " ticks, checksum "
                      << session.checksum() << '\n';
            return WaveRunResult::exit;
        }
        const auto next_tick_nanoseconds=static_cast<std::int64_t>(
            ((game_tick+1U)*UINT64_C(1000000000)+ticks_per_second-1U)/ticks_per_second);
        if (smoke_ticks==0U) {
            std::this_thread::sleep_until(
                clock_start+std::chrono::nanoseconds(next_tick_nanoseconds));
        }
    }
    return WaveRunResult::exit;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc>0 && argv!=nullptr && argv[0]!=nullptr)
            battletanx::configure_user_data_directory(argv[0]);
        const auto data_path=battletanx::user_data_directory();
        std::filesystem::create_directories(data_path);
        const auto settings_path=data_path/"settings.ini";
        auto settings=battletanx::PcSettings::load(settings_path);
        if (!std::filesystem::exists(settings_path)) settings.save(settings_path);

        bool show_window=true;
        std::uint64_t smoke_ticks=0U;
        std::filesystem::path rom_path;
        if (argc==1 && !settings.last_rom_path.empty()) {
            rom_path=battletanx::path_from_utf8(settings.last_rom_path);
        } else if (argc==2) {
            rom_path=argv[1];
        } else if (argc==3 && (std::string_view(argv[1])=="--window" ||
                              std::string_view(argv[1])=="--validate" ||
                              std::string_view(argv[1])=="--smoke")) {
            show_window=std::string_view(argv[1])!="--validate";
            if (std::string_view(argv[1])=="--smoke") smoke_ticks=240U;
            rom_path=argv[2];
        } else {
            std::cerr << "usage: BattleTanx [--validate|--window|--smoke] /path/to/BattleTanx.z64\n";
            return 2;
        }
        if (rom_path.empty()) {
            std::cerr << "usage: BattleTanx [--validate|--window|--smoke] /path/to/BattleTanx.z64\n";
            return 2;
        }
        const auto pak_path=battletanx::Platform::controller_pak_path();
        const auto initial_pak=probe_controller_pak(pak_path);
        auto campaign=initial_pak.record.value_or(battletanx::default_campaign_record());
        if (initial_pak.record) {
            std::cout << "Mounted verified Controller Pak image at "
                      << pak_path.string() << '\n';
        }
        const auto rom=battletanx::Rom::load_verified(rom_path);
        settings.remember_rom(rom_path);
        settings.save(settings_path);
        std::cout << "Verified BattleTanx " << rom.header().game_code
                  << " Rev " << static_cast<unsigned>(rom.header().revision)
                  << " (" << rom.header().title << ")\n";
        std::size_t decoded_bytes=0U;
        std::size_t decoded_entries=0U;
        for (const auto& descriptor : battletanx::resource_descriptors()) {
            const auto decoded=battletanx::load_resource(rom,descriptor);
            const auto bundle=battletanx::Bundle::parse(decoded);
            decoded_bytes+=decoded.size();
            decoded_entries+=bundle.entries().size();
        }
        std::cout << "Validated " << battletanx::resource_descriptors().size()
                  << " retail resource bundles: " << decoded_bytes
                  << " decoded bytes, " << decoded_entries << " entries\n";
        constexpr std::size_t angle_table_offset=0xC9620U;
        constexpr std::size_t angle_table_size=battletanx::AngleTable::value_count*4U;
        const auto angles=battletanx::AngleTable::from_big_endian(
            rom.bytes().subspan(angle_table_offset,angle_table_size));
        auto bindings=battletanx::load_default_input_bindings(rom);
        if (std::abs(angles.sine(0x4000U)-1.0F)>0.0001F ||
            std::abs(angles.cosine(0U)-1.0F)>0.0001F) {
            throw std::runtime_error("retail angle table failed cardinal-direction validation");
        }
        std::cout << "Loaded retail 16-bit angle table: "
                  << battletanx::AngleTable::value_count << " samples\n";
        const auto sounds=battletanx::load_sound_effect_bank(rom);
        const auto pickup_models=load_pickup_model_slots(rom);
        const auto turret_models=load_turret_model_slots(rom);
        const auto roster_table=rom.bytes().subspan(
            battletanx::controller_roster_rom_offset,
            battletanx::controller_roster_count*battletanx::controller_roster_size);
        auto platform=show_window ?
            battletanx::Platform::create("BattleTanx native source port",settings) : nullptr;
        if (platform && smoke_ticks==0U) {
            const auto selected=run_campaign_frontend(
                *platform,battletanx::load_hud_assets(rom),sounds,pak_path,initial_pak);
            if (!selected) return 0;
            campaign=*selected;
        }
        const bool saved_bindings=std::any_of(
            campaign.bindings.begin(),campaign.bindings.end(),
            [](std::uint32_t word) { return word!=0U; });
        if (saved_bindings) bindings.words=campaign.bindings;
        else campaign.bindings=bindings.words;

        battletanx::campaign_flow::CampaignFlow campaign_flow;
        const auto* wave=resume_campaign(campaign_flow,campaign);
        battletanx::campaign_save::SaveTransaction save_transaction(campaign);
        for (;;) {
            auto hud_assets=battletanx::load_hud_assets(rom);
            if (wave->world_stream!=1U) hud_assets.level_map.reset();
            std::optional<battletanx::RgbaImage> briefing_image;
            if (wave->briefing_image>=0) briefing_image=battletanx::load_campaign_briefing_image(
                rom,static_cast<std::size_t>(wave->briefing_image));
            const auto briefing_page=battletanx::load_campaign_briefing_page(
                rom,static_cast<std::size_t>(wave->wave_index));
            std::optional<battletanx::WorldLevel> active_world;
            std::optional<battletanx::NavigationLevel> active_navigation;
            std::optional<std::vector<std::uint8_t>> active_world_stream;
            std::size_t world_groups=0U;
            std::size_t world_items=0U;
            std::size_t navigation_triangles=0U;
            battletanx::ModelGeometryAudit model_audit{};
            for (const auto& descriptor : battletanx::level_stream_descriptors()) {
                auto world=battletanx::load_world_level(rom,descriptor);
                const auto level_audit=battletanx::audit_model_geometry(rom,world);
                model_audit.part_references+=level_audit.part_references;
                model_audit.material_blobs+=level_audit.material_blobs;
                model_audit.geometry_blobs+=level_audit.geometry_blobs;
                model_audit.vertices+=level_audit.vertices;
                model_audit.triangles+=level_audit.triangles;
                world_groups+=world.groups.size();
                world_items+=world.items.size();
                if (descriptor.index==wave->world_stream) {
                    active_world=std::move(world);
                    active_world_stream=battletanx::load_world_stream(rom,descriptor);
                }
                if (descriptor.navigation) {
                    auto navigation=battletanx::load_navigation_level(rom,descriptor);
                    navigation_triangles+=navigation.triangles.size();
                    if (descriptor.index==wave->world_stream) active_navigation=std::move(navigation);
                }
            }
            std::cout << "Validated 22 world streams and 18 navigation streams: "
                      << world_groups << " groups, " << world_items << " placements, "
                      << navigation_triangles << " navigation triangles\n";
            std::cout << "Validated " << model_audit.geometry_blobs
                      << " level-local model geometry references: " << model_audit.vertices
                      << " vertices, " << model_audit.triangles << " triangles\n";
            if (!active_world || !active_navigation || !active_world_stream) {
                throw std::runtime_error("campaign wave level streams are unavailable");
            }
            const auto active_assets=battletanx::ModelAssets::load(rom,*active_world);
            std::optional<battletanx::MusicTrack> music;
            const auto music_track=battletanx::music_selection::music_track_for_level(wave->mission_id);
            if (show_window && music_track)
                music=battletanx::render_music_sequence(rom,*music_track);
            if (show_window && !music)
                throw std::runtime_error("campaign level has no native music mapping");
            for (const std::size_t slot:{0x0BU,0x0CU,0x0DU,0x0EU,0x24U,0x41U,0x52U}) {
                if (active_assets.set_base()+slot>=active_assets.sets().size() ||
                    active_assets.set(active_assets.set_base()+slot).lod_count==0U) {
                    throw std::runtime_error("campaign level native model slot is absent");
                }
            }
            std::cout << "Decoded campaign level " << wave->mission_id << " native model cache: "
                      << active_assets.meshes().size() << " meshes, "
                      << active_assets.materials().size() << " materials\n";
            std::cout << "Decoded " << sounds.sounds.size()
                      << " native sound effects at " << sounds.sample_rate << " Hz\n";
            if (music) {
                std::cout << "Rendered native music sequence " << music->sequence_index
                          << ": " << music->stereo_samples.size()/2U
                          << " stereo frames\n";
            }
            std::cout << "Native gameplay runtime is ready.\n";
            if (!show_window) break;
            const auto result=run_platform_diagnostic(
                    *platform,*active_world,*active_navigation,angles,bindings,active_assets,
                    sounds,*music,hud_assets,briefing_image,briefing_page,
                    pickup_models,turret_models,roster_table,*wave,
                    battletanx::parse_team_spawn_table(*active_world_stream),
                    campaign,campaign_flow,save_transaction,settings,pak_path,smoke_ticks);
            if (result==WaveRunResult::exit) break;
            if (result==WaveRunResult::title) {
                platform->set_music_track(*music,0.0F);
                const auto selected=run_campaign_frontend(
                    *platform,hud_assets,sounds,pak_path,probe_controller_pak(pak_path));
                if (!selected) break;
                campaign=*selected;
                const bool has_bindings=std::any_of(
                    campaign.bindings.begin(),campaign.bindings.end(),
                    [](std::uint32_t word) { return word!=0U; });
                if (has_bindings) bindings.words=campaign.bindings;
                else campaign.bindings=bindings.words;
                wave=resume_campaign(campaign_flow,campaign);
                save_transaction=battletanx::campaign_save::SaveTransaction(campaign);
                continue;
            }
            if (result==WaveRunResult::selection_previous ||
                result==WaveRunResult::selection_next) {
                campaign.unit_type=battletanx::cycle_player_bundle(
                    campaign.unit_type,
                    result==WaveRunResult::selection_previous ? -1 : 1);
                battletanx::write_controller_pak_record(
                    pak_path,battletanx::serialize_campaign_record(campaign));
                battletanx::campaign_flow::FlowCommand options;
                options.kind=battletanx::campaign_flow::FlowCommandKind::set_options;
                options.options=battletanx::campaign_flow::options_from_record(campaign);
                static_cast<void>(campaign_flow.apply(options));
                std::cout << "Selected campaign lineup " << campaign.unit_type << '\n';
                continue;
            }
            if (result==WaveRunResult::retry) {
                std::cout << "Restarting campaign wave "
                          << static_cast<unsigned>(wave->wave_index) << '\n';
                continue;
            }
            if (result==WaveRunResult::complete) {
                std::cout << "Campaign complete. Final progress remains on the Controller Pak.\n";
                break;
            }
            launch_flow_battle(campaign_flow);
            wave=campaign_flow.current_wave();
            if (wave==nullptr) throw std::runtime_error("campaign flow lost the next wave");
            std::cout << "Loading campaign wave " << static_cast<unsigned>(wave->wave_index)
                      << " from campaign progress\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

