#pragma once

#include "battletanx/gameplay.hpp"

#include <array>
#include <cstdint>

namespace battletanx {

enum class HostButton : std::size_t {
    face_south,
    face_west,
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
    std::array<bool,static_cast<std::size_t>(HostButton::count)> buttons{};
};

ControllerPad map_host_controller(const HostControllerState& state);

} // namespace battletanx
