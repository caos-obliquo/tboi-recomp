# Handoff — tboi-recomp (2026-11-17)

## 1. Goal
Clean-room recompile of The Binding of Isaac: Repentance+ v1.9.7.17.J460
(`isaac-ng.exe`, sha256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`,
Steam user copy), native on Linux x86_64 and Android arm64. Mandatory
next-phase deliverable: the playtesting box — **landed** (white-box sandbox,
no game data; issue #79 stays open until witnessed or waived).

## 2. Changed files
- `docs/handoff.md` (tracked, 2026-11-17) — PR #88 (box), PR #91 (record) merged.
- Box: `include/isaec/engine/sandbox.hpp`, `src/engine/sandbox.cpp`,
  `tests/test_sandbox.cpp`, `src/platform/sdl_main.cpp` (`isaec_window slice`
  playable; `frames=0` = until Esc), README row (same commit), `CMakeLists.txt`.
- This cycle: PR #91 + PR #93; record orphans #89/#92 closed as superseded;
  issue #90 closed with evidence (issuecomment-6939449515).
- `tools/isaac-archive.py`, `build-input/`, `HANDOFF.md` — user-owned, untracked.

## 3. Verified commands and results
- `ctest` 1/1; `./build/tests/isaec_tests` — 108 cases, 0 failed.
- Headless smoke `SDL_VIDEODRIVER=dummy ./build/isaec_window slice 320 240 90`
  — 90 frames, EXIT 0 (~57 Hz dummy driver).
- CI 9/9 every time: PR #88 (run 44020322024), #91 (44023364478), #93
  (44023702572). `main` @ `f1d2538`, version 0.1.17, `release.py check` ok.
- Issues: #74 CLOSED; #90 CLOSED/COMPLETED; #79 OPEN (interactive run
  not witnessed); #77 CLOSED. No `origin/release/*` branches remain.
- Latest tag stays v0.1.3; 0.1.4–0.1.17 recorded, never tagged; no `[v0.1.4]`.

## 4. Open questions
- #79: owner witnesses interactive keyboard/pad run (needs a display) or
  explicitly waives; last #79 comment issuecomment-6938840091.
- Record-PR design unchanged: every merged PR → checkless record PR
  (GITHUB_TOKEN PRs trigger no CI). Mitigated (#85 job-level skip + Actions
  checkbox flip), not redesigned. Durable fix = separate decision.
- No tags/releases beyond v0.1.3; pre-existing `[0.1.2]` ordering quirk.

## 5. Exact next step
Owner: `./build/isaec_window slice 1280 720 0` (needs display) or waive the
interactive check; then close #79. Do not start #64/#66/#52/#3/#4/#35 as
"mandatory" (owner, 2026-11-17). `HANDOFF.md` stays untracked.
