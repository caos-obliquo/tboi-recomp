# Contributing

Thank you for helping. This project is a clean-room reimplementation of the
runtime that *The Binding of Isaac: Repentance* exposes to its own Lua scripts.
Every native function here is written from observed behaviour and public
documentation — never copied from Nicalis code.

## The one rule that is not negotiable

**Do not add any Nicalis-owned content to this repository.**

No Lua sources, no sprites, no audio, no video, no game bytecode, no
`resources/packed/*.a` archive, no extracted copies of any of it, and no
decompiled Nicalis code pasted into a source file. Not in an issue, not in a
test fixture, not "just temporarily". The repository ships our engine and
nothing else; users point it at their own installation at runtime, the same
way OpenMW points at Morrowind.

If you are unsure whether something is derivable or copied: it is copied.
Stop and ask in an issue before committing it.

## What *is* acceptable

- Names and counts of functions that the engine must expose. An interface fact
  is not anyone's copyright.
- Behaviour derived from the public modding wiki, forum posts, and the game's
  own modding API documentation.
- Anything you measured yourself from a copy you own, expressed as
  measurements (offsets, sizes, counts) rather than copied bytes.
- Your own clean-room implementation of a documented behaviour.

## Before opening a pull request

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

CI additionally runs a `-Werror` build, AddressSanitizer + UndefinedBehaviorSanitizer,
an Android arm64 configure against a real NDK, and a check that the generated
API spec is in sync with its committed source. All of it must pass.

Add a test with every behaviour change. `tests/test_harness.hpp` is three
macros: `ISAEC_TEST(name)`, `CHECK(expr)`, `REQUIRE(expr)`.

## Style

The codebase is C++17 and follows the surrounding file's conventions: 4-space
indent, `snake_case` for functions and members, `PascalCase` for types, trailing
member underscores for members. Prefer self-documenting names to comments; a
comment is warranted when a decision is surprising or the "why" is not visible
from the code.

## Reverse-engineering findings

Findings belong in an issue, not in a commit message. When you post one, say
**how you measured it**, not just what you concluded — the value of a finding
is entirely in whether the next person can reproduce it. Write address
arithmetic down as expressions over the section table, never as bare hex.

Do not commit disassembly dumps, decompiler output, or extracted binaries.
Commit the *tool* that produced the finding, not its raw output.

## Reporting a bug

Include the platform, the compiler version, the exact cmake flags, and the full
output of the failing command. If a test fails, the test name and its `CHECK`
expression identify the bug more precisely than any description you can write.

By contributing you agree that your work is licensed under the MIT licence in
[LICENSE](LICENSE).
## Releases

The version lives in one place: the `project(... VERSION x.y.z ...)` line of
`CMakeLists.txt`. `tools/release.py` is the only thing that reads or writes it,
so there is no second copy to drift.

```sh
python3 tools/release.py version        # print it
python3 tools/release.py body 0.2.0     # render release notes for a version
python3 tools/release.py check          # CI gate: does the CHANGELOG cover it
python3 tools/release.py bump patch     # bump and rewrite CMakeLists.txt
```

To cut a release:

1. Add a `## [vX.Y.Z] - YYYY-MM-DD` section to `CHANGELOG.md`, with `### Added`,
   `### Changed`, `### Fixed`, `### Removed` as they apply. Write it for someone
   reading it in six months, not for yourself reading it today.
2. Set `VERSION x.y.z` in `CMakeLists.txt`.
3. Commit, push, then `git tag vX.Y.Z && git push origin vX.Y.Z`.

The Release workflow notices a tag on `main`, builds a linux-x86_64 tarball,
publishes it, and then bumps the patch version itself with a `[skip ci]`
commit — so the next release only needs a tag.

**A fix entry carries its cause.** `- Something was broken` is not a changelog
line. Use the `Why:` / `Effect:` form:

```markdown
- **Playback stopped when a track could not be downloaded.**
  Why: the skip-on-failure path looked for the next track in the queue, but the
  queue had already been replaced by the album split that caused the failure.
  Effect: the next track is resolved from the failed download itself.
```

`tools/release.py check` fails the build if the version in `CMakeLists.txt` has
no CHANGELOG section, so a release can never publish a bare commit list in place
of an explanation someone wrote.

## Commit messages

Conventional Commits, one line, imperative: `feat(renderer):`, `fix(ci):`,
`docs(tools):`, `chore:`. Scope is optional but prefer one when the change is
confined to a directory. Anything containing `[skip ci]` will not start a
release.
