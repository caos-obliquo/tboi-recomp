# tboi-recomp

> ⚠️ !! WARNING !! ⚠️
> **THE MOST AI-SLOP SHIT YOU HAVE EVER SEEN**
>
> This repository contains industrial quantities of AI-assisted reverse engineering, vibe coding, ugly experiments, suspiciously effective slop, and a clean-room engine that does not yet play a single frame.
>
> **If you are anti-AI, please close this repository now.** Continued exposure may be harmful to your blood pressure, sleep schedule, and faith in software engineering. Yes, it is AI-assisted. You have been warned.

---

A clean-room reimplementation of *The Binding of Isaac: Repentance*, built to run
on arm64 Android and Linux x86_64 from one source tree.

**This is an independent project. It is not affiliated with, endorsed by, or
connected to Nicalis or Edmund McMillen.** "The Binding of Isaac" is a trademark
of its respective owners and is used here only to identify the game this project
aims to be compatible with.

> **Status: nothing here plays the game.** The binding surface is fully mapped
> (64 classes + 3 namespaces, 1003 methods + 461 properties) and all 911 recovered
> bodies are decompiled. **372 members** are implemented, led by the binary-derived
> `Vector`, `Color`, `RNG` and `BitSet128` classes; the rest still refuse loudly. Read
> [Progress](#progress) before getting excited.

---

## Contents

- [Why it exists](#why-it-exists)
- [Legal boundary](#legal-boundary)
- [What is being rebuilt, and from what](#what-is-being-rebuilt-and-from-what)
- [How this compares to other recompilations](#how-this-compares-to-other-recompilations)
- [The binding surface](#the-binding-surface)
- [Progress](#progress)
- [What "located" means, and what it does not](#what-located-means-and-what-it-does-not)
- [Build](#build)
- [Layout](#layout)
- [Open work](#open-work)
- [Documentation](#documentation)
- [Credits](#credits)

---

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

---

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

GPL-3.0-only licensed, as of issue [#62](https://github.com/caos-obliquo/tboi-recomp/issues/62), so that this project may adapt [`the-browsing-of-isaac`](https://github.com/chiikabu/the-browsing-of-isaac)'s own original code with attribution. The game code underneath anything here is Nicalis' and its licence is unchanged by this choice. See [`LICENSE`](LICENSE).

---

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

---

## How this compares to other recompilations

Five projects are worth reading side by side. Between them they cover every
approach available to someone in this position. **No Nicalis-derived code is
in this repository**, and nothing from these projects has entered `src/`; this
repository is GPL-3.0-only so that *their own original host/platform glue* may
be adapted with attribution, which remains a separate decision made file by
file.

### The closest one: [repentogxm](https://github.com/0xl0cal/repentogxm)

A PlayStation Vita port of **this exact game**, by static recompilation of the PC
executable to ARM. Not a similar game — the same binary class, targeted at a
handheld, GPL-2.0, 31 stars.

It works: title screen, menus, rooms, bosses, saves, music, and the External
Item Descriptions mod, at the Vita's native 960x544, with game logic at 30
updates per second presenting up to 60 FPS. There is no emulator — the PC
functions are translated ahead of time into C and loaded at a fixed address.

Two things in it matter more here than the port itself:

1. **It publishes its measurements**, including the failures:

   | scene | result |
   |---|---|
   | menus | 60 FPS |
   | rooms with enemies | 55–59 FPS |
   | dense rooms (~78 draw calls/frame) | 33–34 FPS |
   | door transition with resource loads | 0.9–1.1 s stall |
   | floor change | 1.5–4.3 s |

   Its conclusion — *below 60 FPS the cost is CPU render time: translated game
   code, draws, and Lua* — is the same conclusion [#19](https://github.com/caos-obliquo/tboi-recomp/issues/19)
   needs to reach, and it is a reminder that the binding surface being
   reconstructed here *is* the per-frame cost centre.

2. **Its credits are a map.** It names
   [`TeamREPENTOGON`](https://github.com/TeamREPENTOGON/REPENTOGON) as the
   source of "most of the useful Isaac function names, signatures, layouts" —
   a LibZHL project targeting **Repentance+**, the same version as the binary
   this project reads. That turns
   [#17](https://github.com/caos-obliquo/tboi-recomp/issues/17) from a derivation
   problem into a cross-check, and it is tracked as
| [#20](https://github.com/caos-obliquo/tboi-recomp/issues/20) | Cross-check the 372 anonymous bodies against the REPENTOGON libzhl | **partially advanced** — the 911 bodies are named; 105 field offsets confirmed against the `.zhl` layouts, the differences tracked ([§21](docs/binding-surface-findings.md)) |
   name [`Gibbed.Rebirth`](https://github.com/gibbed/Gibbed.Rebirth) for archive
   encryption and framing, which is the open half of
   [#2](https://github.com/caos-obliquo/tboi-recomp/issues/2).

Its README describes itself, in the author's words, as AI-assisted and
experimental. That is worth stating plainly rather than hiding: it is a
different project with a different licence, consulted as a reference. No code
from it is used here.

### The browser one: [the-browsing-of-isaac](https://github.com/chiikabu/the-browsing-of-isaac)

Hosted at [tboi.online](https://tboi.online/). The **real Repentance+ game
code, statically recompiled from the Windows executable to WebAssembly via
Emscripten**, running in Chromium (desktop and Android) by touch or pad.
GPL-3.0 for its own work; the game code inside it is Nicalis', as the project
itself states. Credits TeamREPENTOGON (`libzhl` signatures/layouts) and
Gibbed.Rebirth.

Two things in it matter here:

1. **Its own original code is the mobile/input design you cannot re-derive for
   free.** `platform/input.js` (keyboard -> Win32 VK codes), `web/game/input.js`,
   `scripts/recomp/web/gamepad.mjs` (browser Gamepad API -> **XInput** state:
   standard-mapping button order, thumbs as signed 16-bit with up positive,
   triggers 0-255, four slots, packet number), `scripts/recomp/web/touch_controls.mjs`,
   and `scripts/recomp/host/src/host_shims_xinput.c` (the game polls
   `XInputGetState` per pad each frame; hot-plug via `XInputGetCapabilities`).
   This repository is GPL-3.0-only so those files may be adapted, with
   attribution, instead of re-derived. No adaptation has landed yet; each one
   lands as its own reviewed change.
2. **Its `decomp/` is cross-check evidence, not a source.** `game-update-slice.json`,
   `game-render-slice.json`, `frontier.json` and `port-roots.json` are one team's
   interpretation of the binary. Their offsets, VAs and layouts are treated as
   *unverified external derivations*: any of them recorded here must be
   re-verified against this project's own binary first, exactly as
   [#20](https://github.com/caos-obliquo/tboi-recomp/issues/20) does with the
   REPENTOGON `.zhl` files. The mechanically recompiled game bodies
   (`native/decomp/*`, ~266k lines) and `boot.wasm` are Nicalis-derived and
   **never enter this tree**, in any language.

### The other three

| | **this project** | [skate3recomp](https://github.com/mchughalex/skate3recomp) | [ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) |
|---|---|---|---|
| Target | *Isaac: Repentance* (2021, PC 32-bit) | *Skate 3* (2010, Xbox 360) | *skate.* (2025, EA free-to-play) |
| Technique | **clean-room reimplementation** | **static recompilation** via ReXGlue | **runtime injection** into the shipping game |
| Reads the original by | recovering the interface, writing fresh behaviour | translating the code ahead of time | leaving the code alone |
| You must own | your own Steam copy | your own Xbox 360 disc dump | your own Steam copy |
| Scale | 911 bodies mapped, **372 members implemented** | 1.6k stars, playable end to end | 1.2k stars, GPL-3.0 |
| Renderer | `NullRenderer` — counts calls, draws nothing | native D3D12/Vulkan, **>2x fps at ~¼ GPU power** | inherits the original's |

### What skate3recomp proves is possible

Its [v2.0.0 release notes](https://github.com/mchughalex/skate3recomp/releases)
replaced an emulated console GPU with a native renderer covering the whole game,
reporting **more than double the frame rate at roughly a quarter of the GPU
power draw**, with Apple Silicon going from ~25 FPS to ~250 FPS at 4K.

Two of its decisions are worth copying regardless of technique:

1. **Visual parity before enhancement.** It ports the original's *own* material
   shading first, so "looks different" is never confused with "looks better".
   Enhancements — MSAA, soft shadows, bloom, render scale up to 3x, ultrawide —
   are opt-in and off the critical path.
2. **The old path stays reachable.** A live renderer toggle plus automatic
   fallback on an unrecoverable error, with an on-screen indicator. A
   reimplementation with no escape hatch is worse than the thing it replaced.

Both are now scoped in [#19](https://github.com/caos-obliquo/tboi-recomp/issues/19),
together with the frame-rate cap and low-end-device targets Isaac's 2021 build
cannot offer: selectable 30/60 caps, render scale independent of window size, and
upscaling so a weak GPU can lower internal resolution and stay smooth.

### What ReSkate shows about the other approach

ReSkate loads a runtime *into* a live game process and extends it: offline play,
multiplayer, mods. That route needs no reimplementation at all, which is exactly
why it is unavailable here — a clean-room project has no business injecting into
code it is supposed to be independently rebuilding. Its
[settings surface](https://reskate.app/) is still a useful model for how the
graphics session should eventually be presented to a player.

The same split is documented for Bloodborne in
[`docs/reimplementation-references.md`](docs/reimplementation-references.md):
bbport runs the original code, its Android port adds a guest-CPU translation
layer, and this project takes the third path.
---

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

The game also ships a **patched Lua**, not stock 5.3: its index space is extended
below a sentinel so the game can address a hidden per-frame array. That is
established from the shipped DLL rather than assumed — see
[`docs/binding-surface-findings.md` §14.6](docs/binding-surface-findings.md).

---

## Progress

| Area | State |
|---|---|
| Class registry, metatable model, inheritance | **done** — 16 tests green |
| Declarative `ApiSpec` → `Binding` codegen | **done** |
| Recovered surface registers and resolves | **done** (as stubs) — 64 classes + 3 namespaces, 1003 methods + 461 properties, all behind `not_implemented_trampoline` except the 372 implemented members |
| Directory-backed data source | **done** |
| Headless renderer (counts, draws nothing) | **done** |
| **Playtesting box (white-box sandbox)** | **done** — controllable room, no game data: a square entity moved by arrows/WASD or the stick of XInput-shaped pad 0 (deadzone applied in the engine layer), 60 Hz fixed-step movement in `src/engine/sandbox.cpp` clamped to the room, unit-covered in `tests/test_sandbox.cpp`; `./build/isaec_window slice`, `frames=0` plays until Esc — supersedes #67's fixed marker; see [#67](https://github.com/caos-obliquo/tboi-recomp/issues/67), [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79) |
| **Controller input (XInput-shaped)** | **done** — engine layer: four pad slots, XInput `wButtons` bit layout, signed up-positive 16-bit thumbsticks, triggers 0–255, `dwPacketNumber` bumping on change, 7849 deadzone; **SDL2 backend** (`isaec::platform::SdlGamepadSource`) — device enumeration + hot-plug rescan + both-motor rumble; **145/145 tests green**; never run against a live game — see [#65](https://github.com/caos-obliquo/tboi-recomp/issues/65) |
| **Closure bodies located** | **911 distinct bodies** of 1064 call sites — accounting complete |
| **Closure bodies decompiled** | **911 / 911**, 0 failures, classified into 11 shapes |
| **Property-accessor ABI** | **recovered** — [§14](docs/binding-surface-findings.md) |
| **Lua binding index space** | **patched Lua identified** — [§14.6](docs/binding-surface-findings.md) |
| **Closure bodies named** | **911 / 911** — every closure body carries a name ([§15](docs/binding-surface-findings.md)) |
| **Closures attributed to a class** | **2012 records over 64 classes** (+ 3 global namespaces) — 1538 (class, property) pairs ([§16](docs/binding-surface-findings.md)) |
| → method / property / metamethod split | **method 1026 · property 861 · metamethod 125** — read off the helper that performs each registration ([§16.5](docs/binding-surface-findings.md)) |
| **Body shape census** | **911 bodies measured** — `argc` bound 0…8 (52 unmeasured), `returns` 0 → 359 · 1 → 534 · unknown → 18 ([§17](docs/binding-surface-findings.md)) |
| **Generated binding surface** | **64 classes + 3 namespaces, 1003 methods + 461 properties** — generated from the code-derived table, all trampolines |
| **Implemented classes** | **`Vector`** — 15 methods plus the class-table `FromAngle`, the `X`/`Y` properties and 6 metamethods (`__add`, `__sub`, `__mul`, `__div`, `__unm`, `__tostring`); the 22 native function bodies are translated from the reference binary, `Clamp`/`Clamped` and the arithmetic operand rules included ([§18](docs/binding-surface-findings.md)). **`Color`** — the 7 colour properties, 5 methods (`Reset`, `SetTint`, `SetColorize`, `SetOffset`, the undocumented `Lerp`) and `__mul`, from their native functions; the colorize block at offsets 16..28 is named ([§19](docs/binding-surface-findings.md)). **`RNG`** — the five methods (`GetSeed`, `SetSeed`, `RandomInt`, `RandomFloat`, `Next`) translated from their native functions, with the callable class table and 16-byte xorshift state; implementation and tests are green, the numeric oracle is still open ([§25](docs/binding-surface-findings.md)). **`BitSet128`** — the two methods (`Get`, `Set`), six bitwise/equality metamethods and the callable `BitSet128(low, high)` constructor, all translated from their native functions over the four-word payload; implementation and tests are fully static and green ([§26](docs/binding-surface-findings.md)). **`PosVel`** — the `Position` and `Velocity` properties, with the game's own field offsets ([§20](docs/binding-surface-findings.md)). **45 field classes** — every class whose properties are plain fields, generated from the measured layout: 327 field entries with the game's own offsets, types and widths (including the 17 string fields), `ActiveItemDesc`, `KColor`, `Entity`, `ProjectileParams`, `TearParams`, `Sprite` and 37 more ([§24](docs/binding-surface-findings.md)) |
| **Field layout cross-check** | **105 recovered field offsets confirmed** against the Repentogon `.zhl` layouts — 11 under another name, 3 moved; **187 method names confirmed** ([§21](docs/binding-surface-findings.md)) |
| Field layout | **21 classes with a complete layout**, 434 fields with an offset and a type (174 int, 104 float, 48 bool, 75 object) — **44 of the classes are implemented** as plain field objects, with 310 plain fields ([§24](docs/binding-surface-findings.md)) |
| **Closure bodies implemented** | **0 / 911 translated** — the closures are glue; 42 native methods behind them are now translated from the binary (22 `Vector`, 6 `Color`, 5 `RNG`, 9 `BitSet128`) |
| **Packed archive (`ARCH000`) container** | **container solved**, mode 0x00/0x01/0x02 chunked arms decoded |
| → `ARCH000` payload codec | **mode 0x02 mostly decoded** — stored records copy at `offset + 9` (378/378 PNGs valid), chunked entries inflate chunk-by-chunk with a `0x400` verbatim arm (1155 entries, 203/203 PNGs valid); 2647 entries remain an unresolved third arm and fail explicitly instead of returning noise; `tests/test_archive.cpp` runs a real-archive check when `ISAEC_TBOI_ARCHIVE` is set — see [#2](https://github.com/caos-obliquo/tboi-recomp/issues/2), [#110](https://github.com/caos-obliquo/tboi-recomp/issues/110) |
| → `ARCH000` 64-bit name hash | **not started** — see [#1](https://github.com/caos-obliquo/tboi-recomp/issues/1) |
| GPU rendering | **not started** — see [#5](https://github.com/caos-obliquo/tboi-recomp/issues/5) |
| Graphics/performance epic | **scoped, not started** — see [#19](https://github.com/caos-obliquo/tboi-recomp/issues/19) |
| **Class coverage** | 64 classes + 3 global namespaces, every name code-derived |
| **Repentance+ (2024 co-op update) API** | **not examined at all** — see [#6](https://github.com/caos-obliquo/tboi-recomp/issues/6) |

Every recovered member resolves to a `not_implemented_trampoline` unless it is
one of the 372 implemented members: 327 generated field entries, 15 `Vector`
methods, `Vector.FromAngle`, 6 `Vector` metamethods, 5 `Color` methods,
`Color.__mul`, 5 `RNG` methods, the `RNG` constructor, 2 `BitSet128` methods,
6 `BitSet128` metamethods, the `BitSet128` constructor, and the 2 `PosVel`
properties. That is the honest state: the
*names* are recovered, the *dispatch* works, the *bodies* are read, and the
`Vector`/`Color`/`RNG` arithmetic is translated — and almost nothing else does
anything yet.

The last three rows of the "located / decompiled / named / implemented" block are
four different states and are deliberately not collapsed. **An address is not an
implementation**, and a decompiled body is not a named one.

---

## What "located" means, and what it does not

The **911** addresses come from walking forward from each of the 1064
`lua_pushcclosure` call sites and reading the argument shape, which is fixed:
`push <nupvalues>`, `push <fn>`, optional upvalue setup, `push L`, `call`. The
accounting closes exactly — 911 distinct bodies over 1064 call sites, every one
of them named.

That last sentence is new, and the previous version of this file was wrong
about it. The earlier count of **441** was not a count of bodies: it was the
number of bodies an earlier probe could *reach*, because that probe walked
**backwards** from the call site and so landed on the previous registration's
literal — usually a class-name lookup key rather than the closure's own name.
451 of its 524 names were wrong, and the difference is now written up, with the
disproof, in [`docs/binding-surface-findings.md` §15](docs/binding-surface-findings.md).

all 911 have been decompiled, with kuna standalone at 1.3 s per body
against Ghidra's 607 s on the same address. Every one falls into one of 11
recognised shapes, and the property-accessor mechanism is now written out in
full — including the instruction-level disassembly and the PE import directory
that back it. See [`docs/binding-surface-findings.md` §14](docs/binding-surface-findings.md).

Three numbers are easy to read as more than they are:

- The **45** class-named bodies are that class's `__gc` closure — a
  destructor — not a type constructor. Counting them as recovered methods
  would overstate the surface.
- The **417** metatable-key bodies are property accessors, so they map onto the
  *field* layer rather than the method layer.
- A **name** is not a **signature**. Every closure now has a name *and* a
  class ([§16](docs/binding-surface-findings.md)) — 64 classes, 1538
  (class, property) pairs, split method / property / metamethod — but what a
  property returns is still open
  ([#3](https://github.com/caos-obliquo/tboi-recomp/issues/3)).

The 655 figure this project has tracked since the beginning is a count of
recovered *names*, and it was never shown to equal a count of *bodies*. It is
now known not to.

---

## Build

Requires CMake ≥ 3.16 and a C++17 compiler. Lua 5.3.6 is vendored, so no
system Lua is needed. libpng is required unless you pass `-DISAEC_WITH_PNG=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Headless smoke test against a real install:

```sh
./build/isaec_headless "/path/to/steamapps/common/The Binding of Isaac Rebirth"
```

Playtesting box (no game data needed):

```sh
./build/isaec_window slice [width] [height] [frames]   # defaults 1280 720 300
./build/isaec_window slice 1280 720 0                   # 0 = play until Esc
```

Draws a white-box room (floor, walls) with a controllable marker square —
arrows/WASD or the stick of XInput-shaped pad 0 moves it (pad Y flipped to
screen convention, the engine's 7849 deadzone applies). Movement math lives in
`src/engine/sandbox.cpp` and is unit-tested in `tests/test_sandbox.cpp`. Paces
via `steady_clock` with vsync, Esc or window close quits cleanly. Measured
pacing ≈ 60 Hz (est 59.75 Hz windowed; ~57 Hz under `SDL_VIDEODRIVER=dummy`,
2026-11-17). It proves the Renderer abstraction end to end — it is
not the game and draws no Isaac assets. GPU rendering proper is still
[#5](https://github.com/caos-obliquo/tboi-recomp/issues/5).

Cross-compiling for Android (requires the NDK):

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android-arm64.cmake \
  -DANDROID_NDK=$ANDROID_NDK_HOME
```

The Android toolchain turns `ISAEC_WITH_PNG` off, because the sysroot ships no
libpng and the alternative is linking a host x86-64 archive into an AArch64
binary. Nothing decodes images on Android until that is solved — see
[#16](https://github.com/caos-obliquo/tboi-recomp/issues/16).

CI builds on Linux (gcc and clang) and macOS, configures against the Android NDK,
and additionally gates on `-Werror`, ASan+UBSan, spec freshness, and the binding
extractor's self-check.

---

## Layout

```
include/isaec/
  api/binding.hpp          class registry, inheritance, property model
  api/registry.hpp         declarative ApiSpec -> Binding, missing-symbol table
  api/generated/           codegen output, do not edit
  data/source.hpp          DataSource: one interface over directories
  data/image.hpp           decoded RGBA raster; the one place PNG is spoken
  data/collection.hpp      ordered string-keyed collection
  engine/engine.hpp        mount / start / tick / stop
  engine/renderer.hpp      Renderer: pure virtual, platform fills it in
  engine/clock.hpp         SteadyClock
  platform/                null_renderer (headless), sdl_renderer, slice scene in sdl_main.cpp
src/                       implementations
tools/                     research + codegen scripts; see tools/README.md
  inspect-archive.py       ARCH000 reader; asserts the index arithmetic
  disasm.py                objdump-backed aligned instruction stream
  probe_closures.py        name -> closure body, anchored on the call site
  decompile_corpus.py      resumable batch decompiler for the 911 bodies
docs/                      measured findings; see docs/binding-surface-findings.md
bindings/                  recovered surface + generated spec
tests/                     145 cases with PNG, ctest
third_party/lua-5.3.6/    vendored, checksum in third_party/CMakeLists.txt
```

Lua is pinned to **5.3** and must stay that way — Isaac's interpreter is Lua 5.3.
5.4, 5.5, and LuaJIT are all wrong. Do not replace this with
`find_package(Lua)`.

Namespace is `isaec`, not `isaac`, deliberately, so nothing here reads as an
official Nicalis artifact.

---

## Open work

Work is issue-driven; [`AGENTS.md` §1](AGENTS.md) defines the loop. The
milestone `M1 Foundation` covers the current blockers.

| # | What | State |
|---|---|---|
| [#1](https://github.com/caos-obliquo/tboi-recomp/issues/1) | Locate the runtime archive loader | **blocked** — enumeration ruled out; approach exhausted |
| [#2](https://github.com/caos-obliquo/tboi-recomp/issues/2) | `ARCH000` payload codec | **partially advanced** — mode 0x02 chunk framing recovered; 1533/4180 entries decode (581 PNGs, all valid); 2647-entry third arm open |
| [#3](https://github.com/caos-obliquo/tboi-recomp/issues/3) | Recover bodies and signatures | **partially advanced** — 911/911 decompiled, the shape census done, 24 members implemented, **0 of 911 bodies translated** |
| [#4](https://github.com/caos-obliquo/tboi-recomp/issues/4) | Per-class attribution | **largely resolved** — 64 classes + 3 namespaces, code-derived; hierarchy not recoverable |
| [#5](https://github.com/caos-obliquo/tboi-recomp/issues/5) | Real GPU renderer | **blocked** — nothing to draw yet |
| [#6](https://github.com/caos-obliquo/tboi-recomp/issues/6) | Repentance+ API surface | **partially advanced** |
| [#7](https://github.com/caos-obliquo/tboi-recomp/issues/7) | Purge HUD/Isaac contamination | **resolved** — the code-derived surface replaces the contaminated one |
| [#12](https://github.com/caos-obliquo/tboi-recomp/issues/12) | Name the patched-Lua anchored value | **open** — the last gap in the ABI |
| [#16](https://github.com/caos-obliquo/tboi-recomp/issues/16) | Cross-built libpng for Android | **open** |
| [#17](https://github.com/caos-obliquo/tboi-recomp/issues/17) | Name the 372 anonymous bodies | **resolved** — all 911 bodies are named and class-attributed |
| [#18](https://github.com/caos-obliquo/tboi-recomp/issues/18) | `macos-arm64` never dispatches | **resolved** — the required context was removed, the job is gone |
| [#19](https://github.com/caos-obliquo/tboi-recomp/issues/19) | Graphics + performance epic | **scoped, not started** |
| [#20](https://github.com/caos-obliquo/tboi-recomp/issues/20) | Cross-check the 372 anonymous bodies against REPENTOGON | **open** — highest-leverage item on the board |

---

## Documentation

| Document | What it is authoritative for |
|---|---|
| [`docs/binding-surface-findings.md`](docs/binding-surface-findings.md) | **addresses, counts, and the accessor ABI** — the source of truth |
| [`docs/arch000-format.md`](docs/arch000-format.md) | container format claims only |
| [`docs/HANDOFF.md`](docs/HANDOFF.md) | what is in flight, what is blocked, what is a dead end |
| [`docs/rea-toolchain.md`](docs/rea-toolchain.md) | the reverse-engineering toolchain and its traps |
| [`docs/roadmap.md`](docs/roadmap.md) | priority order, references and what each skips — the build order |
| [`docs/handoff.md`](docs/handoff.md) | the state of the project on 2026-11-17 — 5-section snapshot; mandatory next deliverable: the playtesting box ([#79](https://github.com/caos-obliquo/tboi-recomp/issues/79)) |
| [`docs/reimplementation-references.md`](docs/reimplementation-references.md) | what was borrowed from other recompilations, and what was rejected |
| [`tools/README.md`](tools/README.md) | what each tool does, and what it does *not* establish |
| [`AGENTS.md`](AGENTS.md) | working rules |

`tools/inspect-archive.py` parses and validates any Isaac archive today. It
refuses to print a table for input whose index arithmetic does not close, so a
wrong reading of the format fails loudly instead of producing confident garbage.

---

## Credits

* The Binding of Isaac was created by Edmund McMillen and published by Nicalis.
  This project is an independent reimplementation and is not endorsed by either.
* Method names and structural observations were recovered by the maintainer
  from a copy of the game they own, using their own tooling. No game code or
  assets are redistributed here.
* Inspired by the OpenMW and OpenTTD model of shipping an engine that reads a
  user's own game install.
* [`repentogxm`](https://github.com/0xl0cal/repentogxm),
  [`skate3recomp`](https://github.com/mchughalex/skate3recomp),
  [`ReSkate`](https://github.com/Dingo-Shenanigans/ReSkate) and
  [`the-browsing-of-isaac`](https://github.com/chiikabu/the-browsing-of-isaac)
  are unrelated projects by other authors. No Nicalis-derived code from any of
  them is used here; `the-browsing-of-isaac`'s own original host/platform
  code may be adapted with attribution under GPL-3.0-only.
* Interface facts learned from other projects are **verified against this
  project's own binary** before being recorded, and disagreements are reported
  rather than averaged away.
