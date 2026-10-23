#include "battletanx/battle_session.hpp"

#include <memory>
#include <stdexcept>

namespace battletanx {
namespace {

PlayerTank to_player_tank(const UnitRecord& unit) {
    PlayerTank tank;
    sync_player_tank_from_unit(tank,unit);
    return tank;
}

struct MotionData {
    WorldLevel world;
    NavigationLevel navigation;
    AngleTable angles;
};

UnitMotion motion_over(std::shared_ptr<const MotionData> data) {
    return [data](UnitRecord& unit,const DriveInput& input) {
        if (unit.navigation_triangle==0U) {
            unit.navigation_triangle=find_nearest_navigation_triangle(
                data->navigation,NavigationVertex{unit.x,unit.z});
            if (unit.navigation_triangle==0U) return;   // off the navigation mesh: stay put
        }
        PlayerTank tank=to_player_tank(unit);
        update_player_tank(tank,input,data->world,data->navigation,data->angles);
        const auto ammo=unit.ammo[retail::tank_primary_weapon];
        const auto last=unit.primary_weapon.last_fire_tick;
        sync_unit_from_player_tank(unit,tank);
        unit.ammo[retail::tank_primary_weapon]=ammo;   // weapons are owned by the session
        unit.primary_weapon.last_fire_tick=last;
    };
}

NavigationLevel open_ground_navigation() {
    NavigationLevel navigation;
    navigation.vertices={{0.0F,0.0F},{-200000.0F,-200000.0F},{200000.0F,-200000.0F},{0.0F,400000.0F}};
    NavigationTriangle unused;
    NavigationTriangle ground;
    ground.corners={NavigationCorner{1U,0U},NavigationCorner{2U,0U},NavigationCorner{3U,0U}};
    navigation.triangles={unused,ground};
    if (!navigation_point_inside_triangle(navigation,NavigationVertex{0.0F,0.0F},1U)) {
        std::swap(navigation.triangles[1].corners[1],navigation.triangles[1].corners[2]);
    }
    if (!navigation_point_inside_triangle(navigation,NavigationVertex{0.0F,0.0F},1U)) {
        throw std::logic_error("open-ground navigation triangle does not enclose the origin");
    }
    return navigation;
}

void write_owner(StateWriter& writer,const OwnerRecord& owner) {
    writer.boolean(owner.present);
    writer.boolean(owner.computer);
    writer.u8(owner.team);
    writer.u8(owner.live_units);
    writer.u8(owner.squad_cap);
    writer.s16(owner.respawn_bank[0]);
    writer.s16(owner.respawn_bank[1]);
    writer.boolean(owner.respawn_enabled);
    writer.s32(owner.losses);
    writer.s32(owner.team_losses);
    writer.s32(owner.kills);
    writer.s32(owner.score);
    writer.f32(owner.armor_scale);
    writer.f32(owner.damage_scale);
}

void write_unit(StateWriter& writer,const UnitRecord& unit) {
    writer.u8(static_cast<std::uint8_t>(unit.kind));
    writer.u8(unit.owner);
    writer.u8(unit.team);
    writer.boolean(unit.alive);
    writer.boolean(unit.primary);
    writer.u8(static_cast<std::uint8_t>(unit.control));
    writer.u16(unit.flags);
    writer.f32(unit.x);
    writer.f32(unit.y);
    writer.f32(unit.z);
    writer.f32(unit.heading_degrees);
    writer.f32(unit.turret_degrees);
    writer.f32(unit.velocity_x);
    writer.f32(unit.velocity_z);
    writer.u8(static_cast<std::uint8_t>(unit.track_left));
    writer.u8(static_cast<std::uint8_t>(unit.track_right));
    writer.u8(unit.track_phase_left);
    writer.u8(unit.track_phase_right);
    writer.s32(unit.health);
    writer.s32(unit.max_health);
    writer.s32(unit.shield_strength);
    writer.u8(unit.primary_weapon.kind);
    writer.s32(unit.primary_weapon.last_fire_tick);
    writer.u8(unit.secondary_weapon.kind);
    writer.s32(unit.secondary_weapon.last_fire_tick);
    for (const std::int16_t ammo : unit.ammo) writer.s16(ammo);
    writer.s32(unit.spawn_tick);
    writer.s32(unit.last_hit_tick);
    writer.f32(unit.hit_shake);
    writer.id(unit.target);
    writer.u16(unit.navigation_triangle);
}

void write_shell(StateWriter& writer,const ShellRecord& shell) {
    writer.f32(shell.x);
    writer.f32(shell.y);
    writer.f32(shell.z);
    writer.f32(shell.velocity_x);
    writer.f32(shell.velocity_z);
    writer.f32(shell.speed);
    writer.f32(shell.travelled);
    writer.f32(shell.range);
    writer.u16(shell.heading);
    writer.s16(shell.life);
    writer.u8(shell.damage);
    writer.u8(shell.attacker);
    writer.boolean(shell.local);
    writer.id(shell.ignore);
}

} // namespace

UnitMotion level_unit_motion(const WorldLevel& world,const NavigationLevel& navigation,
                             const AngleTable& angles) {
    return motion_over(std::make_shared<const MotionData>(MotionData{world,navigation,angles}));
}

UnitMotion open_ground_unit_motion(const AngleTable& angles) {
    return motion_over(std::make_shared<const MotionData>(
        MotionData{WorldLevel{},open_ground_navigation(),angles}));
}

RoutePlanner level_route_planner(const NavigationLevel& navigation) {
    auto data=std::make_shared<const NavigationLevel>(navigation);
    return [data](const RouteRequest& request) {
        RouteResponse response;
        response.unit=request.unit;
        response.serial=request.serial;
        const NavigationVertex start_point{request.from_x,request.from_z};
        const NavigationVertex goal_point{request.to_x,request.to_z};
        const std::uint16_t start_triangle=find_nearest_navigation_triangle(*data,start_point);
        const std::uint16_t goal_triangle=find_nearest_navigation_triangle(*data,goal_point);
        if (start_triangle==0U || goal_triangle==0U) return response;
        const NavigationRoute route=plan_navigation_route(
            *data,RouteEndpoint{start_point,start_triangle},RouteEndpoint{goal_point,goal_triangle},
            retail_route_blocked_mask(*data,start_triangle,goal_triangle));
        if (route.stop!=NavigationRouteStop::reached) return response;
        response.reachable=true;
        NavigationVertex previous=start_point;
        float distance=0.0F;
        for (std::size_t i=0U; i<route.count; ++i) {
            if (route.corners[i]==0U) continue;
            distance+=navigation_route_distance(previous,route.waypoints[i]);
            previous=route.waypoints[i];
        }
        distance+=navigation_route_distance(previous,goal_point);
        response.nav_distance=distance;
        response.nav_state=route.count<=1U ? std::uint8_t{1U} : std::uint8_t{0U};
        if (route.count>0U && route.corners[0]!=0U) {
            response.waypoint_x=route.waypoints[0].x;
            response.waypoint_z=route.waypoints[0].z;
        } else {
            response.waypoint_x=goal_point.x;
            response.waypoint_z=goal_point.z;
        }
        return response;
    };
}

BattleSession::BattleSession(SessionConfig config,const AngleTable& angles,UnitMotion motion,
                             WorldCollision world,RoutePlanner routes)
    : config_(config),angles_(angles),motion_(std::move(motion)),world_(std::move(world)),
      route_planner_(std::move(routes)),mission_(config.mission),
      campaign_ai_(config.campaign_ai),rng_(config.rng_seed) {
    config_.mission.external_objective=true;
    mission_=MissionState(config_.mission);
    config_.combat.game_mode=config_.mission.game_kind;
    config_.pickups.game_mode=static_cast<std::int32_t>(config_.mission.game_kind);
    pickups_.reserve(pickup_capacity);
    if (!motion_) throw std::invalid_argument("battle session requires a unit motion boundary");
}

void BattleSession::add_owner(std::uint8_t index,const OwnerSetup& setup) {
    if (index>=owner_capacity) throw std::out_of_range("owner index exceeds the four retail owner records");
    if (objective_) throw std::logic_error("owners cannot change after objective initialization");
    OwnerRecord& owner=owners_[index];
    owner=OwnerRecord{};
    owner.present=true;
    owner.computer=setup.computer;
    owner.team=setup.team;
    owner.squad_cap=setup.squad_cap;
    owner.respawn_bank[0]=setup.respawn_bank;
    owner.respawn_enabled=setup.respawn_enabled;
    owner.armor_scale=setup.armor_scale;
    owner.damage_scale=setup.damage_scale;
    objective_bases_[index]=setup.objective_base;
    if (setup.difficulty) campaign_ai_.set_owner_difficulty(index,*setup.difficulty);
}

std::optional<EntityId> BattleSession::spawn_unit(const UnitSpawn& spawn) {
    if (spawn.owner>=owner_capacity || !owners_[spawn.owner].present) return std::nullopt;
    OwnerRecord& owner=owners_[spawn.owner];
    const auto id=units_.allocate();
    if (!id) return std::nullopt;
    UnitRecord& unit=*units_.get(*id);
    unit=make_unit(spawn.kind,spawn.owner,owner.team,spawn.x,spawn.z,spawn.heading,tick_,
                   owner.computer ? UnitControl::computer : UnitControl::player);
    unit.flags=spawn.flags;
    unit.primary=!owner.computer && owner.live_units==0U;
    ++owner.live_units;
    GameplayEvent event;
    event.kind=GameplayEventKind::unit_spawned;
    event.tick=tick_;
    event.subject=*id;
    event.owner=spawn.owner;
    event.x=unit.x;
    event.z=unit.z;
    events_.push(event);
    return id;
}

bool BattleSession::add_pickup(const PickupRecord& pickup) {
    if (pickups_.size()>=pickup_capacity) return false;
    pickups_.push_back(pickup);
    return true;
}

void BattleSession::update_pickup_state() {
    std::array<PickupCollector,unit_capacity> collectors{};
    std::size_t count=0U;
    units_.for_each([&](EntityId id,UnitRecord& unit) {
        if (!unit.alive) return;
        const std::uint8_t owner_class=unit.owner<owner_capacity ? owners_[unit.owner].team : unit.team;
        collectors[count++]=PickupCollector{id,&unit,owner_class,
                                            static_cast<std::uint8_t>(unit.control==UnitControl::computer ? 2U : 0U)};
    });
    collect_pickups(pickups_,std::span<PickupCollector>(collectors.data(),count),tick_,config_.pickups,
                    events_,sounds_);
    update_pickups(pickups_,tick_,config_.pickups,rng_,pickup_weights_ ? &*pickup_weights_ : nullptr);
}

void BattleSession::set_respawn_point(std::uint8_t owner,RespawnPoint point) {
    if (owner>=owner_capacity) throw std::out_of_range("respawn owner index");
    respawn_points_[owner]=point;
}

DamageResult BattleSession::apply_damage(EntityId unit,std::int32_t amount,std::uint8_t attacker,
                                         bool direct) {
    CombatContext combat=combat_context();
    return apply_unit_damage(combat,unit,amount,attacker,direct);
}

ObjectiveEventStatus BattleSession::queen_taken(std::uint8_t holder_team,
                                                std::uint8_t collector_team) {
    initialize_objective();
    return objective_->on_queen_taken(holder_team,collector_team);
}

ObjectiveEventStatus BattleSession::queen_stored(std::uint8_t base_team) {
    initialize_objective();
    return objective_->on_queen_stored(base_team);
}

ObjectiveEventStatus BattleSession::queen_contact(std::uint8_t owner) {
    initialize_objective();
    return objective_->on_queen_contact(owner);
}

ObjectiveEventStatus BattleSession::marker_contact(std::uint8_t owner) {
    initialize_objective();
    return objective_->on_marker_contact(owner);
}

ObjectiveEventStatus BattleSession::self_destruct(std::uint8_t owner) {
    initialize_objective();
    return objective_->on_self_destruct(owner);
}

std::optional<EntityId> BattleSession::owner_unit(std::uint8_t owner) const {
    std::optional<EntityId> found;
    units_.for_each([&](EntityId id,const UnitRecord& unit) {
        if (!found && unit.owner==owner && unit.alive) found=id;
    });
    return found;
}

CombatContext BattleSession::combat_context() {
    return CombatContext{units_,owners_,events_,sounds_,config_.combat,tick_};
}

void BattleSession::release_destroyed_units() {
    units_.for_each([&](EntityId id,UnitRecord& unit) {
        if (!unit.alive) units_.release(id);
    });
}

void BattleSession::update_units(const SessionInput& input) {
    std::array<CampaignAiCommand,unit_capacity> campaign_commands{};
    std::array<RouteRequest,unit_capacity> route_requests{};
    CampaignAiFrame frame;
    frame.tick=tick_;
    frame.route_budget=true;
    const CampaignAiOutput ai_output=campaign_ai_.update(
        units_,owners_,angles_,rng_,frame,campaign_commands,route_requests);
    if (route_planner_) {
        for (std::size_t i=0U; i<ai_output.routes; ++i) {
            (void)campaign_ai_.apply_route_response(route_planner_(route_requests[i]));
        }
    }

    CombatContext combat=combat_context();
    ProjectileContext projectiles{combat,shells_,world_,angles_};
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const EntityId id=units_.slot_id(slot);
        UnitRecord* unit=units_.get(id);
        if (unit==nullptr || !unit->alive) continue;
        DriveInput drive{};
        if (unit->control==UnitControl::player && unit->owner==0U) {
            drive=input.drive;
        } else if (unit->control==UnitControl::computer) {
            for (std::size_t i=0U; i<ai_output.commands; ++i) {
                if (campaign_commands[i].command.unit==id) {
                    drive=campaign_commands[i].command.input;
                    unit->target=campaign_commands[i].command.target;
                    if (campaign_commands[i].self_destruct) {
                        unit->flags|=retail::flag_instant_death;
                        (void)apply_unit_damage(combat,id,1U,unit->owner,false);
                        (void)self_destruct(unit->owner);
                    }
                }
            }
        }
        if (!unit->alive) continue;
        motion_(*unit,drive);
        if (drive.fire) {
            fire_unit_weapon(projectiles,id,unit->primary_weapon,
                             FireRequest{true,drive.fire_pressed});
        }
    }
}

void BattleSession::spawn_due_respawns() {
    for (const RespawnRequest& request : mission_.respawns()) {
        const auto& point=respawn_points_[request.owner];
        if (!point) continue;
        const auto id=spawn_unit(UnitSpawn{retail::UnitKind::tank,request.owner,point->x,point->z,
                                           point->heading,0U});
        if (id && !owners_[request.owner].computer) {
            GameplayEvent event;
            event.kind=GameplayEventKind::player_respawned;
            event.tick=tick_;
            event.subject=*id;
            event.owner=request.owner;
            event.x=point->x;
            event.z=point->z;
            events_.push(event);
        }
    }
    mission_.clear_respawns();
}

void BattleSession::initialize_objective() {
    if (objective_) return;
    ObjectiveConfig objective_config;
    objective_config.game_kind=config_.mission.game_kind;
    objective_config.result_frames=config_.mission.result_frames;
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        objective_config.owners[i]=ObjectiveOwner{
            owners_[i].present,owners_[i].computer,owners_[i].team,
            static_cast<std::uint8_t>(objective_bases_[i] ? 1U : 0U)};
    }
    ObjectiveCreateResult created=ObjectiveRuntime::create(objective_config);
    if (!created.runtime) {
        throw std::runtime_error("invalid battle objective: "+
                                 std::string(to_string(created.error)));
    }
    objective_=std::move(*created.runtime);
}

void BattleSession::update_objective() {
    initialize_objective();
    const auto emitted=events_.events();
    if (objective_event_cursor_>emitted.size()) objective_event_cursor_=0U;
    for (std::size_t i=objective_event_cursor_; i<emitted.size(); ++i) {
        (void)objective_->on_event(emitted[i]);
    }
    objective_event_cursor_=emitted.size();
    ObjectiveInput input;
    input.match=mission_.state();
    input.tick=tick_;
    for (std::size_t i=0U; i<owner_capacity; ++i) input.squad_alive[i]=mission_.squad_alive(i);
    const ObjectiveResult result=objective_->step(input);
    std::array<OwnerResult,owner_capacity> owner_results{};
    for (std::size_t i=0U; i<owner_capacity; ++i) owner_results[i]=objective_->result(i);
    mission_.apply_external_objective(result.next_state,owner_results,tick_,events_);
}

void BattleSession::step(const SessionInput& input) {
    if (mission_.simulation_running() && mission_.state()!=MatchState::paused) {
        release_destroyed_units();
        CombatContext combat=combat_context();
        ProjectileContext projectiles{combat,shells_,world_,angles_};
        update_shells(projectiles);
        update_units(input);
        update_pickup_state();
    }
    mission_.step(owners_,units_,tick_,input.start_pressed,input.advance,events_);
    update_objective();
    spawn_due_respawns();
    if (mission_.state()!=MatchState::paused) ++tick_;
}

void BattleSession::serialize(StateWriter& writer) const {
    writer.s32(tick_);
    writer.u32(rng_.seed());
    writer.u8(static_cast<std::uint8_t>(mission_.state()));
    writer.s32(mission_.frames_remaining());
    writer.s32(mission_.live_tick());
    writer.s32(mission_.elapsed_ticks());
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        writer.u8(static_cast<std::uint8_t>(mission_.result(i)));
        writer.s32(mission_.respawn_due_tick(i));
        write_owner(writer,owners_[i]);
    }
    if (objective_) {
        writer.boolean(true);
        objective_->serialize(writer);
    } else {
        writer.boolean(false);
    }
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        writer.boolean(units_.slot_occupied(slot));
        writer.u16(units_.slot_generation(slot));
        if (const UnitRecord* unit=units_.get(units_.slot_id(slot))) write_unit(writer,*unit);
    }
    campaign_ai_.serialize(writer);
    writer.u32(static_cast<std::uint32_t>(pickups_.size()));
    for (const PickupRecord& pickup : pickups_) {
        writer.id(pickup.id);
        writer.u8(pickup.kind);
        writer.f32(pickup.x);
        writer.f32(pickup.z);
        writer.boolean(pickup.active);
        writer.boolean(pickup.consumed);
        writer.s32(pickup.respawn_tick);
        writer.s32(pickup.expire_tick);
        writer.s32(pickup.owner_class);
        writer.boolean(pickup.removed);
    }
    for (std::size_t slot=0U; slot<shell_capacity; ++slot) {
        writer.boolean(shells_.slot_occupied(slot));
        writer.u16(shells_.slot_generation(slot));
        if (const ShellRecord* shell=shells_.get(shells_.slot_id(slot))) write_shell(writer,*shell);
    }
}

std::string BattleSession::checksum() const {
    StateWriter writer;
    serialize(writer);
    return writer.hex();
}

} // namespace battletanx
