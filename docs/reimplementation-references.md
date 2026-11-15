# Reference reimplementations, and what actually transfers

Two existing game reimplementations were studied as structural references. Both
are named here so the comparison is reusable, and so the parts that do **not**
apply are recorded as deliberately rejected rather than overlooked.

| project | stars | licence | language |
|---|---|---|---|
| [`deadinside28/bloodborne_pc`](https://github.com/deadinside28/bloodborne_pc) | 3251 | GPL-2.0 | C++ |
| [`ducvd89/Bloodborne-Android`](https://github.com/ducvd89/Bloodborne-Android) | 222 | GPL-2.0 | C++ |

The two are near-identical in layout, so they are one design, not two.

## What those projects do

bbport runs the **original game code**. The PS4 x86-64 executable runs natively
on the CPU, a purpose-built runtime replaces the PS4 system libraries, and
graphics are translated to Vulkan by a renderer derived from shadPS4 and extended
with FSR. Their own README draws the analogy precisely: as Wine replaces system
libraries, and as DXVK translates graphics.

The Android port keeps that strategy and adds FEX to run the guest x86-64 CPU
code on ARM.

**This is the opposite of this project.** `tboi-recomp` is a clean-room
reimplementation in C++: no original code, no original assets. The legal posture
matches theirs ("no game files are included"), but the engineering approach does
not. Nothing below depends on adopting their strategy.

## Structure

```
cpu/          guest CPU layer (Android port only: FEX, host_call_arm64.S)
gpu/          Vulkan translation layer
src/          runtime_<subsystem>.c   ~20 files
tools/        Python + Perl, incl. got_import.py, dump_view.py
docs/         CHANGES_*.md, ANDROID_*.md, ROADMAP.md, DEVELOPMENT_LOG
launcher/  packaging/  patches/  scripts/  third_party/
build.sh  run.sh  shell.nix  .gitmodules
```

## What transfers

**1. Subsystem-split sources — adopt this.**
Their `src/` is one file per OS subsystem: `runtime_memory.c`, `runtime_file.c`,
`runtime_audio.c`, `runtime_thread.c`, `runtime_savedata.c`, `runtime_pad.c`,
`runtime_mutex.c`, `runtime_sema.c`, `runtime_rwlock.c`, `runtime_rtc.c`,
`runtime_net.c`, and so on. Each file is a bounded domain with its own state,
and none is unwieldy.

The direct analogue here is the Lua native-function surface. 441 recovered
bodies should live in one file per domain — entity, player, render, input,
audio, data, lua bridge — not in a flat list of 441. This also serves the
repo's 250-LOC ceiling, which a generated flat table would breach immediately.

**2. Dated change entries — adopt this.**
They write `CHANGES_2026-10-06.md` rather than appending to one growing file.
`binding-surface-findings.md` is currently a 13-section append log. The findings
content should stay; the *changelog* half of it should become dated entries, so
the address authority does not become unreadable over time.

**3. Generated import tables.**
`import_names.inc` is generated and consumed by the build. This project already
does the equivalent with `tools/gen-api-spec.py` and the
`generated-spec-is-current` CI check. No change needed — recorded so it is not
mistaken for a gap.

**4. A `probe` in the source tree.**
They keep `src/probe.c` alongside the runtime. Consistent with keeping
`tools/` first-class here.

## What does not transfer

- **Running the original code.** Out of scope by design, not by oversight.
- **`cpu/` and `gpu/` as emulation layers.** Isaac's recovered native functions
  are ordinary small helpers, not a CPU emulator plus a graphics translator.
  A domain split is useful; an emulation split is not.
- **`shell.nix`.** CMake is already the build system and CI is green. Adding a
  second build description is cost without benefit.
- **`.gitmodules` for `third_party`.** Lua 5.3.6 is vendored deliberately, so
  there is no system Lua to resolve.

## The lesson that matters more than the layout

bbport is *playable*. It ships save loading, sound, input, an unlocked frame
rate and FSR upscaling. This project is at **0 of 441 bodies implemented**,
after substantially more reverse-engineering effort.

The gap is not diligence and it is not analysis quality — the binding surface is
now known exactly, with a complete accounting and no unaccounted remainder. The
gap is that knowing *where* a function is says nothing about what it should
*do*, and 441 of those is a large, unordered queue.

Their README is also a model for honesty: an explicit "Status: experimental,
playable", a named list of what was actually tested, and a specific statement
of the one step remaining. That is the register this project's progress table
should keep.
---

# The Browsing of Isaac, and the honest split

[`chiikabu/the-browsing-of-isaac`](https://github.com/chiikabu/the-browsing-of-isaac),
hosted at tboi.online, is the **real Repentance+ game code, statically
recompiled from the Windows executable to WebAssembly via Emscripten**, running
in Chromium (desktop and Android), playable by touch or gamepad. GPL-3.0 for
its own work; the game code inside it remains Nicalis'. Credits TeamREPENTOGON
(`libzhl` signatures/layouts, GPL-2.0) and Gibbed.Rebirth. Licence of this
repository changed MIT -> GPL-3.0-only (issue
[#62](https://github.com/caos-obliquo/tboi-recomp/issues/62)) so that the
following split is available.

## Half 1 — their own original code: adaptable, with attribution

GPL-3.0-only permits this repository to adapt, with credit, any file that is
chiikabu's original work rather than a translation of the game:

| artifact | what it is |
|---|---|
| `platform/input.js` | keyboard -> Win32 VK codes, `DEFAULT_KEYMAP`, stateful tracker |
| `platform/frame-loop.js`, `platform/mount.js`, `platform/window.js`, `platform/browser-mount.js` | host glue: loop, mount, window, loading stages |
| `web/js/*`, `web/game/*` | menu scene, render host, update bridge, prototypes |
| `scripts/recomp/web/gamepad.mjs` | browser Gamepad API -> **XInput** state (`wButtons` bit layout, thumbs as signed 16-bit up-positive, triggers 0-255, four slots, `dwPacketNumber`, memoised polling) |
| `scripts/recomp/web/touch_controls.mjs`, `touch_game.mjs`, `touch_input.mjs` | native touch control surface; README: "you play by touching the game" |
| `scripts/recomp/host/src/host_shims_xinput.c` | what the game does: `LoadLibraryA("XInput1_4.dll")`, resolves `XInputGetState`/`SetState`/`GetCapabilities`, hot-plug via capabilities poll, state poll per pad per frame |
| `scripts/recomp/*.mjs`, `scripts/decomp/*.mjs` | build/orchestration tooling as a design reference |

Adaptation means: a reviewed change with attribution, not a silent copy. Test
the adapted code here; do not ship their tree.

## Half 2 — Nicalis-derived recompilation: read-only cross-check evidence

Never enters this tree, in any language, under any licence:

- `native/decomp/*_pure_helpers.cpp` (~266k lines): mechanically recorded
  instruction-level translations of game bodies (allocator, ANM2, SFX,
  `Game::Update`, `Game::Render`, `LuaEngine`).
- `boot.wasm` and everything it does.
- `decomp/game-update-slice.json`, `decomp/game-render-slice.json`,
  `decomp/frontier.json`, `decomp/port-roots.json`: one team's *interpretation*
  (VAs, offsets, layouts, `abiVersion`). Usable only as unverified external
  derivations: anything adopted must be re-verified against this project's own
  binary first, as in #17/#20. Agreement is kept; disagreement is reported, not
  averaged.
- `docs/decomp-port.md`, `decomp-history.md`: their failure log (tree-poison
  incidents, ANM2 ABI pitfalls, slice regressions) is worth reading as trap
  documentation. Read, not copied.

## What this project keeps doing

Clean-room recovery of the binding surface against the project's own binary,
Win32/XInput facts as interface evidence, implementation written from scratch.
The licence change makes Half 1 available; it does not make Half 2 available,
and no licence this project picks ever will.
