#pragma once

#include "battletanx/gameplay.hpp"

#include <array>
#include <cstdint>

namespace battletanx {

enum class HostButton : std::size_t {
    face_south,
    face_west,
    face_east,
    start,
    dpad_up,
    dpad_down,
    dpad_left,
    dpad_right,
    left_shoulder,
    right_shoulder,
    count,
};

struct HostControllerState {
    std::int16_t left_x{};
    std::int16_t left_y{};
    std::int16_t right_x{};
    std::int16_t right_y{};
    std::int16_t left_trigger{};
    std::int16_t right_trigger{};
    std::array<bool,static_cast<std::size_t>(HostButton::count)> buttons{};
};

struct HostInputOptions {
    float deadzone{6000.0F/32767.0F};
    float sensitivity{1.0F};
};

ControllerPad map_host_controller(const HostControllerState& state,
                                  HostInputOptions options={});

} // namespace battletanx
