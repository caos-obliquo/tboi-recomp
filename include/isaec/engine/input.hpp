// XInput-shaped controller state.
//
// Reimplemented from scratch for this engine. The shape (four pads, 16-bit
// signed thumbsticks with up-positive Y, triggers 0..255, a 16-bit wButtons
// bitmask and a dwPacketNumber that increments on change) follows the
// public XInput API, which is what mod authors and the original game's
// controller layer were shaped against. Not a copy of any source file.
//
// Layout and deadzones
// --------------------
// sThumb* are signed 16-bit, up-positive (XInput convention: +Y is up).
// bLeftTrigger / bRightTrigger are unsigned 0..255.
// wButtons is 16 bit; the bit values are named individually below.
// dwPacketNumber is 0 until the first state is published; it increments
// whenever any observable value changes, so a caller can cheaply poll and
// always know whether the pad moved.
//
// Deadzone policy: 7849 (XInput-equivalent, ~24%) applied per axis by
// magnitude: a thumb axis whose magnitude is at or below 7849 reads as zero;
// above it the raw value passes through unchanged (no invented shaping).
// Triggers and buttons are read verbatim; no SDL-level deadzone is applied.
// The policy is fixed in source and documented here, not configurable.
//
// Exactly four pad slots, XInput's USERINDEX_*. A fifth device appearing is
// ignored and reported nowhere; nothing in the original accepts more.
//
// Legal: the controller mapping design follows the XInput public ABI only.
// The glue idioms adapted from the-browsing-of-isaac's Half-1 scripts
// (GPL-3.0-only since PR #63, chiikabu) are acknowledged in
// docs/reimplementation-references.md. Half-2 engine lift code never
// enters this tree.

#ifndef ISAEC_ENGINE_INPUT_HPP
#define ISAEC_ENGINE_INPUT_HPP

#include "isaec/platform/types.hpp"

namespace isaec::engine {

// How many controller slots XInput exposes. More devices are ignored.
inline constexpr usize kMaxGamepads = 4;

// Thumb axis magnitude beyond which a stick is considered at rest.
// XInput-equivalent: 7849 / 32767 (~24%).
inline constexpr i32 kThumbDeadzone = 7849;

// XInput wButtons bitmask. Values match the public XInput ABI.
namespace GamepadButton {
inline constexpr u16 kDpadUp = 0x0001;
inline constexpr u16 kDpadDown = 0x0002;
inline constexpr u16 kDpadLeft = 0x0004;
inline constexpr u16 kDpadRight = 0x0008;
inline constexpr u16 kStart = 0x0010;
inline constexpr u16 kBack = 0x0020;
inline constexpr u16 kLeftThumb = 0x0040;
inline constexpr u16 kRightThumb = 0x0080;
inline constexpr u16 kLeftShoulder = 0x0100;
inline constexpr u16 kRightShoulder = 0x0200;
inline constexpr u16 kA = 0x1000;
inline constexpr u16 kB = 0x2000;
inline constexpr u16 kX = 0x4000;
inline constexpr u16 kY = 0x8000;
}  // namespace GamepadButton

// The XInput-shaped state for one pad.
struct GamepadState {
    u32 dw_packet_number = 0;
    u16 w_buttons = 0;
    u8 b_left_trigger = 0;
    u8 b_right_trigger = 0;
    // Signed 16-bit, up-positive (positive Y is up), XInput scale.
    i16 s_thumb_lx = 0;
    i16 s_thumb_ly = 0;
    i16 s_thumb_rx = 0;
    i16 s_thumb_ry = 0;
};

// True iff any observable field differs from `previous`, excluding
// dw_packet_number itself.
[[nodiscard]] bool gamepad_state_changed(const GamepadState& a,
                                         const GamepadState& previous) noexcept;

// Applies the radial deadzone policy to the two thumb pairs, in place.
void apply_thumb_deadzone(GamepadState& state) noexcept;

// Publishes `raw` (a freshly-read sample) as the state of slot `index`.
// Increments dw_packet_number of the slot iff the sample differs from the
// previous sample. Out-of-range slots are ignored.
void publish_sample(GamepadState slots[kMaxGamepads], usize index,
                    const GamepadState& raw) noexcept;

// Maps a raw SDL game-controller axis value (SDL_GetAxis: -32768..32767) to
// this engine's i16 thumb scale unchanged (clamped). SDL reports up as
// negative Y, so callers negate Y axes when publishing a GamepadState.
[[nodiscard]] i16 axis_to_i16(i32 axis) noexcept;

// Maps a raw SDL trigger axis value (0..32767 useful range) to 0..255 with
// rounding; negative clamped to zero.
[[nodiscard]] u8 axis_to_u8_trigger(i32 axis) noexcept;

// Rumble request: both motors, 0..65535 (XInput scale).
struct RumbleRequest {
    u16 left_motor = 0;
    u16 right_motor = 0;
};

}  // namespace isaec::engine

#endif  // ISAEC_ENGINE_INPUT_HPP
