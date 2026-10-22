# tboi-recomp — handoff

Everything below was verified by running it. Where something is unknown or known
broken, it says so explicitly rather than glossing it.

---

## 1. The project

`tboi-recomp` is a clean-room reimplementation of *The Binding of Isaac:
Repentance*, built to run on arm64 Android and Linux x86_64 from one source
tree.

| | |
|---|---|
| Reference | the **Windows PC / Steam build** — `isaac-ng.exe`, a 32-bit x86 PE. Not iOS, not Android. |
| Output | **arm64-android** first, then **linux-x86_64**. |
| Remote | `https://github.com/caos-obliquo/tboi-recomp.git` |
| Test install | `$HOME/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth` |

The reference cannot run on Android and is not being ported or recompiled. What
is reimplemented is the portable half: data reading, the Lua VM, and the native
functions the shipped Lua calls into. Why the PC build and not iOS: the only iOS
copy available was a pirated repack with an injected cheat framework, and a
clean-room project cannot be built on a tampered artifact.

---

## 2. What has shipped, and what has not

| area | state |
|---|---|
| Lua binding layer | registry, single-parent inheritance, property model, `ApiSpec` codegen — 16 tests green |
| `ARCH000` container | solved; the reader asserts index arithmetic instead of trusting it |
| Payload codec | all three modes: `0x00` XOR keystream, `0x01` plain LZW, `0x02` LCG keystream |
| PNG decoding | from an in-memory buffer, via libpng |
| Headless binary | reads an archive, decodes, counts draws, exits |
| Windowed binary | SDL2 window; the renderer **counts calls and draws nothing** |
| Native binding surface | **911 bodies located, all named and class-attributed (64 classes); 911 decompiled; 0 implemented** |

The last row is the whole ballgame. The binding surface is the per-frame cost centre — `repentogxm` measures its own port and concludes that below 60 FPS the
cost is *translated code, draws, and Lua* — so until those bodies are
implemented there is nothing to render. See §13, §14, §15 and §16.

---

## 3. Running it

```sh
cmake -S . -B build -j$(nproc) && cmake --build build -j$(nproc)
./build/tests/isaec_tests                                       # 62 cases, 0 failed
./build/isaec_headless <install>/resources/packed/graphics.a 0 120 640 480
```

Nothing is pre-extracted. Container → span → codec → PNG → texture → present, in
one process, against a real Steam install.

Two source forms:

```
isaec_headless <archive.a> <entry-index>  [frames] [width] [height]
isaec_headless <image.png>                [frames] [width] [height]
```

`libpng` is required unless `-DISAEC_WITH_PNG=OFF`, in which case nothing decodes
images and the five PNG tests are not built. Lua is pinned to 5.3 and vendored.


---

## 4. Format findings

Full write-up lives in **`docs/arch000-format.md`** (that file is the authoritative
record; this section is the summary).

### Container

14-byte header `ARCH000`, then payloads, then a 20-byte-per-entry table at the end.
Each entry is five little-endian `u32`: `name_hash, special, offset, size, checksum`.

`open()` validates five things, all of which are pinned by tests: the magic,
`count != 0`, `index_offset + count*20 == file_size`, `entries[0].offset == 14`,
and offsets strictly ascending.

### Three modes

| mode | name | status |
|---|---|---|
| `0x00` | bogocrypt1 | **VERIFIED** against real archives |
| `0x01` | plain LZW | **DEFECTIVE** — see below |
| `0x02` | PRNG keystream | **UNVERIFIED** — produces noise on real data |

Mode is a file-level byte, not per entry.

### mode 0x00 — the cipher span

The keystream walks 1024-byte (256-word) blocks, **zero-padded**. The last block is
padded even when the entry doesn't fill it, so an entry's decode length is *not* its
asset size:

```
cipher_span(i) = entries[i+1].offset - entries[i].offset
```

For the last entry that means `file_size - offset`, which sweeps in the trailing
entry table. Everything past the recorded `size` is discarded after decoding, so it
does not matter — but both the Python and the C++ do it this way and the tests pin it.

Concretely: `graphics.a` entry 0 has `size=12327`, `span=12328`. Decode 12327 and the
PNG's IEND CRC is corrupt; decode 12328 and truncate, and it's valid.

Mode 0x00 is the **only** mode that ever had external ground truth — PNG chunk CRCs
and zlib inflate. It checks out on all 2503 entries of `graphics.a`, `music.a`,
`sfx.a`, `videos.a`.

### mode 0x02 — the 9-byte record header

Mode-0x02 archives put a 9-byte header before each payload:

```
[0:2]  u16 LE = size + 5
[2]    0x00      reserved
[3]    0x80      flags  <- 0x80 marks "stored in the clear"
[4]    0x01      kind
[5:7]  u16 LE = size
[7]    ~size & 0xFF      length check
[8]    0xfd or 0xfc      flavour
```

`declared_total == size+5 && declared_size == size && length_check == (~size)&0xFF`
is the "stored" predicate. It is exact — the count of entries satisfying it equals
the count of raw PNG magics in the file, precisely, in every archive checked:

```
repentance.a    378 stored  == 378 PNG magics
afterbirthp.a   587 stored  == 587 PNG magics
afterbirth.a      0 stored  ==   0 PNG magics
```

**965 PNGs are recoverable with no cipher at all.** `Archive::decode` returns them
verbatim.

### Name resolution — djb2-add-5381

```c
u32 h = 5381;
for (byte in lowercased_path) h = h * 33 + byte;
```

Over the full path with extension, forward slashes, lowercased. Confirmed by 22,186
matches across 21 archives.

The corpus is **`tools/ResourceExtractor/filelist.txt`** in the game install
(1,028,325 bytes, 18,789 paths; also copied under `macOS/` and `Linux/`). All 18,789
hash to distinct values — zero collisions. Auto-discovery walks up three parents
from `resources/packed/X.a` to find it; `ISAEC_FILELIST` overrides; `--names` forces it.

Per-archive coverage — **22,186 / 27,236 = 81.46%**:

```
afterbirth.a        2396/2647  90.52%     repentance_de.a     888/888   100.00%
afterbirth_jp.a      393/404   97.28%     repentance_es.a     834/834   100.00%
afterbirth_kr.a      458/657   69.71%     repentance_fr.a     914/914   100.00%
afterbirthp.a      10185/10228 99.58%     repentance_jp.a     845/845   100.00%
afterbirthp_jp.a     166/176   94.32%     repentance_kr.a     896/896   100.00%
afterbirthp_kr.a       0/2      0.00%     repentance_ru.a     875/875   100.00%
animations.a           1/1     100.00%     repentance_zh.a     941/941   100.00%
config.a               21/24    87.50%     rooms.a               3/13     23.08%
fonts.a                 6/10    60.00%     sfx.a               205/301    68.11%
graphics.a           2060/2300  89.57%     videos.a             17/17    100.00%
music.a                82/83    98.80%
repentance.a             0/4180   0.00%   <- the gap, see 5.3
```

Cross-check that the hash is right: `graphics.a` entry 0 is
`resources/font/pftempestasevencondensed_0.png`, and that entry decodes to a 512x256
RGBA image — exactly a font-atlas page — sitting next to the matching
`pftempestasevencondensed.fnt` in `fonts.a`.

---

## 5. Known-broken and unknown — read this before trusting output

### 5.1 mode 0x02 PRNG does not reproduce real payloads

The `PrngStream` was recovered from disassembly (wrapper at `0xa89c90`, PRNG at
`0xaa93e0`). It is **wrong**, or at least incomplete. Over 1406 decrypted entries:

- the first decoded byte is uniformly distributed (commonest value 13, ~5 expected)
- `decoded[0:4] == raw[0:4]` in **0 of 1406** — it changes every entry, which rules
  out a seed/offset mix-up and leaves "wrong algorithm"
- no PNG / OGG / BMF magic at any of four tried offsets
- output is indistinguishable from noise

**The existing C++/Python agreement tests prove nothing here.** Both are transcriptions
of the same disassembly reading. That was flagged as the available check in §9.4 of
the format doc, and §9.5 now records that the stronger check — a real packed archive —
was run and failed.

`Archive::decode` therefore **refuses** non-stored mode 0x02 with an error naming
the mode (0x02), instead of returning bytes known to be wrong. `isaec_window` on
`videos.a 0` exits 1 with `not a decodable PNG: entry 0
resources/gfx/cutscenes/001_epilogue.ogv`. A renderer that draws noise is worse than
one that reports it cannot decode the entry.

Mode 0x01 (LZW) is no longer refused: a probe (`/tmp/opencode/lzw_probe.py`) ran
`tools/isaac-archive.py`'s `decode_entry` over `config.a` (24 entries) and `fonts.a`
(10 entries) and the Python LZW decoder — byte-identical in logic to the C++ codec
— produced valid XML (`achievements`, `ambushRoom`, `babies`, `backdrops`, …,
`entities`) and valid BMF fonts ("PF Tempesta Seven Condensed", "Team Meat font",
"Droid Sans", "Terminus"). The framing is sound and the codec was right all along;
the defect was in the C++ layer, which never called it. `Archive::decode` now
calls `ArchiveCodec::decode(buffer, mode_, entry.special, 0)` and moves the
decoded buffer into `out` (the recorded `size` is the stream length, so `out` is
not truncated). The earlier "diverges in length" observation was a measurement
artifact of the probe, not a codec defect.

### 5.2 mode 0x01 LZW: resolved (the C++ layer was refusing)

### 5.3 `repentance.a` is 0/4180 named

Not a hash problem. Its 4180-hash set is **disjoint** from `afterbirthp.a`,
`afterbirth.a` and the manifest alike. 4180 × 18789 / 2^32 ≈ 1.8%, so zero overlap is
exactly what correct-hash-over-unlisted-paths predicts.

Harvested 4,357 text files across `mods/ resources/ data/ tools/` for path-like
strings, in several casing and separator variants: zero hits. Only three copies of
`filelist.txt` exist and all three are the same 18,789-path list.

Working theory: `filelist.txt` is the **base-game** extraction list. `afterbirthp.a`
resolves 99.58% because Afterbirth+ was folded into the base install;
`repentance.a` is the separate Repentance DLC archive the base extractor never
enumerated. The list *does* contain 528 paths with "repentance" in them, but they're
`resources/music/repentance/` and similar — not the DLC's gfx assets.

This was bounded and stopped deliberately. 18.5% of entries stay unnamed.

### 5.4 The pirated copy is not needed

The user downloaded a 1.5 GB Repentagon-based warez repack (`~/Downloads/The.Binding
.of.Isaac.Repentance.v4.0.2.rar`). It was declined as a source and is redundant — the
Steam install's `isaac-ng.exe` is **larger** and the install has 22 archives to the
repack's 14. It is safe to delete.

---

## 6. Open leads

Ranked by expected value.

1. **Mode 0x01 LZW.** Real bug, real content, fixable. Decodes XML/BMF then breaks.
   Prime suspects: dictionary reset at a specific code, and code-width growth past a
   threshold. `size` is authoritative so there's a length oracle to test against.
   Evidence to re-derive: `config.a` e0 got 45575 vs 25894.

2. **Mode 0x02 PRNG.** The disassembly reading is wrong. Either re-disassemble with
   more context, or brute-force the family: the shape (`state = state*MULT + INC`,
   output from the high words, variable rotate) suggests a known 64-bit LCG family.
   One right `MULT` would resolve 3000+ entries at once.

3. **`repentance.a` names.** Needs a Repentance-era manifest. Candidates not yet
   exhausted: strings inside `isaac-ng.exe`, the `mods/` directory contents in full,
   or brute-forcing paths against Isaac's DLC naming conventions.

4. **Merge the stack.** Nothing blocks it; it's 5 green commits.

5. **Archive entry names in the window tool** are already done. What's missing is a
   resource-index that groups entries by directory, or a `--names` dump mode in the
   C++ tool to match the Python one.

---

## 7. Operational gotchas — these will cost you hours if you don't know

**The repo path is `/home/caos/src/isaec-engine` and it flaps.** Intermittently the
`read` and `edit` tools *and* python's `open()` raise `NotFound` on the same path in
the same session, while `ls` works. Working pattern:

```python
import glob, time
def find_repo():
    for _ in range(60):
        try:
            for h in glob.glob('/home/caos/src/*/tools/isaac-archive.py'):
                d = os.path.dirname(os.path.dirname(h))
                if os.path.isdir(os.path.join(d, '.git')): return d
        except OSError: pass
        time.sleep(0.3)
```
Run everything with `workdir=/tmp/opencode`. Never `workdir` the repo itself.

**`write` and `edit` silently no-op on the repo path.** They report
"Wrote file successfully" and create zero bytes. The only reliable flow:
`write` to `/tmp/opencode/stage/<name>`, then copy in from python with an
`OSError` retry loop and a readback assertion on the exact byte count.

**`task` delegation is broken environment-wide** — `JSON schema exceeds the maximum
nesting depth of 10 levels`. Every agent/tool call fails. All work has to be inline.

**Never signal, kill or pkill any process you did not spawn yourself.** The live
Wayland session (dwl, foot, wmenu) is off-limits. No exceptions, no "cleanup" step.

**`shutil.copy2` preserves mtime**, so restoring a source file over a stale object
makes `make` skip the recompile and report a phantom failure against good code.
After any restore, `os.utime(path, None)` or delete the `.o`.

**Negative-control your tests.** Every serious fix in this session was found by
breaking it on purpose and confirming the test failed. One test was *vacuous* — the
OOB canary used a 37-byte span, 37 % 4 == 1, so the buggy swap loop's last word stayed
in bounds and the canary was unreachable; it passed against the exact bug it claimed
to guard. Write canary tests across all alignments.

**A `git clone`'s `origin` is the clone source, not the upstream.** A push from a
clone will happily report `rc=0` while hitting no network at all. Print
`git remote -v` before believing a push.

**`git ls-remote --heads origin 'feature/'` silently returns empty.** That pattern
doesn't match. Use no pattern and filter locally.

**Any repo patch script must be idempotent.** A non-guarded patch script ran twice
and doubled every insertion in `test_codec.cpp`. Guard with
`if MARKER in src: exit` and assert exact occurrence counts before `replace`.

---

## 8. Conventions established

- C++17. No `std::expected`. `<filesystem>` needs no extra link flag here.
- Public APIs return `[[nodiscard]] bool foo(..., out)` with a `const std::string&
  error() const noexcept` accessor. No exceptions in public APIs.
- `isaec/data/codec.hpp` exposes exactly one thing:
  `static bool decode(std::vector<u8>&, u8 mode, u32 seed, usize cipher_span = 0)`.
  It decodes **the whole buffer in place from offset 0**, so the caller slices first.
  `cipher_span` only affects mode 0x00.
- Fixture `.inc` files spell `u8`/`u32`/`usize` **unqualified at global scope**, so
  they must be included *after* the project headers *and* after the
  `using isaec::u8;` declarations. An `#include` after using-declarations is legal.
- `struct Bogocrypt1Fixture { const char* name; u32 special_hash; const u8* input;
  usize input_size; usize size; const u8* expected; };` — `input_size` is the span,
  `size` is the asset length, `expected` is the first `size` decoded bytes. Byte
  arrays must be emitted in a first pass *before* the table, not inside the
  initializer.
- Zero `-Wall -Wextra` warnings is a hard requirement.
- Commit messages: `feat(scope): imperative summary`, then prose saying **why**,
  explicitly naming which tests fail against the old code.
- Run python with `-u`. Without it output is buffered and a `| tail` pipeline shows
  nothing when the command times out.

---

## 9. File map

```
docs/arch000-format.md        the format spec — authoritative, read this first
docs/HANDOFF.md               this file

include/isaec/data/codec.hpp        ArchiveCodec: the three cipher modes
src/data/codec.cpp                  bogocrypt1 (block stride + OOB fix), LZW, PRNG
include/isaec/data/image.hpp        decode_png from a buffer, no file I/O
src/data/image.cpp
include/isaec/data/archive.hpp      Archive: container, entries, spans, names
src/data/archive.cpp
include/isaec/platform/sdl_renderer.hpp
src/platform/sdl_renderer.cpp
src/platform/sdl_main.cpp           isaec_window — both source forms

tests/test_codec.cpp                26 cases (4 are the negative-control-able cipher tests)
tests/test_image.cpp                5 cases
tests/test_archive.cpp              31 cases (15 container + 16 container-vs-reference)
tests/{bogocrypt1,lzw,prng}_fixtures.inc   generated byte-exact fixtures
tools/gen_bogocrypt1_fixtures.py    regenerates bogocrypt1_fixtures.inc from the Python ref
tools/isaac-archive.py              the Python reference tool — the ground truth
```

`tools/isaac-archive.py` is the **reference implementation**. When the C++ and the
Python disagree, the Python is usually right and the C++ has a defect — that
division of labour is why `test_archive.cpp` has a test that runs every
`bogocrypt1_fixtures.inc` case through the real container reader and byte-compares.

### Where the scratch work lives

`/tmp/opencode/` — all of it disposable, nothing the repo needs:

```
repo.py            find_repo()
run_build.py       build + test driver for the real repo
build_split.py     fresh configure+build+test for the split workspace
split/             --no-hardlinks clone + overlaid tree, where branch surgery happens
presplit-backup/   byte-verified backup of the pre-split working tree (122 files)
stage/             the only place `write` actually works for repo content
probe_*.py         investigation scripts (keep these, they document how findings were made)
negctl*.py         negative-control harnesses
```

Keep `probe_*.py` and `negctl*.py`. They are the evidence trail for every claim in
section 4, and re-deriving a finding is far more expensive than reading the script
that made it.

---

## 10. Test inventory

62 cases. What each group is actually protecting:

- **`codec_xor_matches_reference_fixtures`** — 16 fixtures, byte-exact vs the Python.
  Fails on the pre-fix cipher at `partial_word`.
- **`codec_xor_leaves_bytes_past_span_untouched`** — canary past the span, spans
  32–47 and 63–65 (all four residues mod 4) x seeds 1–64. Fails the pre-fix cipher
  at seed 1, span 34. **This test was vacuous until it was rewritten.**
- **`codec_xor_cipher_span_changes_the_tail`** — proves passing `size` where `span`
  belongs produces different bytes. Note it must compare the **whole span**, not
  just the first `size` bytes: the divergence lives in the borrowed tail, and
  whether it also surfaces below `size` depends on whether the final word's
  `key & 15` rotation is a swap that moves the differing byte inward.
- **`codec_xor_rejects_span_beyond_buffer`** — `span > data.size()` returns false and
  leaves the buffer untouched.
- **`resource_name_hash_matches_the_shipped_entry_hashes`** — 4 vectors whose values
  were independently observed in shipped archives. Negative control: seed
  `5381u`→`0u` ⇒ 6 of the 7 new name tests fail.
- **`archive_decode_of_a_xor_entry_matches_the_reference_decoder`** — every
  `bogocrypt1_fixtures.inc` case laid out as entry 0 of a two-entry container so
  `cipher_span(0) == input_size` exactly.
- **`archive_decode_refuses_only_the_modes_this_build_cannot_verify`** — the honesty lock
  on §5.1 and §5.2 (LZW decodes; only non-stored PRNG is refused).
- **`archive_cipher_span_reaches_the_next_entry_not_the_asset_length`** — pins the
  span rule, including the last-entry behaviour that sweeps in the entry table.

Container tests write synthetic archives to
`std::filesystem::temp_directory_path()/"isaec_archive_tests"`, so the suite runs with
no game install.

---

## 11. Reverse-engineering tooling state

Added 2026-10-18. Full detail in `docs/rea-toolchain.md`; summary:

| tool | version | state |
|---|---|---|
| rea-agents | 6.7.0 | 207 MCP tools; registered for OpenCode |
| ghidra | 12.1.4 | available, `doctor --provider ghidra` → `healthy: true` |
| kuna | 1.906 | available at `~/.local/bin/kuna`, `REA_KUNA_COMMAND` |
| hopper | — | declined (needs sudo); installable later, not the blocker |
| ida | — | blocked, commercial IDA Pro |

Every Ghidra call on `isaac-ng.exe` used to time out. Root cause: REA imports
the 9,362,440-byte PE into an **ephemeral** Ghidra project, runs whole-program
auto-analysis, answers one query, and deletes it. Each request repeats the whole
import, so nothing is ever cached. Proof: `pgrep -c java` returned **0** while
calls were timing out, and REA documents a 330,000 ms startup deadline that an
MCP client can beat.

Applied in `mcp.rea.environment`:

```
REA_GHIDRA_STARTUP_TIMEOUT_MS=900000     # 330s -> 15min
GHIDRA_HEADLESS_MAXMEM=6G                # 2G default is tight for 9.3MB
REA_GHIDRA_SEED_FILE=~/.local/share/rea/seeds/isaac-ng-body-seeds.tsv   # 911 lines
```

The seed file is the real lever: it tells Ghidra the 911 body addresses we
already know so it defines those functions instead of searching for them.
Its digest is part of the analysis profile and a session **fails to open** if the
seeds differ, so it must be regenerated deliberately. The list is generated:
`tools/gen_body_seeds.py` rebuilds it from the code-derived attribution table
(`tools/probe_class_attribution.py --json-out`), and the previous 441-line
file is kept beside it as `isaac-ng-body-seeds-441.tsv`.

**Not yet demonstrated.** Configuration is verified healthy; no decompilation of
`isaac-ng.exe` has succeeded. Status is "configured, not proven".

## 12. Repository workflow

`main` is protected (set 2026-10-18):

```
approvals required   0     solo repo: the owner cannot approve their own PR,
                            so requiring 1 blocked every merge forever
required checks     11     every real CI job context, verified against the
                            contexts GitHub has actually recorded on main
force push / delete  blocked
linear history      required
conversation        must be resolved
enforce admins      false   deliberate escape hatch
```

Branch model: one branch per issue, `feature/` `fix/` `add/` `docs/` prefixed,
opened as a PR. Never commit to `main`.

**The 11 required contexts include `linux-arm64`, `macos-arm64` and
`macos-x86_64`, which do not exist yet** — [PR #10](https://github.com/caos-obliquo/tboi-recomp/pull/10) adds them. They are required
by name; if a name is wrong, PRs block permanently. Confirm they report under
those exact names when [PR #10](https://github.com/caos-obliquo/tboi-recomp/pull/10)'s CI runs, and correct the list if not.

Open: **[PR #10](https://github.com/caos-obliquo/tboi-recomp/pull/10)** `feature/arm-ci-coverage` — native ARM64 CI, explicit macOS
architectures.

## 13. Method-binding recovery (supersedes the earlier 92)

> **The names in this section are retracted.** They come from a *backwards* scan
> from the `lua_pushcclosure` call, and the Lua compiler pushes the closure
> before the name, so the scan lands on the previous registration's literal:
> 451 of the 524 names here are wrong, and the 441-body figure is not a count
> of closure bodies at all. See §16 and
> `docs/binding-surface-findings.md` §15.

The earlier claim of 92 confirmed bindings was **wrong** and was retracted. The
correct anchor is the `lua_pushcclosure` call site, not the method name.

```
1064 lua_pushcclosure call sites
  524 named registrations
  539 closures whose name lives in another function (terminator hit first)
    1 call site with no body
distinct bodies: 63 + 417 metatable-key + 20 shared-name = 441
```

Nothing is unaccounted for. Detail and the disproof in
`docs/binding-surface-findings.md` §13.

The 63 one-to-one bindings are **type constructors**, not methods. The 417
metatable-key bodies are property accessors (`__propget` alone: 203 bodies from
209 registrations). The 655 this project tracks is a count of *names*, and
nothing yet shows it equals a count of *bodies* — [#3](https://github.com/caos-obliquo/tboi-recomp/issues/3) turns on that.

## 14. The property-accessor ABI is recovered (supersedes "0 of 441 signatures")

All 911 bodies are decompiled and classified. Mechanism, evidence and the
instruction-level disassembly are in
`docs/binding-surface-findings.md` §14; the toolchain correction is in
`docs/rea-toolchain.md`.

```
911 / 911 decompiled, 0 failures, ~1.3 s each (kuna standalone)
434 / 441 begin with a lua_type argument guard   <- not reproduced, see §16
375 / 441 read the bound object through the single constant 0xfff0b9d7
 64       dispatch through vtable slot 0 (60 getters, 4 setters)
 90       construct an 8-byte userdata of { type tag, payload }
 11 shapes, 441 of 441 classified, no residual bucket
```

The uniform accessor:

```c
// verify this object is a userdata (434/441 bodies do exactly this)
if (lua_type(L, 1) != LUA_TUSERDATA) return luaL_argerror(L, 1, "expected object");

IsaacObject *obj = (IsaacObject *)lua_touserdata(L, 0xfff0b9d7); // the receiver
void *result      = (*obj->vtable->slot0)(obj);                  // the getter

if (!result) { lua_pushnil(L); return 1; }                       // null path
IsaacRef *ref = (IsaacRef *)lua_newuserdata(L, 8);
ref->tag = 0x00b753c8; ref->payload = result;                     // wrap
lua_rawgetp(L, 0xfff0b9d8, 0x00c8297f);                          // metatable
lua_setmetatable(L, -2);
return 1;                                                        // getters return 1
```

Setters are the same shape with `luaL_checkinteger(L, 2)` and `return 0`.

**`0xfff0b9d7` is resolved.** It looked like a corrupt stack index; it is not.
The shipped `Lua5.3.3r.dll` contains the sentinel `0xfff0b9d8` inside Lua's own
index translation and routes values below it into a typed array lookup, so
`0xfff0b9d7` is slot 1 of a hidden array anchored on the current stack frame.
Stock Lua 5.3.6, vendored in `third_party/`, is the positive control and has no
such sentinel. 772 of 833 `lua_touserdata` call sites in `.text` pass it. Full
disassembly and reconstruction in `docs/binding-surface-findings.md` §14.6.

## 15. Next steps, in order

0. ~~Resolve what `0xfff0b9d7` means~~ — **done**, see §14 above.
1. ~~Attach a **class** to each of the 911 named bodies~~ — **done**, see §17.
1b. ~~Split the surface into methods / properties / metamethods~~ — **done**,
    §17; `bindings/code-derived-surface.json` is the generated table.
1c. ~~Measure the shape of every body (arguments seen, values returned)~~ —
    **done**, §18; `tools/probe_body_arity.py`, carried into the generated spec.
2. Fix CI: `CMakeLists.txt:50` `pkg_check_modules(libpng16)` is a hard
   requirement and no runner has it, so every build job dies in Configure.
   Root cause is a missing dependency, not the runner labels — those are fixed.
3. Wire the metamethods into the runtime binding (they are in the spec only).
4. Split the recovered surface into one source file per domain
   (`entity`, `player`, `render`, `input`, `audio`, `data`, `lua`), following
   the `runtime_<subsystem>` pattern in `docs/reimplementation-references.md`.
5. Instrument which Lua methods the game actually calls during boot, and
   implement in that measured order rather than address order.
6. Recover the class hierarchy. Not recoverable from the registration code
   (`__parent` is a stack table); it needs a runtime or REPENTOGON cross-check.

## 16. Property names are recovered; the earlier names were off by one

`tools/probe_property_names.py` scans **forward** from every `lua_pushcclosure`
call and recovers the name each closure is registered under, because the Lua
compiler pushes the closure first and the name afterwards
(`pushstring` → `rotate` → `rawset`). Full mechanism, hand-checked
disassembly and the retractions are in
`docs/binding-surface-findings.md` §15.

```
1064 lua_pushcclosure call sites
2010 registration records (one per caller for multi-caller helpers)
2010 records with a name  ->  0 call sites without a name
1289 distinct names (1275 property-style + 14 metamethod names)
 911 distinct closure bodies, all 911 named
```

What this retires:

- The 524 names `probe_closures.py` produced by walking backwards. **73 agree
  with the forward scan, 451 differ** — the backward scan lands on the
  previous registration's literal, usually a class-name lookup key.
- The "441 bodies" figure. 441 is the number of bodies the backward scan could
  *reach* (63 + 417 + 20). There are **911** distinct closure bodies over all
  1064 call sites; the seed file now covers all 911 of them and **470 bodies
  have never been decompiled**.
- The "63 one-to-one bindings are type constructors" claim. 45 of them are the
  class's `__gc` closure; the rest are ordinary methods.

What this does not deliver: no body is implemented, and no body is attached to
a class. Naming is one of the four states tracked in the `README.md` progress
table, and it is the only one that moved.

## 17. Per-class attribution

`tools/probe_class_attribution.py` attributes every closure registration to
the game class that owns it, from the code rather than from string adjacency.
Full mechanism, the class table and the contamination delta are in
`docs/binding-surface-findings.md` §16.

```
66 class-registration routines (0x0085d040), 64 distinct classes
one master function (0x00866960) holds every class scope, in class order
3 global namespaces (Isaac, Input, Options) -- not classes
2012 attributed records, 1538 (class, property) pairs, 911 bodies attributed
kind split: method 1026 / property 861 / metamethod 125
surface: 64 classes + 3 namespaces, 938 class methods, 434 class properties,
         76 class metamethods, 65 namespace methods, 27 namespace properties
```

The scope rule: `0x0085dab0` is a *constructor* that opens a scope and
`0x0085d980` is the destructor that ends it (neither is a class registrar); the
class comes from the last class-registrar call, unless a construct happened
after it, in which case the record belongs to the namespace opened by that
construct. The classification is per call site, so a helper that installs both
sides of a property (`0x00875230`, Vector `X`/`Y`) yields one getter and one
setter.

The documented HUD/Isaac contamination is resolved: `HUD` has 12 properties,
`Isaac` is not a class at all, and the class list grew from 9 to 64. Three
corrections to the earlier pass: the mod-data functions are `global:Isaac` and
not `global:Input`, `IsVulnerableEnemy` belongs to `Entity`, and `__mul`
belongs to `Color`.

What this does not deliver: no body is implemented, metamethods are recorded
in the spec but have no runtime registration path, and the class hierarchy
(the `__parent` link) is not recoverable -- it is a Lua table taken from the
stack, never a name literal.

## 18. Shape census: argc and returns per body (Phase 4)

`tools/probe_body_arity.py` measures, for all 911 bodies, the highest Lua
stack index the body touches (the receiver lives at index 1, so `argc` is that
index minus one) and the number of values the body returns to Lua. Helper
calls are followed recursively, so a body whose arguments are read inside a
helper still gets a number.

**`argc` is an upper bound, not a verified argument count.** The body may
ignore the arguments it reads, and the game may pass arguments the body never
looks at. `argc_evidence` records where the number came from (`index`,
`helper`, `receiver-only`, `none`).

Census over the 911 bodies: argc 0 → 465, 1 → 297, 2 → 60, 3 → 39, 4 → 21,
5 → 14, 6 → 8, 7 → 4, 8 → 3, no evidence → 52; returns 0 → 359, 1 → 534,
unknown → 18. Over the 1003 methods of the surface: argc 0 → 539, 1 → 273,
2 → 85, 3 → 46, 4 → 28, 5 → 14, 6 → 7, 7 → 4, 8 → 3, unmeasured → 4; returns
1 → 587, 0 → 395, unmeasured → 21.

The numbers are now carried in `bindings/code-derived-surface.json`,
`bindings/isaac-api-spec.json` and the generated header, and the C++ test
`spec_carries_the_measured_shape_census` pins the hand-checked `Vector`
members.

Still not established: the *meaning* of the arguments (the bodies call the
game's C++ virtuals, so the semantics are not in the binary), and the number
of arguments the game actually passes. See
docs/binding-surface-findings.md §17.

## 19. The Vector class is implemented (first bodies that run)

The first slice of Phase 5 is in: the `Vector` class has real
implementations instead of `not_implemented_trampoline()` stubs.

- `include/isaec/api/vector.hpp` + `src/api/vector.cpp`: the value type (two
  32 bit floats) and the arithmetic.
- `include/isaec/api/vector_lua.hpp` + `src/api/vector_lua.cpp`: the Lua
  face — the metatable, the `__index` / `__newindex` dispatch, 13 methods,
  the `X` and `Y` properties and the five arithmetic metamethods.
- `tests/test_vector.cpp`: the arithmetic on its own, and the class driven
  from a script through a real `lua_State`.

Implemented: 15 of the 1464 members (13 methods, `X`, `Y`), on 9 of the 911
closure bodies, plus `__add`, `__sub`, `__mul`, `__div` and `__unm`.

**What is recovered and what is not.** The names, the ABI (receiver at index
1, visible arguments from index 2, 0 or 1 return values, a 16 byte userdata
with the payload at offset 0, the property roles, the metamethod set) come
from the binary. **The arithmetic does not**: the original's closures call
the game's C++ `Vector` through a vtable pointer, so the maths in
`vector.cpp` is the documented Isaac API reimplemented from scratch.

Two members of the class stay loud stubs on purpose: `Vector:Clamp` and
`Vector:Clamped` read **four** numbers in the original (the shared helper
`0x8ba890` does `luaL_checknumber` at indices 2, 3, 4 and 5) while the
public API documents two. The semantics of four numbers are unknown, so
those two members still raise `isaec: Vector:Clamp is not implemented`.
`Vector.__tostring` is not implemented either, and `Vector:Lerp`'s fourth
argument (a boolean the original forwards to the game's own routine) is
ignored.

`install_vector_class(binding)` replaces only the members it implements and
is called *after* the generated spec is applied; every other member of every
other class still raises. See docs/binding-surface-findings.md §18.


## 23. The field layout of the game objects is recovered

`tools/probe_field_layout.py` reads, for every property, the offset of the
field the getter reads and the type it pushes. 53 of the 64 classes have a
layout, 21 of them complete, 434 fields in total (174 int, 104 float, 48
bool, 75 object, 1 string, 32 unknown).

Two traps are worth remembering:

* the offset-zero field prints as `[ecx]` without a `+0x0`, so an accessor
  parser that requires the offset would drop every first field;
* an accessor that reads two locations is not a field accessor -- the list
  `Size` accessor computes `(end - begin) / 8` and would be given a
  meaningless offset.

The `object` typed fields point at the game's own objects. The port cannot
produce them, so those properties stay loud stubs.
## 24. The field classes are implemented

The layout of the game objects is recovered (section 22 of the findings), so
the classes whose properties are plain fields can be implemented as they are.
`FieldClass` (`include/isaec/api/field_class.hpp`) is the shared helper: a
table of `{name, offset, type}` per class, one getter and one setter for the
whole class, and the property name passed as a second argument by the shared
dispatch.

Nine classes are implemented: `ActiveItemDesc` (7 fields), `KColor` (4),
`GridEntityDesc` (8), `TemporaryEffect` (2), `QueueItemData` (2),
`GridEntityPit` (1), `GridEntitySpikes` (1), `GridEntityTNT` (1) and `Sprite`
(4) -- 30 members over 27 closure bodies. The object-valued members stay loud
stubs, because an object field points into the game's heap.

The state of the port: 54 of the 1464 members do something, 40 of the 911
bodies are covered, and **0 of 911 bodies are translated**. What the port
reproduces is the *interface* the binary shows; the behaviour behind the
interface is the documented Isaac API, reimplemented.

### Next steps, in order

1. The remaining `Vector` members (`Clamp`, `Clamped`, `__tostring`) once
   the argument discrepancy is settled.
2. The next class group: the pure-math and container classes that need no
   game state (`Color`, `PosVel`, `RNG`, the list classes).
3. The classes that need the game's C++ objects (`Entity`, `Player`) are a
   different problem: their closures call game virtuals, so they need the
   game side, not just the binding.
4. The REPENTOGON `libzhl` cross-check (#20) for the names and the arities.

## 20. The Color class is implemented; the value classes share one dispatch

`Color` is the second class that runs. Its seven properties (`R`, `G`, `B`,
`A`, `RO`, `GO`, `BO`) are fully determined by the binary: one getter body
(`0x0089f590`) and one setter body (`0x0089f500`), with a per-field
accessor in the upvalue that reads the field at a fixed offset. The
offsets are in `include/isaec/api/color.hpp`, and they are the game's own.

Its four methods and the `__mul` metamethod call into the game's C++ colour
object, which is not in the binary, so they keep the loud stub. Their
argument shapes are known (four numbers for `SetColorize`/`SetTint`, three
for `SetOffset`, none for `Reset`), their meaning is not.

The dispatch that reads a property and finds a method now lives in
`include/isaec/api/lua_value.hpp` and is shared by `Vector` and `Color`:
`ValueClass::install` creates the metatable and installs the read and the
write dispatch, `ValueClass::check` refuses a value of another class, and
`ValueClass::push` creates an instance.

Next steps, in order:

1. the classes whose members are pure data or pure arithmetic
   (`PosVel`, `RNG`, the list classes), one group at a time;
2. the classes that need the game side (`Entity` and its subclasses, `Room`,
   `Level`), which cannot be implemented before the engine has the state
   they read;
3. the #20 cross-check against REPENTOGON's libzhl;
4. the open issues: #28 (the definition behind the "434/441 lua_type
   guard"), #33 (the class hierarchy is not recoverable from the binary),
   #35 (the other 71 metamethods are not wired).

## 21. The PosVel class is implemented (the first fully recovered class)

`PosVel` is a position and a velocity, both Vectors, and it is the first
class whose members are *entirely* recoverable from the binary: the property
upvalues are the field offsets (0 for the position, 8 for the velocity) and
the closure bodies move eight bytes -- two floats -- to and from them. The
getter hands out a copy, the setter takes a Vector.

What was added:

* `include/isaec/api/posvel.hpp` -- the value type and the recovered offsets;
* `include/isaec/api/posvel_lua.hpp`, `src/api/posvel_lua.cpp` -- the Lua
  face: the two properties, which replace the stubs of the generated spec;
* `tests/test_posvel.cpp` -- the offsets, the round trip, the write, the copy
  semantics and the refusal of a value that is not a Vector.

`PosVel` is the first class that uses the shared value-class dispatch
(`include/isaec/api/lua_value.hpp`) instead of a private metatable, so a
class that only adds members does not copy the Lua glue any more.

The honest count: 24 of the 1464 registered members do something (Vector 15,
Color 7, PosVel 2), they cover 13 of the 911 bodies, and still **0 of the 911
bodies are translated**.

Next steps, in order:

1. the rest of the pure-data classes (`RNG`, the list classes, the config
   lists) and the classes whose properties are plain fields
   (`QueueItemData`, `TemporaryEffect`);
2. the classes that need the game side (`Entity` and its subclasses, `Room`,
   `Level`), which cannot be implemented before the engine has the state they
   read;
3. the #20 cross-check against REPENTOGON's libzhl;
4. the open issues: #28 (the definition behind the "434/441 lua_type
   guard"), #33 (the class hierarchy is not recoverable from the binary),
   #35 (the other 71 metamethods are not wired), #37/#38 (the `Vector`
   members whose shape does not match the public API), #40 (the `Color`
   methods that call into C++ that is not in the binary).

## 22. The field layout is cross-checked against an independent source

The cross-check now also compares the **method names**: 187 confirmed, 639 only in the binary, 410 only in the layout. The `.zhl` files are hand-written and incomplete, so the two unmatched buckets mostly measure the source, not the recovery. The parameter lists of the declared methods are not compared yet.

`tools/crosscheck_libzhl.py` compares the field offsets recovered from the
closure bodies with the `.zhl` class layouts the Repentogon project
publishes (`TeamREPENTOGON/REPENTOGON`, `libzhl/functions/`). The tool
downloads the files (public, no token) and caches them outside the repo.

Measured: 64 classes in the surface, 43 with a published layout, **105
fields confirmed**, 11 at the same offset under a different name, 3 moved,
141 only in the binary, 493 only in the layout. The first hand checks agree:
`Vector` x = 0 / y = 4, `PosVel` pos = 0 / vel = 8, `ActiveItemDesc` item =
0 / charge = 8 / battery charge = 12, `ProjectileParams` dot product limit =
120.

The differences are tracked, not dropped: the `Room` layout of the later
release does not match this build (`Room.Variant` at 0xc is `_gridWidth` in
the layout), `GridEntityDoor` grew by 0x114 bytes, and `Entity.Friction` at
0x36c has no counterpart in the layout at all.

The `.zhl` files describe *Repentance+*, a later release than the build this
project reads, so a difference is not automatically an error in either
direction, and a matching offset does not prove that two members are the
same member.

### Next steps, in order

1. The "field object" slice: a generic field support in the C++ layer (a
   flat payload and a table of name, offset and type) for the classes whose
   properties are all plain fields -- `ActiveItemDesc`, `ProjectileParams`,
   `Item`, `EntityPlayer`, `EntityLaser`, `EntityFamiliar`, `EntityNPC` --
   with the types read from the getter bodies and the layout checked
   against the `.zhl` offsets.
2. The remaining classes that need the game side: the list classes, `RNG`,
   the entity classes.
3. The rest of the open issues: #28, #33, #35, #37, #38, #40, #5, #6, #12.

## 25. The field classes are generated

The nine field classes of §24 are now generated from the measured layout:
`tools/gen-field-classes.py` reads `bindings/code-derived-field-layout.json`
and writes `include/isaec/api/generated/field_classes.hpp` and
`src/api/field_classes_generated.cpp`. The hand written tables are gone.

* 45 classes, 327 plain fields, 220 distinct closure bodies.
* The CI context `generated-spec-is-current` regenerates both files and
  diffs them, so the tables cannot drift from the JSON.
* The JSON still needs the binary and is refreshed by hand with
  `tools/probe_field_layout.py`; that is the one step CI cannot repeat.
* The honest count: **342 of the 1464 members do something**, they cover
  **222 of the 911 bodies**, and **0 of the 911 bodies are translated**.
* The 17 string fields are implemented as well: a `std::string` in the game's
  layout (a pointer, a length, a capacity), with the port owning the bytes it
  stores and the collector of the class releasing them. Making them work
  exposed a bug in the shared value-class support: `lua_newuserdata` does not
  clear the memory it hands out, so the payload is now cleared explicitly.
* The width of a field is recovered from the load in the getter body (a dword,
  a word, a byte, or two dwords for a 64 bit field), not from the push helper:
  the helper that pushes a 16 bit value is also used for a one byte field. The
  census has no unknown type left (166 int, 104 float, 11 int16, 5 int64,
  3 uint8, 48 bool, 80 object, 17 string).

The next steps, in order:

1. The remaining plain fields of the classes whose layout is incomplete
   (`Entity` has 24 of 37 fields, `Item` 30, `EntityPlayer` 26): the types
   that could not be recovered need a second look at their getter bodies.
2. The class hierarchy (#33) and the metamethod wiring (#35).
3. The classes whose values live in the game: the methods, the object
   fields and the list classes. These need the game side, not more
   disassembly.
