# Repentance+ recomp roadmap — priority order, references, what skips path

Goal (the maintainer's, verbatim):

> "the main for this project of mine is to make recomp of the latest version (or repentance+)"

This exe — a Steam-owned `isaac-ng.exe`, v1.9.7.17.J460, sha256
`3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b` — **is** the
latest. Everything below plans against it, nothing else.

This is a plan, not a new set of facts. Address and count claims live in
[`binding-surface-findings.md`](binding-surface-findings.md) (authoritative);
external-derivation grades live in `lua-api-reference.md` (consultation record, PR #72).
Nothing in this document authorizes implementing an `unverified` row.

---

## How every reference was used

Repos and guides may be **consulted**; nothing foreign enters the tree unless it
is (a) our own measurement of our own binary, or (b) Half-1 glue adapted under
the GPL-3.0-only flip (issue #62, attribution required), or (c) named here as a
record with no code.

| Reference | What it saves | What we never take | Grade notes |
|---|---|---|---|
| Our own exe (`3bdfc8ba…`) + REA/Ghidra | everything; names stay anonymous (no PDB), verified per probe | — | authoritative for addresses (see `rea-toolchain.md`) |
| `docs/binding-surface-findings.md` | the whole map (911 bodies, §13–§24) | — | authoritative |
| TeamREPENTOGON `.zhl` (1.1.3, 244 files) | 105 confirmed field offsets; method-name tally corroboration; Vector/PosVel layout agreement | raw bytes ever committed; any VA inferred from a byte scan | `oracle-agreed` layouts; `oracle-disagreed` (Vector::Lerp preamble absent); rest `unverified` (PR #72) |
| `docs/lua-api-reference.md` + IsaacDocs (wofsauge), Filloax autocomplete | Lua API member order & semantics for doc-driven work | any text copied in | IsaacDocs/autocomplete `unverified` (PR #72); LuaDecomps rejected |
| `chiikabu/the-browsing-of-isaac` Half-1 (GPL-3.0, own work) | **host-layer design, month(s) of it**: `gamepad.mjs` Gamepad→XInput mapping (#65), `host_shims_xinput.c` (XInputGetState/Caps flow), touch controls, mount/frame-loop patterns | **Half-2, ever**: lift, `boot.wasm`, slice JSONs, `*_pure_helpers.cpp` (~266k LOC) | adapt with attribution only, one PR per file |
| browsing-of-isaac `DEVELOPING.md` + `docs/decomp-*.md` | the pipeline's failure log — avoided trap classes (tree poison, missed starts, pypcode version pinning, "green means nothing was asserted") | any of its tooling or outputs | read-only trap documentation |
| `antoniotorres02/isaac-switch-native` | **host responsibility map** for a native build (window/input/audio/GL/save-dir); measured traps: `pypcode@4.0.1` vs upstream's `3.3.3`, Ghidra 12.1.2+JDK21 vs 12.2+Java25, `b4743967…` unpack-sha gate | its code, its patches, its pin; also its approach (needs an unpack we do not use) | read-only; proves x86-64 Linux PC + Switch mobile-form-factor feasibility on our exact game |
| `ihatectf` macOS arm64 port (upstream issue #21) | arm64 + libffi Steam-API bridge sketch; "1,984 missed starts" failure class | code | read-only |
| `0xl0cal/repentogxm` (Vita 1.7.9b, alpha) | confirms a lift+host loop ships and is maintainable; Vita focus | code | methodology only; version mismatch |
| `whylurking/IsaacRecomp` (Flash-era, Android APK) | Android UX/precedent (SDL2/GLES, touch bindings, release-as-APK) | code; wrong engine era | methodology only |
| `hunchcldev/isaac-highfps` | frame-pacing/trampoline ideas; the 1.9.7.17 busy-loop confirmation | its `winmm.dll` | not a fix for the heat bug |
| Android-port modding guide ("Isaac Port Modding", `com.tboi.rebirth`) | **mod-system semantics** for later: JSON `mod.json`, `patch/` per-record overlays with highest-wins, `options/<k>/<v>/` overlays, fixed-table overflow→whole-mod refusal, `check.txt` diagnostics | the port's code/binaries (Nicalis-derived, closed source) | consulted-only design reference |
| `noahhhi/isaac-repentance-plus-cpu-fix` | confirms busy-loop is an upstream defect of this exact build | editing user's exe (never) | our rewrite avoids the class by construction |
| `Guantol-Lemat/Isaac.LuaDecomps`, tweet pseudocode | nothing | any of it as engine fact | rejected |
| REPENTOGON DLLs/installer zip (`~/Downloads`) | nothing | n/a | untouched |

What these can compress is **discovery and design time** — names, layouts,
traps, host shape, mod-format semantics. What they cannot compress is
**implementation**: the 911 closure bodies (`0 translated` today) are work, not
a download.

---

## The path, in priority order

Each phase names its tracker. Epics stay open until merged evidence; nothing
closes on a promise.

### Phase 0 — binding surface mapped (done)
911/911 located, decompiled, named; 64 classes + 3 namespaces; 1003 methods +
461 properties; property ABI recovered (§14); shape census done (§17).

### Phase 1 — renderer & data spine (riskiest, first)
`NullRenderer` exists; SDL2 windowed slice validates submit/present (#68, user
merge pending). What a real game additionally needs, before a single menu can
draw:

- ~~**ANM2 animation loader**~~ — parser landed ([anm2-format.md](anm2-format.md),
  #124): 4104 of 4104 documents in three archives. Playback and texture binding
  remain renderer work.
- **GPU/SDL renderer proper (#5)** — replace white-box with real textures;
  vsync gate per #66.
- **ARCH000 container & decode paths** — partially addressed (#52 open: the
  LZW mode-1 defect); required for any asset read.
- Watermark of damage admission: sprites are the gate for "it looks like Isaac".

### Phase 2 — audio (#64)
Injected `Audio` mirroring `Renderer`; null + SDL2 backends; generated WAV
fixtures only; never game audio in the repo; out of scope: Lua SFX hooks,
music streaming.

### Phase 3 — input (#65)
XInput-shaped state (packet number, wButtons bits, signed-16 thumbs up-positive,
triggers 0–255, 4 slots, hot-plug); adapt `gamepad.mjs`/`host_shims_xinput.c`
with attribution per #62/GPL-3.0-only; tests in the spirit of upstream's
gampepad tests; Android touch = later phase (needs Phase 1).

### Phase 4 — binding increments (the long tail)
Order of implementation, motivated by the shape census (§17):

1. **Returns-1 bodies** (534 of 911) — trivially testable, huge coverage.
2. **The 45 class-attributed `__gc` bodies** and property ABI (§14) wiring for
   the 64 classes — lets scripts call what exists.
3. **Complete the Vector/Color/PosVel class bodies** against public API docs;
   `Clamp`/`Clamped` deliberately stay loud stubs (#37/#38).
4. **Metamethods** — wire the 76 from the spec (#35).
5. **Per-domain body batches** — entity → player → game → render → lua bridge,
   250-LOC ceiling per PR, `tools/` regenerators stay in sync.
6. **Class hierarchy** — `__parent` stack-table recovery (#33).

Burn-down is visible in the Progress table of README (located/implemented
moved in the same commit); `0 / 911 translated` is the honest current
denominator.

### Phase 5 — bootstrap & menus
`Game::Update` bootstrap VA `0x006fadc0` (entry boundary `FUN_006fadc0`;
extent **unmeasured** — this is not a byte range claim). Implement just enough
for the main menu to draw: mount user's install, load scripts, run Lua update/
render hooks, present. Menus are the game's own Lua — we supply the native
surface they call, nothing else.

### Phase 6 — targets (#66)
Linux x86_64 first-class first (clean-clone verify, pacing/vsync, reads on the
user's own install), then Android arm64 via NDK. The libpng cross-build (#16)
is solved by fetching a pinned static libpng; the slice is packaged as an
arm64 debug APK and built in CI. On-device verification and README sideload
docs remain. **No Android-native Repentance+ port exists publicly.**

### Phase 7 — mod system (future, design phase only)
When bindings settle: resource-mod overlays using the Android-port guide's
semantics (JSON description, `patch/` per-record precedence, `options/`
overlays, overflow-refusal, `check.txt` reporting). We additionally run Lua
(5.3.6 vendored), which that port never had — PC mod scripts become the
ultimate upstream. Reference: modding guide recorded as consulted-only in
`reimplementation-references.md`.

### Phase 8 — tracking discipline
Every phase closes with merged evidence (tests, README Progress rewrite,
same-turn issue comment). Epic #71 tracks this document's execution.

---

## What other people's work can and cannot skip

| Can skip | Cannot skip |
|---|---|
| Offset discovery (§21 .zhl cross-check — done) | Translating the 911 closure bodies |
| Host/glue design (ximput maps, touch idioms, #65) | ANM2/ARCH000 decoders — your own install+binary or nothing |
| Trap classes (version pins, missed starts, hash gates) | The unpacked-exe route is **not** for us; user's exe is the only input |
| Mod-format semantics (Android-port guide) | Legal wall: nothing Nicalis-derived enters the tree |
| Menus content — the game's own Lua runs unmodified | Distribution: engine + tooling only, always |
| Heat busy-loop — avoided by construction (#67/#68) | Sound bug — SDL backend (#64) is the fix; Proton workarounds are not |

---

## Current blockers (all measured, 2026-11-15)

- PR #68 (white-box slice) and PR #72 (Lua-API consultation doc) **await the
  maintainer's merge** — do not merge for him.
- #64 (Audio), #65 (Input), #66 (Linux/Android epic), #16 (Android libpng)
  open and unowned until picked up.
- #52 (ARCH000 LZW decode) silently corrupts chunk mode — blocks Phase 1 data
  path if ARCH000 is in it.
- Sound on his current Proton session is a Proton/OpenAL Soft issue (`pactl
  list sink-inputs` empty); unrelated to engine progress but worth fixing for
  daily play (`PULSE_LATENCY_MSEC=60 %command%`).
- Heat: upstream 1.9.7.17 busy-loop; his call on any exe modification; never
  ours.

---

## What we say no to

- "Compile the lift wasm/C natively" (switch-native-style) — not distributable,
  needs an unpack step this project refuses, learns nothing reusable for our
  tree. Read-only lesson only.
- "Wrap tboi.online as an app" (TWA/Bubblewrap) — a launcher, not a product;
  not this repo's purpose.
- "Rust port" — new project, same gates, slower; not on this path.
- Shipping any game asset, code, or text — never, in any phase.
