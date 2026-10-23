// Cross-module scenarios over the gameplay world runtimes: placed objects,
// the route service, campaign spawn planning and unit combat. All data is
// synthetic; nothing here reads retail content.
//
// Harness rules that are test policy (not retail behaviour):
//  * units move with axis-clamped steps of unit_step toward their steering point;
//  * a turret shot that names a unit index deals turret_shot_damage to it;
//  * units return fire on a turret only after that turret has fired, so the
//    acquire -> fire -> destroy order is a property of the scenario.

#include "battletanx/campaign_spawn.hpp"
#include "battletanx/combat.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/navigation_route.hpp"
#include "battletanx/placed_objects.hpp"
#include "battletanx/projectile_runtime.hpp"
#include "battletanx/route_service.hpp"
#include "battletanx/runtime_math.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_kinds.hpp"
#include "runtime_test_support.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace battletanx;
using retail::UnitKind;

namespace {

constexpr float unit_step=40.0F;
constexpr std::int32_t turret_shot_damage=5;
constexpr std::int32_t return_fire_damage=25;
constexpr std::int32_t return_fire_interval=15;
constexpr float return_fire_range_sq=800.0F*800.0F;

// Synthetic grid mesh: index 0 vertex/triangle are unused sentinels; each
// 100x100 cell is two triangles (same construction as route_service_tests).
NavigationLevel grid(std::uint16_t width,std::uint16_t height) {
    NavigationLevel level;
    level.vertices.push_back({});
    level.triangles.push_back({});
    const auto vertex_id=[&](std::uint16_t x,std::uint16_t y) {
        return static_cast<std::uint16_t>(1U+y*(width+1U)+x);
    };
    for (std::uint16_t y=0U; y<=height; ++y) {
        for (std::uint16_t x=0U; x<=width; ++x) {
            level.vertices.push_back({static_cast<float>(x)*100.0F,static_cast<float>(y)*100.0F});
        }
    }
    for (std::uint16_t y=0U; y<height; ++y) {
        for (std::uint16_t x=0U; x<width; ++x) {
            NavigationTriangle a,b;
            a.corners={{{vertex_id(x,y),0U},{vertex_id(x+1U,y+1U),0U},{vertex_id(x+1U,y),0U}}};
            b.corners={{{vertex_id(x,y),0U},{vertex_id(x,y+1U),0U},{vertex_id(x+1U,y+1U),0U}}};
            level.triangles.push_back(a);
            level.triangles.push_back(b);
        }
    }
    std::map<std::pair<std::uint16_t,std::uint16_t>,std::uint16_t> edges;
    for (std::size_t t=1U; t<level.triangles.size(); ++t) {
        for (std::uint16_t side=0U; side<3U; ++side) {
            auto& corners=level.triangles[t].corners;
            std::uint16_t a=corners[side].vertex,b=corners[(side+1U)%3U].vertex;
            if (b<a) std::swap(a,b);
            const auto id=static_cast<std::uint16_t>(t*3U+side);
            const auto found=edges.find({a,b});
            if (found==edges.end()) { edges[{a,b}]=id; continue; }
            corners[side].adjacent=found->second;
            level.triangles[found->second/3U].corners[found->second%3U].adjacent=id;
        }
    }
    return level;
}

std::uint16_t tri(std::uint16_t width,std::uint16_t x,std::uint16_t y,bool upper) {
    return static_cast<std::uint16_t>(1U+(y*width+x)*2U+(upper ? 1U : 0U));
}

float step_toward(float from,float to,float limit) {
    const float d=to-from;
    if (d>limit) return from+limit;
    if (d< -limit) return from-limit;
    return to;
}

RouteRequest route_request(EntityId unit,GoalMode mode,NavigationVertex from,NavigationVertex to,
                           std::uint32_t serial) {
    return RouteRequest{.unit=unit,.mode=mode,.from_x=from.x,.from_z=from.z,
                        .to_x=to.x,.to_z=to.z,.serial=serial};
}

// Ticks the service until it plans a unit (dead slots are skipped one per tick).
std::optional<RouteServiceResult> next_planned(RouteService& service,const NavigationLevel& level,
        std::span<const std::vector<std::uint8_t>> occupancy={}) {
    for (std::size_t i=0U; i<=route_service_slots; ++i) {
        const auto result=service.tick(level,true,occupancy);
        if (result) return result;
    }
    return std::nullopt;
}

void hash_units(StateWriter& writer,const UnitPool& units) {
    units.for_each([&](EntityId id,const UnitRecord& unit) {
        writer.id(id);
        writer.boolean(unit.alive);
        writer.u8(unit.owner);
        writer.u8(unit.team);
        writer.f32(unit.x);
        writer.f32(unit.z);
        writer.s32(unit.health);
    });
}

void hash_owners(StateWriter& writer,const std::array<OwnerRecord,owner_capacity>& owners) {
    for (const OwnerRecord& owner : owners) {
        writer.s32(owner.score);
        writer.s32(owner.kills);
        writer.s32(owner.losses);
    }
}

// ------------------------------------------------------------ turret field

enum class Step : std::uint8_t {
    acquired, first_shot, unit_damaged, return_fire, destroyed, score, rebuild_started, rebuilt,
};

static_assert(std::is_same_v<std::underlying_type_t<Step>,std::uint8_t>);

struct Mark {
    Step step{};
    std::int32_t tick{};
};

struct TurretFieldResult {
    std::vector<Mark> log;
    std::vector<placed::Event> turret_events;   // every event of the destroyed turret
    std::int32_t owner0_score{};
    std::int16_t rebuilt_health{};
    bool other_turret_intact{};
    std::size_t damaged_units{};
    std::string digest;
};

struct Field {
    NavigationLevel level=grid(40U,4U);           // 4000 x 400 world
    AngleTable angles{reference_angle_table()};
    RetailRandom rng;
    std::array<TurretOwnerConfig,owner_capacity> configs{};
    std::array<std::uint8_t,4> effect_counts{1U,1U,1U,1U};
    std::array<OwnerRecord,owner_capacity> owners{};
    UnitPool units{};
    std::vector<EntityId> unit_ids;
    std::vector<std::uint16_t> unit_triangles;
    std::vector<TurretTarget> targets;
    placed::World world;
    RouteService routes;
    GameplayEventQueue gameplay{};
    SoundEventQueue sounds{};
    CombatConfig config{};

    explicit Field(std::uint32_t seed) : rng(seed) {
        for (TurretOwnerConfig& c : configs) {
            c.rebuild_time=1.0F;       // 1/100 * 1800 * scale[1] = 18 ticks
            c.fire_delay={10,10,10,10};
            c.effect_kind={2,2,2,2};
        }
        for (std::size_t i=0U; i<owners.size(); ++i) {
            owners[i].present=true;
            owners[i].computer=i!=0U;
            owners[i].team=static_cast<std::uint8_t>(i<2U ? 0U : 1U);
        }
    }
    Field(const Field&)=delete;
    Field& operator=(const Field&)=delete;

    placed::UpdateContext context(std::int32_t tick) {
        targets.clear();
        for (const EntityId id : unit_ids) {
            targets.push_back(TurretTarget{.unit=units.get(id),.targetable=true,.ai_state_two=false});
        }
        return placed::UpdateContext{.angles=&angles,.targets=targets,.owner_configs=configs,
            .owner_effect_counts=effect_counts,.tick=tick,.frame_delta=1.0F,
            .search_range=turret_search_range,.rng=&rng,.area_clear={},.line_of_sight={}};
    }
    EntityId add_unit(std::uint8_t owner,std::uint8_t team,float x,float z) {
        const auto id=units.allocate();
        UnitRecord* unit=id ? units.get(*id) : nullptr;
        if (unit==nullptr) return no_entity;
        *unit=make_unit(UnitKind::tank,owner,team,x,z,0U,0,UnitControl::computer);
        unit->flags=0U;   // TEST POLICY: no spawn shields in these scenarios
        unit_ids.push_back(*id);
        unit_triangles.push_back(find_nearest_navigation_triangle(level,NavigationVertex{x,z}));
        return *id;
    }
    void digest(StateWriter& writer) const {
        world.serialize(writer);
        routes.serialize(writer);
        hash_units(writer,units);
        hash_owners(writer,owners);
    }
};

std::size_t first_index(const std::vector<Mark>& log,Step step) {
    for (std::size_t i=0U; i<log.size(); ++i) {
        if (log[i].step==step) return i;
    }
    return log.size();
}
std::optional<std::int32_t> first_tick(const std::vector<Mark>& log,Step step) {
    const std::size_t i=first_index(log,step);
    if (i==log.size()) return std::nullopt;
    return log[i].tick;
}

TurretFieldResult run_turret_field(std::uint32_t seed) {
    TurretFieldResult out;
    Field f(seed);
    const std::array<float,3> lanes{100.0F,200.0F,300.0F};
    for (const float z : lanes) f.add_unit(0U,0U,50.0F,z);

    const auto near_turret=f.world.spawn_turret(placed::TurretSpawn{.team=1U,.owner=2U,.type=1U,.heading=0U,
        .x=3500.0F,.z=150.0F,.check_area=false,.scaled=true},f.context(0));
    const auto far_turret=f.world.spawn_turret(placed::TurretSpawn{.team=1U,.owner=2U,.type=2U,.heading=0U,
        .x=3500.0F,.z=350.0F,.check_area=false,.scaled=false},f.context(0));
    if (!near_turret || !far_turret) return out;
    if (f.world.get(*near_turret)->turret.target!=no_turret_target) return out;   // column starts out of range

    constexpr float halt_x=2900.0F;
    std::uint32_t serial=1U;
    for (std::size_t i=0U; i<f.unit_ids.size(); ++i) {
        f.routes.bind_unit(f.unit_ids[i],f.unit_triangles[i]);
        const UnitRecord* unit=f.units.get(f.unit_ids[i]);
        f.routes.submit(route_request(f.unit_ids[i],GoalMode::point_route,{unit->x,unit->z},
                                      {halt_x,lanes[i]},serial));
    }

    bool near_fired=false;
    bool rebuilt=false;
    std::int32_t last_return_fire=-return_fire_interval;
    std::vector<bool> damaged(f.unit_ids.size(),false);
    for (std::int32_t tick=1; tick<=900 && !rebuilt; ++tick) {
        f.world.clear_events();
        f.routes.tick(f.level);
        // Movement along the planned routes.
        for (std::size_t i=0U; i<f.unit_ids.size(); ++i) {
            UnitRecord* unit=f.units.get(f.unit_ids[i]);
            if (unit==nullptr || !unit->alive) continue;
            const NavigationVertex from{unit->x,unit->z};
            if (unit->x>=halt_x) continue;
            NavigationVertex steer{halt_x,lanes[i]};
            if (const auto response=f.routes.follow(f.unit_ids[i],from); response && response->reachable) {
                steer={response->waypoint_x,response->waypoint_z};
            }
            unit->x=step_toward(unit->x,steer.x,unit_step);
            unit->z=step_toward(unit->z,steer.z,unit_step);
            const NavigationVertex to{unit->x,unit->z};
            f.unit_triangles[i]=locate_route_triangle(f.level,f.unit_triangles[i],from,to);
            f.routes.set_unit_position(f.unit_ids[i],to);
            f.routes.set_unit_triangle(f.unit_ids[i],f.unit_triangles[i]);
            if (unit->x>=halt_x) f.routes.cancel(f.unit_ids[i]);
        }

        const bool had_target=f.world.get(*near_turret)->turret.target!=no_turret_target;
        f.world.update(f.context(tick));
        const placed::Entry* entry=f.world.get(*near_turret);
        if (!had_target && entry!=nullptr && entry->turret.target!=no_turret_target &&
            first_index(out.log,Step::acquired)==out.log.size()) {
            out.log.push_back(Mark{Step::acquired,tick});
        }

        // Return fire from unit 0 once the near turret has shot at the column.
        UnitRecord* gunner=f.units.get(f.unit_ids[0]);
        if (near_fired && entry!=nullptr && entry->turret.health>0 &&
            (entry->turret.flags&turret_rebuilding)==0U && gunner!=nullptr && gunner->alive &&
            tick-last_return_fire>=return_fire_interval) {
            const float dx=entry->turret.x-gunner->x,dz=entry->turret.z-gunner->z;
            if (dx*dx+dz*dz<=return_fire_range_sq) {
                last_return_fire=tick;
                const placed::MessageResult hit=f.world.hit(*near_turret,placed::Hit{.source_kind=34U,
                    .damage=return_fire_damage,.source_owner=0U,.heading=direction_angle(dx,dz),
                    .shielded=false,.flame_forced=false},
                    f.owners,tick,0);
                if (hit==placed::MessageResult::handled) out.log.push_back(Mark{Step::return_fire,tick});
            }
        }

        CombatContext combat{.units=f.units,.owners=f.owners,.events=f.gameplay,.sounds=f.sounds,
                             .config=f.config,.tick=tick};
        for (const placed::Event& e : f.world.events()) {
            if (e.entity==*near_turret) out.turret_events.push_back(e);
            switch (e.kind) {
            case placed::EventKind::shot_fired:
                if (e.entity==*near_turret && !near_fired) {
                    near_fired=true;
                    out.log.push_back(Mark{Step::first_shot,tick});
                }
                if (e.shot && e.shot->target<f.unit_ids.size()) {
                    const DamageResult r=apply_unit_damage(combat,f.unit_ids[e.shot->target],turret_shot_damage,
                                                           e.owner,false);
                    if (r!=DamageResult::ignored) {
                        damaged[e.shot->target]=true;
                        if (e.entity==*near_turret && first_index(out.log,Step::unit_damaged)==out.log.size()) {
                            out.log.push_back(Mark{Step::unit_damaged,tick});
                        }
                    }
                }
                break;
            case placed::EventKind::destroyed:
                if (e.entity==*near_turret) out.log.push_back(Mark{Step::destroyed,tick});
                break;
            case placed::EventKind::score_awarded:
                if (e.entity==*near_turret) out.log.push_back(Mark{Step::score,tick});
                break;
            case placed::EventKind::rebuild_started:
                if (e.entity==*near_turret) out.log.push_back(Mark{Step::rebuild_started,tick});
                break;
            case placed::EventKind::rebuilt:
                if (e.entity==*near_turret) {
                    out.log.push_back(Mark{Step::rebuilt,tick});
                    rebuilt=true;
                }
                break;
            default:
                break;
            }
        }
    }
    if (const placed::Entry* entry=f.world.get(*near_turret)) out.rebuilt_health=entry->turret.health;
    if (const placed::Entry* entry=f.world.get(*far_turret)) {
        out.other_turret_intact=entry->turret.health==static_cast<std::int16_t>(turret_health[2]);
    }
    for (const bool d : damaged) out.damaged_units+=d ? 1U : 0U;
    out.owner0_score=f.owners[0].score;
    StateWriter writer;
    f.digest(writer);
    out.digest=writer.hex();
    return out;
}

// ------------------------------------------------------------ spawn + blast

WorldItem item(std::int32_t type,std::uint8_t flags,std::uint8_t parameter,std::uint16_t id,
               float x,float z,std::uint16_t angle=0U) {
    WorldItem result;
    result.type=type;
    result.flags=flags;
    result.parameter=parameter;
    result.id=id;
    result.x=x;
    result.z=z;
    result.angle=angle;
    return result;
}

// Type-25 marker: group in the high nibble, kind in the low nibble.
WorldItem marker(std::uint8_t group,std::uint8_t kind,float x,float z) {
    return item(25,0x10U,static_cast<std::uint8_t>((group<<4U)|kind),0U,x,z);
}

CampaignSpawnInput campaign_input() {
    CampaignSpawnInput input;
    input.game_kind=3;
    input.active_player_count=4;
    for (std::size_t owner=0U; owner<spawn_owner_count; ++owner) {
        SpawnOwnerSetup& setup=input.owners[owner];
        setup.present=true;
        setup.computer=owner!=0U;
        setup.team=static_cast<std::uint8_t>(owner);
        setup.lineup={0U,1U,2U,0U,1U};
        setup.lineup_count=5U;
    }
    return input;
}

struct BlastResult {
    bool planned{};
    std::size_t placed_units{};
    std::size_t splashed_units{};
    std::size_t far_units_intact{};
    bool turret_matches_plan{};
    bool turret_destroyed_by_blast{};
    bool explosion_then_splash{};
    std::string digest;
};

BlastResult run_spawn_blast(std::uint32_t seed) {
    BlastResult out;
    WorldLevel level;
    // Owner 1 sub-kind 1 owned object (bank type 22): id bits owner<<1, sub-kind<<3.
    const auto owned_id=static_cast<std::uint16_t>((1U<<1U)|(1U<<3U));
    level.items={marker(0U,10U,100.0F,100.0F),
                 marker(1U,0U,1000.0F,1000.0F),marker(1U,0U,1060.0F,1000.0F),
                 marker(1U,1U,1000.0F,1050.0F),marker(1U,0U,1500.0F,1000.0F),
                 item(22,0U,0U,owned_id,2000.0F,2000.0F,0x4000U)};
    WorldGroup bank;
    bank.item_count=static_cast<std::uint16_t>(level.items.size());
    WorldGroup placed_group;
    placed_group.first_item=bank.item_count;
    level.groups={bank,placed_group};
    level.bank_group=0U;

    RetailRandom plan_rng(seed);
    SpawnPlanError error{};
    const auto plan=build_campaign_spawn_plan(campaign_input(),level,nullptr,plan_rng,&error);
    if (!plan || plan->owned_objects.size()!=1U) return out;
    out.planned=true;

    Field f(seed);
    f.owners={};
    for (std::size_t i=0U; i<owner_capacity; ++i) {
        f.owners[i].present=plan->owners[i].present;
        f.owners[i].computer=i!=0U;
        f.owners[i].team=plan->owners[i].team;
    }
    for (const PlannedUnit& planned : plan->units) {
        const auto id=f.units.allocate();
        UnitRecord* unit=id ? f.units.get(*id) : nullptr;
        if (unit==nullptr) return out;
        *unit=make_unit(planned.kind,planned.owner,planned.team,planned.x,planned.z,planned.heading,0,
                        planned.owner==0U ? UnitControl::player : UnitControl::computer);
        unit->flags=0U;   // TEST POLICY: held/computer spawn flags are not modelled here
        f.unit_ids.push_back(*id);
        ++out.placed_units;
    }

    const OwnedObjectPlacement& object=plan->owned_objects[0];
    const auto turret=f.world.spawn_turret(placed::TurretSpawn{.team=object.team,.owner=object.owner,
        .type=object.sub_kind,.heading=object.heading,.x=object.x,.z=object.z,
        .check_area=false,.scaled=object.flagged},f.context(0));
    if (!turret) return out;
    const placed::Entry* turret_entry=f.world.get(*turret);
    out.turret_matches_plan=turret_entry!=nullptr &&
        turret_entry->turret.health==static_cast<std::int16_t>(object.health) &&
        turret_entry->turret.type==object.sub_kind && turret_entry->turret.team==object.team;

    // Explosive barrel in the middle of owner 1's squad.
    constexpr std::int32_t bx=1020,bz=1020;
    const auto barrel=f.world.spawn_breakable(placed::BreakableRecord{.flags=placed::breakable_explosive,.debris=0U,.heading=0U,
        .x=bx,.z=bz,.released=false},placed::Bounds{});
    if (!barrel) return out;
    if (f.world.hit(*barrel,placed::Hit{.source_kind=2U,.damage=0,.source_owner=0U,.heading=0U,.shielded=false,
        .flame_forced=false},f.owners,1,3)!=
        placed::MessageResult::handled) return out;
    f.world.clear_events();
    f.world.update(f.context(1));

    CombatContext combat{.units=f.units,.owners=f.owners,.events=f.gameplay,.sounds=f.sounds,
                         .config=f.config,.tick=1};
    std::size_t explosion_at=f.world.events().size(),splash_at=f.world.events().size();
    for (std::size_t i=0U; i<f.world.events().size(); ++i) {
        const placed::Event& e=f.world.events()[i];
        if (e.entity!=*barrel) continue;
        if (e.kind==placed::EventKind::explosion && e.value==22 && explosion_at==f.world.events().size()) {
            explosion_at=i;
        }
        if (e.kind!=placed::EventKind::splash) continue;
        splash_at=i;
        for (const EntityId id : f.unit_ids) {
            const UnitRecord* unit=f.units.get(id);
            if (unit==nullptr) continue;
            const float dx=unit->x-e.x,dz=unit->z-e.z;
            if (dx*dx+dz*dz>placed_splash_radius*placed_splash_radius) continue;
            if (apply_unit_damage(combat,id,static_cast<std::int32_t>(placed_splash_value),no_owner,false)!=
                DamageResult::ignored) ++out.splashed_units;
        }
    }
    out.explosion_then_splash=explosion_at<splash_at && splash_at<f.world.events().size();
    for (const EntityId id : f.unit_ids) {
        const UnitRecord* unit=f.units.get(id);
        if (unit!=nullptr && unit->health==unit->max_health) ++out.far_units_intact;
    }

    // The same explosion delivered to the turret as message 3 (falloff over 4000).
    f.world.clear_events();
    const AngleTable blast_angles=reference_angle_table();
    if (f.world.blast(*turret,static_cast<float>(bx),static_cast<float>(bz),f.owners,3,
                     blast_angles)==
        placed::MessageResult::handled) {
        const placed::Entry* entry=f.world.get(*turret);
        out.turret_destroyed_by_blast=entry!=nullptr && entry->turret.health<=0;
    }

    StateWriter writer;
    plan->serialize(writer);
    f.digest(writer);
    out.digest=writer.hex();
    return out;
}

} // namespace

RUNTIME_TEST(world_turret_field_acquire_fire_destroy_rebuild_order) {
    const TurretFieldResult r=run_turret_field(11U);
    const std::size_t acquired=first_index(r.log,Step::acquired);
    const std::size_t shot=first_index(r.log,Step::first_shot);
    const std::size_t damaged=first_index(r.log,Step::unit_damaged);
    const std::size_t returned=first_index(r.log,Step::return_fire);
    const std::size_t destroyed=first_index(r.log,Step::destroyed);
    const std::size_t score=first_index(r.log,Step::score);
    const std::size_t started=first_index(r.log,Step::rebuild_started);
    const std::size_t rebuilt=first_index(r.log,Step::rebuilt);
    REQUIRE(rebuilt<r.log.size());
    REQUIRE(acquired<shot);
    REQUIRE(shot<damaged);
    REQUIRE(shot<returned);
    REQUIRE(returned<destroyed);
    REQUIRE(destroyed<score);
    REQUIRE(score<started);
    REQUIRE(started<rebuilt);
    // Two hits of 25 destroy the 50-health turret. A rebuilt turret may be hit
    // again later in the same scenario, so only count hits before destruction.
    std::size_t hits=0U;
    for (std::size_t i=0U; i<destroyed; ++i)
        hits+=r.log[i].step==Step::return_fire ? 1U : 0U;
    REQUIRE(hits==2U);
    // Rebuild delay: rebuild_time 1.0 -> 1/100 * 1800 * owner-count scale 1.0.
    REQUIRE(*first_tick(r.log,Step::rebuilt)-*first_tick(r.log,Step::rebuild_started)>=18);
    // The scripted gunner acts after world.update and lands one shot on the
    // freshly rebuilt turret in that same tick.
    REQUIRE(r.rebuilt_health==static_cast<std::int16_t>(turret_health[1]-return_fire_damage));
    REQUIRE(r.owner0_score==turret_destroy_score[1]);
    REQUIRE(r.other_turret_intact);
    REQUIRE(r.damaged_units>=1U);
    // The destroyed event carries the turret type and the attacking owner.
    bool destroyed_event=false;
    for (const placed::Event& e : r.turret_events) {
        if (e.kind==placed::EventKind::destroyed) destroyed_event=e.value==1 && e.owner==0U;
    }
    REQUIRE(destroyed_event);
    return true;
}

RUNTIME_TEST(world_route_service_round_robin_with_occupancy_retarget_cancel) {
    constexpr std::uint16_t width=6U;
    const NavigationLevel level=grid(width,3U);
    RouteService service;
    const std::array<EntityId,3> ids{EntityId{0U,1U},EntityId{1U,1U},EntityId{2U,1U}};
    const std::array<std::uint16_t,3> starts{tri(width,0U,0U,false),tri(width,0U,1U,false),tri(width,0U,2U,false)};
    std::array<NavigationVertex,3> positions{};
    for (std::size_t i=0U; i<ids.size(); ++i) {
        positions[i]=navigation_triangle_centroid(level,starts[i]);
        REQUIRE(service.bind_unit(ids[i],starts[i]));
    }

    // Occupancy per unit from its two teammates.
    std::array<std::vector<std::uint8_t>,route_service_slots> occupancy{};
    for (std::size_t i=0U; i<ids.size(); ++i) {
        std::vector<RouteTeammate> mates;
        for (std::size_t j=0U; j<ids.size(); ++j) {
            mates.push_back(RouteTeammate{.unit=ids[j],.alive=true,.excluded=false,
                                          .position=positions[j],.triangle=starts[j]});
        }
        occupancy[ids[i].slot]=build_route_occupancy(level,ids[i],positions[i],starts[i],mates);
        REQUIRE(occupancy[ids[i].slot].size()==level.triangles.size());
        REQUIRE(occupancy[ids[i].slot][starts[i]]==0U);
        for (std::size_t j=0U; j<ids.size(); ++j) {
            if (j!=i) REQUIRE(occupancy[ids[i].slot][starts[j]]==1U);
        }
    }

    NavigationVertex target=navigation_triangle_centroid(level,tri(width,5U,1U,false));
    for (std::size_t i=0U; i<ids.size(); ++i) {
        REQUIRE(service.submit(route_request(ids[i],GoalMode::chase,positions[i],target,1U))==
                RouteSubmitResult::accepted);
    }

    // Six budgeted visits: every unit twice, in a repeating cycle.
    std::array<std::uint8_t,6> order{};
    std::optional<RouteResponse> old_response;
    for (std::uint8_t& slot : order) {
        const auto result=next_planned(service,level,occupancy);
        REQUIRE(result.has_value());
        REQUIRE(result->status==RouteServiceStatus::routed);
        REQUIRE(result->response.serial==1U);
        REQUIRE(result->response.unit==ids[result->slot]);
        slot=result->slot;
        if (slot==1U) old_response=result->response;
    }
    for (std::size_t i=0U; i<3U; ++i) REQUIRE(order[i]==order[i+3U]);
    REQUIRE(order[0]!=order[1] && order[1]!=order[2] && order[0]!=order[2]);
    REQUIRE(old_response.has_value() && service.is_current(*old_response));

    // The target moves: unit 1 retargets with a newer serial.
    target=navigation_triangle_centroid(level,tri(width,5U,0U,true));
    REQUIRE(service.submit(route_request(ids[1],GoalMode::chase,positions[1],target,2U))==
            RouteSubmitResult::retargeted);
    REQUIRE(!service.is_current(*old_response));
    bool replanned=false;
    for (std::size_t visit=0U; visit<3U && !replanned; ++visit) {
        const auto result=next_planned(service,level,occupancy);
        REQUIRE(result.has_value());
        if (result->slot!=1U) continue;
        replanned=true;
        REQUIRE(result->response.serial==2U);
        REQUIRE(service.is_current(result->response));
        const RouteUnitState* state=service.state(ids[1]);
        REQUIRE(state!=nullptr && state->has_route && state->route_serial==2U);
        REQUIRE(state->route_goal.x==target.x && state->route_goal.z==target.z);
    }
    REQUIRE(replanned);

    // The target is lost for unit 2: cancel drops request and route.
    REQUIRE(service.cancel(ids[2]));
    const RouteUnitState* cancelled=service.state(ids[2]);
    REQUIRE(cancelled!=nullptr && !cancelled->active && !cancelled->has_route);
    REQUIRE(!service.follow(ids[2],positions[2]).has_value());
    REQUIRE(service.submit(route_request(ids[2],GoalMode::chase,positions[2],target,1U))==
            RouteSubmitResult::stale_serial);
    for (std::size_t visit=0U; visit<4U; ++visit) {
        const auto result=next_planned(service,level,occupancy);
        REQUIRE(result.has_value());
        REQUIRE(result->slot!=2U);
    }
    const auto steer=service.follow(ids[0],positions[0]);
    REQUIRE(steer.has_value() && steer->reachable);
    return true;
}

RUNTIME_TEST(world_spawn_plan_places_units_and_barrel_blast_hits_squad) {
    const BlastResult r=run_spawn_blast(5U);
    REQUIRE(r.planned);
    REQUIRE(r.placed_units==5U);            // player + four owner-1 markers
    REQUIRE(r.turret_matches_plan);
    REQUIRE(r.explosion_then_splash);
    REQUIRE(r.splashed_units==3U);          // the three markers within 108 of the barrel
    REQUIRE(r.far_units_intact==2U);        // player start and the 1500,1000 marker
    REQUIRE(r.turret_destroyed_by_blast);   // 1414 away: 50 + trunc(0.646*50) > 50 health
    return true;
}

RUNTIME_TEST(world_combined_state_digest_is_reproducible) {
    const TurretFieldResult a=run_turret_field(11U);
    const TurretFieldResult b=run_turret_field(11U);
    const BlastResult c=run_spawn_blast(5U);
    const BlastResult d=run_spawn_blast(5U);
    REQUIRE(!a.digest.empty() && !c.digest.empty());
    REQUIRE(a.digest==b.digest);
    REQUIRE(c.digest==d.digest);
    StateWriter first;
    StateWriter second;
    for (const std::string* part : {&a.digest,&c.digest}) {
        for (const char ch : *part) first.u8(static_cast<std::uint8_t>(ch));
    }
    for (const std::string* part : {&b.digest,&d.digest}) {
        for (const char ch : *part) second.u8(static_cast<std::uint8_t>(ch));
    }
    REQUIRE(first.hex()==second.hex());
    REQUIRE(a.log.size()==b.log.size());
    for (std::size_t i=0U; i<a.log.size(); ++i) {
        REQUIRE(a.log[i].step==b.log[i].step && a.log[i].tick==b.log[i].tick);
    }
    return true;
}

RUNTIME_TEST_MAIN()
