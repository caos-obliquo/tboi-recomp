// XInput-shaped controller state, engine layer. See include/isaec/engine/input.hpp.
//
// The pure layer: nothing here talks to a window system or a socket. The
// SDL2 backend lives in src/platform/sdl_input.cpp and feeds this layer
// through publish_sample(). Keeping it pure means it is unit-testable without
// a display, which is what makes it safe to run on CI.

#include "isaec/engine/input.hpp"

#include <cstdlib>

namespace isaec::engine {

namespace {

std::uint32_t magnitude16(i32 axis) noexcept {
    return static_cast<std::uint32_t>(axis < 0 ? -axis : axis);
}

}  // namespace

bool gamepad_state_changed(const GamepadState& a, const GamepadState& b) noexcept {
    return a.w_buttons != b.w_buttons || a.b_left_trigger != b.b_left_trigger ||
           a.b_right_trigger != b.b_right_trigger || a.s_thumb_lx != b.s_thumb_lx ||
           a.s_thumb_ly != b.s_thumb_ly || a.s_thumb_rx != b.s_thumb_rx ||
           a.s_thumb_ry != b.s_thumb_ry;
}

// The deadzone is the same value the XInput headers publish
// (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE == 7849), applied per axis. The retail
// handling of small deflections above the deadzone is not recoverable from
// the binary, so the magnitude is passed through unchanged: zeroing only
// at-or-below 7849 is the documented floor, nothing more is invented here.
void apply_thumb_deadzone(GamepadState& state) noexcept {
    const auto deadzone = static_cast<std::uint32_t>(kThumbDeadzone);
    if (magnitude16(state.s_thumb_lx) <= deadzone) state.s_thumb_lx = 0;
    if (magnitude16(state.s_thumb_ly) <= deadzone) state.s_thumb_ly = 0;
    if (magnitude16(state.s_thumb_rx) <= deadzone) state.s_thumb_rx = 0;
    if (magnitude16(state.s_thumb_ry) <= deadzone) state.s_thumb_ry = 0;
}

void publish_sample(GamepadState slots[kMaxGamepads], usize index, const GamepadState& raw) noexcept {
    if (slots == nullptr || index >= kMaxGamepads) return;
    GamepadState sampled = raw;
    apply_thumb_deadzone(sampled);
    GamepadState& slot = slots[index];
    const bool changed = slot.dw_packet_number == 0 || gamepad_state_changed(slot, sampled);
    if (changed) {
        const std::uint32_t previous = slot.dw_packet_number;
        slot = sampled;
        slot.dw_packet_number = previous + 1;
    }
}

i16 axis_to_i16(i32 axis) noexcept {
    if (axis > 32767) return 32767;
    if (axis < -32768) return -32768;
    return static_cast<i16>(axis);
}

u8 axis_to_u8_trigger(i32 axis) noexcept {
    if (axis <= 0) return 0;
    if (axis >= 32767) return 255;
    return static_cast<u8>((axis * 255 + 16383) / 32767);
}

}  // namespace isaec::engine
