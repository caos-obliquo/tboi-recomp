# HANDOFF — ANM2 playback (#128)

Session state. Durable record and limitations: `docs/HANDOFF.md` §35 and
`docs/anm2-format.md`; this file does not repeat them.

## Goal

Issue #128: advance root/layer/null ANM2 timelines from `Delay` at `Info@Fps`,
honour `Loop`, compose layer poses, deliver triggers once in `AtFrame` order,
and draw a real document through `isaec_window anm2`.

## Changed files

- `include/isaec/engine/animation.hpp`, `src/engine/animation.cpp` (new):
  `AnimationPlayer`, `LayerPose`, `NullPose`.
- `tests/test_anm2_player.cpp` (new): synthetic timing, loop, pose, trigger and
  delay negative-control tests; registered in `CMakeLists.txt` and
  `tests/CMakeLists.txt`.
- `include/isaec/engine/renderer.hpp`, `src/platform/sdl_renderer.cpp`:
  `DrawCall` rotation, pivot and RGB tint, applied by the SDL backend.
- `src/platform/sdl_main.cpp`: `anm2` route and `run_anm2`.
- `docs/anm2-format.md`, `docs/roadmap.md`, `README.md`, `docs/HANDOFF.md`:
  playback and runner state.

## Verified commands and results

- `cmake --build build -j2 --target isaec_window isaec_tests` — exit 0.
- `./build/tests/isaec_tests` — `169 cases, 0 failed`.
- `git diff --check` — clean.
- `xvfb-run -a build/isaec_window anm2 <repentance.a> 20 7205 - 90 1280 720
  <afterbirthp.a>` — exit 0, 90 frames, 90 draws, 90 presented, animation
  frame 4 of 8.

## Open questions

- Interpolation: keys marked `Interpolated` are held, not blended.
- FPS conversion: `seconds * Fps` matches decompiled crossing logic but is not
  runtime-verified against the shipped executable.
- Archive open takes 37 s on the 633 MB container (#130).

## Exact next step

Fix #130 (`Archive::open` single sized read) on its own branch, then decide
interpolation by checking `Interpolated` keys against game code.
