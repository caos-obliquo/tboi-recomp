// White-box playtesting sandbox: pure movement and input math.
//
// Everything here is platform-independent and SDL-free on purpose: the test
// binary is what CI runs on every pull request. The windowed wiring in
// sdl_main.cpp is not exercised by these tests.

#include "isaec/engine/sandbox.hpp"

#include <cmath>

#include "test_harness.hpp"

using namespace isaec;

namespace {

bool approx(f32 a, f32 b) { return std::fabs(a - b) < 0.001f; }

engine::GamepadState pad(i16 lx = 0, i16 ly = 0) {
    engine::GamepadState raw;
    raw.s_thumb_lx = lx;
    raw.s_thumb_ly = ly;
    return raw;
}

ISAEC_TEST(sandbox_axes_keyboard_cardinals) {
    engine::SandboxMove m = engine::sandbox_axes(false, true, false, false, pad());
    CHECK(approx(m.axis_x, 1.0f));
    CHECK(approx(m.axis_y, 0.0f));

    m = engine::sandbox_axes(true, false, false, false, pad());
    CHECK(approx(m.axis_x, -1.0f));

    m = engine::sandbox_axes(false, false, false, true, pad());
    CHECK(approx(m.axis_y, 1.0f));

    m = engine::sandbox_axes(false, false, true, false, pad());
    CHECK(approx(m.axis_y, -1.0f));
}

ISAEC_TEST(sandbox_axes_keyboard_diagonal_is_unit_length) {
    const engine::SandboxMove m = engine::sandbox_axes(false, true, false, true, pad());
    CHECK(approx(std::hypot(m.axis_x, m.axis_y), 1.0f));
    CHECK(m.axis_x > 0.0f);
    CHECK(m.axis_y > 0.0f);
}

ISAEC_TEST(sandbox_axes_pad_is_rescaled_and_y_flipped) {
    // Stick pushed fully up (XInput up-positive) reads as negative axis_y
    // in screen coordinates (+y down).
    const engine::SandboxMove up = engine::sandbox_axes(false, false, false, false, pad(0, 32767));
    CHECK(approx(up.axis_y, -1.0f));

    const engine::SandboxMove half = engine::sandbox_axes(false, false, false, false, pad(16383, 0));
    CHECK(approx(half.axis_x, 0.5f));
    CHECK(approx(half.axis_y, 0.0f));
}

ISAEC_TEST(sandbox_axes_sources_add_and_clamp_to_one) {
    // Full keyboard right plus full stick right clamps to 1.
    const engine::SandboxMove both =
        engine::sandbox_axes(false, true, false, false, pad(32767, 0));
    CHECK(approx(both.axis_x, 1.0f));

    // Opposing sources cancel.
    const engine::SandboxMove cancel =
        engine::sandbox_axes(true, false, false, false, pad(32767, 0));
    CHECK(approx(cancel.axis_x, 0.0f));
}

ISAEC_TEST(sandbox_step_moves_by_dt_and_speed) {
    const engine::SandboxBounds bounds{0.0f, 0.0f, 200.0f, 200.0f};
    const engine::SandboxPos start{100.0f, 100.0f};
    const engine::SandboxMove right{1.0f, 0.0f};

    const engine::SandboxPos step = engine::sandbox_step(start, right, bounds, 20.0f, 100.0f, 0.5f);
    CHECK(approx(step.x, 150.0f));
    CHECK(approx(step.y, 100.0f));

    // Double the dt moves twice as far.
    const engine::SandboxPos step2 =
        engine::sandbox_step(start, right, bounds, 20.0f, 100.0f, 1.0f);
    CHECK(approx(step2.x, 200.0f - 10.0f));  // clamped to room edge
}

ISAEC_TEST(sandbox_step_entity_stays_fully_inside_room) {
    const engine::SandboxBounds bounds{10.0f, 20.0f, 100.0f, 80.0f};
    const engine::SandboxPos center{60.0f, 60.0f};

    const engine::SandboxPos right =
        engine::sandbox_step(center, engine::SandboxMove{1.0f, 0.0f}, bounds, 20.0f, 10000.0f, 1.0f);
    CHECK(approx(right.x, 10.0f + 100.0f - 10.0f));  // 100: right edge minus half entity

    const engine::SandboxPos left =
        engine::sandbox_step(center, engine::SandboxMove{-1.0f, 0.0f}, bounds, 20.0f, 10000.0f, 1.0f);
    CHECK(approx(left.x, 10.0f + 10.0f));  // 20: left edge plus half entity

    const engine::SandboxPos down =
        engine::sandbox_step(center, engine::SandboxMove{0.0f, 1.0f}, bounds, 20.0f, 10000.0f, 1.0f);
    CHECK(approx(down.y, 20.0f + 80.0f - 10.0f));  // 90

    const engine::SandboxPos up =
        engine::sandbox_step(center, engine::SandboxMove{0.0f, -1.0f}, bounds, 20.0f, 10000.0f, 1.0f);
    CHECK(approx(up.y, 20.0f + 10.0f));  // 30
}

ISAEC_TEST(sandbox_step_zero_move_zero_dt_negative_speed_stay_put) {
    const engine::SandboxBounds bounds{0.0f, 0.0f, 100.0f, 100.0f};
    const engine::SandboxPos pos{50.0f, 50.0f};

    CHECK(approx(engine::sandbox_step(pos, engine::SandboxMove{0.0f, 0.0f}, bounds, 20.0f,
                                      240.0f, 1.0f)
                     .x,
                 50.0f));
    CHECK(approx(engine::sandbox_step(pos, engine::SandboxMove{1.0f, 0.0f}, bounds, 20.0f,
                                      240.0f, 0.0f)
                     .x,
                 50.0f));
    CHECK(approx(engine::sandbox_step(pos, engine::SandboxMove{1.0f, 0.0f}, bounds, 20.0f,
                                      -240.0f, 1.0f)
                     .y,
                 50.0f));
}

ISAEC_TEST(sandbox_step_room_smaller_than_entity_centers_it) {
    const engine::SandboxBounds bounds{0.0f, 0.0f, 10.0f, 10.0f};
    const engine::SandboxPos pos{0.0f, 0.0f};

    const engine::SandboxPos out = engine::sandbox_step(pos, engine::SandboxMove{1.0f, 1.0f},
                                                        bounds, 20.0f, 100.0f, 1.0f);
    CHECK(approx(out.x, 5.0f));
    CHECK(approx(out.y, 5.0f));
}

}  // namespace
