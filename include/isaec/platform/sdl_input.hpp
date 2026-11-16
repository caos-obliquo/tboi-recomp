// The SDL2 backend for the engine's XInput-shaped controller state.
//
// The engine's pure layer (isaec/engine/input.hpp) owns the GamepadState
// ABI; this header exposes the SDL2 reader that produces it. The idioms here
// are adapted, not copied, from the Half-1 glue of the-browsing-of-isaac
// (chiikabu, GPL-3.0-only since PR #63; attribution in
// docs/reimplementation-references.md). No upstream code is lifted and no
// native/decomp lift output is present.
//
// Ownership: the backend performs its own poll-time scan of the joystick
// list on every call, so it never drains the window/renderer's SDL event
// queue. The owning loop keeps sole responsibility for SDL_PollEvent.

#ifndef ISAEC_PLATFORM_SDL_INPUT_HPP
#define ISAEC_PLATFORM_SDL_INPUT_HPP

#include "isaec/engine/input.hpp"
#include "isaec/platform/types.hpp"

#include <array>
#include <cstddef>

namespace isaec::platform {

// Reads up to engine::kMaxGamepads SDL_GameController devices into
// engine::GamepadState slots. Extra devices are ignored and the fact is
// documented in docs/reimplementation-references.md.
class SdlGamepadSource {
public:
    SdlGamepadSource() = default;
    ~SdlGamepadSource();
    SdlGamepadSource(const SdlGamepadSource&) = delete;
    SdlGamepadSource& operator=(const SdlGamepadSource&) = delete;

    // Re-scans joysticks (opens/closes slots to match), samples all four
    // pads and publishes through engine::publish_sample(). Call once per
    // frame from the loop owner. Safe with no gamepads attached.
    void poll();

    // The current four pad states.
    [[nodiscard]] const std::array<engine::GamepadState, engine::kMaxGamepads>& states() const noexcept {
        return states_;
    }

    // Requests force feedback on both rumble motors with the given intensity
    // (0..0xffff) and duration (ms). False when the pad is absent, closed, or
    // does not support rumble.
    [[nodiscard]] bool rumble(std::size_t slot, u16 left_motor, u16 right_motor, u32 duration_ms);

private:
    void close_all();
    void zero_and_publish(std::size_t slot);

    std::array<void*, engine::kMaxGamepads> pads_{};  // SDL_GameController* opaque
    std::array<engine::GamepadState, engine::kMaxGamepads> states_{};
    bool initialized_ = false;
};

}  // namespace isaec::platform
#endif
