#include "battletanx/host_input.hpp"

#include <algorithm>

namespace battletanx {
namespace {
constexpr std::uint16_t button_a=0x8000U;
constexpr std::uint16_t button_b=0x4000U;
constexpr std::uint16_t button_z=0x2000U;
constexpr std::uint16_t button_start=0x1000U;
constexpr std::uint16_t button_d_up=0x0800U;
constexpr std::uint16_t button_d_down=0x0400U;
constexpr std::uint16_t button_d_left=0x0200U;
constexpr std::uint16_t button_d_right=0x0100U;
constexpr std::uint16_t button_l=0x0020U;
constexpr std::uint16_t button_r=0x0010U;
constexpr std::uint16_t button_c_up=0x0008U;
constexpr std::uint16_t button_c_down=0x0004U;
constexpr std::uint16_t button_c_left=0x0002U;
constexpr std::uint16_t button_c_right=0x0001U;
constexpr int stick_deadzone=6000;
constexpr int trigger_threshold=16000;
constexpr int c_button_threshold=16000;

std::int8_t scale_axis(int value) {
    value=std::clamp(value,-32767,32767);
    if (value>=-stick_deadzone && value<=stick_deadzone) return 0;
    return static_cast<std::int8_t>(value*127/32767);
}

bool pressed(const HostControllerState& state,HostButton button) {
    return state.buttons[static_cast<std::size_t>(button)];
}
} // namespace

ControllerPad map_host_controller(const HostControllerState& state) {
    ControllerPad result{};
    result.stick_x=scale_axis(state.left_x);
    result.stick_y=scale_axis(-static_cast<int>(state.left_y));
    if (pressed(state,HostButton::face_south)) result.buttons|=button_a;
    if (pressed(state,HostButton::face_west) || pressed(state,HostButton::face_east)) {
        result.buttons|=button_b;
    }
    if (state.left_trigger>trigger_threshold || state.right_trigger>trigger_threshold) {
        result.buttons|=button_z;
    }
    if (pressed(state,HostButton::start)) result.buttons|=button_start;
    if (pressed(state,HostButton::dpad_up)) result.buttons|=button_d_up;
    if (pressed(state,HostButton::dpad_down)) result.buttons|=button_d_down;
    if (pressed(state,HostButton::dpad_left)) result.buttons|=button_d_left;
    if (pressed(state,HostButton::dpad_right)) result.buttons|=button_d_right;
    if (pressed(state,HostButton::left_shoulder)) result.buttons|=button_l;
    if (pressed(state,HostButton::right_shoulder)) result.buttons|=button_r;
    if (state.right_y<-c_button_threshold) result.buttons|=button_c_up;
    if (state.right_y>c_button_threshold) result.buttons|=button_c_down;
    if (state.right_x<-c_button_threshold) result.buttons|=button_c_left;
    if (state.right_x>c_button_threshold) result.buttons|=button_c_right;
    return result;
}

} // namespace battletanx
