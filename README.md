# tboi-recomp

A clean-room reimplementation of *The Binding of Isaac: Repentance*, built to run
on arm64 Android and Linux x86_64 from one source tree.

**This is an independent project. It is not affiliated with, endorsed by, or
connected to Nicalis or Edmund McMillen.** "The Binding of Isaac" is a trademark
of its respective owners and is used here only to identify the game this project
aims to be compatible with.

> **Status: early scaffold. 0 of 655 native function bodies are implemented.
> Nothing here plays the game.** The table below is the honest picture. See
> [Progress](#progress) before you get excited.

## Why it exists

I cannot play this game anymore. Launching it on my laptop sends the temperature
straight to 100 degrees before the menu finishes loading, and that is not a
driver I can fix, update, or tune. The game I have played for the better part of
two decades is the game I am locked out of.

Mobile is the obvious answer, and it should have been an easy one. Isaac's
original build is a 32-bit Windows executable bound to DirectX 9 and Win32
audio — it cannot run on a phone, and it never will. Everything that *can* be
rebuilt is the part that is genuinely portable: reading the data, hosting the
Lua VM, and implementing the native functions the game already calls into from
Lua.

Edmund has always been forward about where his games go and who they reach.
Fitting that is the point of this.

If this works, the game comes back to people the hardware already left behind.
That is worth the months.

## Legal boundary

This repository contains **no code, data, sprites, audio, video, or text** from
Nicalis or Edmund McMillen. Not in the source, not in the tests, not in a
fixture, not "just temporarily".

What it does contain is *interface facts*: method names, counts, and structural
observations, recovered by reading a binary the maintainer already owns. Method
names are facts about an interface. Every implementation in this repository is
written from scratch here.

The engine ships **no game data**. It is pointed at a copy you already own, the
way OpenMW points at Morrowind and OpenTTD points at Transport Tycoon. If you
do not own *The Binding of Isaac*, this project has nothing to show you.

MIT licensed. See [`LICENSE`](LICENSE).

## What is being rebuilt, and from what

This is the distinction that matters most and that is most often gotten wrong:

| | What it is | Detail |
|---|---|---|
| **Reference binary** | The thing being read | The **Windows PC / Steam build** — `isaac-ng.exe`, a **32-bit x86 PE**. *Not* an iOS or Android build. |
| **Output target** | What this engine produces | **arm64-android** first, then **linux-x86_64**. |

The reference is a 32-bit Windows PE linked against DirectX 9 and Win32 audio.
It cannot run on Android and never will. So it is not being ported, recompiled,
or re-linked. What is being reimplemented is the portable half of the game: a
data reader, a Lua VM, and the native functions the shipped Lua calls into.

Why the PC build and not iOS? The iOS build is ARM64 Mach-O and would have been
*easier* to analyse. It was rejected: the only iOS copy available was a pirated
repack with an injected cheat framework, and a clean-room project cannot be
built on a tampered artifact. The legally-owned Steam install is both the
correct and the ethical reference.

## The binding surface

Isaac's gameplay is Lua. That Lua calls into 655 native methods across at least
9 classes, registered through a custom metatable model — **not** `lua_register`,
which appears zero times in the binary. The recovered metatable keys are:

```
__class __parent __index __propget __propset __newindex __const __metatable __type
```

Inheritance is single-parent through `__parent`. A property registered without a
setter is additionally mirrored into `__const`, so it reads as read-only from
Lua. [`include/isaec/api/binding.hpp`](include/isaec/api/binding.hpp) implements
exactly this model — do not "simplify" it into a `lua_register` table, that is a
different API.

The header is deliberately free of any Lua include, so callers can register
classes without Lua on their include path. Only `binding.cpp` includes Lua.

## Progress

| Area | State |
|---|---|
| Class registry, metatable model, inheritance | **done** — 16 tests green |
| Declarative `ApiSpec` → `Binding` codegen | **done** |
| Recovered 655-method surface registers and resolves | **done** (as stubs) |
| Directory-backed data source | **done** |
| Headless renderer (counts, draws nothing) | **done** |
| **655 native function bodies** | **0 / 655** |
| **Method signatures** (`argc`, return shape) | **0 / 655** |
| **Packed archive (`ARCH000`) container** | **container solved**, codec open |
| **→ `ARCH000` payload codec** | **not started** — blocked on #1 |
| **→ `ARCH000` 64-bit name hash** | **not started** — lower priority, see #2 |
| **GPU rendering** | **not started** |
| **Class coverage** | 9 of ~23 classes |
| **Repentance+ (2024 co-op update) API** | **not examined at all** |

Every recovered method currently resolves to a `not_implemented_trampoline`.
That is the honest state: the *names* are recovered and the *dispatch* works,
but no function does anything yet.

The class-coverage number is blocked on the same work as the function bodies,
not independent of it. See issue #3.

## Build

Requires CMake ≥ 3.16 and a C++17 compiler. Lua 5.3.6 is vendored, so no
system Lua is needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Headless smoke test against a real install:

```sh
./build/isaec_headless "/path/to/steamapps/common/The Binding of Isaac Rebirth"
```

Cross-compiling for Android (requires the NDK):

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android-arm64.cmake \
  -DANDROID_NDK=$ANDROID_NDK_HOME
```

CI builds on Linux (gcc and clang) and macOS, configures against the Android
NDK, and additionally gates on `-Werror`, ASan+UBSan, and spec-freshness.

## Layout

```
include/isaec/
  api/binding.hpp          class registry, inheritance, property model
  api/registry.hpp         declarative ApiSpec -> Binding, missing-symbol table
  api/generated/           codegen output, do not edit
  data/source.hpp          DataSource: one interface over directories
  data/collection.hpp      ordered string-keyed collection
  engine/engine.hpp        mount / start / tick / stop
  engine/renderer.hpp      Renderer: pure virtual, platform fills it in
  engine/clock.hpp         SteadyClock
  platform/                null_renderer (headless)
src/                       implementations
tools/                     research + codegen scripts; see tools/README.md
  inspect-archive.py        ARCH000 reader; asserts the index arithmetic
docs/                       measured format specs; see docs/arch000-format.md
bindings/                  recovered surface + generated spec
tests/                     16 cases, ctest
third_party/lua-5.3.6/     vendored, checksum in third_party/CMakeLists.txt
```

Lua is pinned to **5.3** and must stay that way — Isaac's interpreter is Lua 5.3.
5.4, 5.5, and LuaJIT are all wrong. Do not replace this with
`find_package(Lua)`.

Namespace is `isaec`, not `isaac`, deliberately, so nothing here reads as an
official Nicalis artifact.

## Issues

Open work is tracked as numbered GitHub issues with acceptance criteria. The
milestone `M1 Foundation` covers the current blockers:

- Recover the 655 native function bodies and signatures (#3) — also unblocks
  per-class attribution and therefore full class coverage.
- Locate the runtime archive loader, then recover the `ARCH000` payload codec
  (#1, #2). The container itself is solved; see
  [`docs/arch000-format.md`](docs/arch000-format.md).
- Implement a real GPU renderer backend (#5).
- Survey the Repentance+ API surface (#6).

`tools/inspect-archive.py` parses and validates any Isaac archive today. It
refuses to print a table for input whose index arithmetic does not close, so a
wrong reading of the format fails loudly instead of producing confident garbage.

`tools/README.md` documents how the binding surface was recovered, which leads
are dead, and two address-arithmetic traps that cost real time.

## Regenerating the binding spec

```sh
python3 tools/gen-api-spec.py
```

Idempotent. Writes both the JSON and a C++ header, so a malformed method name
fails at compile time rather than at load time. CI enforces freshness with a
`git diff --exit-code` check.

## Credits

* The Binding of Isaac was created by Edmund McMillen and published by Nicalis.
  This project is an independent reimplementation and is not endorsed by either.
* Method names and structural observations were recovered by the maintainer
  from a copy of the game they own, using their own tooling. No game code or
  assets are redistributed here.
* Inspired by the OpenMW and OpenTTD model of shipping an engine that reads a
  user's own game install.