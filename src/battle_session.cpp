#include "battletanx/battle_session.hpp"
#include "battletanx/geometry.hpp"
#include "battletanx/unit_kinds.hpp"

#include <algorithm>
#include <cmath>
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
        const auto definition=unit_kind_definition(static_cast<std::uint8_t>(unit.kind));
        if (!definition) return;
        const VehicleDriveParameters parameters{
            definition->movement.accel,definition->movement.inverse_mass,
            definition->movement.grip,definition->movement.drag,definition->footprint};
        update_player_tank(tank,input,data->world,data->navigation,data->angles,parameters);
        const auto ammo=unit.ammo;
        const auto primary=unit.primary_weapon;
        sync_unit_from_player_tank(unit,tank);
        unit.ammo=ammo;                         // weapons are owned by the session
        unit.primary_weapon=primary;
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
    spawners_.reserve(spawner_capacity);
    turrets_.reserve(turret_capacity);
    for (TurretOwnerConfig& owner : turret_owner_config_) {
        owner.effect_kind.fill(retail::shell_damage);
        owner.fire_delay.fill(30);
    }
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
    std::array<bool,5> occupied{};
    units_.for_each([&](EntityId,const UnitRecord& existing) {
        if (existing.alive && existing.owner==spawn.owner &&
            static_cast<std::size_t>(existing.squad_slot)<occupied.size()) {
            occupied[existing.squad_slot]=true;
        }
    });
    std::uint8_t squad_slot=spawn.squad_slot;
    if (squad_slot==0xFFU) {
        const auto free=std::find(occupied.begin(),occupied.end(),false);
        if (free==occupied.end()) return std::nullopt;
        squad_slot=static_cast<std::uint8_t>(free-occupied.begin());
    } else if (static_cast<std::size_t>(squad_slot)>=occupied.size() || occupied[squad_slot]) {
        return std::nullopt;
    }
    const auto id=units_.allocate();
    if (!id) return std::nullopt;
    UnitRecord& unit=*units_.get(*id);
    unit=make_unit(spawn.kind,spawn.owner,owner.team,spawn.x,spawn.z,spawn.heading,tick_,
                   owner.computer ? UnitControl::computer : UnitControl::player);
    unit.flags=spawn.flags;
    unit.squad_slot=squad_slot;
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

bool BattleSession::add_spawner(const SpawnerDefinition& definition) {
    if (spawners_.size()>=spawner_capacity || definition.owner>=owner_capacity) return false;
    SessionSpawner runtime;
    const std::int32_t serial=definition.linked ? 1 : 0;
    runtime.state=make_spawner_state(definition,serial);
    // The linked kind-5 object is not yet simulated. Keep its serial stable
    // so the spawner remains live; its future destruction can replace this view.
    if (definition.linked) runtime.linked=LinkedEffectView{serial,false};
    spawners_.push_back(runtime);
    return true;
}

void BattleSession::set_turret_owner_config(std::uint8_t owner,const TurretOwnerConfig& config) {
    if (owner>=owner_capacity) throw std::out_of_range("turret owner index");
    turret_owner_config_[owner]=config;
}

bool BattleSession::add_turret(const OwnedObjectPlacement& placement) {
    if (turrets_.size()>=turret_capacity || placement.owner>=owner_capacity ||
        placement.sub_kind>=turret_types || !owners_[placement.owner].present) return false;
    std::array<TurretTarget,unit_capacity> targets{};
    TurretContext context{angles_,targets,turret_owner_config_[placement.owner],0U,tick_,1.0F,
                          turret_search_range,rng_,{}, {}};
    auto turret=spawn_turret(placement.team,placement.owner,placement.sub_kind,placement.heading,
                             placement.x,placement.z,false,placement.flagged,context);
    if (!turret) return false;
    turrets_.push_back(*turret);
    return true;
}

bool BattleSession::add_queen_base(std::uint8_t owner,float x,float z) {
    if (owner>=owner_capacity || !owners_[owner].present || queen_bases_.size()>=owner_capacity) return false;
    if (std::any_of(queen_bases_.begin(),queen_bases_.end(),[owner](const QueenBase& base) {
            return base.owner==owner;
        })) return false;
    queen_bases_.push_back(QueenBase{owner,x,z});
    queens_.push_back(QueenRecord{QueenState::stored,owner,owner,no_entity,x,z,0});
    objective_bases_[owner]=true;
    return true;
}

void BattleSession::set_spawner_budget(std::uint8_t owner,std::int32_t budget) {
    if (owner>=owner_capacity) throw std::out_of_range("spawner owner index");
    spawner_budgets_[owner]=budget;
}

void BattleSession::set_spawner_gate(std::uint8_t owner,bool open) {
    if (owner>=owner_capacity) throw std::out_of_range("spawner owner index");
    spawner_gates_[owner]=open;
}

void BattleSession::update_spawners() {
    for (SessionSpawner& runtime : spawners_) {
        const std::uint8_t owner_index=runtime.state.definition.owner;
        if (owner_index>=owner_capacity || !owners_[owner_index].present) continue;
        SpawnerOwnerView owner;
        owner.budget=spawner_budgets_[owner_index];
        owner.gate_open=spawner_gates_[owner_index];
        units_.for_each([&](EntityId,const UnitRecord& unit) {
            if (unit.alive && unit.owner==owner_index &&
                static_cast<std::size_t>(unit.squad_slot)<owner.squad_slot_alive.size()) {
                owner.squad_slot_alive[unit.squad_slot]=true;
            }
        });
        const SpawnAreaClear area_clear=[&](const SpawnAreaQuery& query) {
            const auto definition=unit_kind_definition(query.shape);
            if (!definition) return false;
            const retail::Footprint& f=definition->footprint;
            const OrientedRect candidate{
                query.x,query.z,static_cast<float>(f.min_x),static_cast<float>(f.max_x),
                static_cast<float>(f.min_z),static_cast<float>(f.max_z),query.angle};
            bool clear=true;
            units_.for_each([&](EntityId,const UnitRecord& unit) {
                if (!clear || !unit.alive) return;
                const retail::Footprint& u=unit.footprint;
                const OrientedRect other{
                    unit.x,unit.z,static_cast<float>(u.min_x),static_cast<float>(u.max_x),
                    static_cast<float>(u.min_z),static_cast<float>(u.max_z),
                    degrees_to_retail_angle(unit.heading_degrees)};
                if (oriented_rects_overlap(candidate,other,angles_)) clear=false;
            });
            return clear;
        };
        const SpawnerUpdateResult result=update_spawner(
            runtime.state,runtime.linked,owner,tick_,rng_,area_clear);
        spawner_budgets_[owner_index]=owner.budget;
        if (!result.request) continue;
        const SpawnRequest& request=*result.request;
        const auto kind=static_cast<retail::UnitKind>(request.shape);
        const auto spawned=spawn_unit(UnitSpawn{
            kind,request.owner,request.x,request.z,request.heading,0U,request.squad_slot});
        if (!spawned && spawner_budgets_[owner_index]>=0) ++spawner_budgets_[owner_index];
    }
}

void BattleSession::update_turrets() {
    std::array<TurretTarget,unit_capacity> targets{};
    for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
        const UnitRecord* unit=units_.get(units_.slot_id(slot));
        targets[slot]=TurretTarget{unit,unit!=nullptr && unit->alive,
                                   unit!=nullptr && unit->control==UnitControl::computer};
    }
    CombatContext combat=combat_context();
    WeaponBaseContext base{combat,world_,angles_};
    WeaponContext weapons{base,weapon_state_,effects_,rng_};
    attach_turret_collision(weapons);
    for (std::size_t turret_index=0U; turret_index<turrets_.size(); ++turret_index) {
        TurretRecord& turret=turrets_[turret_index];
        if (turret.removed || turret.owner>=owner_capacity) continue;
        std::uint8_t count=0U;
        for (const TurretRecord& other : turrets_) {
            if (!other.removed && other.owner==turret.owner && count<4U) ++count;
        }
        const auto area_clear=[&](float x,float z,float half) {
            bool clear=true;
            units_.for_each([&](EntityId,const UnitRecord& unit) {
                if (unit.alive && std::abs(unit.x-x)<=half && std::abs(unit.z-z)<=half) clear=false;
            });
            return clear;
        };
        const auto line_of_sight=[&](float x,float z,std::uint8_t target) {
            if (target>=targets.size() || targets[target].unit==nullptr || !world_.segment) return true;
            const UnitRecord& unit=*targets[target].unit;
            return !world_.segment(x,z,unit.x,unit.z).has_value();
        };
        TurretContext context{angles_,targets,turret_owner_config_[turret.owner],count,tick_,1.0F,
                              turret_search_range,rng_,area_clear,line_of_sight};
        const TurretUpdateResult result=update_turret(turret,context);
        if (!result.shot) continue;
        (void)spawn_turret_projectile(
            weapons,*result.shot,turret.owner,static_cast<std::uint16_t>(turret_index));
        SoundEvent sound;
        sound.sound_id=result.shot->sound;
        sound.x=turret.x;
        sound.z=turret.z;
        sound.positional=true;
        sounds_.push(sound);
    }
}

void BattleSession::attach_turret_collision(WeaponContext& context) {
    context.external_hit=[this](float x0,float z0,float x1,float z1,std::uint16_t ignore) {
        std::optional<WeaponContext::ExternalHit> nearest;
        for (std::size_t index=0U; index<turrets_.size() && index<0xFFFFU; ++index) {
            const TurretRecord& turret=turrets_[index];
            if (index==ignore || turret.removed || turret.health<=0 || turret.type>=turret_types) continue;
            const float half=turret_scale[turret.type]*turret_half_extent;
            const retail::Footprint footprint{
                static_cast<std::int16_t>(-half),static_cast<std::int16_t>(half),
                static_cast<std::int16_t>(-half),static_cast<std::int16_t>(half)};
            const auto distance=segment_footprint_distance(
                x0,z0,x1,z1,turret.x,turret.z,angles_.sine(turret.heading),
                angles_.cosine(turret.heading),footprint);
            if (distance && (!nearest || *distance<nearest->distance)) {
                nearest=WeaponContext::ExternalHit{static_cast<std::uint16_t>(index),*distance};
            }
        }
        return nearest;
    };
    context.external_response=[this](std::uint16_t token,const ProjectileContact& contact) {
        if (token>=turrets_.size()) return ObjectResponse{HitResponse::pass_through,0U};
        TurretRecord& turret=turrets_[token];
        const TurretDamageResult result=damage_turret(
            turret,2U,contact.damage,contact.attacker,contact.angle,owners_,tick_,
            static_cast<std::int32_t>(config_.mission.game_kind));
        return ObjectResponse{result.handled ? HitResponse::impact : HitResponse::pass_through,0U};
    };
}

void BattleSession::update_queens() {
    constexpr float queen_contact_half=20.0F;
    constexpr float base_contact_half=195.0F;
    constexpr std::int32_t dropped_return_ticks=120;
    for (QueenRecord& queen : queens_) {
        if (queen.state==QueenState::carried) {
            UnitRecord* carrier=units_.get(queen.carrier);
            if (carrier==nullptr || !carrier->alive) {
                queen.state=QueenState::dropped;
                queen.carrier=no_entity;
                queen.dropped_tick=tick_;
                continue;
            }
            queen.x=carrier->x;
            queen.z=carrier->z;
            for (const QueenBase& base : queen_bases_) {
                if (base.owner>=owner_capacity || owners_[base.owner].team!=carrier->team) continue;
                if (std::fabs(carrier->x-base.x)>base_contact_half ||
                    std::fabs(carrier->z-base.z)>base_contact_half) continue;
                queen.state=QueenState::stored;
                queen.base_owner=base.owner;
                queen.carrier=no_entity;
                queen.x=base.x;
                queen.z=base.z;
                (void)queen_stored(owners_[base.owner].team);
                break;
            }
            continue;
        }
        if (queen.state==QueenState::dropped && tick_>queen.dropped_tick+dropped_return_ticks) {
            const auto home=std::find_if(queen_bases_.begin(),queen_bases_.end(),[&](const QueenBase& base) {
                return base.owner==queen.home_owner;
            });
            if (home!=queen_bases_.end()) {
                queen.state=QueenState::stored;
                queen.base_owner=home->owner;
                queen.x=home->x;
                queen.z=home->z;
                (void)queen_stored(owners_[home->owner].team);
            }
            continue;
        }
        std::optional<EntityId> collector;
        units_.for_each([&](EntityId id,const UnitRecord& unit) {
            if (collector || !unit.alive || std::fabs(unit.x-queen.x)>queen_contact_half ||
                std::fabs(unit.z-queen.z)>queen_contact_half) return;
            if (queen.state==QueenState::stored && queen.base_owner<owner_capacity &&
                unit.team==owners_[queen.base_owner].team) return;
            collector=id;
        });
        if (!collector) continue;
        UnitRecord* unit=units_.get(*collector);
        if (unit==nullptr) continue;
        if (config_.mission.game_kind==4U && unit->owner==0U) (void)queen_contact(0U);
        if (queen.state==QueenState::stored && queen.base_owner<owner_capacity) {
            (void)queen_taken(owners_[queen.base_owner].team,unit->team);
        }
        queen.state=QueenState::carried;
        queen.carrier=*collector;
        queen.x=unit->x;
        queen.z=unit->z;
    }
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
    WeaponBaseContext projectiles{combat,world_,angles_};
    WeaponContext weapons{projectiles,weapon_state_,effects_,rng_};
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
            (void)fire_weapon(weapons,id,unit->primary_weapon,
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
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        bool pending_spawner=false;
        if (spawner_budgets_[i]!=0) {
            pending_spawner=std::any_of(
                spawners_.begin(),spawners_.end(),[i](const SessionSpawner& runtime) {
                    return !runtime.state.finished &&
                           runtime.state.definition.owner==static_cast<std::uint8_t>(i);
                });
        }
        input.squad_alive[i]=mission_.squad_alive(i) || pending_spawner;
    }
    const ObjectiveResult result=objective_->step(input);
    std::array<OwnerResult,owner_capacity> owner_results{};
    for (std::size_t i=0U; i<owner_capacity; ++i) owner_results[i]=objective_->result(i);
    mission_.apply_external_objective(result.next_state,owner_results,tick_,events_);
}

void BattleSession::step(const SessionInput& input) {
    if (mission_.simulation_running() && mission_.state()!=MatchState::paused) {
        release_destroyed_units();
        CombatContext combat=combat_context();
        WeaponBaseContext projectiles{combat,world_,angles_};
        WeaponContext weapons{projectiles,weapon_state_,effects_,rng_};
        attach_turret_collision(weapons);
        update_weapon_projectiles(weapons);
        update_units(input);
        update_turrets();
        update_spawners();
        update_pickup_state();
        update_queens();
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
    weapon_state_.serialize(writer);
    writer.u32(static_cast<std::uint32_t>(turrets_.size()));
    for (const TurretRecord& turret : turrets_) turret.serialize(writer);
    for (const TurretOwnerConfig& owner : turret_owner_config_) {
        writer.f32(owner.rebuild_time);
        for (const std::int16_t value : owner.effect_kind) writer.s16(value);
        for (const std::int16_t value : owner.fire_delay) writer.s16(value);
    }
    writer.u32(static_cast<std::uint32_t>(queen_bases_.size()));
    for (const QueenBase& base : queen_bases_) {
        writer.u8(base.owner); writer.f32(base.x); writer.f32(base.z);
    }
    writer.u32(static_cast<std::uint32_t>(queens_.size()));
    for (const QueenRecord& queen : queens_) {
        writer.u8(static_cast<std::uint8_t>(queen.state));
        writer.u8(queen.home_owner); writer.u8(queen.base_owner); writer.id(queen.carrier);
        writer.f32(queen.x); writer.f32(queen.z); writer.s32(queen.dropped_tick);
    }
    writer.u32(static_cast<std::uint32_t>(spawners_.size()));
    for (const SessionSpawner& runtime : spawners_) {
        const SpawnerState& state=runtime.state;
        const SpawnerDefinition& d=state.definition;
        writer.u32(d.placement_index);
        writer.f32(d.x); writer.f32(d.z); writer.u16(d.heading); writer.u16(d.id);
        writer.u8(d.owner); writer.u8(d.mode); writer.u8(d.shape); writer.u8(d.count_field);
        writer.s16(d.interval_ticks); writer.u8(d.parameter); writer.u8(d.gate_bits);
        writer.boolean(d.linked); writer.u8(static_cast<std::uint8_t>(d.source));
        writer.s32(state.last_tick); writer.s32(state.linked_serial); writer.boolean(state.finished);
        writer.boolean(runtime.linked.has_value());
        if (runtime.linked) {
            writer.s32(runtime.linked->serial);
            writer.boolean(runtime.linked->destroyed);
        }
    }
    for (std::size_t owner=0U; owner<owner_capacity; ++owner) {
        writer.s32(spawner_budgets_[owner]);
        writer.boolean(spawner_gates_[owner]);
    }
}

std::string BattleSession::checksum() const {
    StateWriter writer;
    serialize(writer);
    return writer.hex();
}

} // namespace battletanx
