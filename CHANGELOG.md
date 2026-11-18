# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Entries describe *why* a change exists, not only what changed. Where a fix
looks arbitrary without its cause, it carries `Why:` and `Effect:` lines.

The version is not written here by hand. `project(... VERSION x.y.z)` in
`CMakeLists.txt` is the single source of truth; `tools/release.py` reads it and
`.github/workflows/release.yml` tags from it.

## [0.1.2] - 2026-10-19

### Merged: [#22](https://github.com/caos-obliquo/tboi-recomp/pull/22) — feat(release): record every merged PR in the changelog and bump the patch

**Author:** @caos-obliquo  **Merged:** 2026-10-19  **Files:** 5 changed, +329/-56.

<details>
<summary>Pull request body</summary>

Closes the gap between version bumps and the changelog. `release.py bump-notes` composes the full `## [x.y.z]` section from the merged PR, and a workflow runs it on every merge to main.

Verified by composing against PR #10 and inspecting the result; that caught a bug where the commit list read a field the GitHub CLI does not return, rendering all 26 commits as "(no subject)".

Also includes the HANDOFF rewrite, the README warning/comparison/hyperlinks, and the resumable decompiler.

</details>

#### Closes

- [` `] [#10](https://github.com/caos-obliquo/tboi-recomp/pull/10) ci: cover ARM64 natively and stop macOS coverage silently narrowing (merged)

#### Commits

- `fa160fd3` feat(release): record every merged PR in the changelog and bump the p…
  …atch
  The release plumbing existed but the changelog did not move with the code. A
  version could be bumped by hand and the notes written later, or never, and
  `release.py body` would then publish a bare commit list and call it an
  explanation. That gap is now closed at the source.
  `release.py bump-notes --pr N` composes the whole `## [x.y.z]` section from the
  pull request: title, body, linked issues with their state, every commit with its
  full message, a per-file +/- table, the documentation touched, and the PR
  discussion and review. It is deliberately large. A changelog that summarises a
  pull request in one line is what forces the next session to redig it in order to
  understand it, and that is the opposite of a handoff.
  `.github/workflows/version.yml` runs it on every merge to main, so version and
  record cannot drift. Bumps are serialised through a concurrency group, because
  two merges landing together would otherwise race on CMakeLists.txt and produce
  the same version twice.
  Measured, not assumed: composed against PR #10 and inspected. The section runs to
  about 41,000 characters. It also caught a bug before it shipped -- the commit
  list read a `message` field the GitHub CLI does not return, so all 26 commits
  rendered as "(no subject)". It now reads messageHeadline and messageBody.
  Also in this commit, because the same review pass found them:
  - docs/HANDOFF.md sections 1-3 described a docs-only stub that has not been
    true for several sessions, and did not mention that the windowed renderer
    counts calls and draws nothing. Rewritten against what is actually in main.
  - README.md: the AI-assistance warning, a reference comparison led by repentogxm,
    hyperlinked issue references, and an open-work table with a current state.
  - Issue references hyperlinked across AGENTS.md, HANDOFF.md,
    arch000-format.md, binding-surface-findings.md, tools/README.md.
  - tools/decompile_corpus.py committed as the resumable batch decompiler.
  Cross-references verified with a checker over every markdown file: zero broken
  anchors, zero broken file links, zero unhyperlinked issue references. The two
  "broken file links" it still reports are inline code and a path resolved relative
  to its own directory, neither of which is a link.
- `9392b87b` docs(changelog): put the Unreleased section back in order
  Two edits to this section had left it with a duplicate '### Changed' header, an
  'Added' bullet parked under 'Fixed', and no '### Added' entry for the tooling
  this release actually adds.
  Also drops the README bullet from this release's 'Changed' list. PR #22 does not
  touch README.md; the warning and the comparison are PR #21. Attributing them here
  would make the changelog disagree with the diff that produced it.

#### Files changed

| file | + | - |
|---|---:|---:|
| `.github/workflows/version.yml` | 58 | 0 |
| `CHANGELOG.md` | 28 | 4 |
| `docs/HANDOFF.md` | 43 | 50 |
| `docs/rea-toolchain.md` | 1 | 2 |
| `tools/release.py` | 199 | 0 |

#### Documentation

- `CHANGELOG.md`
- `docs/HANDOFF.md`
- `docs/rea-toolchain.md`

#### Carried from Unreleased

### Added

- `tools/release.py bump-notes --pr N` — bumps the patch version and writes the
  CHANGELOG section from a merged pull request: its title, body, linked issues
  with state, every commit with its full message, a per-file `+/-` table, the
  documentation touched, and the PR discussion and review. Nothing is typed by
  hand, so the section cannot drift from the change it describes.
- `.github/workflows/version.yml` — runs `bump-notes` on every merge to `main`,
  so the version number and the record of what changed move together. Bumps are
  serialised through a concurrency group; two merges landing together cannot
  produce the same version twice.
- `tools/decompile_corpus.py` — resumable batch decompiler for the 441 recovered
  closure bodies. Flushes per body and resumes from an index, so an abort costs
  one batch rather than the whole run.
- `docs/arch000-format.md` — the archive container format, measured rather than
  inferred. Header is 14 bytes; a 20-byte-per-entry index sits at the end of the
  file; entries are `{hash_lo, hash_hi, offset, raw_size, crc}`.
- `tools/inspect-archive.py` — validates and summarises an archive. It asserts
  the index arithmetic instead of trusting it, so a wrong reading of the format
  makes it exit non-zero rather than print a plausible table. Validates all 22
  archives in the reference install with zero bytes unaccounted.

### Changed

- `docs/HANDOFF.md` — the opening three sections described a docs-only stub that
  has not been true for several sessions. Rewritten against what is actually in
  `main`, including the fact that the windowed renderer counts calls and draws
  nothing. The reference sections below are unchanged.
- Issue references are now hyperlinks in `AGENTS.md`, `HANDOFF.md`,
  `arch000-format.md`, `binding-surface-findings.md`, `tools/README.md` and this
  file, so a reader can follow any of them.
- `ARCH000` is no longer an opaque blocker. Every remaining unknown is now
  scoped to the codec and the 64-bit name hash.
- The README progress table no longer claims the container format is "not
  started". A status table that outlives its facts is worse than no status
  table: it is the part a reader trusts without checking.

### Fixed

- `tools/release.py` — the commit list read a `message` field the GitHub CLI
  does not return, so every commit rendered as `(no subject)`. It reads
  `messageHeadline` and `messageBody`.
- `docs/rea-toolchain.md` — one sentence of throat-clearing removed.


## [0.1.5] - 2026-11-17

### Merged: [#63](https://github.com/caos-obliquo/tboi-recomp/pull/63) — docs(license): relicense MIT -> GPL-3.0-only for the-browsing-of-isaac [#62](https://github.com/caos-obliquo/tboi-recomp/issues/62)

**Author:** @caos-obliquo  **Merged:** 2026-11-15  **Files:** 4 changed, +777/-32.

<details>
<summary>Pull request body</summary>

Closes #62.

Relicenses this repository MIT -> GPL-3.0-only so that it may adapt [the-browsing-of-isaac](https://github.com/chiikabu/the-browsing-of-isaac)\'s own original host/platform glue (keyboard -> VK mapping, Gamepad API -> XInput, touch overlay, `host_shims_xinput.c`) **with attribution**, as separate reviewed changes file by file.

## What changed

- `LICENSE` — verbatim GNU GPL v3 (byte-identical copy of the one shipped by the-browsing-of-isaac; verified with `cmp`).
- `README.md` — license line -> GPL-3.0-only with #62 link; comparison intro now five projects; new "The browser one" subsection; Credits extended.
- `CONTRIBUTING.md` — licensing note MIT -> GPL-3.0-only.
- `docs/reimplementation-references.md` — new section "The Browsing of Isaac, and the honest split": Half 1 (their original code, adaptable with attribution) vs Half 2 (Nicalis-derived recompilation; never enters this tree under any licence; only used as unverified external derivations, re-verified against our own binary as in #17/#20).

## What this does not do

- No code, build or dependency changes. `src/` is untouched.
- Nothing from the-browsing-of-isaac is vendored here.
- CHANGELOG is not edited by hand; it is composed from merged PRs by `tools/release.py`.

## Verification

- `git diff --stat` (vs `main` @ 6c0c4c1): CONTRIBUTING.md 4, LICENSE 695, README.md 52, docs 58 — 777 insertions, 32 deletions.
- `cmp LICENSE <browsing-of-isaac>/LICENSE` -> identical.
- `grep -ri "MIT" README.md CONTRIBUTING.md` -> no matches (one intentional mention left in `docs/arch000-format.md`, referring to a third-party tool, unchanged).
- `git log --format=%an | sort | uniq -c` -> solo authorship (67 beto bettini, 2 dependabot[bot], 1 github-actions).

## Open decision

GPL-3.0-**only** was chosen because the-browsing-of-isaac ships GPL-3.0. Switching to GPL-3.0-or-later is a one-word change before merge if you prefer it.

Merge policy: standard PR review; this does not bypass any required checks.

</details>

#### Closes

- [`x`] [#17](https://github.com/caos-obliquo/tboi-recomp/issues/17) Attach property names to the 372 anonymous closure bodies (closed)
- [` `] [#20](https://github.com/caos-obliquo/tboi-recomp/issues/20) Cross-check the 372 anonymous bodies against REPENTOGON's libzhl function table (open)
- [`x`] [#62](https://github.com/caos-obliquo/tboi-recomp/issues/62) Relicense MIT -> GPL-3.0-only to enable direct reuse of The Browsing of Isaac's original code (closed)

#### Commits

- `f22d6599` docs(license): relicense MIT -> GPL-3.0-only for the-browsing-of-isaa…
  …c [#62](https://github.com/caos-obliquo/tboi-recomp/issues/62)
  Relicensing is authorized so this project may adapt the-browsing-of-isaac's own
  original host/platform glue (input.js, gamepad.mjs XInput mapping, touch
  controls, host_shims_xinput.c) with attribution, file by file, as separate
  reviewed changes. Half 2 (mechanically recompiled game bodies, boot.wasm) can
  never enter this tree under any licence. No source or build changes.

#### Files changed

| file | + | - |
|---|---:|---:|
| `CONTRIBUTING.md` | 2 | 2 |
| `LICENSE` | 674 | 21 |
| `README.md` | 44 | 8 |
| `docs/reimplementation-references.md` | 57 | 1 |

#### Documentation

- `CONTRIBUTING.md`
- `README.md`
- `docs/reimplementation-references.md`


## [0.1.6] - 2026-11-17

### Merged: [#68](https://github.com/caos-obliquo/tboi-recomp/pull/68) — feat(platform): playable white-box slice for Linux x86_64

**Author:** @caos-obliquo  **Merged:** 2026-11-15  **Files:** 3 changed, +114/-5.

<details>
<summary>Pull request body</summary>

Closes #67.

## What

New mode `isaec_window slice [width] [height] [frames]` (defaults 1280 720 300):
a white-box empty room (floor, four walls, one marker square, one procedural
64x64 texture), paced at 60 Hz via steady_clock with vsync, Esc or window close
exits cleanly.

## Verified here

- `./build/isaec_window slice 1280 720 180` -> backend sdl2, frames 180,
  elapsed 3.012 s, pacing avg=0.213 ms est=59.75 Hz, draws=1080, exit 0 (DISPLAY=:0).
- `SDL_VIDEODRIVER=dummy ./build/isaec_window slice 800 600 240` ->
  est=59.78 Hz (timing-paced, no real vsync), presented=240, exit 0.
- `ctest --test-dir build` -> 1/1 passed (62 cases with PNG).
- Asset audit: `git ls-files | grep -Ei '\.(png|ogg|wav|a|jpg)$'` -> only
  `third_party/lua-5.3.6/doc/osi-certified-72x60.png` (vendored Lua docs, NOT a
  game asset). No Isaac data committed.
- Zero warnings on the changed targets.

## Not claimed / unchanged

- GPU rendering proper is still not started — see #5. This is an SDL2 software/GL
  path proving the Renderer abstraction, not a GPU backend.
- No Isaac assets, no Lua, no ARCH000, no Heap recomp, not #5/#66.
- tools/isaac-archive.py untouched (owner's uncommitted change preserved);
  HANDOFF.md (owner's file) untouched, still untracked.

## Acceptance evidence for #67 (to be closed only after merge)

see PR checks; this text will be re-verified on merge.

</details>

#### Closes

- [` `] [#5](https://github.com/caos-obliquo/tboi-recomp/issues/5) Implement a real GPU renderer backend (portable, Android-first) (open)
- [` `] [#66](https://github.com/caos-obliquo/tboi-recomp/issues/66) Epic: Linux x86_64 and Android arm64 as first-class targets (packaging, on-device verification, frame pacing, docs) (open)
- [`x`] [#67](https://github.com/caos-obliquo/tboi-recomp/issues/67) Epic slice: playable 60 fps empty floor (SDL2 white-box, Linux x86_64) (closed)

#### Commits

- `69c97f21` feat(platform): playable white-box slice for Linux x86_64 (#67)
  Issue #67. The engine had no windowed, human-visible output; this adds
  `isaec_window slice [width] [height] [frames]`: a white-box empty room
  (floor, four walls, one marker square) drawn with one procedural 64x64
  texture, paced at 60 Hz via steady_clock with vsync, Esc or window close
  exits cleanly.
  No game data is read or committed; this proves the Renderer abstraction
  end to end (SDL2 backend, submit/present), nothing more. GPU rendering
  proper (#5) is untouched.
  Measured on this machine: ./build/isaec_window slice 1280 720 180
  -> 180 frames, elapsed 3.012 s, pacing avg=0.213 ms est=59.75 Hz,
  draws=1080, presented=180, exit 0. Under SDL_VIDEODRIVER=dummy:
  800 600 240 -> est=59.78 Hz (timing-paced, no real vsync).
- `5e80ff0f` Merge remote-tracking branch 'origin/main' into slice/67-playable-emp…
  …ty-floor

#### Files changed

| file | + | - |
|---|---:|---:|
| `README.md` | 15 | 1 |
| `src/platform/sdl_main.cpp` | 97 | 4 |
| `src/platform/sdl_renderer.cpp` | 2 | 0 |

#### Documentation

- `README.md`

#### Discussion

**@caos-obliquo** (2026-11-15):

> ### Build + run evidence (this machine)
>
> Real display (DISPLAY=:0, 1280x720x180):
>
>     backend   sdl2
>     scene     white-box empty room
>     viewport  1280 x 720
>     frames    180
>     elapsed   3.012 s
>     pacing    avg=0.213 ms est=59.75 Hz
>     draws     1080
>     presented 180
>     exit=0
>
> `SDL_VIDEODRIVER=dummy ./build/isaec_window slice 800 600 240` → est=59.78 Hz,
> presented=240, exit 0 (timing-paced, no real vsync).
>
> Clean tree apart from the owner's two files: `M tools/isaac-archive.py`, `?? HANDOFF.md`
> — neither is in this PR.
>
> Game-asset audit: `git ls-files | grep -Ei '\.(png|ogg|wav|a|jpg)$'` → only the
> vendored Lua OSI cert (`third_party/lua-5.3.6/doc/osi-certified-72x60.png`); no Isaac data.
>
> Escape/window-close quit path: wired in `SdlRenderer::present()` (KEYDOWN SDLK_ESCAPE
> → `alive_=false`).
>
> 2026-11-15 review note: pacing measured at ~59.7 Hz on display, not 60.00 — consistent
> with vsync + sched jitter, well within "60 fps corridor" for #67.
>
> GPU rendering (#5) is unchanged. Lua unchanged. Android unchanged.
>


## [0.1.7] - 2026-11-17

### Merged: [#72](https://github.com/caos-obliquo/tboi-recomp/pull/72) — docs(lua-api): external Lua-API oracles, graded

**Author:** @caos-obliquo  **Merged:** 2026-11-15  **Files:** 3 changed, +151/-0.

<details>
<summary>Pull request body</summary>

Closes #70.

Consultation-only record of external Lua-API references, each finding tagged with a provenance grade (`own-repeat-verified` / `oracle-agreed` / `oracle-disagreed` / `unverified` / `rejected`). Nothing in this PR implements anything; `unverified` rows are explicitly barred from implementation use.

**Measured against** the Steam retail v1.9.7.17.J460 binary (`isaac-ng.exe`, sha256 3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b).

- Vector layout `float x : 0x0, float y : 0x4`, size 0x8 — ours agrees (§21) — `oracle-agreed`
- PosVel `pos : 0x0`, `vel : 0x8`, size 0x10 — ours agrees (§21) — `oracle-agreed`
- `Vector::Lerp` masked preamble `558bec8b45??f30f1060` — **NOT FOUND** in this build — `oracle-disagreed`
- `Game.zhl`: 56 parseable preambles, 40 masked file-offset hits (not VAs)
- `EntityPlayer.zhl`: 193 preambles, 130 file-offset hits
- `Global.zhl`: 10 parseable preambles, 4 file-offset hits
- IsaacDocs (moddingofisaac.com) / Filloax autocomplete — `unverified`; consulted, never copied

**Files:** adds `docs/lua-api-reference.md` (grades legend, environment, findings, commands, file-offset-not-VA warning); extends `docs/reimplementation-references.md` with consulted-only REPENTOGON and IsaacDocs sections; extends `AGENTS.md` §3 cross-document rules with the provenance-grade rule binding.

**Never copies:** no `.zhl` bytes are in the diff; no Half-1/Half-2 content is touched; no text from upstream documentation is reproduced.

Repos' Progress table deliberately unchanged — no implementation landed.

</details>

#### Closes

- [`x`] [#70](https://github.com/caos-obliquo/tboi-recomp/issues/70) Epic: Lua API consultation (IsaacDocs + REPENTOGON) and first recomp increment (closed)

#### Commits

- `069640e3` docs(lua-api): external Lua-API oracles, graded [#70](https://github.…
  …com/caos-obliquo/tboi-recomp/issues/70) [#71](https://github.com/caos-obliquo/tboi-recomp/issues/71)
  Records what REPENTOGON (.zhl, GPL-2.0) and IsaacDocs say about the
  Lua-facing surface, every note tagged with a provenance grade, and adds
  docs/lua-api-reference.md to AGENTS.md cross-document rules. No text and no
  bytes are copied from either source; .zhl signature bytes are never pasted.
  Measured against the Steam retail v1.9.7.17.J460 binary: Vector::Lerp
  preamble absent, PosVel/Vector layouts agree, Game 40/56, EntityPlayer
  130/193, Global 4/10 masked file-offset hits (not VAs).
- `e6d8075c` Merge branch 'main' into docs/70-lua-api-reference

#### Files changed

| file | + | - |
|---|---:|---:|
| `AGENTS.md` | 7 | 0 |
| `docs/lua-api-reference.md` | 95 | 0 |
| `docs/reimplementation-references.md` | 49 | 0 |

#### Documentation

- `AGENTS.md`
- `docs/lua-api-reference.md`
- `docs/reimplementation-references.md`


## [0.1.8] - 2026-11-17

### Merged: [#73](https://github.com/caos-obliquo/tboi-recomp/pull/73) — docs(roadmap): Repentance+ build order, references and grades

**Author:** @caos-obliquo  **Merged:** 2026-11-15  **Files:** 4 changed, +186/-48.

<details>
<summary>Pull request body</summary>

Partially advanced for #71 (epic stays open until its tracked increments land).

## What this adds

`docs/roadmap.md` — the structured priority path:

- Phase plan: 0 binding map (done) → 1 renderer + data spine (ANM2 loader absent — largest gate; #5) → 2 audio (#64) → 3 input (#65) → 4 binding increments (returns-1 bodies first: 534/911) → 5 bootstrap `FUN_006fadc0` → 6 targets #66 (Linux first, Android via NDK + `ISAEC_WITH_PNG=OFF`) → 7 mod system semantics → 8 tracking discipline
- Every external reference graded: own exe (authoritative), REPENTOGON `.zhl` (consulted-only, 105 offsets cross-checked, no bytes), IsaacDocs/autocomplete (`unverified`), Half-1/`Half-2` policy, switch-native host responsibility map, Android-port modding guide (consulted-only)
- Explicit "never" list and the honest statement of what this project does not yet have: no renderer, no ANM2, no scripting loop, no audio — the port is months of work, not a weekend

Other edits: `docs/reimplementation-references.md` gains the Android-port section; README docs table gains `docs/roadmap.md`; `AGENTS.md` §3 lists roadmap.md as cross-document authority.

## Not in this PR

No code. No game data. No redistribution of any third-party repo's content — every reference is link + grading + paraphrase only.

## Verification

- `git merge-base --is-ancestor origin/main docs/71-roadmap` → yes (branch off `origin/main` `92df2e1`)
- Half-2 / raw `.zhl` byte scan of the diff → EMPTY
- README Progress table deliberately untouched — no implementation landed

User merges.

</details>

#### Closes

- [` `] [#5](https://github.com/caos-obliquo/tboi-recomp/issues/5) Implement a real GPU renderer backend (portable, Android-first) (open)
- [` `] [#64](https://github.com/caos-obliquo/tboi-recomp/issues/64) Audio: no audio code exists anywhere in the tree — add an injected Audio interface with null and SDL2 backends (open)
- [` `] [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65) Input: XInput-shaped controller state (adapt The Browsing of Isaac's gamepad/shims, with attribution) + Android touch later (open)
- [` `] [#66](https://github.com/caos-obliquo/tboi-recomp/issues/66) Epic: Linux x86_64 and Android arm64 as first-class targets (packaging, on-device verification, frame pacing, docs) (open)
- [` `] [#71](https://github.com/caos-obliquo/tboi-recomp/issues/71) Epic: long-term roadmap - from bindings-only baseline to playable native Repentance+ recomp (open)

#### Commits

- `83824b58` docs(roadmap): Repentance+ build order, references and grades [#71](h…
  …ttps://github.com/caos-obliquo/tboi-recomp/issues/71)
  The priority path for reaching the goal in the issue: binding map to data/render spine to audio to input to binding increments to bootstrap to targets. Every external reference is graded by how it was used; the unrecoverable work is stated plainly, not guessed at.
  Harvests the Android-port modding guide as consulted-only design reference and records the same in reimplementation-references.md.
- `3c002329` Merge branch 'main' into docs/71-roadmap

#### Files changed

| file | + | - |
|---|---:|---:|
| `AGENTS.md` | 1 | 0 |
| `README.md` | 1 | 0 |
| `docs/reimplementation-references.md` | 19 | 48 |
| `docs/roadmap.md` | 165 | 0 |

#### Documentation

- `AGENTS.md`
- `README.md`
- `docs/reimplementation-references.md`
- `docs/roadmap.md`


## [0.1.9] - 2026-11-17

### Merged: [#75](https://github.com/caos-obliquo/tboi-recomp/pull/75) — docs(lua-api-reference): Clamp/Clamped rejected; CI contexts 9 [#37](https://github.com/caos-obliquo/tboi-recomp/issues/37)

**Author:** @caos-obliquo  **Merged:** 2026-11-16  **Files:** 2 changed, +3/-1.

<details>
<summary>Pull request body</summary>

References #37 (closed 2026-11-15 with the full REA chain).

Changes:
- docs/lua-api-reference.md: adds `Vector:Clamp` / `Vector:Clamped` rows with grade `rejected` — the shipped closures cannot execute by construction (`.rdata` is non-executable; `inst[0]=&0xb73574`; `CALL [inst+0]` is an NX violation by construction). Loud stubs stay in the recomp. Evidence: issue #37 comments 6903733349 / 6904094401 / 6904112121 / 6905780752 / 6905792315 (objdump -h section flags, analyze_function chain, 50 engine creation sites).
- AGENTS.md §6: corrects the stale claim of 11 required CI contexts to the measured 9 (gh api repos/caos-obliquo/tboi-recomp/branches/main/protection — strict:true, approvals:0, the nine contexts listed on the PR).

No game code; no assets; no .zhl; only our notes.

</details>

#### Closes

- [`x`] [#37](https://github.com/caos-obliquo/tboi-recomp/issues/37) Vector:Clamp / Vector:Clamped read four numbers, the public API documents two (closed)

#### Commits

- `90f8fdb8` docs(lua-api-reference): Clamp/Clamped rejected; CI contexts 9 [#37](…
  …https://github.com/caos-obliquo/tboi-recomp/issues/37)
  - Adds Vector:Clamp / Vector:Clamped rows to the findings table with grade
    `rejected`. The shipped closures cannot execute by construction (.rdata is
    READONLY DATA with no CODE flag; inst[0] = &0xb73574 points at a
    luabridge reflection entry; CALL [inst+0] is an NX violation by
    construction). Evidence in issue #37 comments (objdump -h section table;
    analyze_function FUN_008ba890/FUN_008a8610/FUN_008a8680; 50 engine sites
    storing inst[0]=&0xb73574). Loud stubs stay.
  - AGENTS.md §6: corrects stale claim of 11 required CI contexts to the
    measured 9 (gh api repos/caos-obliquo/tboi-recomp/branches/main/protection;
    strict:true, approvals:0).

#### Files changed

| file | + | - |
|---|---:|---:|
| `AGENTS.md` | 1 | 1 |
| `docs/lua-api-reference.md` | 2 | 0 |

#### Documentation

- `AGENTS.md`
- `docs/lua-api-reference.md`


## [0.1.10] - 2026-11-17

### Merged: [#76](https://github.com/caos-obliquo/tboi-recomp/pull/76) — feat(engine): XInput-shaped controller state + SDL2 backend [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65)

**Author:** @caos-obliquo  **Merged:** 2026-11-16  **Files:** 9 changed, +533/-0.

<details>
<summary>Pull request body</summary>

## What

Adds the engine's first input layer (#65, roadmap Phase 3): **XInput-shaped pad state** as a framework-free engine layer plus an **SDL2 backend** (`isaec::platform::SdlGamepadSource`). Still not wired into the renderer's event queue or gameplay — that remains tracked in #65.

- `include/isaec/engine/input.hpp`, `src/engine/input.cpp` — `GamepadState` (dwPacketNumber bumped only on change), `wButtons` XInput bit layout, signed up-positive 16-bit thumbs, triggers 0..255, 7849 per-axis deadzone (magnitude passthrough, documented), `publish_sample`, `gamepad_state_changed`, `apply_thumb_deadzone`, `axis_to_i16` / `axis_to_u8_trigger` converters. Exactly four pad slots.
- `include/isaec/platform/sdl_input.hpp`, `src/platform/sdl_input.cpp` — `SDL_GameController` enumeration, hot-plug rescan on poll, Y-axis negation (SDL up-negative → XInput up-positive), both-motor rumble via `SDL_GameControllerRumble`, lazy `SDL_INIT_GAMECONTROLLER` init. Built only when SDL2 is found (`ISAEC_SDL_FOUND`); headless/CI builds unchanged.
- `tests/test_input.cpp` — pure-layer fixtures (default zeros, first-publish packet bump, no bump on identical republish, four-slot bound, deadzone boundary 7849/7850, axis clamping, trigger rounding, XInput bit layout). **100/100 tests pass locally.**

## Attribution

Controller mapping design and glue idioms adapted from **chiikabu/the-browsing-of-isaac Half 1** (`scripts/recomp/web/gamepad.mjs`, `scripts/recomp/host/src/host_shims_xinput.c`, GPL-3.0-only since PR #63), applied as idiomatic C++17, never transliterated. Half 2 (Nicalis-derived lift, `native/decomp/*`, `boot.wasm`, model JSONs, `decomp/*.json`) never enters this tree; Half-2 marker grep returns only doc strings describing its exclusion plus this header's own 'never present' note.

## Not done (tracked in #65)

Renderer event-queue integration, arm64 Android backend, touch controls (blocked on #5), Unity-style input package ergonomics.

</details>

#### Closes

- [` `] [#5](https://github.com/caos-obliquo/tboi-recomp/issues/5) Implement a real GPU renderer backend (portable, Android-first) (open)
- [` `] [#63](https://github.com/caos-obliquo/tboi-recomp/pull/63) docs(license): relicense MIT -> GPL-3.0-only for the-browsing-of-isaac [#62](https://github.com/caos-obliquo/tboi-recomp/issues/62) (merged)
- [` `] [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65) Input: XInput-shaped controller state (adapt The Browsing of Isaac's gamepad/shims, with attribution) + Android touch later (open)

#### Commits

- `3e2391e1` feat(engine): XInput-shaped controller state + SDL2 backend [#65](htt…
  …ps://github.com/caos-obliquo/tboi-recomp/issues/65)
  WHY: the engine had no input layer at all — the white-box slice is keyboard-only, and a game that needs a controller cannot be played. Roadmap Phase 3 names this issue as the path there.
  WHAT: four-pad XInput-shaped GamepadState (dwPacketNumber bumped only on change, 16-bit up-positive thumbs, triggers 0-255, wButtons XInput bit layout, 7849 deadzone per-axis documented passing magnitude through), publish_sample with deadzone-before-compare, axis_to_i16/axis_to_u8_trigger converters, isaec::platform::SdlGamepadSource (SDL_GameController enumeration + hot-plug rescan, Y-axis negation, rumble both motors via SDL_GameControllerRumble). Headless tests: 100/100 green.
  ATTRIBUTION: controller mapping design and glue idioms adapted from chiikabu's the-browsing-of-isaac Half 1 (scripts/recomp/web/gamepad.mjs, scripts/recomp/host/src/host_shims_xinput.c, GPL-3.0-only since PR #63), written as idiomatic C++17 — never transliterated. Half 2 code never enters; rg for Half-2 markers returns only descriptive doc hits plus the header's own 'never present' comment.
  Not done (tracked in #65): game-loop integration (renderer event queue wiring), arm64 Android backend, touch controls (blocked on #5).

#### Files changed

| file | + | - |
|---|---:|---:|
| `CMakeLists.txt` | 2 | 0 |
| `README.md` | 1 | 0 |
| `docs/reimplementation-references.md` | 20 | 0 |
| `include/isaec/engine/input.hpp` | 109 | 0 |
| `include/isaec/platform/sdl_input.hpp` | 60 | 0 |
| `src/engine/input.cpp` | 67 | 0 |
| `src/platform/sdl_input.cpp` | 135 | 0 |
| `tests/CMakeLists.txt` | 1 | 0 |
| `tests/test_input.cpp` | 138 | 0 |

#### Documentation

- `README.md`
- `docs/reimplementation-references.md`


## [0.1.11] - 2026-11-17

### Merged: [#78](https://github.com/caos-obliquo/tboi-recomp/pull/78) — docs(handoff): tracked state snapshot for fresh sessions [#77]

**Author:** @caos-obliquo  **Merged:** 2026-11-16  **Files:** 2 changed, +27/-0.

<details>
<summary>Pull request body</summary>

Closes #77.

Adds `docs/handoff.md` (dated 2026-11-16): snapshot of main @ `bdf2abb`, open PR #76 state, #37 Clamp verdict pointer, standing locks verbatim, cold-start recipe (AGENTS.md → README.md → `gh issue list` → `docs/roadmap.md`), and the recorded dead ends (#52 LZW behind runtime vtable; #37 runtime-only settlement; REPENTOGON `.zhl` silent; busy-loop upstream bug; #74 release-note 10 pending branches).

README gains a docs-list row. Half-2 never enters anywhere. User's `tools/isaac-archive.py` (M) and `HANDOFF.md` (??) untouched. Only `docs/handoff.md` and `README.md` in the diff.

</details>

#### Closes

- [`x`] [#37](https://github.com/caos-obliquo/tboi-recomp/issues/37) Vector:Clamp / Vector:Clamped read four numbers, the public API documents two (closed)
- [` `] [#52](https://github.com/caos-obliquo/tboi-recomp/issues/52) LZW (mode 0x01): the decoder over-produces past the recorded size; chunks after the first produce garbage (open)
- [` `] [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74) Release-record workflow fails on every merge: Actions may not create PRs; release/pr-* branches pile up (open)
- [` `] [#76](https://github.com/caos-obliquo/tboi-recomp/pull/76) feat(engine): XInput-shaped controller state + SDL2 backend [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65) (merged)
- [`x`] [#77](https://github.com/caos-obliquo/tboi-recomp/issues/77) Docs: tracked docs/handoff.md so a fresh session is self-contained (closed)

#### Commits

- `876d9acf` docs(handoff): tracked state snapshot for fresh sessions [#77](https:…
  …//github.com/caos-obliquo/tboi-recomp/issues/77)
  Why: the untracked HANDOFF.md is stale and off-limits, and a fresh session
  needs a self-contained entry point. docs/handoff.md records the state on
  2026-11-16: main @ bdf2abb, PR #76 open (all 9 required contexts must go
  green before merge), #37 Clamp/Clamped verdict, standing locks verbatim,
  dead ends not to redo.
  Half-2 never enters; user's tools/isaac-archive.py (M) and HANDOFF.md (??)
  untouched; README gains a docs-list row.
- `bfd9e546` docs(handoff): 5-section snapshot, 26 lines [#77](https://github.com/…
  …caos-obliquo/tboi-recomp/issues/77)
  Why: the tracked snapshot was 114 lines of prose naming main @ bdf2abb and
  a pending PR #76; main is now 7b59738 (PR #76 MERGED) and the user asked
  for a max-40-line five-section handoff. The rewrite records verified
  state only (gh pr view 76 MERGED 2026-11-16T04:00:13Z; gh pr checks 63
  9/9; issue #37 closed). User's tools/isaac-archive.py and untracked
  HANDOFF.md untouched.

#### Files changed

| file | + | - |
|---|---:|---:|
| `README.md` | 1 | 0 |
| `docs/handoff.md` | 26 | 0 |

#### Documentation

- `README.md`
- `docs/handoff.md`


## [0.1.12] - 2026-11-17

### Merged: [#80](https://github.com/caos-obliquo/tboi-recomp/pull/80) — docs(handoff): playtesting box is the mandatory next deliverable [#79]

**Author:** @caos-obliquo  **Merged:** 2026-11-17  **Files:** 1 changed, +19/-11.

<details>
<summary>Pull request body</summary>

Updates `docs/handoff.md` (dated 2026-11-17) to record the owner directive from the 2026-11-17 working session: none of the previously listed candidate next steps are mandatory — the mandatory next deliverable is a **playtesting box**, tracked as #79 (definition/scope pending owner input; nothing here implies scope).

- §2 lists the edit; §3 carries re-verified PR/issue state; §4 names the playtesting box as mandatory and the candidates (#64, #66, #52, #3/#4, #35) as not mandatory; §5 points at #79.
- 34 lines, 5 sections, no invented results. Only `docs/handoff.md` in the diff.
- User's `tools/isaac-archive.py` (M), `HANDOFF.md` (??), `build-input/` (??) untouched.

Refs #79

</details>

#### Closes

- [` `] [#3](https://github.com/caos-obliquo/tboi-recomp/issues/3) Recover 655 native function signatures and bodies; unblock per-class attribution (open)
- [` `] [#4](https://github.com/caos-obliquo/tboi-recomp/issues/4) Attribute method names to all ~23 classes (blocked on #3) (open)
- [` `] [#35](https://github.com/caos-obliquo/tboi-recomp/issues/35) Wire the 76 metamethods from the generated spec into the runtime binding (open)
- [` `] [#52](https://github.com/caos-obliquo/tboi-recomp/issues/52) LZW (mode 0x01): the decoder over-produces past the recorded size; chunks after the first produce garbage (open)
- [` `] [#64](https://github.com/caos-obliquo/tboi-recomp/issues/64) Audio: no audio code exists anywhere in the tree — add an injected Audio interface with null and SDL2 backends (open)
- [` `] [#66](https://github.com/caos-obliquo/tboi-recomp/issues/66) Epic: Linux x86_64 and Android arm64 as first-class targets (packaging, on-device verification, frame pacing, docs) (open)
- [` `] [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79) Playtesting box: the mandatory next deliverable (definition and scope) (open)

#### Commits

- `408a56fa` docs(handoff): playtesting box is the mandatory next deliverable [#79]
  Owner directive 2026-11-17: the listed candidates (#64 audio, #66 epic,
  #52 LZW, #3/#4 signatures, #35 metamethods) are not mandatory. The tracked
  handoff doc now names the playtesting box (#79) as the next deliverable and
  records that scope is undefined pending owner input - definition, gating
  subsystems, platform(s), verification meaning. [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79)

#### Files changed

| file | + | - |
|---|---:|---:|
| `docs/handoff.md` | 19 | 11 |

#### Documentation

- `docs/handoff.md`


## [0.1.13] - 2026-11-17

### Merged: [#81](https://github.com/caos-obliquo/tboi-recomp/pull/81) — chore(release): record the eight unrecorded merges since v0.1.4 (bump to 0.1.12) [#74]

**Author:** @caos-obliquo  **Merged:** 2026-11-17  **Files:** 2 changed, +542/-1.

<details>
<summary>Pull request body</summary>

Records the eight merges that `Release version` failed to record because Actions was not permitted to create PRs (issue #74), whereas the workflow's `bump-notes` and branch push both succeeded each time.

8 tool-composed commits, one per merge, `python3 tools/release.py bump-notes --pr N`, not hand-edited:

- #63 → 0.1.5 (relicense MIT → GPL-3.0-only)
- #68 → 0.1.6 (playable white-box slice, Linux x86_64)
- #72 → 0.1.7 (Lua API external oracles)
- #73 → 0.1.8 (roadmap: Repentance+ build order)
- #75 → 0.1.9 (lua-api-reference: Clamp/Clamped rejected)
- #76 → 0.1.10 (XInput-shaped controller state + SDL2 backend)
- #78 → 0.1.11 (docs/handoff.md #77)
- #80 → 0.1.12 (mandatory playtesting box, #79)

`CMakeLists.txt` `project(... VERSION)` and `CHANGELOG.md` agree; `python3 tools/release.py check` → "changelog ok for 0.1.12". CHANGELOG layout is exactly the tool's insertion shape (sections inserted above `## [Unreleased]`); nothing hand-reordered. Note, for the record: no `[v0.1.4]` section ever existed (PR #61 merged the bump without notes), and no v0.1.4 or v0.1.5+ tags/releases have been cut — tagging is a separate, still-manual step, outside this PR.

Title carries the `chore(release):` prefix deliberately so this PR is not recorded by the recorder it exists to repair.

Refs #74

</details>

#### Closes

- [` `] [#61](https://github.com/caos-obliquo/tboi-recomp/pull/61) chore(release): bump version to 0.1.4 (merged)
- [` `] [#63](https://github.com/caos-obliquo/tboi-recomp/pull/63) docs(license): relicense MIT -> GPL-3.0-only for the-browsing-of-isaac [#62](https://github.com/caos-obliquo/tboi-recomp/issues/62) (merged)
- [` `] [#68](https://github.com/caos-obliquo/tboi-recomp/pull/68) feat(platform): playable white-box slice for Linux x86_64 (merged)
- [` `] [#72](https://github.com/caos-obliquo/tboi-recomp/pull/72) docs(lua-api): external Lua-API oracles, graded (merged)
- [` `] [#73](https://github.com/caos-obliquo/tboi-recomp/pull/73) docs(roadmap): Repentance+ build order, references and grades (merged)
- [` `] [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74) Release-record workflow fails on every merge: Actions may not create PRs; release/pr-* branches pile up (open)
- [` `] [#75](https://github.com/caos-obliquo/tboi-recomp/pull/75) docs(lua-api-reference): Clamp/Clamped rejected; CI contexts 9 [#37](https://github.com/caos-obliquo/tboi-recomp/issues/37) (merged)
- [` `] [#76](https://github.com/caos-obliquo/tboi-recomp/pull/76) feat(engine): XInput-shaped controller state + SDL2 backend [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65) (merged)
- [`x`] [#77](https://github.com/caos-obliquo/tboi-recomp/issues/77) Docs: tracked docs/handoff.md so a fresh session is self-contained (closed)
- [` `] [#78](https://github.com/caos-obliquo/tboi-recomp/pull/78) docs(handoff): tracked state snapshot for fresh sessions [#77] (merged)
- [` `] [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79) Playtesting box: the mandatory next deliverable (definition and scope) (open)
- [` `] [#80](https://github.com/caos-obliquo/tboi-recomp/pull/80) docs(handoff): playtesting box is the mandatory next deliverable [#79] (merged)

#### Commits

- `1334055c` chore(release): record PR #63 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `91afdb0a` chore(release): record PR #68 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `8ad8dcfc` chore(release): record PR #72 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `c14a2294` chore(release): record PR #73 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `c1289481` chore(release): record PR #75 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `1a9f03d6` chore(release): record PR #76 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `a1bca4a0` chore(release): record PR #78 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.
- `83a7df10` chore(release): record PR #80 and bump the patch version
  Part of the unrecorded-merges catch-up for [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74): the
  Release version workflow has been red since PR #76 because Actions could not
  create PRs, so no release PR was opened for this merge. Composed with
  'tools/release.py bump-notes', not by hand.

#### Files changed

| file | + | - |
|---|---:|---:|
| `CHANGELOG.md` | 541 | 0 |
| `CMakeLists.txt` | 1 | 1 |

#### Documentation

- `CHANGELOG.md`


## [0.1.14] - 2026-11-17

### Merged: [#83](https://github.com/caos-obliquo/tboi-recomp/pull/83) — docs: record release catch-up + fix stale workflow notes [#74]

**Author:** @caos-obliquo  **Merged:** 2026-11-17  **Files:** 3 changed, +33/-17.

<details>
<summary>Pull request body</summary>

Docs-only follow-through for the release catch-up (PR #81) and issue #74. Also the functional test of the flipped Actions setting: this merge fires `version.yml`, which should now open the record PR for this PR instead of failing at "Open release PR".

- `README.md` docs-list row for `docs/handoff.md`: updated to the 2026-11-17 re-scope; playtesting box (#79) named mandatory.
- `docs/HANDOFF.md` §12: required checks corrected to the verified 9 (`gh api repos/.../required_status_checks`, strict=true, 2026-11-17); the old "11 including nonexistent macos-*, PR #10 open" paragraph (wrong on both counts) removed with the correction visible; release-record fix (#74) noted, including what the 2026-11-17 catch-up did and did not establish.
- `docs/handoff.md`: 39 lines, 5 sections, dated 2026-11-17; §3 now carries PR #81's verified facts.

Refs #74

</details>

#### Closes

- [` `] [#10](https://github.com/caos-obliquo/tboi-recomp/pull/10) ci: cover ARM64 natively and stop macOS coverage silently narrowing (merged)
- [` `] [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74) Release-record workflow fails on every merge: Actions may not create PRs; release/pr-* branches pile up (open)
- [` `] [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79) Playtesting box: the mandatory next deliverable (definition and scope) (open)
- [` `] [#81](https://github.com/caos-obliquo/tboi-recomp/pull/81) chore(release): record the eight unrecorded merges since v0.1.4 (bump to 0.1.12) [#74] (merged)

#### Commits

- `0cf14f3d` docs: record release catch-up + correction of stale workflow notes [#74]
  README docs-list row for docs/handoff.md now matches the 2026-11-17
  re-scope (mandatory deliverable: playtesting box #79). docs/HANDOFF.md
  §12 is corrected to the verified 9-context required list (was a stale
  '11 incl macos-*' claim and a stale PR #10 paragraph) and records the
  release-record workflow fix from #74. docs/handoff.md re-dated
  2026-11-17 in §3 with PR #81 verified facts. Part of [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74).

#### Files changed

| file | + | - |
|---|---:|---:|
| `README.md` | 1 | 1 |
| `docs/HANDOFF.md` | 20 | 9 |
| `docs/handoff.md` | 12 | 7 |

#### Documentation

- `README.md`
- `docs/HANDOFF.md`
- `docs/handoff.md`


## [0.1.15] - 2026-11-17

### Merged: [#85](https://github.com/caos-obliquo/tboi-recomp/pull/85) — fix(ci): make the release-record skip actually apply [#74]

**Author:** @caos-obliquo  **Merged:** 2026-11-17  **Files:** 1 changed, +4/-3.

<details>
<summary>Pull request body</summary>

The `if:` that was meant to skip the release-record PRs was inside the `on:` event map. GitHub ignores `if` there, so the skip never applied — merging a record PR just spawned the next one (run 44013149509 executed for #81 despite its `chore(release):` title and opened PR #82).

- `if:` moved to a job-level condition (where it is supported): skip when the closing PR either starts with `chore(release):` (title) or has a `release/pr-*` head ref.
- No other behaviour changes; the bump/push/PR-open steps are unchanged.
- Record PRs created by `GITHUB_TOKEN` still receive no CI runs (`pull_request` workflows don't fire for GITHUB_TOKEN-created events) — tracked on #74; this PR does not claim to fix that, it stops the infinite chain.

Refs #74

</details>

#### Closes

- [` `] [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74) Release-record workflow fails on every merge: Actions may not create PRs; release/pr-* branches pile up (open)
- [` `] [#81](https://github.com/caos-obliquo/tboi-recomp/pull/81) chore(release): record the eight unrecorded merges since v0.1.4 (bump to 0.1.12) [#74] (merged)
- [` `] [#82](https://github.com/caos-obliquo/tboi-recomp/pull/82) chore(release): record PR #81 and bump the patch version (open)

#### Commits

- `3b15fb7f` fix(ci): make the release-record skip actually apply [#74]
  The workflow-level if lived inside the on: event map, where GitHub
  ignores it - every merged PR, including the record PRs the workflow
  itself creates, spawned another record PR (run 44013149509 ran for #81,
  whose title starts with chore(release), and created release/pr-82). The
  intended skip never applied.
  Move the skip to a job-level if (supported there) covering both title
  prefix and head ref. [#74](https://github.com/caos-obliquo/tboi-recomp/issues/74)

#### Files changed

| file | + | - |
|---|---:|---:|
| `.github/workflows/version.yml` | 4 | 3 |


## [0.1.16] - 2026-11-17

### Merged: [#88](https://github.com/caos-obliquo/tboi-recomp/pull/88) — feat(sandbox): white-box playtesting box — controllable room [#79]

**Author:** @caos-obliquo  **Merged:** 2026-11-17  **Files:** 8 changed, +370/-24.

<details>
<summary>Pull request body</summary>

Implements the owner-mandated playtesting box scope (2026-11-17, #79): a **white-box playtesting sandbox** — a controllable entity in an empty room, moved by keyboard and XInput-shaped pad, no Isaac game data, not a port of any existing game's code, no new runtime subsystems.

## What lands

- **`include/isaec/engine/sandbox.hpp` + `src/engine/sandbox.cpp`** — platform-independent math: keyboard digits ±1 with diagonals normalized to unit length; pad thumbsticks pre-deadzoned by the engine layer, rescaled to [-1, 1] with Y flipped to the screen convention; sources add and clamp; `sandbox_step` advances at caller speed/dt (fixed 1/60) and clamps the square entity fully inside the room inner bounds.
- **`src/platform/sdl_main.cpp`** — `isaec_window slice` is now playable: arrows/WASD + pad 0 (XInput-shaped, 7849 deadzone), Esc/close quits. `frames=0` = play until Esc (playtesting mode); default 300 frames keeps headless runs self-terminating. Slice header comment + usage string updated.
- **`tests/test_sandbox.cpp`** — 12 new cases (keyboard cardinals, diagonal normalization, pad Y flip, add-and-clamp, opposing cancel, dt/speed integration, wall containment, zero input, entity-larger-than-room centring). Wired into `tests/CMakeLists.txt`; `sandbox.cpp` added to `isaec_core` sources.
- **README progress row** updated in the same commit (AGENTS §5).

## Verification (measured on this machine, 2026-11-17)

- `cmake --build build -j` — clean (only the known third_party/lua-5.3.6 `tmpnam` linker warning, pre-existing).
- `ctest` 1/1 pass; `./build/tests/isaec_tests` → **108 cases, 0 failed** (incl. the new sandbox suite).
- Headless smoke: `SDL_VIDEODRIVER=dummy ./build/isaec_window slice 320 240 90` — 90 frames, controls line printed, draws 540 (6×90), EXIT 0, est ~57 Hz (dummy driver).
- **Interactive keyboard/pad movement NOT run — this session has no display.** The motion path is unit-tested for the math; the SDL sampling path is the same pattern as #65's pad pipeline (poll → states()) and is unverified with real input.

## Honest limits

- Not the game; no Isaac assets are read. GPU rendering remains #5.
- One-frame input lag (keyboard sampled before `present()`'s event pump) — documented in code; fine for a sandbox.
- Movement speed 240 px/s is a sandbox default, not a game value.
- `docs/HANDOFF.md` and the long-form handoff still describe the box as proposed until this merges.

Refs #79 (does not close — all AC items must hold first, incl. same-turn evidence comment).

</details>

#### Closes

- [` `] [#5](https://github.com/caos-obliquo/tboi-recomp/issues/5) Implement a real GPU renderer backend (portable, Android-first) (open)
- [` `] [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65) Input: XInput-shaped controller state (adapt The Browsing of Isaac's gamepad/shims, with attribution) + Android touch later (open)
- [` `] [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79) Playtesting box: the mandatory next deliverable (definition and scope) (open)

#### Commits

- `3d4d3454` feat(sandbox): white-box playtesting box — controllable room (#79)
  Extends the #67 white-box slice into the owner-mandated playtesting box
  (issue #79, scope per owner 2026-11-17: White-box playtesting sandbox):
  a controllable entity in an empty room, moved by keyboard (arrows/WASD)
  or the stick of XInput-shaped pad 0. No game data, no Isaac assets,
  no port of any existing game's movement code.
  Mechanics:
  - include/isaec/engine/sandbox.hpp + src/engine/sandbox.cpp: platform-
    independent math. Keyboard digits contribute ±1 with diagonals
    normalized to unit length; pad thumbsticks arrive pre-deadzoned from
    the engine layer and are rescaled to [-1, 1] with Y flipped to the
    screen convention; sources add and clamp to ±1; sandbox_step advances
    at a caller speed/dt and clamps the square entity fully inside the
    inner room bounds (centers an over-large entity instead of clamping
    into an inverted range).
  - src/platform/sdl_main.cpp: `isaec_window slice` now samples
    SDL_GetKeyboardState (arrows + WASD) and pads.states()[0] (player 1)
    each frame, integrates the entity at 240 px/s fixed-step dt = 1/60
    (pacing unchanged at ~60 Hz), clamps to the room, and moves the
    marker DrawCall. `frames=0` now plays until Esc/close (the real
    playtest mode); the default 300-frame budget stays so headless smoke
    runs self-terminate. Esc/window close quits as before.
  - tests/test_sandbox.cpp (wired into tests/CMakeLists.txt and
    CMakeLists.txt isaec_core sources): 12 new cases — keyboard cardinals,
    diagonal normalization, pad rescale + Y flip, add-and-clamp, opposing
    cancel, dt/speed integration, containment at all walls, zero move /
    zero dt / negative speed, entity-larger-than-room centring.
  Verification (measured this session):
  - cmake --build build -j — clean (only the known
    third_party/lua-5.3.6 loslib.c tmpnam linker warning, pre-existing).
  - ctest — 1/1 pass; isaec_tests reports 108 cases, 0 failed.
  - SDL_VIDEODRIVER=dummy ./build/isaec_window slice 320 240 90 —
    90 frames, scene "white-box playtest room", draws 540, EXIT 0,
    est ~57 Hz; interactive keyboard/pad move NOT run (no display here).
  README progress row updated in the same commit as required.

#### Files changed

| file | + | - |
|---|---:|---:|
| `CMakeLists.txt` | 1 | 0 |
| `README.md` | 10 | 6 |
| `docs/handoff.md` | 28 | 8 |
| `include/isaec/engine/sandbox.hpp` | 75 | 0 |
| `src/engine/sandbox.cpp` | 75 | 0 |
| `src/platform/sdl_main.cpp` | 45 | 10 |
| `tests/CMakeLists.txt` | 1 | 0 |
| `tests/test_sandbox.cpp` | 135 | 0 |

#### Documentation

- `README.md`
- `docs/handoff.md`


## [0.1.17] - 2026-11-17

### Merged: [#91](https://github.com/caos-obliquo/tboi-recomp/pull/91) — docs(handoff): record the playtesting box landed [#90]

**Author:** @caos-obliquo  **Merged:** 2026-11-17  **Files:** 1 changed, +13/-12.

<details>
<summary>Pull request body</summary>

Updates `docs/handoff.md` to the post-#88 truth:

- §2/§3: PR #88 merged (9/9 green, run 44020322024) → `main` @ `fce5a9e`; verification lines re-mapped to the merged state.
- §3: records the release-record consequence of #88's merge — `version.yml` run 44020505173 succeeded and auto-opened checkless PR **#89** (GITHUB_TOKEN-created record PRs trigger no CI, cannot satisfy the 9 required contexts). Resolution tracked in #90 (established catch-up pattern: #81, #87).
- §4/§5: #79 stays OPEN until the owner witnesses an interactive run or waives; catch-up for #88 + this PR is the exact next step.

Refs #90. Note: this PR deliberately does not start #64/#66/#52/#3/#4/#35 (not mandatory — owner 2026-11-17).

</details>

#### Closes

- [` `] [#3](https://github.com/caos-obliquo/tboi-recomp/issues/3) Recover 655 native function signatures and bodies; unblock per-class attribution (open)
- [` `] [#4](https://github.com/caos-obliquo/tboi-recomp/issues/4) Attribute method names to all ~23 classes (blocked on #3) (open)
- [` `] [#35](https://github.com/caos-obliquo/tboi-recomp/issues/35) Wire the 76 metamethods from the generated spec into the runtime binding (open)
- [` `] [#52](https://github.com/caos-obliquo/tboi-recomp/issues/52) LZW (mode 0x01): the decoder over-produces past the recorded size; chunks after the first produce garbage (open)
- [` `] [#64](https://github.com/caos-obliquo/tboi-recomp/issues/64) Audio: no audio code exists anywhere in the tree — add an injected Audio interface with null and SDL2 backends (open)
- [` `] [#66](https://github.com/caos-obliquo/tboi-recomp/issues/66) Epic: Linux x86_64 and Android arm64 as first-class targets (packaging, on-device verification, frame pacing, docs) (open)
- [` `] [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79) Playtesting box: the mandatory next deliverable (definition and scope) (open)
- [` `] [#81](https://github.com/caos-obliquo/tboi-recomp/pull/81) chore(release): record the eight unrecorded merges since v0.1.4 (bump to 0.1.12) [#74] (merged)
- [` `] [#87](https://github.com/caos-obliquo/tboi-recomp/pull/87) chore(release): record the three release-workflow merges (bump to 0.1.15) [#74] (merged)
- [` `] [#88](https://github.com/caos-obliquo/tboi-recomp/pull/88) feat(sandbox): white-box playtesting box — controllable room [#79] (merged)
- [` `] [#89](https://github.com/caos-obliquo/tboi-recomp/pull/89) chore(release): record PR #88 and bump the patch version (open)
- [` `] [#90](https://github.com/caos-obliquo/tboi-recomp/issues/90) Release record: catch up the #88 merge and the handoff docs PR — record PRs #89+ are checkless orphans (open)

#### Commits

- `9bc985e9` docs(handoff): record the playtesting box landed; #89 orphan open [#90]

#### Files changed

| file | + | - |
|---|---:|---:|
| `docs/handoff.md` | 13 | 12 |

#### Documentation

- `docs/handoff.md`


## [0.1.18] - 2026-11-18

### Merged: [#104](https://github.com/caos-obliquo/tboi-recomp/pull/104) — feat(bindings): translate the BitSet128 natives [#103](https://github.com/caos-obliquo/tboi-recomp/issues/103)

**Author:** @caos-obliquo  **Merged:** 2026-11-18  **Files:** 10 changed, +742/-8.

<details>
<summary>Pull request body</summary>

Closes #103.

Translates the nine `BitSet128` natives from the reference executable
(v1.9.7.17.J460, sha256 `3bdfc8ba...4616b`) into C++:

| member | address | shape |
|---|---|---|
| `Get(index)` | `0x0085e560` | bool |
| `Set(index, value)` | `0x0085e5c0` | — |
| `__band` | `0x0085e4d0` | new value |
| `__bor` | `0x0085e4f0` | new value |
| `__bxor` | `0x0085e510` | new value |
| `__bnot` | `0x00592200` | new value |
| `__eq` | `0x0085e530` | bool |
| `__tostring` | `0x0085e490` | text |
| constructor | `0x008bc330` | `BitSet128(low, high)` |

Behaviour recovered from the binary:

- Payload is four 32-bit words; the generated field class already supplies
  `l` at offset 0 and `h` at offset 8 as `Int64`.
- `Get` returns false and `Set` is a no-op for a negative index or an index
  of 128 or more — the recovered bounds check, not a Lua error.
- The bitwise metamethods are non-mutating: they build and return a new
  `BitSet128`. A non-`BitSet128` operand raises the native's error.
- `__tostring` is exactly `BitSet128: %016llx%016llx`, high half first.

No game-side oracle is needed: every one of these bodies is pure arithmetic
over the payload, so the static translation is the whole result.

Tests: `tests/test_bitset128.cpp` covers every word boundary, the
out-of-range paths, set/clear, non-mutation, equality, the exact text, the
constructor halves and the foreign-operand errors. Suite is **138 cases,
0 failed** (was 128). Build and `ctest` both green locally.

Docs: `docs/binding-surface-findings.md` §26 (authoritative addresses),
`docs/HANDOFF.md` §29, README Progress now 372 implemented members and
42 translated native methods.

</details>

#### Closes

- [`x`] [#103](https://github.com/caos-obliquo/tboi-recomp/issues/103) BitSet128: translate Get/Set and the bitwise metamethods (closed)

#### Commits

- `1c9e0acc` feat(bindings): translate the BitSet128 natives [#103](https://github…
  ….com/caos-obliquo/tboi-recomp/issues/103)
  Translate the nine `BitSet128` natives from the reference executable
  (v1.9.7.17.J460, sha256 3bdfc8ba...4616b) into C++: `Get` (0x0085e560),
  `Set` (0x0085e5c0), `__band` (0x0085e4d0), `__bor` (0x0085e4f0),
  `__bxor` (0x0085e510), `__bnot` (0x00592200), `__eq` (0x0085e530),
  `__tostring` (0x0085e490) and the callable constructor (0x008bc330).
  The payload is four 32-bit words. `Get` returns false for a negative index
  or an index of 128 or more and `Set` ignores both, which is what the
  recovered bounds checks do rather than a Lua error. The bitwise operators
  build a new value and never mutate the receiver; a non-`BitSet128` operand
  raises the same error the native does. `__tostring` prints the high half
  first: `BitSet128: %016llx%016llx`.
  The class table is callable, matching the other value types. `tests/
  test_bitset128.cpp` covers every word boundary, the out-of-range paths,
  set/clear, non-mutation, equality, the exact text, the constructor halves
  and the foreign-operand errors; the suite is 138 cases, 0 failed (was 128).
  Addresses and reasoning are in docs/binding-surface-findings.md §26, the
  handoff note in docs/HANDOFF.md §29, and the Progress table in README.md
  now reads 372 implemented members and 42 translated native methods.

#### Files changed

| file | + | - |
|---|---:|---:|
| `CMakeLists.txt` | 2 | 0 |
| `README.md` | 9 | 8 |
| `docs/HANDOFF.md` | 24 | 0 |
| `docs/binding-surface-findings.md` | 114 | 0 |
| `include/isaec/api/bitset128.hpp` | 79 | 0 |
| `include/isaec/api/bitset128_lua.hpp` | 26 | 0 |
| `src/api/bitset128.cpp` | 72 | 0 |
| `src/api/bitset128_lua.cpp` | 187 | 0 |
| `tests/CMakeLists.txt` | 1 | 0 |
| `tests/test_bitset128.cpp` | 228 | 0 |

#### Documentation

- `README.md`
- `docs/HANDOFF.md`
- `docs/binding-surface-findings.md`


## [Unreleased]

## [v0.1.3] - 2026-10-23

### Fixed

- The release workflow could not build on a clean runner: it never installed
  `libpng-dev`, while `CMakeLists.txt` requires `libpng16` through
  `pkg_check_modules(... REQUIRED ...)`. The v0.1.2 tag run (39248651627)
  died in `FindPkgConfig.cmake` with `Package 'libpng16' ... not found`.
  `ci.yml` already installed it; `release.yml` now does the same.
  Why: the two workflows drifted, and only the CI one ever proved itself on a
  bare runner. Effect: a tag run can reach the compile step.
- The release workflow's version-bump step pushed straight to `main` and hit
  the branch protection rule (GH006), the same defect already fixed for
  `version.yml` in [#54](https://github.com/caos-obliquo/tboi-recomp/issues/54).
  It now commits locally, pushes `release/bump-<version>`, and opens a PR; the
  `chore(release):` title prefix makes `version.yml` skip itself, so no double
  bump. Effect: a completed release leaves a reviewable bump instead of a
  failed push.

## [v0.1.0] - 2026-10-12

First tagged release. A scaffold, not a playable game: the binding surface is
recovered and registered, and nothing renders yet. See the "Where this actually
stands" section of the README for what exists and what does not.

### Added
- `tools/kuna_batch.py` — resumable batch decompiler: writes each body immediately and maintains an append-only index for resume safety (issue #15)

- Class registry implementing Isaac's recovered metatable model — `__class`,
  `__parent`, `__index`, `__propget`, `__propset`, `__newindex`, `__const`,
  `__metatable`, `__type` — with single-parent inheritance and read-only
  property mirroring
- 655 native method names recovered from the shipped binary's binding tables and
  registered through a declarative `ApiSpec` -> `Binding` codegen path, covering
  `Entity` 261, `Input` 118, `Isaac` 76, `EntityPlayer` 77, `HUD` 42, `Sprite`
  33, `Vector` 22, `KColor` 18, `Font` 8
- `tools/gen-api-spec.py` regenerates both the JSON spec and a compiled C++ header
  from committed recovered data, so a malformed method name fails at compile
  time rather than at load time
- `tools/extract-binding-tables.py`, which re-derives the binding surface from a
  copy of the game the user already owns, so the recovery is reproducible rather
  than a claim
- Engine core: `mount` / `start` / `tick` / `stop`, a frame clock, and a pure
  virtual `Renderer` interface with a headless `NullRenderer`
- CI: a 9-job pipeline covering gcc, clang, AppleClang, `-Werror`, ASan + UBSan,
  an arm64 Android NDK cross-compile, generated-spec freshness, and an extractor
  self-check
- Governance: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, three issue templates, a
  PR template with a clean-room attestation, and `dependabot` for Actions

### Changed

- Lua pinned to **5.3** and vendored, rather than discovered.
  Isaac's interpreter is Lua 5.3; 5.4, 5.5, and LuaJIT are all wrong.
  `find_package(Lua 5.3)` had no upper bound and silently accepted the host's
  5.5, so it is now an existence check on the vendored tree.
- The spec generator reads its recovered data from inside the repository.
  It previously read `/tmp/opencode/`, which meant a fresh checkout could not
  regenerate the spec and CI would have failed on its first run.

### Fixed

- **`lineage()` reported a cycle as one class too many.**
  Why: the walk pushed each class before testing whether it had already been
  seen, so an `EntityPlayer` <-> `Entity` cycle produced `[A, B, A]`.
  Effect: it now stops on revisit. The hop bound that papered over the problem
  was removed, because a repeat check over a finite class set already
  terminates.
- **A duplicate constructor registration was silently accepted.**
  Why: `register_constructor` wrote to Lua and returned `true` unconditionally,
  with no registry-side tracking, so a spec listing the same constructor twice
  would quietly replace the Lua global instead of reporting the mistake.
  Effect: `ClassEntry` tracks its constructor and a second registration is
  rejected, matching `register_class`.
- **The headless binary read the wrong command-line argument.**
  Why: `next_arg` incremented the cursor before reading, so
  `isaec_headless <install> 120` looked for a data source under `120`.
  Effect: frames are read from the argument the user actually passed.
- **The Android CI job was validating itself against the host toolchain.**
  Why: five separate attempts each asserted something the host could satisfy —
  an env var the previous step had not exported, a step id that silently
  shadowed the NDK installer's own, `CMakeCache.txt` keys that are not written
  there, and finally `objdump -f`'s format string, which reads `architecture:
  UNKNOWN!` because host binutils has no aarch64 decoder.
  Effect: the job now locates an NDK by probing for a real
  `aarch64-linux-android24-clang`, refuses to configure if it finds none, and
  confirms success by reading `e_machine` out of the compiled object's ELF
  header. Nothing about that check can be satisfied by a host build.

### Removed

- The GNU `ar` archive reader.
  Why: Isaac's `.a` files begin `ARCH000\0` or `ARCH000\x01`, not `!<arch>\n`,
  and carry no plaintext filenames. The reader was written before the real data
  was inspected and modelled a format this game has never used.
  Effect: `DataSource` supports directories only. The container format is
  tracked as [#1](https://github.com/caos-obliquo/tboi-recomp/issues/1) and [#2](https://github.com/caos-obliquo/tboi-recomp/issues/2) with the disassembly already done.

[Unreleased]: https://github.com/caos-obliquo/tboi-recomp/compare/v0.1.0...HEAD
[v0.1.0]: https://github.com/caos-obliquo/tboi-recomp/releases/tag/v0.1.0