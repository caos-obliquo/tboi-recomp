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