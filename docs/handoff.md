# Handoff — tboi-recomp (2026-11-17)

## 1. Goal
Clean-room recompile of The Binding of Isaac: Repentance+ v1.9.7.17.J460
(`isaac-ng.exe`, sha256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`,
Steam user copy), native on Linux x86_64 + Android arm64.

## 2. Changed files
- `docs/handoff.md` — dated 2026-11-17. Mandatory next deliverable is the
  **playtesting box**, tracked as
  [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79).
- `README.md` — docs-list row for `docs/handoff.md`; progress row for the
  playtesting box (same commit as the sandbox code).
- `docs/HANDOFF.md` — §12 corrected: 9 required contexts (was a stale
  "11 incl macOS" claim; PR #10 merged 2026-10-19), release-record fix (#74)
  noted.
- `src/engine/sandbox.cpp`, `include/isaec/engine/sandbox.hpp` — platform-independent
  sandbox math (keyboard digits normalized; pad thumb deadzoned by the engine
  layer; axes combined and clamped to ±1; entity clamped to the room).
- `src/platform/sdl_main.cpp` — `isaec_window slice` is now playable:
  arrows/WASD or XInput pad 0 stick move the marker square; `frames=0` plays
  until Esc; default 300 unchanged.
- `tests/test_sandbox.cpp` (+ `tests/CMakeLists.txt`, `CMakeLists.txt`) —
  108-case suite green.

## 3. Verified commands and results
- `gh issue view 77` → CLOSED/COMPLETED (auto via `Closes #77`); AC re-scoped
  (owner decision, 2026-11-16). #76 MERGED 2026-11-16T04:00:13Z.
- #67 CLOSED; white-box slice done (SDL2, ≈60 Hz, no game data). NOT a playtesting box.
- Exe sha256 → `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`.
- `gh pr checks 81` → 9/9 green (run 44012965729); squash-merged → `main` @ `a49d727`;
  0.1.4 → 0.1.12 (release catch-up for #74); 14 stale `release/pr-*` branches deleted.
- Second catch-up: `gh pr checks 87` → 9/9 green (run 44019631826); squash-merged →
  `main` @ `67d1edb`; records #81/#83/#85 (0.1.13–0.1.15); #74 closed (issuecomment-6938733163).
- Release-record workflow: Actions checkbox flipped 2026-11-17 so
  `version.yml` can open record PRs (issuecomment-6938733163); the `if:` inside
  `on:` was silently ignored — moved to job level (PR #85); v0.1.3 remains the
  latest tag/release; no v0.1.4+ tags cut.
- Playtesting box sandbox (branch `feature/79-playtest-sandbox`):
  `cmake --build build -j` clean (only the known tmpnam warning); `ctest` 1/1,
  **108 cases, 0 failed**; headless smoke `SDL_VIDEODRIVER=dummy ./build/isaec_window
  slice 320 240 90` → 90 frames, scene "white-box playtest room", EXIT 0;
  interactive move not run (no display).

## 4. Open questions

- **Playtesting box** — owner-scoped 2026-11-17: white-box playtesting
  sandbox; scope comment posted (issuecomment-6938614445). Implementation on
  `feature/79-playtest-sandbox`; AC must all be met + same-turn evidence
  comment before #79 closes.
- NOT mandatory (owner, 2026-11-17): #64 audio, #66 Linux/Android epic,
  #52 LZW (behind runtime vtable `0xba05bc`), #3/#4 signatures, #35 metamethods.
- Clamp/Clamped runtime semantics; REPENTOGON `.zhl` silent on Clamp.

## 5. Exact next step
Land the playtesting sandbox PR for [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79)
(9/9 green, README progress row in the same commit), then post the same-turn
evidence comment; #79 closes only when its AC is fully checked.
Do not start #64/#66/#52/#3/#4/#35 as "mandatory" — they are not.
