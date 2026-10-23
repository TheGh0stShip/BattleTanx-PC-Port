#include "battletanx/angle.hpp"
#include "battletanx/audio.hpp"
#include "battletanx/battle_session.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/camera.hpp"
#include "battletanx/campaign_catalog.hpp"
#include "battletanx/campaign_record.hpp"
#include "battletanx/campaign_spawn.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/fixed_tick.hpp"
#include "battletanx/hud.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/level.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/model.hpp"
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
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
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

enum class WaveRunResult { quit, retry, advance };

WaveRunResult run_platform_diagnostic(const battletanx::WorldLevel& world,
                             const battletanx::NavigationLevel& navigation,
                             const battletanx::AngleTable& angles,
                             const battletanx::InputBindings& bindings,
                             const battletanx::ModelAssets& assets,
                             const battletanx::SoundBank& sounds,
                             const battletanx::MusicTrack& music,
                             const battletanx::HudAssets& hud_assets,
                             const std::array<std::uint32_t,pickup_model_kind_count>& pickup_models,
                             const std::array<std::uint32_t,battletanx::turret_types>& turret_models,
                             std::span<const std::uint8_t> roster_table,
                             const battletanx::CampaignWaveDefinition& wave,
                             std::optional<battletanx::TeamSpawnTable> team_spawns,
                             battletanx::CampaignRecord campaign,
                             const battletanx::PcSettings& settings,
                             const std::filesystem::path& pak_path,
                             std::uint64_t smoke_ticks=0U) {
    auto platform=battletanx::Platform::create("BattleTanx native source port",settings);
    platform->set_sound_bank(sounds);
    platform->set_music_track(music,settings.master_volume*settings.music_volume*
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
    battletanx::BattleSession session(
        session_config,angles,battletanx::level_unit_motion(world,navigation,angles),
        battletanx::level_world_collision(world,navigation,angles),
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
            scale,scale,wave.game_kind==3 || wave.game_kind==6,
            battletanx::owner_difficulty_from_profile(profile),
            owner==0U ? launch.player_score : wave.owners[owner].initial_score});
        session.set_spawner_budget(
            static_cast<std::uint8_t>(owner),spawn_plan->owners[owner].reserve);
        if (!spawn_plan->respawn_points[owner].points.empty()) {
            const auto& point=spawn_plan->respawn_points[owner].points.front();
            session.set_respawn_point(static_cast<std::uint8_t>(owner),
                                      {point.x,point.z,point.heading});
        }
    }
    if (wave.game_kind==3 || wave.game_kind==4 || wave.game_kind==6) {
        for (const auto& item:world.items) {
            if (item.type<11 || item.type>14) continue;
            const std::size_t team_slot=static_cast<std::size_t>(item.type-11);
            const std::uint8_t owner=spawn_plan->slot_rank[team_slot];
            if (!session.add_queen_base(owner,item.x,item.z)) continue;
        }
        if ((wave.game_kind==3 || wave.game_kind==6) && session.queens().empty()) {
            throw std::runtime_error("campaign queen objective has no base placements");
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
        if (!session.add_spawner(planned.definition)) {
            throw std::runtime_error("failed to register a planned campaign spawner");
        }
    }
    for (const auto& placement:spawn_plan->owned_objects) {
        if (!session.add_turret(placement)) {
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
    bool progress_saved=false;
    while (platform->pump(current)) {
        const auto pad=battletanx::translate_player_input(current[0],previous[0]);
        auto input=battletanx::translate_drive_input(pad,bindings);
        battletanx::SessionInput session_input;
        session_input.drive=input;
        session_input.start_pressed=(pad.pressed&0x1000U)!=0U;
        session_input.advance=(pad.pressed&(0x8000U|0x1000U))!=0U;
        if (smoke_ticks!=0U) {
            session_input.advance=game_tick==0U;
            if (game_tick>=130U) {
                session_input.drive.throttle=80.0F;
                session_input.drive.rotation=game_tick<180U ? 18.0F : -12.0F;
                session_input.drive.fire=true;
                session_input.drive.fire_pressed=game_tick==130U;
            }
        }
        const bool advance_requested=session_input.advance;
        previous=current;
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-clock_start).count();
        const auto target_tick=smoke_ticks!=0U ? game_tick+1U : battletanx::fixed_rate_tick(
            static_cast<std::uint64_t>(elapsed),ticks_per_second);
        while (game_tick<target_tick) {
            session.step(session_input);
            for (const auto& event:session.gameplay_events()) {
                if (event.kind==battletanx::GameplayEventKind::unit_damaged && event.owner==0U) {
                    platform->rumble(0U,std::clamp(static_cast<float>(event.value)/40.0F,0.2F,1.0F),250U);
                }
                if (event.kind==battletanx::GameplayEventKind::unit_destroyed && event.owner==0U)
                    platform->rumble(0U,1.0F,500U);
                const bool cleared=event.kind==battletanx::GameplayEventKind::mission_succeeded;
                const bool endurance_finished=
                    event.kind==battletanx::GameplayEventKind::mission_failed &&
                    wave.advances_on_failure;
                if ((cleared || endurance_finished) && !progress_saved) {
                    battletanx::CampaignWaveClear clear;
                    const auto* next=battletanx::next_campaign_wave(wave);
                    clear.mission_id=next!=nullptr ? static_cast<std::uint8_t>(next->mission_id) : 0U;
                    clear.carry=session.owners()[0].score;
                    clear.unit_type=campaign.unit_type;
                    clear.bonus=campaign.bonus;
                    clear.control_scheme=campaign.control_scheme;
                    clear.difficulty=campaign.difficulty;
                    clear.powerups=campaign.powerups.raw;
                    clear.powerup_regen=campaign.powerup_regen.raw;
                    clear.base_defense=campaign.base_defense.raw;
                    clear.unlimited_ammo=campaign.unlimited_ammo.raw;
                    clear.sfx_volume=campaign.sfx_volume;
                    clear.music_volume=campaign.music_volume;
                    clear.bindings=campaign.bindings;
                    battletanx::apply_campaign_wave_clear(campaign,clear);
                    battletanx::write_controller_pak_record(
                        pak_path,battletanx::serialize_campaign_record(campaign));
                    progress_saved=true;
                    std::cout << "Saved campaign progress to the Controller Pak\n";
                }
            }
            const auto listener_id=session.owner_unit(0U);
            const auto* listener=listener_id ? session.units().get(*listener_id) : nullptr;
            for (const auto& sound:session.sound_events()) {
                const float pan=listener!=nullptr && sound.positional ?
                    std::clamp((sound.x-listener->x)/2000.0F,-1.0F,1.0F) : 0.0F;
                platform->play_sound(sound.sound_id,pan,
                    settings.master_volume*settings.effects_volume*
                    static_cast<float>(campaign.sfx_volume)/7.0F);
            }
            session.clear_events();
            ++game_tick;
            session_input.drive.fire_pressed=false;
            session_input.drive.special_pressed=false;
            session_input.start_pressed=false;
            session_input.advance=false;
        }
        const auto active_player=session.owner_unit(0U);
        if (active_player) {
            const auto* player=session.units().get(*active_player);
            if (player==nullptr) throw std::runtime_error("the player tank handle became stale");
            battletanx::sync_player_tank_from_unit(tank,*player);
            const auto vehicle_audio=battletanx::vehicle_sound_mix(
                player->velocity_x,player->velocity_z,
                settings.master_volume*settings.effects_volume*
                static_cast<float>(campaign.sfx_volume)/7.0F);
            platform->set_looping_sound(0U,0x10U,0.0F,
                                        vehicle_audio.engine_gain,vehicle_audio.engine_pitch);
            if (vehicle_audio.skid_gain>0.0F) {
                platform->set_looping_sound(1U,0x12U,0.0F,
                                            vehicle_audio.skid_gain,1.0F);
            } else {
                platform->stop_looping_sound(1U);
            }
        } else {
            platform->stop_looping_sound(0U);
            platform->stop_looping_sound(1U);
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
        for (const auto& turret:session.turrets()) {
            if (turret.removed || (turret.flags&battletanx::turret_rebuilding)!=0U ||
                turret.type>=turret_models.size()) continue;
            const auto ground=terrain.sample(turret.x,turret.z);
            const float y=ground ? ground->height : 0.0F;
            auto transform=battletanx::yaw_translation_matrix(
                turret.heading,turret.x,y,turret.z,angles);
            const float scale=battletanx::turret_scale[turret.type];
            for (std::size_t row=0U; row<3U; ++row) {
                for (std::size_t column=0U; column<3U; ++column) {
                    transform[row*4U+column]*=scale;
                }
            }
            draw.items.push_back({assets.set_base()+turret_models[turret.type],transform,3U,0});
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
        for (const auto& queen:session.queens()) {
            if (queen.state==battletanx::QueenState::carried ||
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
        platform->present(renderer.pixels(),battletanx::native_width,battletanx::native_height);
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
            return WaveRunResult::quit;
        }
        if (advance_requested && session.mission().state()==battletanx::MatchState::round_over) {
            return progress_saved ? WaveRunResult::advance : WaveRunResult::retry;
        }
        const auto next_tick_nanoseconds=static_cast<std::int64_t>(
            ((game_tick+1U)*UINT64_C(1000000000)+ticks_per_second-1U)/ticks_per_second);
        if (smoke_ticks==0U) {
            std::this_thread::sleep_until(
                clock_start+std::chrono::nanoseconds(next_tick_nanoseconds));
        }
    }
    return WaveRunResult::quit;
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
        auto pak=battletanx::load_controller_pak_record(pak_path);
        if (!pak) {
            battletanx::write_controller_pak_record(
                pak_path,battletanx::serialize_campaign_record(
                    battletanx::default_campaign_record()));
            pak=battletanx::load_controller_pak_record(pak_path);
        }
        if (!pak) throw std::runtime_error("failed to mount virtual Controller Pak");
        auto campaign=battletanx::parse_campaign_record(*pak);
        if (!campaign) throw std::runtime_error("Controller Pak campaign record is invalid");
        const auto* wave=battletanx::campaign_wave_for_record(*campaign);
        if (wave==nullptr) throw std::runtime_error("Controller Pak campaign is complete or has an unknown mission");
        std::cout << "Mounted verified Controller Pak image at "
                  << pak_path.string() << '\n';
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
        const bool saved_bindings=std::any_of(
            campaign->bindings.begin(),campaign->bindings.end(),
            [](std::uint32_t word) { return word!=0U; });
        if (saved_bindings) {
            bindings.words=campaign->bindings;
        } else {
            campaign->bindings=bindings.words;
            battletanx::write_controller_pak_record(
                pak_path,battletanx::serialize_campaign_record(*campaign));
        }
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
        for (;;) {
            auto hud_assets=battletanx::load_hud_assets(rom);
            if (wave->world_stream!=1U) hud_assets.level_map.reset();
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
            if (show_window) music=battletanx::render_music_sequence(
                rom,static_cast<std::uint32_t>(wave->mission_id));
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
                    *active_world,*active_navigation,angles,bindings,active_assets,
                    sounds,*music,hud_assets,pickup_models,turret_models,roster_table,*wave,
                    battletanx::parse_team_spawn_table(*active_world_stream),
                    *campaign,settings,pak_path,smoke_ticks);
            if (result==WaveRunResult::quit) break;
            if (result==WaveRunResult::retry) {
                std::cout << "Restarting campaign wave "
                          << static_cast<unsigned>(wave->wave_index) << '\n';
                continue;
            }
            pak=battletanx::load_controller_pak_record(pak_path);
            if (!pak) throw std::runtime_error("failed to reload the Controller Pak after wave clear");
            campaign=battletanx::parse_campaign_record(*pak);
            if (!campaign) throw std::runtime_error("saved Controller Pak campaign record is invalid");
            wave=battletanx::campaign_wave_for_record(*campaign);
            if (wave==nullptr) {
                std::cout << "Campaign complete. Final progress is saved to the Controller Pak.\n";
                break;
            }
            std::cout << "Loading campaign wave " << static_cast<unsigned>(wave->wave_index)
                      << " from saved progress\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

