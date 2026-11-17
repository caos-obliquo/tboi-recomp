#include "isaec/engine/sandbox.hpp"

#include <algorithm>
#include <cmath>

namespace isaec::engine {

namespace {

// XInput thumbs rescale into [-1, 1] against their positive full scale.
constexpr f32 kThumbRange = 32767.0f;
inline constexpr f32 kDiagonalScale = 0.7071067811865476f;  // 1 / sqrt(2)

[[nodiscard]] f32 clamp_axis(f32 value) noexcept {
    return std::clamp(value, -1.0f, 1.0f);
}

}  // namespace

SandboxMove sandbox_axes(bool left, bool right, bool up, bool down,
                         const GamepadState& pad) noexcept {
    const f32 key_x = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    const f32 key_y = (down ? 1.0f : 0.0f) - (up ? 1.0f : 0.0f);

    // A diagonal keyboard pair is normalized to unit length so diagonals are
    // not sqrt(2) faster than cardinals.
    f32 keyboard_x = key_x;
    f32 keyboard_y = key_y;
    if (keyboard_x != 0.0f && keyboard_y != 0.0f) {
        keyboard_x *= kDiagonalScale;
        keyboard_y *= kDiagonalScale;
    }

    // states() arrive already deadzoned by publish_sample; Y flips from
    // XInput up-positive to screen down-positive.
    const f32 pad_x = clamp_axis(static_cast<f32>(pad.s_thumb_lx) / kThumbRange);
    const f32 pad_y = -clamp_axis(static_cast<f32>(pad.s_thumb_ly) / kThumbRange);

    SandboxMove move{};
    move.axis_x = clamp_axis(keyboard_x + pad_x);
    move.axis_y = clamp_axis(keyboard_y + pad_y);
    return move;
}

SandboxPos sandbox_step(SandboxPos pos, const SandboxMove& move,
                        const SandboxBounds& bounds, f32 entity_size,
                        f32 speed_px_per_sec, f32 dt_seconds) noexcept {
    const f32 half = entity_size * 0.5f;
    const f32 step = speed_px_per_sec * (dt_seconds > 0.0f ? dt_seconds : 0.0f);

    SandboxPos next{};
    next.x = pos.x + move.axis_x * step;
    next.y = pos.y + move.axis_y * step;

    // Keep the whole entity inside the room. A room smaller than the entity
    // is centered rather than clamped into an inverted range.
    const f32 min_x = bounds.x + half;
    const f32 max_x = bounds.x + bounds.width - half;
    const f32 min_y = bounds.y + half;
    const f32 max_y = bounds.y + bounds.height - half;

    if (min_x > max_x) {
        next.x = bounds.x + bounds.width * 0.5f;
    } else {
        next.x = std::clamp(next.x, min_x, max_x);
    }
    if (min_y > max_y) {
        next.y = bounds.y + bounds.height * 0.5f;
    } else {
        next.y = std::clamp(next.y, min_y, max_y);
    }
    return next;
}

}  // namespace isaec::engine
