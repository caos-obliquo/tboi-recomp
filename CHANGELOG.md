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


## [0.1.3] - 2026-10-23

### Merged: [#55](https://github.com/caos-obliquo/tboi-recomp/pull/55) — fix(ci): land version bumps via PR instead of pushing to protected main

**Author:** @caos-obliquo  **Merged:** 2026-10-23  **Files:** 1 changed, +15/-4.

<details>
<summary>Pull request body</summary>

Fixes #54. The Release version workflow now commits the bump on a release branch and opens a PR to main instead of pushing directly to protected main.

</details>

#### Closes

- [`x`] [#54](https://github.com/caos-obliquo/tboi-recomp/issues/54) fix(ci): version workflow pushes directly to protected main (closed)

#### Commits

- `c8471021` fix(ci): land version bumps via PR instead of pushing to protected ma…
  …in [#54]

#### Files changed

| file | + | - |
|---|---:|---:|
| `.github/workflows/version.yml` | 15 | 4 |


## [Unreleased]

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