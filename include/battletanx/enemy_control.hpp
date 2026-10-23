#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/deterministic_rng.hpp"
#include "battletanx/entity_id.hpp"
#include "battletanx/gameplay.hpp"
#include "battletanx/unit_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace battletanx {

// ---------------------------------------------------------------------------
// Retail constants (USA Rev 0).
namespace enemy_retail {
// find_nearest_enemy_unit (func_800A1170): initial best (D_80072C78) and the
// range scale applied to range^2 (D_80072C7C).
inline constexpr float nearest_initial_best=1.0e8F;
inline constexpr float nearest_range_scale=0.92F;
// entity_is_targetable (func_8009FB8C): now - 0x5A >= spawn stamp.
inline constexpr std::int32_t spawn_grace_ticks=retail::target_grace_ticks;
// ai_update_unit_drive_controls (func_800E2DE8): alignment cone per kind.
inline constexpr std::int32_t drive_cone_tank=0x1000;
inline constexpr std::int32_t drive_cone_other=0x2000;
inline constexpr float drive_far_distance=16000.0F; // D_800745D4
inline constexpr float drive_blend_distance=40.0F;  // D_800745D8
inline constexpr float drive_turn_scale=200.0F;     // D_800745E8
inline constexpr float drive_throttle_scale=80.0F;  // D_800745EC
inline constexpr float drive_speed_divisor=16.0F;   // D_800745F0
// ai_steer_toward_target (func_800E2AF0).
inline constexpr std::int32_t aim_saturation=600;
inline constexpr std::int32_t fire_aim_window=400;
inline constexpr float aim_hull_scale=40.0F;
inline constexpr float aim_turret_scale=135.0F;
// ai_update_unit_command (func_800E30F8): aim point "in range" test.
inline constexpr float engagement_range=500.0F;
// func_800E21E4: special-weapon chance ramps (D_80074504..D_80074598).
inline constexpr float chance_vs_class4_slope=0.5F;
inline constexpr float chance_vs_class4_base=0.25F;
inline constexpr float chance_class4_slope=0.65F;
inline constexpr float chance_class4_base=0.10F;
inline constexpr std::uint8_t special_slot_vs_class4=5U;
inline constexpr std::uint8_t special_slot_class4=0U;
inline constexpr std::int32_t ticks_per_minute=30*60;
// Unit +0x14 bit tested as "flags & 4" by the AI command paths.
inline constexpr std::uint16_t flag_ai_class4=0x0004U;
} // namespace enemy_retail

// ---------------------------------------------------------------------------
// Target acquisition.

// func_800A1170. Squared XZ distance with strict '<' in slot order; skips own
// team, dead units and units inside the spawn grace; rejects a best beyond
// (int)(max_range^2 * 0.92). Returns no_entity when nothing qualifies.
[[nodiscard]] EntityId find_nearest_enemy_unit(const UnitPool& units,EntityId self,
                                               std::int32_t tick,float max_range);

// entity_is_targetable (func_8009FB8C), reduced to the recovered fields.
[[nodiscard]] bool unit_is_targetable(const UnitRecord& unit,std::int32_t tick) noexcept;

// ai_update_goal mode 1 invalidation: stale handle, self, dead, same team or
// not targetable -> false.
[[nodiscard]] bool target_still_valid(const UnitPool& units,EntityId self,EntityId target,
                                      std::int32_t tick);

// ---------------------------------------------------------------------------
// Drive law (func_800E2DE8).

struct EnemyDriveTuning {
    // Owner +0x31C. Default 16: owner speed field not recovered for campaign
    // tanks; 16 makes the speed/16 factor equal to 1.
    float speed{16.0F};
    // Unit +0x35E "close range". Default 0: per-unit value not recovered, so
    // the unit never parks inside the drive law.
    float close_range{0.0F};
};

// tracker at the DriveUnit record: previous horizontal distance and the
// latched "approaching" flag.
struct DriveTracker {
    float distance{};
    float delta{};
    bool approaching{};
};

// Signed binangle error target - heading (65536 = 360 degrees) from a unit
// to a point, under forward = (sin h, cos h).
[[nodiscard]] std::int32_t heading_error_to(float heading_degrees,float from_x,float from_z,
                                            float to_x,float to_z) noexcept;

// Throttle and hull rotation toward (goal_x, goal_z). When tracker is null
// the one-tick-lagged approach blend is not applied.
[[nodiscard]] DriveInput compute_enemy_drive(const UnitRecord& self,float goal_x,float goal_z,
                                             const AngleTable& angles,
                                             const EnemyDriveTuning& tuning,
                                             DriveTracker* tracker=nullptr);

// ---------------------------------------------------------------------------
// Aim law (func_800E2AF0).

struct EnemyAim {
    std::int32_t angle_error{};  // signed binangle error
    float hull_rotation{};       // tanks: written to the hull axis (rate*40)
    float aim{};                 // other kinds: written to the aim axis (rate*135)
    bool in_fire_window{};       // |error| < 400
};

// Tanks (no aim axis, turret follows the hull) measure from the hull heading
// and produce hull_rotation; other kinds measure from the turret and produce aim.
[[nodiscard]] EnemyAim compute_enemy_aim(const UnitRecord& self,float target_x,float target_z);

// ---------------------------------------------------------------------------
// Fire decision (func_800E21E4).

struct EnemyFirePolicy {
    // Owner +0x320/+0x324/+0x328 rate * 0.003333 / 16 against a normalized
    // elapsed time. Default 30 ticks: owner rate fields and the time
    // normalization not recovered; 30 is the weapon-1 cooldown (D_80125EC0).
    std::int32_t cooldown_ticks{30};
    // level +0x21C mission length in minutes. Default 10: per-level value
    // not recovered.
    float mission_minutes{10.0F};
    // Default false: the special-weapon selection (func_8007C6D8 gate and the
    // per-weapon table) and the line-of-fire raycast (func_8010874C) are not
    // ported, so specials are never chosen and friendlies never block.
    bool allow_specials{false};
    // Required distance for the gate. Default 500: the aim-point range used
    // by func_800E30F8; retail does not range-check inside func_800E21E4.
    float engagement_range{enemy_retail::engagement_range};
};

struct FireDecision {
    bool fire{};
    bool special{};
    std::uint8_t special_slot{};
    bool stamp_last_fire{};      // retail writes unit +0x368 when not cooling
};

// Deterministic gate plus the recovered probabilistic branches. last_fire_tick
// is the unit +0x368 equivalent.
[[nodiscard]] FireDecision decide_enemy_fire(const UnitRecord& self,const UnitRecord& target,
                                             std::int32_t aim_error,float distance,
                                             std::int32_t tick,std::int32_t last_fire_tick,
                                             const EnemyFirePolicy& policy,RetailRandom& rng);

// ---------------------------------------------------------------------------
// Controller.

enum class EnemyGoalPolicy : std::uint8_t {
    // The retail scorer (func_800E3CF0) and play-stack scripts are not
    // ported; this temporary policy chases the nearest enemy.
    chase_nearest_enemy_temporary,
};

struct EnemyCommand {
    EntityId unit{};
    EntityId target{};
    DriveInput input{};
    bool fire_decided{};
};

struct EnemyControllerConfig {
    EnemyGoalPolicy goal_policy{EnemyGoalPolicy::chase_nearest_enemy_temporary};
    // Acquisition range. Default 16000: the drive far distance; the runtime
    // cull range D_80125858 is not recovered.
    float acquire_range{enemy_retail::drive_far_distance};
    // Hold the hull and only aim inside engagement_range (func_800E30F8 tank
    // path; the u34A/expected-state gate is not ported).
    bool park_in_engagement_range{true};
    EnemyDriveTuning drive{};
    EnemyFirePolicy fire{};
};

class EnemyController {
public:
    explicit EnemyController(EnemyControllerConfig config={}) noexcept : config_(config) {}

    // Computes commands for each live computer-controlled unit in slot order.
    // Does not modify the pool. Returns the number of commands written
    // (at most out.size()).
    std::size_t update(const UnitPool& units,const AngleTable& angles,RetailRandom& rng,
                       std::int32_t tick,std::span<EnemyCommand> out);

    [[nodiscard]] EntityId target_of(std::size_t slot) const noexcept {
        return slot<unit_capacity ? state_[slot].target : no_entity;
    }
    [[nodiscard]] const EnemyControllerConfig& config() const noexcept { return config_; }

private:
    struct SlotState {
        EntityId unit{};
        EntityId target{};
        DriveTracker tracker{};
        std::int32_t last_fire_tick{retail::never_fired_tick};
    };
    EnemyControllerConfig config_;
    std::array<SlotState,unit_capacity> state_{};
};

inline std::size_t update_enemy_controls(EnemyController& controller,const UnitPool& units,
                                         const AngleTable& angles,RetailRandom& rng,
                                         std::int32_t tick,std::span<EnemyCommand> out) {
    return controller.update(units,angles,rng,tick,out);
}

} // namespace battletanx
