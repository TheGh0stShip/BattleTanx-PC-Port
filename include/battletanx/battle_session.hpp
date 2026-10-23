#pragma once

#include "battletanx/combat.hpp"
#include "battletanx/campaign_ai.hpp"
#include "battletanx/campaign_spawn.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/enemy_control.hpp"
#include "battletanx/level.hpp"
#include "battletanx/level_spawns.hpp"
#include "battletanx/mission_state.hpp"
#include "battletanx/navigation_route.hpp"
#include "battletanx/objective_runtime.hpp"
#include "battletanx/pickup_runtime.hpp"
#include "battletanx/state_hash.hpp"
#include "battletanx/unit_kinds.hpp"
#include "battletanx/weapon_runtime.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace battletanx {

// Moves one unit for one 30 Hz tick from its drive input. The session never
// integrates motion itself; it delegates to this boundary.
using UnitMotion=std::function<void(UnitRecord& unit,const DriveInput& input)>;
using RoutePlanner=std::function<RouteResponse(const RouteRequest& request)>;

// Drives a unit through the existing native tank update (update_player_tank)
// against real level data. The navigation triangle is resolved on first use.
[[nodiscard]] UnitMotion level_unit_motion(const WorldLevel& world,const NavigationLevel& navigation,
                                           const AngleTable& angles);

// Same native tank update on unobstructed open ground (one navigation
// triangle enclosing +/-100000 units, no world shapes), for headless runs.
[[nodiscard]] UnitMotion open_ground_unit_motion(const AngleTable& angles);

// Plans a recovered navigation route for a campaign control request. The
// returned response is value-owned and safe to retain across ticks.
[[nodiscard]] RoutePlanner level_route_planner(const NavigationLevel& navigation);

struct SessionConfig {
    MissionConfig mission{};
    CombatConfig combat{};
    CampaignAiConfig campaign_ai{};
    PickupOptions pickups{};
    std::uint32_t rng_seed{};   // retail seeds from osGetTime(); headless runs pass a constant
};

struct OwnerSetup {
    std::uint8_t team{};
    bool computer{};
    std::uint8_t squad_cap{1U};
    std::int16_t respawn_bank{};
    bool respawn_enabled{};
    float armor_scale{retail::human_scale_default};
    float damage_scale{retail::human_scale_default};
    bool objective_base{};
    std::optional<OwnerDifficulty> difficulty{};
};

struct UnitSpawn {
    retail::UnitKind kind{retail::UnitKind::tank};
    std::uint8_t owner{};
    float x{}, z{};
    std::uint16_t heading{};
    std::uint16_t flags{};
    std::uint8_t squad_slot{0xFFU};
};

struct RespawnPoint {
    float x{}, z{};
    std::uint16_t heading{};
};

// Per-tick input for the local human owner (owner 0).
struct SessionInput {
    DriveInput drive{};
    bool start_pressed{};   // pause toggle
    bool advance{};         // dismisses the mission title
};

class BattleSession {
public:
    BattleSession(SessionConfig config,const AngleTable& angles,UnitMotion motion,
                  WorldCollision world={},RoutePlanner routes={});

    void add_owner(std::uint8_t index,const OwnerSetup& setup);
    std::optional<EntityId> spawn_unit(const UnitSpawn& spawn);
    void set_respawn_point(std::uint8_t owner,RespawnPoint point);
    // Level pickups, at most pickup_capacity. Returns false when full.
    static constexpr std::size_t pickup_capacity=256U;
    bool add_pickup(const PickupRecord& pickup);
    bool add_spawner(const SpawnerDefinition& definition);
    static constexpr std::size_t turret_capacity=128U;
    bool add_turret(const OwnedObjectPlacement& placement);
    void set_turret_owner_config(std::uint8_t owner,const TurretOwnerConfig& config);
    void set_spawner_budget(std::uint8_t owner,std::int32_t budget);
    void set_spawner_gate(std::uint8_t owner,bool open);
    void set_pickup_weights(const PickupWeights& weights) { pickup_weights_=weights; }
    [[nodiscard]] std::span<const PickupRecord> pickups() const noexcept { return pickups_; }
    [[nodiscard]] std::span<const TurretRecord> turrets() const noexcept { return turrets_; }
    // Damage from a source outside the session's own shells (splash, contact).
    DamageResult apply_damage(EntityId unit,std::int32_t amount,std::uint8_t attacker,bool direct);
    ObjectiveEventStatus queen_taken(std::uint8_t holder_team,std::uint8_t collector_team);
    ObjectiveEventStatus queen_stored(std::uint8_t base_team);
    ObjectiveEventStatus queen_contact(std::uint8_t owner);
    ObjectiveEventStatus marker_contact(std::uint8_t owner);
    ObjectiveEventStatus self_destruct(std::uint8_t owner);

    // One 30 Hz tick. Order (see the handoff notes for evidence):
    // 1. release units destroyed on an earlier tick
    // 2. shells spawned on earlier ticks move and resolve hits
    // 3. computer controls are decided from the current unit state
    // 4. units in slot order: drive, then weapon fire (new shells wait a tick)
    // 5. pickups: unit contact collection, then respawn timers
    // 6. match state, squad liveness and respawn scheduling
    // 7. due respawns are spawned
    // 8. the game tick advances unless the match is paused
    void step(const SessionInput& input);

    [[nodiscard]] std::span<const GameplayEvent> gameplay_events() const noexcept { return events_.events(); }
    [[nodiscard]] std::span<const SoundEvent> sound_events() const noexcept { return sounds_.events(); }
    [[nodiscard]] std::span<const EffectEvent> effect_events() const noexcept { return effects_.events(); }
    void clear_events() noexcept {
        events_.clear();
        sounds_.clear();
        effects_.clear();
        objective_event_cursor_=0U;
    }

    [[nodiscard]] std::int32_t tick() const noexcept { return tick_; }
    [[nodiscard]] const UnitPool& units() const noexcept { return units_; }
    [[nodiscard]] UnitPool& units() noexcept { return units_; }
    [[nodiscard]] const WeaponProjectilePool& projectiles() const noexcept {
        return weapon_state_.projectiles;
    }
    [[nodiscard]] const std::array<OwnerRecord,owner_capacity>& owners() const noexcept { return owners_; }
    [[nodiscard]] const MissionState& mission() const noexcept { return mission_; }
    [[nodiscard]] MissionState& mission() noexcept { return mission_; }
    [[nodiscard]] const RetailRandom& rng() const noexcept { return rng_; }
    [[nodiscard]] RetailRandom& rng() noexcept { return rng_; }
    [[nodiscard]] const ObjectiveRuntime* objective() const noexcept {
        return objective_ ? &*objective_ : nullptr;
    }
    [[nodiscard]] std::optional<EntityId> owner_unit(std::uint8_t owner) const;

    // Explicit big-endian serialization of all simulation state.
    void serialize(StateWriter& writer) const;
    [[nodiscard]] std::string checksum() const;

private:
    CombatContext combat_context();
    void update_units(const SessionInput& input);
    void release_destroyed_units();
    void spawn_due_respawns();
    void update_pickup_state();
    void update_spawners();
    void update_turrets();
    void attach_turret_collision(WeaponContext& context);
    void initialize_objective();
    void update_objective();

    SessionConfig config_;
    AngleTable angles_;
    UnitMotion motion_;
    WorldCollision world_;
    RoutePlanner route_planner_;
    UnitPool units_;
    WeaponRuntimeState weapon_state_;
    std::array<OwnerRecord,owner_capacity> owners_{};
    std::array<bool,owner_capacity> objective_bases_{};
    std::array<std::optional<RespawnPoint>,owner_capacity> respawn_points_{};
    std::vector<PickupRecord> pickups_;
    struct SessionSpawner {
        SpawnerState state;
        std::optional<LinkedEffectView> linked;
    };
    static constexpr std::size_t spawner_capacity=256U;
    std::vector<SessionSpawner> spawners_;
    std::vector<TurretRecord> turrets_;
    std::array<TurretOwnerConfig,owner_capacity> turret_owner_config_{};
    std::array<std::int32_t,owner_capacity> spawner_budgets_{};
    std::array<bool,owner_capacity> spawner_gates_{};
    std::optional<PickupWeights> pickup_weights_;
    MissionState mission_;
    std::optional<ObjectiveRuntime> objective_;
    CampaignAiController campaign_ai_;
    RetailRandom rng_;
    GameplayEventQueue events_{1024U};
    SoundEventQueue sounds_{1024U};
    EffectEventQueue effects_{1024U};
    std::size_t objective_event_cursor_{};
    std::int32_t tick_{};
};

} // namespace battletanx
