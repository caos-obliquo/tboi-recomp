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
| Payload codec | `0x00` XOR keystream, `0x01` LZW (§32), `0x02` chunked DEFLATE (§30.1); 2647 entries in an unresolved third arm |
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
| `0x01` | LZW | **IMPLEMENTED** — ported from the natives, see §32 |
| `0x02` | chunked DEFLATE | **MOSTLY DECODED** — 1533/4180 entries, third arm open, see §30.1 |

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

`Archive::decode` no longer refuses either arm: mode 0x01 decodes through the
ported LZW state machine (§32) and mode 0x02 through the chunk walker (§30.1).
The unresolved 0x02 entries still fail explicitly rather than returning noise.
`isaec_window` on `videos.a 0` now gets past the LZW arm and fails later at the
entry's own format (`resources/gfx/cutscenes/001_epilogue.ogv` is not a PNG). A
renderer that draws noise is worse than one that reports what it cannot decode.

### 5.2 mode 0x01 LZW: the defect is fixed

**Resolved by [#52](https://github.com/caos-obliquo/tboi-recomp/issues/52).**
The old Python and C++ ports all rebuilt the dictionary per chunk, grew the code
width after insertion, and walked `size` as if it were the compressed span. The
natives show one dictionary per entry, width grown before each read, and a walk
bounded by the compressed span; the container layer then truncates to `size`.
See §32 for the evidence and the negative control.

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

1. **Mode 0x02 third arm.** 2647 of 4180 entries assemble to their declared size
   but match no magic and resist the PRNG and bogocrypt1 families. The native
   stored-block XOR at `0xa89d70` is not yet identified in the data; see §30.1 and
   `docs/arch000-format.md` §9.6.

2. **`repentance.a` names.** Needs a Repentance-era manifest. Candidates not yet
   exhausted: strings inside `isaac-ng.exe`, the `mods/` directory contents in full,
   or brute-forcing paths against Isaac's DLC naming conventions.

3. **Merge the stack.** #106/#108 need a rebase onto the post-#110 codec; the rest
   of the stack is green.

4. **Archive entry names in the window tool** are already done. What's missing is a
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
required checks      10    every real CI job context, verified 2026-11-19
                            against the branch-protection API (strict=true):
                            linux-gcc, linux-clang, linux-arm64, android-arm64,
                            android-apk, warnings-as-errors, asan-ubsan,
                            generated-spec-is-current,
                            binding-extractor-selfcheck,
                            release-notes-are-writable
force push / delete  blocked
linear history      required
conversation        must be resolved
enforce admins      false   deliberate escape hatch
```

Branch model: one branch per issue, `feature/` `fix/` `add/` `docs/` prefixed,
opened as a PR. Never commit to `main`.

An earlier version of this section said the required list was **11** and that
`macos-arm64` / `macos-x86_64` were missing, pending [PR #10](https://github.com/caos-obliquo/tboi-recomp/pull/10). That
text was wrong twice over: there are **9** required contexts (listed above),
and [PR #10](https://github.com/caos-obliquo/tboi-recomp/pull/10) already merged on 2026-10-19. Corrected 2026-11-17.

Release-record workflow: since 2026-11-17 the repository setting *Allow
GitHub Actions to create and approve pull requests* is enabled, so
`.github/workflows/version.yml` can open the record PR after each merge
instead of failing at the `gh pr create` step (the failure was silent, it was
not a required context). The eight merges made while it was broken were
recorded by [PR #81](https://github.com/caos-obliquo/tboi-recomp/pull/81), bumping 0.1.4 → 0.1.12 via
`python3 tools/release.py bump-notes --pr N` per merge. No tags were cut —
`v0.1.3` remains the latest tag/release. See #74.

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
* The honest count: **356 of the 1464 members do something**, and **0 of the
  911 closure bodies are translated**. The bodies are glue; the arithmetic
  they call is translated separately (see §26).
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

## 26. The Vector and Color methods are translated from the natives

The `Vector` and `Color` classes were reimplemented from public API
documentation through §18 and §19. They are now translated from the reference
binary's native functions (v1.9.7.17.J460, sha256 `3bdfc8ba...`), and the
translation was checked against the owned game running under Wine.

### What moved

* **Vector**: 15 instance methods (`Clamp` and `Clamped` are new), the
  class-table `Vector.FromAngle`, X/Y and the arithmetic metamethods. The 17
  native functions at `0x00a0fea0`..`0x00a10420` were read instruction by
  instruction. The 4th argument of `Lerp` is a clamp flag, and the receiver is
  mutated; the Lua-visible result aliases the receiver (#38).
* **Color**: `Reset`, `SetTint`, `SetColorize`, `SetOffset`, `__mul` and the
  undocumented `Color:Lerp` (`0x006832e0`). The block at offsets 16..28 is the
  colorize RGBA, not reserved padding. The measured registration-time
  pointers are `0x00682ed0`, `0x00682f20`, `0x00682fb0`, `0x00682fe0`,
  `0x006832e0` and `0x006831c0`; the numbers quoted in #40 are vtable slots.
* `docs/binding-surface-findings.md` §18/§19 carry the authoritative tables.
* `tests/test_vector.cpp` and `tests/test_color.cpp` were rewritten; ctest is
  119 cases, 0 failed.

### Oracle, and why it is only partial

The owned game was launched through REPENTOGON's launcher, with a probe mod
that logged deterministic cases through `Isaac.DebugString`. 91 lines were
captured in `/tmp/opencode/isaec-oracle/raw.log`; the mod was removed after.

The host was **v1.9.7.12.J273** (sha256 `ceb598b4...`), not the reference
v1.9.7.17.J460. Most Vector natives are byte-identical after a **-0x2a640**
shift, and the two Color functions after **-0x4880**. The exception is
`Vector:Lerp`: the host's native has no clamp branch. Static disassembly of
the reference is authoritative there. `Color:Lerp` does not exist in the host
at all, so its behaviour is static-only.

### Dead ends recorded

* System Wine 11.22 in a fresh prefix cannot start the game (exit 53, empty
  log). Proton 9.0 run directly in the existing 250900 prefix truncates the
  log and then hangs before the first line; `Xvfb :99` does not help.
* The only working launch path is the Steam/REPENTOGON launcher. Changing
  `repentogon_launcher.ini` LaunchMode to 0 starts the game and runs the mods;
  it was restored to 1 afterwards.
* The probe must live at `<install>/mods/<name>/main.lua` with a
  `metadata.xml` next to it; every folder in `mods/` auto-loads.

### Next probes

1. Re-run the same probe against a v1.9.7.17 host if one is ever available;
   the only open oracle gap is `Vector:Lerp`'s clamp and `Color:Lerp`.
2. `Vector.__mul`, `__div`, `__sub`, `__unm` were checked through the oracle
   but their native bodies were not disassembled; read them the same way.
3. The remaining `Color` colorize float values have no Lua getter; a probe
   would need a native field read or a `SetColorize`/`__mul` differential.

## 27. The RNG class is translated; its oracle is still open

`RNG` is implemented on `feature/97-rng-native-methods`: five native methods
(`GetSeed` `0x0040c3a0`, `SetSeed` `0x007e8fe0`, `RandomInt` `0x007e9020`,
`RandomFloat` `0x007e9080`, `Next` `0x007e90f0`), the callable constructor and
the 16-byte state. The authoritative write-up is
`docs/binding-surface-findings.md` §25. `tests/test_rng.cpp` brings the suite
to 128 cases, 0 failed.

What is **not** established: no RNG numeric fixtures were captured from the
game. The probe mod was installed and the launcher reached
`InstallationData::Validate`, but no `Repentogon/isaac-ng.exe` process started;
`LaunchMode = 0` auto-start did not fire either. The launcher setting and the
probe mod were removed afterwards (the setting is back to 1). RNG remains
static-only until a v1.9.7.17 host is available.

Dead end recorded: do not use the v1.9.7.12 Repentogon `SetSeed` to probe the
clamp path. Its `0x7df450` logs `RNG: Invalid ShiftIdx` for an index above 80
and then reads the table out of bounds; the reference binary clamps to row 80.

Next probes, in order:

1. Start a v1.9.7.17 host and run the RNG probe in
   `/tmp/opencode/isaec-oracle-rng/main.lua`; compare `Next`, `RandomInt` and
   `RandomFloat` streams, and the default seed `0xaa17414f`.
2. Disassemble the remaining Vector metamethods (`__mul`, `__div`, `__sub`,
   `__unm`); only their oracles were checked.
3. Close #97 only when the RNG oracle is captured; the implementation and
   tests are already in the branch.

## 28. The Vector arithmetic metamethods are translated; #100

The five arithmetic metamethods and `__tostring` are now translated from the
reference binary on `feature/100-vector-metamethods` (issue #100):

| Metamethod | Native | Reference behaviour |
| --- | --- | --- |
| `__sub` | `0x00a10480` | component-wise subtraction |
| `__mul` | `0x00a104e0` | scale both components by `other.x` |
| `__div` | `0x00a105a0` | divide both components by `other.x` |
| `__unm` | `0x00a10660` | flip each sign bit |
| `__tostring` | `0x0085e630` | `sprintf(buf, 256, "%g %g", ...)` |

`__mul`/`__div` are **not** component-wise, and the shared closure `0x8a8740`
rejects a number operand: `v * 2`, `2 * v` and `v / 2` are Lua errors in the
reference. The v1.9.7.12 oracle host accepted them and returned component-wise
results; that host divergence is recorded in `docs/binding-surface-findings.md`
§18.3 and the reference binary is authoritative.

`tests/test_vector.cpp` now covers the corrected products, scalar rejection,
sign-bit negation and `__tostring`; the suite is 128 cases, 0 failed. The
authoritative write-up is §18.2/§18.4. The next work is the remaining
`__add`-family metadata or the next class, not a re-probe of these natives.

## 29. The BitSet128 class is translated; #103

`BitSet128` is implemented on `feature/103-bitset128-natives` (issue #103):
two methods (`Get` `0x0085e560`, `Set` `0x0085e5c0`), six metamethods
(`__band` `0x0085e4d0`, `__bor` `0x0085e4f0`, `__bxor` `0x0085e510`,
`__bnot` `0x00592200`, `__eq` `0x0085e530`, `__tostring` `0x0085e490`) and
the callable class table (`BitSet128(low, high)`, native `0x008bc330`).

The payload is four 32-bit words; the existing generated field class already
supplies `l` at offset 0 and `h` at offset 8 as `Int64`. `Get` returns false
for a negative index or an index of 128 or more, `Set` ignores both. The
bitwise operators build a new value; the receiver is never mutated.
`__tostring` is exactly `BitSet128: %016llx%016llx` with the high half
first.

`tests/test_bitset128.cpp` brings the suite to **138 cases, 0 failed**. It
covers every word boundary, the out-of-range paths, set/clear, non-mutation,
equality, the exact text, the constructor halves and the foreign-operand
errors. No game-side oracle is needed: every native here is pure arithmetic
over the payload.

The authoritative write-up is `docs/binding-surface-findings.md` §26. Next
work is the next pure-data class or the list classes, per the roadmap.

---

## 30. REA verification of mode 0x02: MiniZ found, PRNG transcription retracted

Work on `verify/109-prng-rea` (issue #109, documented in
`docs/arch000-format.md` §9) used the DX-Ball/TH04 REA workflow — instructions,
callers, constants, recovered C, independent check — on the shipped
`isaac-ng.exe` (SHA256
`3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`).

Native chain, all evidence from Ghidra (untrusted target data, cited as
Evidence IDs):

| VA | role | Evidence ID |
|---|---|---|
| `0xa68c50` | mode dispatch | `ev_9734722ed3d254f67bf9738cc450c60c4beb597c8633a43d6a067455d3ed8e20` |
| `0xa89c90` | PRNG buffer wrapper | `ev_4957dbced24eeaf09e969600de5891fbe57209d27da4f2897048a737dda1c670` |
| `0xaa93e0` | PRNG generator | `ev_fe9f7e0135d4ab37b5a0134573e1ce885d317846453c3bc8905acfe4190a3865` |
| `0xaa93eb` | seed-prep bytes | `ev_dd72ecf227305aee7420801d63a464968849b03d5ed1e79714a10fbbeb85f833` |
| `0xaa9420` / `0xaa9425` | multiplier immediates | `ev_f45a036bf54ad8e5388809fdfe279f2481726b8b312bc91c570720e1db193d97` / `ev_ff92aeb65ab4dd4497213617bee2c070fb258db5c9385a3087470008bdbe9446` |
| `0xa694a0` | vtable refill | `ev_c46289808951fff4cdb4f2dbbe0e6884131cf7a3b438e7d31465c38b57c655db` |
| `0xa69090` | chunk refill | `ev_9bb8bfe6a9261ddc23a7a22df21e6a7c1aecce7cab5b1c1a911903a8f4ba21ab` |
| `0xa89f80` | MiniZ wrapper | `ev_f2927f0f8983277f6d9771f90bb46f4ecb4989990d3add4a69fc5477e24c2881` |
| `0xa89d70` | stored-block XOR | `ev_6ec6deefdee9729772e73b4b5f3e026a7da44d6ae54da659a501a9f4d51b460a` |
| `0xa85710` | MiniZ decompressor | `ev_cd61b34462953efeb4a0ae3a963ac5321ee9b8f9e31e996f0a648a34d174436a` |

Findings:

- Mode 0x02 is **MiniZ-compressed chunks**, not a whole-entry PRNG XOR.
  `0xa69090` reads a 32-bit length (`len & 0x7fffffff`, `flag = len >> 31`,
  `len <= 0x7ff`) and calls `0xa89f80` into a 0x400-byte buffer. The stored
  path calls `0xa89d70` for PRNG XOR; the compressed path calls `0xa85710`
  MiniZ with no XOR. This explains why 1406 real entries decoded to noise.
- Retraction: the PRNG multiplier in `src/data/codec.cpp`,
  `tools/isaac-archive.py` and §9.2 was documented as
  `0x4c957f2d5851f42d`; immediate order gives `0x5851f42d4c957f2d`.
- Retraction: seed preparation wrongly shifted the `seed >> 9` term left 8;
  native bytes give `(((seed<<15)^seed)<<8) ^ (seed>>9) ^ seed`.
- Do not patch production code on this verification branch. The MiniZ stage
  and the two constant fixes are tracked in issue #110, opened before any
  implementation work, per the repository rules. No game bytes are committed.

### 30.1 Measured outcome after implementing issue #110

The chunk framing was walked on the installed `repentance.a`, and the results
retract two parts of the reading above:

- The header top bit is **not** a stored flag. Across 1155 decoded chunked
  entries it is set on the final chunk and nowhere else (`only_last = 1155`,
  `other = 0`, `noflag = 0`). Treat it as an end-of-entry marker; the declared
  size already bounds the output.
- `len == 0x400` chunks are **copied verbatim, no XOR**. 203 entries that mix
  `0x400` chunks with DEFLATE chunks decode to PNGs whose chunk CRCs and IDAT
  validate, which a PRNG XOR of those bytes would break.
- Only stored records carry the nine-byte header. Chunked entries begin
  directly with their first four-byte chunk header; the earlier claim that
  every mode 0x02 entry has one was wrong.

Measured split of the 4180 mode 0x02 entries in `repentance.a`: 378 stored
(378 PNG, all valid), 1155 chunked decoded (203 PNG, 859 XML, 20 text, 17
RIFF, 56 other; 203/203 PNGs valid) and 2647 unresolved. The unresolved ones assemble
to the declared size but match no known magic, are not printable text, and
resist 15 seed formulas × 3 seed preparations × 2 multipliers × 5 state
initialisations × 4 output variants, plus bogocrypt1. They are an open third
arm, recorded in `docs/arch000-format.md` §9.5, and the decoder fails them
explicitly rather than returning noise.

Implementation: `ArchiveCodec::decode_chunked` (`src/data/codec.cpp`) with zlib
`inflateInit2(-15)`; stored records still copy at `offset + 9`. The retracted
PRNG class, its fixture generator and `tests/prng_fixtures.inc` are deleted.
Chunk tests use real raw-DEFLATE streams from `tests/zip_fixture.hpp`. The
opt-in test `archive_real_container_decodes_the_chunked_mode_0x02_arm` runs
only when `ISAEC_TBOI_ARCHIVE` is set and reported
`4180 entries, 1533 decoded, 581 PNG, 581 PNG valid, 378 stored PNG`.

Consequence for the PRNG section: `0xaa93e0`/`0xa89d70` remain an unexercised
reading. The stored-block XOR condition in the native wrapper has not been
identified in the data, so production does not XOR anything and no claim in
the docs depends on the PRNG.

## 31. Android packaging: an arm64 debug APK exists, on-device run does not

State as of 2026-11-19. This section records what is actually built, because
"Android works" is still false.

**What exists.** `android/` is a minimal Gradle project (AGP 8.5.2, Gradle 8.7,
compileSdk/targetSdk 34, minSdk 24, `abiFilters arm64-v8a`). Its only activity
is `org.caos.isaec.MainActivity`, which extends SDL2's `SDLActivity` and loads
`SDL2` then `main`. `libmain.so` is built from `src/platform/android_main.cpp`,
which calls the same `isaec_window_main` as the desktop binary with
`{"slice","1280","720"}` — the white-box room, not a game asset.

**Dependencies.** Both are fetched at configure time, pinned by tag, and built
from source: SDL2 `release-2.32.10` (Android only) and libpng `v1.6.44`
(static, `CMAKE_POSITION_INDEPENDENT_CODE` forced because its objects end up
in `libmain.so`). The NDK sysroot's `libz` is used directly. Nothing host-side
is linked into the target.

**Evidence.** The `android-apk` job builds `:app:assembleDebug`, unzips the
APK, and fails unless `lib/arm64-v8a/libmain.so` and `lib/arm64-v8a/libSDL2.so`
are present, then uploads `isaec-android-debug`. The artifact from run
44482470989 measured 3,653,915 bytes with `libmain.so` 1,321,744,
`libSDL2.so` 2,256,488, and no non-arm64 ABI directory. It is a debug-signed
APK with three dex files; `classes.dex` alone is tiny, the SDL Java classes
land in a later dex, so a size check on `classes.dex` must not be used as a
"Java did not compile" signal (that mistake was made once).

**What is not shown.** No device or emulator ever ran this APK from the build
host (`adb devices` is empty). There is no tap/touch input mapping, no archive
picker, no storage permission flow, and no read of the player's install on
Android. The APK shows the slice and nothing else. Sideload instructions are
in `README.md`; the first physical run is an owner step and stays open on #66.

**CI lesson worth keeping.** The first `android-apk` run failed fetching
`com.google.errorprone:error_prone_annotations:2.3.1` from Maven; a rerun of
the same commit passed unchanged. That is an upstream-registry flake, not a
build defect — check the failing URL before changing the build.

## 32. Mode 0x01 LZW ported from the natives; #52

Work on `fix/52-lzw-decoder`
(issue [#52](https://github.com/caos-obliquo/tboi-recomp/issues/52)). The four
natives were re-derived from the Steam `isaac-ng.exe` with REA/Ghidra, not from
the external format description:

| native | role | Evidence |
|---|---|---|
| `0xa850f0` | dictionary init | `ev_2ba7fae00f476b26809b438919f98d5f79ea9ab52bb30d9d99b40a3ff9939edd` |
| `0xa85090` | MSB-first byte-accumulator bit reader | `ev_022dd8441b68bb8c05295291c706d5d4c3ce7141b14d471b0290afe8c337b690` |
| `0xa85020` | prefix-chain emit | `ev_7e10cbc6d1e68acef69eb4da7e34bfa2845aa5793a7b77352d107fc3a0414a99` |
| `0xa85320` | per-chunk decode loop | `ev_8f84fe3f86c05c3868f6e941f6039201a6865ca0993510ef59215363fb12152e` |

`src/data/codec.cpp` now mirrors them: one dictionary per entry, width grown
before each read, reset when `next_code+1 >= 0x1000`, early abort on an
out-of-range code, and a walk bounded by the compressed span. `Archive::decode`
passes `cipher_span(index)` and truncates to the recorded `size`; it no longer
refuses mode 0x01.

`tools/gen_lzw_fixtures.py` regenerates `tests/lzw_fixtures.inc` (6 synthetic
cases, no game bytes committed) from `tools/isaac-archive.py`. The multi-chunk
cases exercise dictionary persistence and the truncate-back step. The suite
went from 145 to 148 cases, 0 failed. A negative control that reset the
dictionary per chunk made three LZW tests fail, so the persistence assertions
are not vacuous.

Real-archive check on the same day: `config.a` 24/24 entries decoded at
exactly their recorded size (first entry begins `<achievements gfxroot="resources/gfx/ui/`),
`fonts.a` 10/10 exact (BMF with `PF Tempesta Seven`), `rooms.a` 12/13 exact.
`rooms.a` entry 7 under-produces (318647 of 382561 bytes), which is now an
explicit error in `Archive::decode` instead of a zero-padded payload. The cause
is not identified.

What this does **not** establish: at the time this section was written the
reimplementation had not been run against the game's own decoder as an oracle.
§33 closes that gap; agreement there is still between the disassembly, the
Python port and the C++ port, since the game's decoder is not callable here.

---

## 33. Game-side oracle for LZW: implemented, run, negative-controlled

[#107](https://github.com/caos-obliquo/tboi-recomp/issues/107) tracked the
missing oracle from §30. It landed on `feature/107-lzw-oracle` and PR #108,
deliberately separate from #52's branch and PR #106 so each piece carried its
own evidence.


The probe is a C++ decode of every entry of a user-supplied real `config.a`
through `Archive::decode`, compared byte-for-byte and by SHA256 with
`tools/isaac-archive.py` on the same archive. Acceptance is all 24 entries
matching, plus a negative control that fails when either per-chunk dictionary
reset or size-as-span is reintroduced. No game bytes may be committed; the
archive path is supplied at run time. The issue body cites the REA/Ghidra
Evidence IDs from §30, so the native side of the claim is auditable without
re-running REA.

Built and run 2026-11-19 against the Steam `config.a`: `tools/check_lzw_oracle.py`
reported `24/24 entries match`, exit 0, sizes and SHA256 prefixes equal entry by
entry. The negative control was run for real: moving `LzwState` inside the chunk
loop (the defect #52 removed) makes the oracle exit 1 with
`entry 0: codec produced 1087 bytes for entry 0, expected 25894`. The control was
then reverted and the positive run repeated. No game bytes are in the tree; the
archive path is an argument.

---

## 34. ANM2 documents parse

[#124](https://github.com/caos-obliquo/tboi-recomp/issues/124) opened the
Phase 1 gate named in the roadmap: no ANM2 loader existed. `isaec::data::
parse_anm2` now reads the dialect into typed data (spritesheets, layers, nulls,
events, per-animation root/layer/null timelines, triggers). Schema, tolerances
and limitations live in [anm2-format.md](anm2-format.md); this entry records the
state and the evidence.

Corpus, measured 2026-11-23 by running `./build/tests/isaec_tests` with
`ISAEC_TBOI_ARCHIVE` set per archive:

| Archive | entries | decoded | PNG (valid) | ANM2 parsed | root+layer frames |
|---|---|---|---|---|---|
| `repentance.a` | 4180 | 1533 | 581 (581) | 833 / 833 | 88147 |
| `afterbirthp.a` | 10367 | 3472 | 971 (971) | 2249 / 2249 | 193801 |
| `afterbirth.a` | 2647 | 1102 | 0 | 1022 / 1022 | 63549 |

The repository suite went from 148 to 159 cases, 0 failed. The real-archive test
now scales its decode floor with the entry count instead of asserting
Repentance's totals, because `afterbirth.a` decodes 1102 of 2647 entries with no
PNGs at all.

Two tolerances are data-driven, not guesses: shipped documents contain
`Name="" FrameNum="0"` placeholders and animations referencing layers the
document does not define. The parser keeps both: blank animations are never
resolved by name, and dangling ids are counted in `Anm2::dangling_references`.
Structural damage is still fatal.

What this did not establish at the time: nothing consumed the data. Playback and
the windowed runner landed in §35.

## 35. ANM2 playback and a real document drawn (#128)

[#128](https://github.com/caos-obliquo/tboi-recomp/issues/128) asked for
playback plus one visible path. `isaec::engine::AnimationPlayer` advances root,
layer and null timelines from each key's `Delay` at `Info@Fps`, holds `Loop`
animations at `FrameNum`, reports `finished()` for non-looping ones, composes
root+track poses (additive position/rotation, multiplicative percent scales and
tint) and delivers `AtFrame` triggers once each, in order, drainable, re-armed on
every wrap. The timing model came from REA/Ghidra on the shipped executable:
the cursor moves monotonically across crossed integer frames and layer/null
cursors accumulate that timeline's `Delay`. That is static decompiler evidence,
not a runtime replay.

`DrawCall` gained rotation, pivot and RGB tint; `SdlRenderer` applies them with
`SDL_RenderCopyExF` and `SDL_SetTextureColorMod`. `isaec_window anm2` opens the
document archive, parses one entry, decodes one PNG (optionally from a second
archive), and draws each layer's cropped pose at the window centre, 4x.

Evidence, run 2026-11-24 under `xvfb-run -a -s "-screen 0 1280x720x24"` with a
fixed 90-frame budget:

```
isaec_window anm2 repentance.a 20 7205 - 90 1280 720 afterbirthp.a
document   entry 20 fps 30 layers 1 nulls 0 animations 1   (Idle, FrameNum 8, loop)
sheet      afterbirthp.a entry 7205, resources/gfx/eraser_tears.png
frames 90   draws 90   presented 90   animation frame 4   triggers 0
```

`repentance.a` resolves 0 of 4180 names, and none of its documents has a
spritesheet that decodes inside the same container, so the sheet must come from
another archive for this install; the lookup is basename-to-`Spritesheet@Path`
only. The run first spent 61.8 s wall and 56.6 s user CPU before its first
frame, which was a container read defect rather than playback: `Archive::open`
copied per character and took 37 s on the 633 MB archive. Fixed under
[#130](https://github.com/caos-obliquo/tboi-recomp/issues/130); see §36.

Not established: interpolation (keys marked `Interpolated` are held), the exact
seconds-to-frames conversion against the shipped executable at runtime, and any
framebuffer compositing beyond submission order. The suite went from 159 to 169
cases, 0 failed, including a negative control that shows a one-tick key without
`Delay` accumulation reaches the next key when the real timeline does not.

## 36. Archive open: one sized read (#130)

`Archive::open` copied the whole container through `std::istreambuf_iterator`,
one character per iterator increment. On this host that measured 37,316 ms for
`afterbirthp.a` (633,600,463 bytes), which made every large-archive CLI run look
like a hang before the first frame was drawn.

The reader now seeks to the end, sizes the blob once, and issues a single
`read()`. Measured with `ISAEC_TBOI_ARCHIVE=<archive> ./build/tests/isaec_tests`:

| container | bytes | open |
| --- | --- | --- |
| `repentance.a` | 385,003,320 | 155.8 ms |
| `afterbirthp.a` | 633,600,463 | 293.4 ms |

Decode results are unchanged: the same corpus test reports 1533 decoded entries
and 581 valid PNGs for `repentance.a`, 3472 and 971 for `afterbirthp.a`, and
378/598 stored PNGs. The #128 runner now completes its 90-frame budget in
5.2 s wall instead of 61.8 s.

The new opt-in test `archive_real_container_opens_with_one_sized_read` prints
the measured open time and fails above 2000 ms for containers of 64 MiB or
more, so a regression to per-character copying is caught where the corpus is
available. Suite: 170 cases, 0 failed.
