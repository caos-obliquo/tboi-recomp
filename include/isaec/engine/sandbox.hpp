// White-box playtesting sandbox: the pure movement and input math.
//
// This is the platform-independent core of the playtesting box the owner
// mandated on 2026-11-17 (#79): a controllable entity in an empty room,
// drawn by the SDL2 white-box slice. It knows nothing about SDL, the
// window, or the engine lifecycle, so it is covered by unit tests on every
// CI run and stays buildable on a host with no display.
//
// Conventions
// -----------
// Screen coordinates: +x right, +y down. The room boundary is an axis-
// aligned rectangle in pixels; the entity is a square whose position is its
// center, and the entity must remain fully inside the room. Movement axes
// are in [-1, 1]. Speeds are pixels per second; dt is seconds.
//
// Keyboard digits are normalized so a diagonal move is not sqrt(2) faster
// than a cardinal one. Pad thumbsticks are XInput-shaped (up-positive Y);
// the caller applies the engine's radial deadzone policy
// (apply_thumb_deadzone) before combining, and this layer flips Y into the
// screen convention. Combined axes are clamped to [-1, 1].
//
// This file is deliberately not a port of any existing game's movement
// code. It is the simplest controllable-entity model that satisfies the
// owner's mandate: platform-independent, tested, and honest about the fact
// that the loaded content is empty by construction.

#ifndef ISAEC_ENGINE_SANDBOX_HPP
#define ISAEC_ENGINE_SANDBOX_HPP

#include "isaec/engine/input.hpp"
#include "isaec/platform/types.hpp"

namespace isaec::engine {

// A movement vector. Screen coordinates: +y is down. Both axes in [-1, 1].
struct SandboxMove {
    f32 axis_x = 0.0f;
    f32 axis_y = 0.0f;
};

// Inner playable rectangle, in pixels, screen coordinates.
struct SandboxBounds {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
};

// Entity position (its center) after a step.
struct SandboxPos {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

// Combine a digital keyboard state and one pad sample into one move vector.
//
// `pad` thumb axes are taken pre-deadzoneed (caller runs apply_thumb_deadzone)
// and are rescaled from the XInput i16 range into [-1, 1]; Y is flipped so a
// player pushing the stick up produces a negative axis_y (screen up).
// Keyboard contributes ±1 per axis with diagonals normalized to unit length.
// The two sources add and the sum clamps to [-1, 1] per axis.
[[nodiscard]] SandboxMove sandbox_axes(bool left, bool right, bool up, bool down,
                                       const GamepadState& pad) noexcept;

// Advance `pos` by `move * speed_px_per_sec * dt_seconds`, clamped so the
// whole entity (a square of `entity_size` pixels) stays inside `bounds`.
// A zero move, a zero dt, or a negative speed all produce the input position,
// clamped.
[[nodiscard]] SandboxPos sandbox_step(SandboxPos pos, const SandboxMove& move,
                                      const SandboxBounds& bounds, f32 entity_size,
                                      f32 speed_px_per_sec, f32 dt_seconds) noexcept;

}  // namespace isaec::engine

#endif  // ISAEC_ENGINE_SANDBOX_HPP
