#pragma once

#include "battletanx/entity_id.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {

enum class GameplayEventKind : std::uint8_t {
    weapon_fired,
    weapon_empty,
    projectile_impact,
    unit_damaged,
    unit_destroyed,
    unit_spawned,
    pickup_collected,
    player_respawned,
    mission_succeeded,
    mission_failed,
};

// Every reference is an explicit ID; no event carries a host pointer.
struct GameplayEvent {
    GameplayEventKind kind{};
    std::int32_t tick{};
    EntityId subject{};     // unit, projectile or pickup the event is about
    EntityId other{};       // attacker, collector or impacted unit
    std::uint8_t owner{0xFFU};
    std::int32_t value{};   // damage, health remaining, pickup amount, ...
    float x{}, y{}, z{};
};

struct SoundEvent {
    std::uint16_t sound_id{};
    float x{}, y{}, z{};
    EntityId source{};
    bool positional{};      // true: retail positional call (func_80085C68 / func_80085B4C)
    bool local{};           // true: emitted for a locally controlled unit
};

// Ordered, bounded event queue. Events past capacity are counted rather than
// stored so overflow is deterministic and observable.
template <typename Event>
class EventQueue {
public:
    explicit EventQueue(std::size_t capacity=256U) : capacity_(capacity) { events_.reserve(capacity); }
    void push(const Event& event) {
        if (events_.size()>=capacity_) { ++dropped_; return; }
        events_.push_back(event);
    }
    [[nodiscard]] std::span<const Event> events() const noexcept { return events_; }
    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }
    void clear() noexcept { events_.clear(); dropped_=0U; }

private:
    std::size_t capacity_;
    std::vector<Event> events_;
    std::size_t dropped_{};
};

using GameplayEventQueue=EventQueue<GameplayEvent>;
using SoundEventQueue=EventQueue<SoundEvent>;

} // namespace battletanx
