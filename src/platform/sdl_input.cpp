// The SDL2 backend for the engine's XInput-shaped controller state.
// See include/isaec/platform/sdl_input.hpp for the contract.
//
// Adapted, not copied, from the Half-1 glue of the-browsing-of-isaac
// (chiikabu, GPL-3.0-only since PR #63; attribution in
// docs/reimplementation-references.md). The adaptation keeps the contract in
// the engine's pure layer and lets this file stay a thin adapter; no upstream
// source is included.

#include "isaec/platform/sdl_input.hpp"

#include <SDL.h>

#include <cstdio>

namespace isaec::platform {

namespace {

engine::GamepadState zero_state() { return engine::GamepadState{}; }

}  // namespace

SdlGamepadSource::~SdlGamepadSource() { close_all(); }

void SdlGamepadSource::close_all() {
    for (std::size_t i = 0; i < engine::kMaxGamepads; ++i) {
        if (pads_[i] != nullptr) {
            SDL_GameControllerClose(static_cast<SDL_GameController*>(pads_[i]));
            pads_[i] = nullptr;
        }
    }
}

void SdlGamepadSource::zero_and_publish(std::size_t slot) {
    const engine::GamepadState raw = zero_state();
    engine::publish_sample(states_.data(), slot, raw);
}

void SdlGamepadSource::poll() {
    if (!initialized_) {
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
                std::fprintf(stderr, "sdl: input subsystem: %s\n", SDL_GetError());
                return;
            }
        }
        initialized_ = true;
    }

    const int joystick_count = SDL_NumJoysticks();
    if (joystick_count < 0) return;

    // 1) drop slots whose controller is gone.
    for (std::size_t slot = 0; slot < engine::kMaxGamepads; ++slot) {
        SDL_GameController* pad = static_cast<SDL_GameController*>(pads_[slot]);
        if (pad == nullptr) continue;
        if (SDL_GameControllerGetAttached(pad) == SDL_FALSE) {
            SDL_GameControllerClose(pad);
            pads_[slot] = nullptr;
            zero_and_publish(slot);
        }
    }

    // 2) open new matching devices (XInput order), skip non-gamepads.
    for (int i = 0; i < joystick_count; ++i) {
        if (SDL_IsGameController(i) == SDL_FALSE) continue;
        const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(i);
        if (instance < 0) continue;
        bool known = false;
        for (std::size_t slot = 0; slot < engine::kMaxGamepads; ++slot) {
            SDL_GameController* pad = static_cast<SDL_GameController*>(pads_[slot]);
            if (pad != nullptr &&
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == instance) {
                known = true;
                break;
            }
        }
        if (known) continue;
        for (std::size_t slot = 0; slot < engine::kMaxGamepads; ++slot) {
            if (pads_[slot] != nullptr) continue;
            pads_[slot] = SDL_GameControllerOpen(i);
            break;
        }
    }

    // 3) sample open pads.
    for (std::size_t slot = 0; slot < engine::kMaxGamepads; ++slot) {
        SDL_GameController* pad = static_cast<SDL_GameController*>(pads_[slot]);
        if (pad == nullptr) continue;

        engine::GamepadState raw = zero_state();
        const SDL_GameControllerButton buttons[] = {
            SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
            SDL_CONTROLLER_BUTTON_START,   SDL_CONTROLLER_BUTTON_BACK,
            SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
            SDL_CONTROLLER_BUTTON_A,       SDL_CONTROLLER_BUTTON_B,
            SDL_CONTROLLER_BUTTON_X,       SDL_CONTROLLER_BUTTON_Y,
        };
        const u16 button_bits[] = {
            engine::GamepadButton::kDpadUp, engine::GamepadButton::kDpadDown,
            engine::GamepadButton::kDpadLeft, engine::GamepadButton::kDpadRight,
            engine::GamepadButton::kStart, engine::GamepadButton::kBack,
            engine::GamepadButton::kLeftThumb, engine::GamepadButton::kRightThumb,
            engine::GamepadButton::kLeftShoulder, engine::GamepadButton::kRightShoulder,
            engine::GamepadButton::kA, engine::GamepadButton::kB,
            engine::GamepadButton::kX, engine::GamepadButton::kY,
        };
        for (int button_index = 0; button_index < 14; ++button_index) {
            if (SDL_GameControllerGetButton(pad, buttons[button_index]) != 0) {
                raw.w_buttons |= button_bits[button_index];
            }
        }

        raw.s_thumb_lx = engine::axis_to_i16(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
        raw.s_thumb_ly = engine::axis_to_i16(-SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
        raw.s_thumb_rx = engine::axis_to_i16(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
        raw.s_thumb_ry = engine::axis_to_i16(-SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
        raw.b_left_trigger = engine::axis_to_u8_trigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        raw.b_right_trigger = engine::axis_to_u8_trigger(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

        engine::publish_sample(states_.data(), slot, raw);
    }
}

bool SdlGamepadSource::rumble(std::size_t slot, u16 left_motor, u16 right_motor, u32 duration_ms) {
    if (slot >= engine::kMaxGamepads) return false;
    SDL_GameController* pad = static_cast<SDL_GameController*>(pads_[slot]);
    if (pad == nullptr) return false;
    return SDL_GameControllerRumble(pad, left_motor, right_motor, duration_ms) == 0;
}

}  // namespace isaec::platform
