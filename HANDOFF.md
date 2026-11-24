# HANDOFF — ANM2 playback (#128), archive open (#130)

Session state. Durable record and limitations: `docs/HANDOFF.md` §35–§36 and
`docs/anm2-format.md`; this file does not repeat them.

## Goal

#128: advance root/layer/null ANM2 timelines from `Delay` at `Info@Fps`, honour
`Loop`, compose poses, deliver triggers once in `AtFrame` order, draw a real
document through `isaec_window anm2`. #130: stop `Archive::open` copying per
character on large containers.

## Changed files

- `include/isaec/engine/animation.hpp`, `src/engine/animation.cpp` (new):
  `AnimationPlayer`, `LayerPose`, `NullPose`.
- `tests/test_anm2_player.cpp` (new): timing, loop, pose, trigger and Delay
  negative-control tests.
- `include/isaec/engine/renderer.hpp`, `src/platform/sdl_renderer.cpp`:
  `DrawCall` rotation, pivot and RGB tint.
- `src/platform/sdl_main.cpp`: `anm2` route and `run_anm2`.
- `src/data/archive.cpp`, `tests/test_archive.cpp`: one sized read and the
  opt-in open-time check.
- `docs/anm2-format.md`, `docs/roadmap.md`, `README.md`, `docs/HANDOFF.md`.

## Verified commands and results

- `cmake --build build -j2 --target isaec_window isaec_tests` — exit 0.
- `./build/tests/isaec_tests` — `170 cases, 0 failed`.
- `git diff --check` — clean.
- `xvfb-run -a build/isaec_window anm2 <repentance.a> 20 7205 - 90 1280 720
  <afterbirthp.a>` — exit 0, 90 frames/draws/presented, frame 4 of 8, 5.2 s.
- `ISAEC_TBOI_ARCHIVE=<archive> ./build/tests/isaec_tests` — open 155.8 ms
  (`repentance.a`, 385 MB) and 293.4 ms (`afterbirthp.a`, 633 MB); decode
  counts unchanged.

## Open questions

- Interpolation: keys marked `Interpolated` are held, not blended.
- FPS conversion: `seconds * Fps` matches decompiled crossing logic but is not
  runtime-verified against the shipped executable.

## Exact next step

Check `Interpolated` keys against game code, and prove the seconds-to-frames
conversion at runtime before claiming FPS parity.
