// The XInput-shaped pad state: framework-free behaviours.
//
// The pure layer is small by design: anything in this file must stay buildable
// without SDL, because the headless test binary is what CI runs on every pull
// request. Hot-plug scenarios are exercised through publish_sample on synthetic
// slot state; the SDL-facing capture path is not exercised here because SDL
// device events cannot be synthesized without a device.

#include "isaec/engine/input.hpp"

#include "test_harness.hpp"

using namespace isaec;

namespace {

engine::GamepadState sample(u16 buttons = 0, i16 lx = 0, i16 ly = 0, i16 rx = 0, i16 ry = 0,
                            u8 lt = 0, u8 rt = 0) {
    engine::GamepadState raw;
    raw.w_buttons = buttons;
    raw.s_thumb_lx = lx;
    raw.s_thumb_ly = ly;
    raw.s_thumb_rx = rx;
    raw.s_thumb_ry = ry;
    raw.b_left_trigger = lt;
    raw.b_right_trigger = rt;
    return raw;
}

ISAEC_TEST(input_default_state_is_zero) {
    const engine::GamepadState state;
    CHECK(state.dw_packet_number == 0);
    CHECK(state.w_buttons == 0);
    CHECK(state.s_thumb_lx == 0);
    CHECK(state.s_thumb_ly == 0);
    CHECK(state.s_thumb_rx == 0);
    CHECK(state.s_thumb_ry == 0);
    CHECK(state.b_left_trigger == 0);
    CHECK(state.b_right_trigger == 0);
}

ISAEC_TEST(input_publish_bumps_packet_on_change_and_not_otherwise) {
    engine::GamepadState slots[engine::kMaxGamepads] = {};
    const engine::GamepadState first = sample(0x1000);

    // First publish always publishes: the slot starts with packet 0.
    engine::publish_sample(slots, 0, first);
    CHECK(slots[0].dw_packet_number == 1);
    CHECK(slots[0].w_buttons == 0x1000);

    // Republishing the identical sample does not bump the packet.
    engine::publish_sample(slots, 0, first);
    CHECK(slots[0].dw_packet_number == 1);

    // A real change bumps again.
    engine::publish_sample(slots, 0, sample(0x2000));
    CHECK(slots[0].dw_packet_number == 2);
    CHECK(slots[0].w_buttons == 0x2000);

    // gamepad_state_changed sees observable fields, not the packet number.
    CHECK(engine::gamepad_state_changed(slots[0], slots[0]) == false);
}

ISAEC_TEST(input_publish_rejects_out_of_range_slot) {
    engine::GamepadState slots[engine::kMaxGamepads] = {};
    engine::publish_sample(slots, engine::kMaxGamepads, sample(0x1000));
    CHECK(slots[0].dw_packet_number == 0);
    CHECK(slots[0].w_buttons == 0);
}

ISAEC_TEST(input_deadzone_zeroes_small_axes_and_passes_large_ones) {
    engine::GamepadState raw = sample();

    raw.s_thumb_lx = 7849;   // at-or-below the deadzone reads as zero
    engine::apply_thumb_deadzone(raw);
    CHECK(raw.s_thumb_lx == 0);

    raw.s_thumb_lx = -7849;
    engine::apply_thumb_deadzone(raw);
    CHECK(raw.s_thumb_lx == 0);

    raw.s_thumb_ly = 7850;   // one past the deadzone passes through
    engine::apply_thumb_deadzone(raw);
    CHECK(raw.s_thumb_ly == 7850);

    raw.s_thumb_ry = -7850;
    engine::apply_thumb_deadzone(raw);
    CHECK(raw.s_thumb_ry == -7850);
}

ISAEC_TEST(input_publish_applies_deadzone_before_compare) {
    engine::GamepadState slots[engine::kMaxGamepads] = {};
    const engine::GamepadState first = sample(0, 100, 0, 0, 0);  // inside the deadzone

    engine::publish_sample(slots, 0, first);
    CHECK(slots[0].dw_packet_number == 1);
    CHECK(slots[0].s_thumb_lx == 0);

    // Same sample again is identical after the deadzone — no bump.
    engine::publish_sample(slots, 0, first);
    CHECK(slots[0].dw_packet_number == 1);
}

ISAEC_TEST(input_axis_to_i16_clamps) {
    CHECK(engine::axis_to_i16(0) == 0);
    CHECK(engine::axis_to_i16(32767) == 32767);
    CHECK(engine::axis_to_i16(-32768) == -32768);
    CHECK(engine::axis_to_i16(40000) == 32767);
    CHECK(engine::axis_to_i16(-40000) == -32768);
}

ISAEC_TEST(input_axis_to_u8_trigger_maps_range) {
    CHECK(engine::axis_to_u8_trigger(-1) == 0);       // negatives clamp to zero
    CHECK(engine::axis_to_u8_trigger(0) == 0);
    CHECK(engine::axis_to_u8_trigger(32767) == 255);
    CHECK(engine::axis_to_u8_trigger(40000) == 255);  // above range clamps
    CHECK(engine::axis_to_u8_trigger(16383) == 127);  // ~half-scale, rounded
}

ISAEC_TEST(input_button_bits_match_xinput_layout) {
    using namespace isaec::engine;
    CHECK(GamepadButton::kDpadUp == 0x0001);
    CHECK(GamepadButton::kDpadDown == 0x0002);
    CHECK(GamepadButton::kDpadLeft == 0x0004);
    CHECK(GamepadButton::kDpadRight == 0x0008);
    CHECK(GamepadButton::kStart == 0x0010);
    CHECK(GamepadButton::kBack == 0x0020);
    CHECK(GamepadButton::kLeftThumb == 0x0040);
    CHECK(GamepadButton::kRightThumb == 0x0080);
    CHECK(GamepadButton::kLeftShoulder == 0x0100);
    CHECK(GamepadButton::kRightShoulder == 0x0200);
    CHECK(GamepadButton::kA == 0x1000);
    CHECK(GamepadButton::kB == 0x2000);
    CHECK(GamepadButton::kX == 0x4000);
    CHECK(GamepadButton::kY == 0x8000);
}

}  // namespace
