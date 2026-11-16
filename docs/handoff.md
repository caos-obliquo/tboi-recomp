# Handoff — the state of tboi-recomp (2026-11-16)

Snapshot for a fresh session. Read this, then `AGENTS.md`, `README.md`
(Progress table) and `gh issue list`. Goal verbatim:

> recomp of the latest version (or repentance+)

The shipped exe is the latest: **The Binding of Isaac: Repentance+
v1.9.7.17.J460**, sha256 `3bdfc8ba…` (Steam, user-owned). The whole project
is a clean-room reimplementation of that binary. Every fact must be
reproducible by command (AGENTS.md §2). This document records where things
stand; the GitHub issues carry the verdicts.

## Where things are

- `main` at `bdf2abb` (docs PR #75 — Clamp/Clamped rows graded `rejected`,
  contexts corrected 11 → 9).
- **PR #76 — open**: `feature/65-xinput-input` — XInput-shaped controller
  state, engine layer + SDL2 backend; **100/100 tests green**; README rows
  and attribution docs live only on its branch. Merge gate = all **9**
  required contexts green. Agent merges on completion by standing order;
  do not merge while any context is pending/red.
- `docs/roadmap.md` — build phases 0–8 and the per-reference grades.
- `docs/lua-api-reference.md` — provenance grades; unverified rows never
  usable; Clamp/Clamped rejected with cited evidence
  (issue [#37](https://github.com/caos-obliquo/tboi-recomp/issues/37)).
- `docs/reimplementation-references.md` — what external work was consulted;
  attribution of Half-1 (chiikabu/the-browsing-of-isaac, GPL-3.0-only
  post-PR #63).
- Issues with verdicts: #37 (Vector:Clamp/Clamped — permanent stubs), #70
  (Lua-API consultation), #71 (roadmap epic), #66 (Linux/Android epic),
  #64 (audio), #65 (input), #52 (LZW), #69 (IsaacDocs).

## Standing locks

- The user's `tools/isaac-archive.py` (modified) and untracked `HANDOFF.md`
  are never touched, never committed.
- No direct commits to `main`; PRs only; Conventional subjects; commit
  body names the issue it serves.
- `Half-2` never enters the tree in any form. Half-1 may be adapted, never
  copied, always attributed in file headers + commit + PR. Raw `.zhl`
  bytes are never committed — notes and grades only.
- Grades: `own-repeat-verified` / `oracle-agreed` / `oracle-disagreed` /
  `unverified` / `rejected`. An unverified row is never used for
  implementation, tests, or progress claims.
- Progress table in same commit as behaviour; located ≠ implemented;
  named ≠ useful; never round numbers up.
- 250-LOC ceiling per change; issue-first; same-turn comment on every
  issue with a result; close only when all criteria are met.
- Every PR is assigned `caos-obliquo`. Required contexts = **9**:
  linux-gcc, linux-clang, linux-arm64, android-arm64, warnings-as-errors,
  asan-ubsan, generated-spec-is-current, binding-extractor-selfcheck,
  release-notes-are-writable. Never weaken or remove a required context;
  if one fails, fix the cause, never the gate.
- Agent merges PRs only after every required context reports green.

## Do not redo (dead ends with reasons, verified)

- **LZW mode 0x01** (#52) — both the Python reference and the C++ codec
  agree; the bug is in our algorithm reading, not in one implementation.
  The game's own decoder sits behind a runtime-allocated vtable
  (0xba05bc); statically unlocated. Until located, LZW chunks after the
  first decode to garbage; `items.xml`/`rooms.a`/`animations.a` extraction
  blocked. Needs either a static breakthrough or the real decoder.
- **Vector:Clamp / Vector:Clamped** (#37) — closure reads **four**
  `luaL_checknumber`s at stack indices 2..5 (shared helper 0x8ba890, a
  generic four-float struct packer — also Color:SetColorize/SetTint
  0x8a9240, Color ctor 0x8bb3b0, `FUN_008a8610`/`FUN_008a8680`). The
  callee lives behind the per-instance first field (`&0xb73574`, a
  `.rdata` reflection table; `.rdata` is READONLY DATA, not executable).
  Semantics unrecoverable statically; shipped member vestigial-broken
  upstream; permanent `not_implemented_trampoline()` stubs; grade
  `rejected`. Only runtime probing can reopen this.
- **REPENTOGON `Vector2.zhl`** — silent on Clamp/Clamped (no agreement
  either way); consulted only, raw bytes never committed.
- **Busy-loop heat bug** — upstream Repentance+ 1.9.7.17 bug; not
  fixable in this tree; user's call on cpu-fix/downgrade.
- **Release-note job** (#74) — `version.yml` fails at `Open release PR`
  (GitHub Actions not permitted to `gh pr create`); not a required check;
  ten `release/pr-*` branches pending (55,57,58,59,60,61,63,68,72,73).
  Needs the owner checkbox ("Allow GitHub Actions to create and approve
  pull requests") or the workflow changed to not call `gh pr create`.
- **Steam install's `Repentogon/` folder** carries no `.zhl`; figures
  were consulted from TeamREPENTOGON/REPENTOGON @ 0cd8f86 (v1.1.3).
- **Untracked stale `HANDOFF.md`** — the user's old notes; do not read,
  edit, delete, or commit. This tracked handoff supersedes it for fresh
  sessions.

## Build phases (docs/roadmap.md holds details)

0. Binding map — done (911 bodies located/decompiled/named; 342 members
   implemented; Vector/PosVel/Color done).
1. ANM2 loader + ARCH000/LZW data spine + renderer — biggest gate; LZW
   blocked on #52.
2. Audio (#64) — interface + null/SDL2 backends.
3. Input (#65) — Phase 1 XInput-shaped state landed on PR #76; Phase 2
   (Android arm64) + Phase 3 (touch) pending.
4. Binding increments (250-LOC ceiling; returns-1 bodies first).
5. Bootstrap (FUN_006fadc0) + menus from the user's Lua.
6. Targets (#66) — Linux x86_64 first, then Android arm64 via NDK
   (ISAEC_WITH_PNG=OFF).
7. Mod system — Android-port semantics, not shipped code.
8.

## How to cold-start

    cd /home/caos/src/isaac-engine
    cat AGENTS.md README.md docs/roadmap.md docs/handoff.md
    gh issue list --state open
    git log --oneline -5
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure

Then pick the next issue from the list, branch per it, and follow AGENTS.md
start to finish. Never start work on an unopened issue; open one first.
