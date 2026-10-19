# isaec-engine — state of the archive work

Written at the end of a session that took the project from "a docs-only stub" to
"reads Repentance's packed archives, decodes them, and renders them through SDL2".

Everything below was verified by running it. Where something is unknown or known
broken, it says so explicitly rather than glossing it.

---

## 1. The project

`isaec-engine` is a clean-room reimplementation of The Binding of Isaac's asset
pipeline. It reads the `.a` archives the game ships in `resources/packed/`, undoes
the per-entry obfuscation, decodes the PNG payloads and presents them.

Remote: `https://github.com/caos-obliquo/tboi-recomp.git`
Game install used for all testing:
`$HOME/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth`

---

## 2. The branch stack

`main` (`b3eb913`) has **no C++ core at all** — no codec, no image, no archive, no
window. All of that is the work below, split into a **stacked chain**, not siblings:

| # | branch | SHA | tests | contents |
|---|---|---|---|---|
| 1 | `feature/archive-codec` | `52a1668` | 35 | `ArchiveCodec` — the three cipher modes + fixtures |
| 2 | `feature/png-decoder` | `7eb30a9` | 40 | `decode_png` straight out of a buffer |
| 3 | `feature/archive-reader` | `26999c1` | 55 | `Archive` — ARCH000 container, spans, mode dispatch |
| 4 | `feature/sdl2-renderer` | `f3e58f7` | 55 | SDL2 backend + `isaec_window` target |
| 5 | `feature/asset-name-resolution` | `3f276a2` | 62 | djb2-5381 name hash + `filelist.txt` resolution |

Each branch builds clean and tests green **on its own** (verified by fresh
`cmake -S -B` + build + run per branch, with the build directory deleted each time
so `CMakeLists.txt` edits are never masked by a stale cache).

Merge in order, or squash the whole range. Do not merge branch 4 without 1–3:
`sdl_main.cpp` calls both `Archive::decode` and `decode_png`.

**To get the finished state:**
```
git checkout feature/asset-name-resolution
```
That is the tip and contains everything.

---

## 3. What the engine actually does now

```
$ build/isaec_window <install>/resources/packed/graphics.a 0 120 640 480
backend   sdl2
archive   mode=0x00 codec=xor entries=2300  2060/2300 named  hash=0x503f9b44  resources/font/pftempestasevencondensed_0.png
image     entry 0 resources/font/pftempestasevencondensed_0.png 512 x 256
viewport  640 x 480
frames 120 / elapsed 2.011 s / draws 120 / presented 120 / textures 0 live   exit=0
```

Nothing is pre-extracted. Container → span → codec → PNG → texture → present, in one
process, against a real Steam install.

CLI accepts two source forms:
```
isaec_window <image.png>                 [frames] [width] [height]
isaec_window <archive.a> <entry-index>   [frames] [width] [height]
```

Build and test:
```
cmake -S . -B build -j$(nproc) && cmake --build build -j$(nproc)
./build/tests/isaec_tests      # expect: 62 cases, 0 failed
```

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

`Archive::decode` therefore **refuses** mode 0x01 and non-stored mode 0x02 with an
error naming the mode, instead of returning bytes known to be wrong. `isaec_window`
on `videos.a 0` exits 1 with `not a decodable PNG: entry 0
resources/gfx/cutscenes/001_epilogue.ogv`. A renderer that draws noise is worse than
one that reports it cannot decode the entry.

### 5.2 mode 0x01 LZW has a real defect

Decodes **genuine** content — `config.a` yields XML (`<achievements gfxroot=...>`),
`fonts.a` yields BMF with real font names — then diverges in length. Both
over- and under-producing:

```
config.a e0       got 45575   size 25894
fonts.a  e0       got  7408   size  4147
rooms.a  e0       got 190901  size 113413
animations.a e0   got 1099332 size 4049421
```

Tails are garbage, but content *at* the `size` cut point is still coherent
mid-XML. So `size` is the true plaintext length and the LZW stream is correct for a
while (dictionary warm-up) and then breaks. Looks like a dictionary-reset or
code-width bug. Not yet found.

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
- **`archive_decode_refuses_the_modes_this_build_cannot_verify`** — the honesty lock
  on §5.1 and §5.2.
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
REA_GHIDRA_SEED_FILE=~/.local/share/rea/seeds/isaac-ng-body-seeds.tsv   # 441 lines
```

The seed file is the real lever: it tells Ghidra the 441 body addresses we
already know so it defines those functions instead of searching for them.
Its digest is part of the analysis profile and a session **fails to open** if the
seeds differ, so it must be regenerated deliberately.

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

All 441 bodies are decompiled and classified. Mechanism, evidence and the
instruction-level disassembly are in
`docs/binding-surface-findings.md` §14; the toolchain correction is in
`docs/rea-toolchain.md`.

```
441 / 441 decompiled, 0 failures, ~1.4 s each (kuna standalone)
434 / 441 begin with a lua_type argument guard
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

1. Resolve what `0xfff0b9d7` means (see above). Highest leverage.
2. Fix CI: `CMakeLists.txt:50` `pkg_check_modules(libpng16)` is a hard
   requirement and no runner has it, so every build job dies in Configure.
   Root cause is a missing dependency, not the runner labels — those are fixed.
3. Split the recovered surface into one source file per domain
   (`entity`, `player`, `render`, `input`, `audio`, `data`, `lua`), following
   the `runtime_<subsystem>` pattern in `docs/reimplementation-references.md`.
4. Instrument which Lua methods the game actually calls during boot, and
   implement in that measured order rather than address order.
