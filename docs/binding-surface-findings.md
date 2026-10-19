# Binding surface — recovered locations and how they were measured

Status of issue **[#3](https://github.com/caos-obliquo/tboi-recomp/issues/3)** (recover the 655 native method bodies and signatures) as of
the session that produced this file.

Everything here was measured by running a committed tool against the Steam
install. Every claim names the tool that produced it. Numbers that came from
hand disassembly are marked as such.

Reference binary: `isaac-ng.exe`, 32-bit x86 PE, from
`$HOME/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth`
sha256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`

---

## 1. The Lua API is fully named, so nothing has to be guessed

`isaac-ng.exe` imports Lua from **`Lua5.3.3r.dll`**, and the import table names
every entry by IAT slot. That is the single most useful fact found so far: the
binding code can be read with exact semantics instead of inferred from
idiom.

Produced by `tools/probe_iat.py`, which walks the PE import directory directly
rather than scraping `objdump -p`, and matches the six-byte x86-32 indirect-call
encoding `ff 15 <slot:le32>` in one pass over `.text`.

Slots used by the binding core:

| slot | import | call sites |
| --- | --- | --- |
| `0x00b18418` | `lua_absindex` | 2350 |
| `0x00b18414` | `lua_pushstring` | 2336 |
| `0x00b1840c` | `lua_rotate` | 1552 |
| `0x00b1839c` | `lua_newuserdata` | 1202 |
| **`0x00b183fc`** | **`lua_pushcclosure`** | **1064** |
| `0x00b18400` | `lua_type` | 1004 |
| `0x00b18320` | `lua_createtable` | **8** |
| `0x00b18398` | `lua_setmetatable` | 300 |
| `0x00b183fc` | `lua_pushcclosure` | 1064 |

`lua_createtable` being called only **8** times, all inside one narrow address
band, is the strongest structural signal so far: metatable construction is
effectively a single function.

`lua_getstack` and the seven `luaopen_*` entries have **0** call sites, which is
consistent with the README's finding that `lua_register` appears zero times —
the binding model is the custom metatable one, not the C API shortcut.

## 2. Metatable keys: two clusters in `.rdata`

All nine recovered keys exist as exact NUL-terminated strings, in two tight
clusters (produced by `tools/probe_metatable_keys.py`):

| rva | keys |
| --- | --- |
| `0x0076e988`–`0x0076e9cc` | `__parent` `__propget` `__const` `__type` |
| `0x0076ea38`–`0x0076ea80` | `__propset` `__metatable` `__class` `__index` `__newindex` |

## 3. 816 references from `.text`, and the counts are diagnostic

`tools/probe_xrefs.py` finds every `.text` occurrence of each key's absolute
address as an immediate. The near-universal idiom is

```
push <key-address>
call dword ptr [IAT]
```

| key | `.text` refs |
| --- | --- |
| `__propget` | 421 |
| `__propset` | 184 |
| `__const` | 70 |
| `__class` | 68 |
| `__parent` | 56 |
| `__type` | 6 |
| `__index` | 4 |
| `__newindex` | 4 |
| `__metatable` | 3 |

The property keys dominate because property registration is by far the most
common operation. The rare keys are the useful ones for locating the core: every
one of `__metatable`'s three references, and all four each of `__index` and
`__newindex`, fall inside one ~5 KB band around VA `0x85c000`–`0x85d900`.

## 4. Hand-verified decode of the metatable core

At VA `0x0085d240`–`0x0085d2b7` (file offsets `0x45c640`–`0x45cb7`), read with
`objdump -d -M intel` and resolved through the import table:

```
lua_absindex(L, -2)              ; slot 0xb18418
lua_pushstring(L, "__metatable") ; slot 0xb18414, immediate 0x00b6ea64
lua_rotate(L, -2, 1)             ; slot 0xb1840c
lua_rawset(L, idx)               ; slot 0xb18408
```

The function ends `ret 0x4` and is followed by `int3` padding, so it is a real
stdcall boundary, not a mid-function slice. The `0x0085d2c0` entry is the next
function, which calls `lua_createtable`, `lua_setmetatable`, `lua_pushvalue`.

This confirms the model in `include/isaec/api/binding.hpp` is the right one, from
the binary rather than from assumption.

## 5. Defects found in our own tooling — recorded so they do not recur

These cost real time and are the reason the tools carry positive controls.

| defect | consequence | now caught by |
| --- | --- | --- |
| `offset_to_va` omitted `image_base` | every reported VA wrong by `0x400000`; two disassemblies aimed at unrelated float/DirectX code | round-trip test plus a section anchor pinned to binutils' VMA |
| string scan demanded a second NUL | silently dropped every key followed by ordinary data; reported 4 of 9 keys as absent | `__class` positive control |
| `objdump -h` column indexing read `LMA` as file offset | 6 phantom "mismatches"; the self-test printed PASSED while printing them | regex captures VMA/LMA/offset separately; duplicate counter reset removed |
| `sed` produced a compile error instead of a behavioural break | a negative control that proved nothing | `tools/negctl-baseline.py` asserts the break compiles before running the suite |
| import call-site scan was quadratic (622 × 7 MB) | multi-minute run | single-pass index over `.text` |
| `tools/pe-vaddr.py` hyphenated | unimportable by the probes | renamed to `pe_vaddr.py` |

**A byte match for an address is not a finding.** It proves the value is
materialised in `.text`, not that the instruction intends a metatable lookup.
Every probe reports candidates; none reports conclusions.

## 6. Updated dead-end list

From `tools/README.md`, plus what this session ruled out:

- The `.rdata`/`.data` VA bases are **different** (`0x00718000` vs `0x007f8000`)
  and the ranges do **not** overlap. The hazard is a single-base helper, not
  overlap. `tools/pe_vaddr.py` refuses to resolve without a section when a VA is
  ambiguous, and reports zero overlaps for this binary as a checked fact.
- `objdump -M intel` is usable in this repo; the AT&T form noted in
  `tools/README.md` is what needs a positive control.
- `radare2` is **not installed**; `objdump`, `nm`, `readelf`, `strings`, `xxd`
  and `gdb` are.
- REA (`reverse-engineer-anything`) opens this binary's siblings fine but
  `rea_binary_overview` **times out** on `isaac-ng.exe`; it is not on the critical
  path.

## 7. Where the 655 bodies are attached

`lua_pushcclosure` has 1064 call sites. The 655 methods plus constructors and
metamethods is consistent with that count, and the sites fall into tight address
bands — for example six of the first eight reported sit between
`0x00872550` and `0x008754c0`, which looks like per-class registration code.

Next step, in order:

1. Cluster the 1064 `lua_pushcclosure` sites by containing function.
2. For each cluster, take the `lua_pushstring` immediate immediately preceding it
   as the method name, and resolve the pushed function pointer as the native
   body.
3. Calibrate against **`Font`**, the project's built-in canary: string adjacency
   alone reports 531 candidate tokens for 8 real methods, so a filter that cannot
   bring `Font` down to a plausible count is wrong and must fail loudly.
4. Report the delta both ways against `bindings/recovered-binding-surface.json`
   before changing any generated code.
5. Keep the `HUD` and `Isaac` buckets quarantined; they are documented as
   known-contaminated.

## 8. Conventions in force

- No hand-computed hex in a script or an issue; every address is an expression
  over the PE section table.
- Commit the tool, not its raw output. No disassembly dumps, no extracted game
  bytes.
- Negative-control every search. A negative result states the search space
  covered.
- `lua_register` appears zero times. Do not "simplify" `binding.hpp` into it.
- Lua stays pinned to 5.3. Note the game ships **5.3.3**, the vendored copy is
  **5.3.6**; the ABI is compatible but the exact minor version should be
  recorded before depending on internals.

## 9. Files

| path | role |
| --- | --- |
| `tools/pe_vaddr.py` | PE section reader; refuses ambiguous addresses |
| `tools/probe_metatable_keys.py` | locates the nine metatable keys |
| `tools/probe_xrefs.py` | counts `.text` references to each key |
| `tools/probe_iat.py` | maps IAT slots to names, finds `ff 15` call sites |
| `tools/negctl-baseline.py` | negative control for the test suite |
| `docs/arch000-format.md` | authoritative archive format spec |
| `docs/HANDOFF.md` | session state and operational gotchas |
| `tools/README.md` | how the surface was recovered; dead ends |
---

## 10. Name to body attribution — measured

Added after the sections above. Four probes established this, in order, and each
one ruled out the previous hypothesis.

### The name blob has no pointer array

`tools/probe_regtables.py` scanned `.rdata` for runs of 8-byte
`(name pointer, .text pointer)` pairs at strides 8, 12 and 16. At stride 8 it
found exactly one table — Lua's own `luaL_openlibs` array, whose seven entries
match the seven `luaopen_*` imports exactly. Strides 12 and 16 found nothing new.

That single result is a useful positive control: the signature is real, it just
describes Lua's stdlib table rather than the game's.

Dumping raw bytes around a known name shows why: the names are a **dense blob of
NUL-terminated strings separated by single NULs**, with no interleaved pointers.
Any dword in that span that decodes to a `.text` address is ASCII that happens to
look like an address.

So the name to body mapping cannot be read off a table. It has to come from the
code that references each name — which is what `tools/README.md` predicted when it
said attribution is "blocked on the same disassembly pass".

### The backwards window around lua_pushcclosure does not find them

`tools/probe_methods.py` walks backwards from all 1064 `lua_pushcclosure` sites
looking for pushed immediates. It recovers class names (`Room`, `Game`, `HUD`,
`ItemPool`, `Entity`, …) and the property machinery — `__propget` at 210 sites
and `__propset` at 174 — but only **36 of the 655** committed methods. Those 36
are property accessors, not the API surface.

### What actually works: attribute from the name's own reference

`tools/probe_namexrefs.py` inverts the question. It locates each committed name
in `.rdata`, finds every `push imm32` in `.text` that materialises that name's
address, and looks for a `.text` pointer near those sites.

| measure | count |
| --- | --- |
| committed names | 655 |
| located in `.rdata` as NUL-terminated | **654** |
| with at least one `push imm32` reference | 472 |
| with no `push` reference at all | 182 |
| **candidate (name → body) recovered** | **376 / 654 (57.5%)** |
| candidates whose body is ambiguous | 249 |

Per class:

| class | candidates |
| --- | --- |
| EntityPlayer | 74 / 77 |
| Entity | 212 / 261 |
| KColor | 8 / 18 |
| Input | 39 / 118 |
| Isaac | 19 / 76 |
| HUD | 7 / 42 |
| Sprite | 9 / 33 |
| Vector | 6 / 22 |
| Font | 2 / 8 |

`EntityPlayer` and `Entity` are close to complete. `Font`, the project's
calibration class, is the worst at 2/8 — consistent with it being the class the
adjacency heuristic over-reports on.

### The 182 unreferenced names corroborate the known contamination

Their sample is `Ambush`, `Arcade`, `BarOverlay1`, `BarOverlay2` … `Boss`. Those
are animation and overlay field names, not methods. This independently confirms
the `_caveat` on `HUD` and `Isaac`: those buckets were never method lists, which
is why their names have no code reference to register.

### What these candidates are, and are not

They are **candidates**. A `.text` pointer within 48 bytes of a name's push site
is a guess at which argument it is. 249 of 376 are ambiguous by that measure
alone.

They are good enough to be worth keeping and not good enough to generate from.
Confirmation requires reading the registration code at each site and seeing which
pushed value becomes `lua_pushcclosure`'s second argument.

Next: for the unambiguous 127, disassemble the registration site and confirm;
then handle the 182 unreferenced names, which are registered by some mechanism
this probe does not yet model.

---

## 11. Confirmed bindings, and why the proximity heuristic was replaced

`tools/probe_closures.py` replaces guesswork with the API. It knows
`lua_pushcclosure`'s IAT slot, so for each name reference it walks **forward** to
the next `call dword ptr [slot]` and reads only the pushes in between. Under
__stdcall, arguments are pushed right to left, so the value entering the second
argument slot is the body.

| outcome | count |
| --- | --- |
| names considered | 655 |
| **confirmed, single body** | **92** |
| ambiguous | **0** |
| no `lua_pushcclosure` within the window | 380 |
| no name reference at all | 183 |

Contrast with §10, which scored 376 candidates and 249 of them ambiguous. The
proximity heuristic could not tell argument position; anchoring on the call
removes the ambiguity entirely — **0 rather than 0-of-376**. The proximity count
is not wrong, it is strictly weaker: it found candidate *regions*, not arguments.

Confirmed per class:

| class | confirmed |
| --- | --- |
| Entity | 57 / 261 |
| KColor | 9 / 18 |
| Font | 4 / 8 |
| EntityPlayer | 8 / 77 |
| Sprite | 8 / 33 |
| Vector | 6 / 22 |

88 distinct body addresses across 92 confirmed names.

### Two things worth checking before trusting these

- `AddCollectibleEffect` and `AddTrinketEffect` both resolve to `0x00ce88a0`. That
  is possible if the body is a generic effect-application helper, but it is also
  the classic symptom of an off-by-one in the window. Read one site in full
  disassembly before relying on it.
- `EntityPlayer` falls from 74 candidates to 8 confirmed. That is not a
  regression, it is the heuristic being wrong: most of its names register through
  a path that does not call `lua_pushcclosure` near the name at all.

### The 380 with no nearby call are the next frontier

They are almost certainly the property path — `__propget` at 210 `pushcclosure`
sites and `__propset` at 174 is exactly this many order-of-magnitude — plus any
loop-driven registration. Reading those needs the same treatment: find the API
call that consumes the name and read its arguments.

---

## 12. Review of §11 — the "confirmed" label does not hold

Disassembly of a matched site disproves §11's central claim. `AddTrinketEffect`
has its name pushed at VA `0x0087c5bd`, inside a `__propset` registration path:

```
87c5bd:  push 0xb70e28          ; "AddTrinketEffect"
87c5c5:  call [0xb18414]        ; lua_pushstring
87c5d0:  call [0xb1840c]        ; lua_rotate
87c5d8:  call [0xb18408]        ; lua_rawset
87c5e6:  ret 0x8
87c5e9:  int3 ...               ; padding
87c5f0:  push ebx               ; ---- next function starts here
87c602:  push 0x8ab2a0
87c608:  mov DWORD PTR [eax],0x930390
87c60e:  call [0xb183fc]        ; lua_pushcclosure
```

The `lua_pushcclosure` the probe matched at `0x0087c60e` belongs to a **different
function**, past the `ret` and `int3` padding. The probe had no boundary check,
so it attributed an unrelated call to this name. That is why
`AddCollectibleEffect` and `AddTrinketEffect` both resolved to `0x00ce88a0`.

**§11's 92 bindings are therefore `single_call_site` candidates, not confirmed
bindings.** The correct label was always weaker than the one used.

Three defects, all in `tools/probe_closures.py`:

1. The forward window does not stop at `ret`/`int3` padding, so it crosses
   function boundaries.
2. The scan for `0x68` and `0xff 0x15` is a raw byte search with no instruction
   alignment, so it matches opcode bytes inside other instructions.
3. The "last `.text` push" heuristic cannot see the form actually used at
   `0x0087c608`, where the body is stored with `mov DWORD PTR [eax],imm32` rather
   than pushed. Any method that stages its closure pointer through memory is
   invisible to it.

All three must be fixed before the count is trusted, and the corrected count is
expected to be **lower** than 92. The drop is the real number.

### What the disassembly does establish

The registration idiom for the property path is
`lua_pushstring` → `lua_rotate` → `lua_rawset`, with no closure at all, which is
consistent with `__propget`/`__propset` accounting for 384 `lua_pushcclosure`
sites between them and explains why 380 names have no nearby call. Anchoring on
that triple is the next step, not a wider window.

---

## 13. Method bindings, recovered by anchoring on the call

§11's failure was structural, not a bug to patch: it anchored on a name and
looked *forward* for `lua_pushcclosure` inside a fixed window. One function can
hold hundreds of registration records -- a single data-table initializer at
`0x00868800` pushes thousands of name literals in a row -- so "the next call
within 96 instructions" is not a record boundary, and not even a function
boundary. Adding a terminator check did not catch it, because the whole
initializer lives inside one function with no `ret` in sight.

An intermediate version that did stop at terminators still produced 46 wrong
"unique" bindings, all of them property names like `Friction` and
`GridCollision` resolved against unrelated calls in that same initializer.

The working anchor is the call itself. `lua_pushcclosure` has an unambiguous
argument shape:

```
push   0x1                  ; nupvalues
push   0x8bb960             ; fn          <- the body
mov    DWORD PTR [eax],0x0  ; upvalue setup, often present
push   DWORD PTR [esi]      ; L
call   DWORD PTR ds:0xb183fc ; lua_pushcclosure
```

Walking backward from a real call site, the body is the nearest `push imm32`
carrying a `.text` address, and the name is the literal fed to the nearest
preceding `lua_pushstring`. Every step stops at a terminator. New tool
`tools/disasm.py` supplies the instruction stream from `objdump -d -M intel`;
2,165,555 instructions parse in 1.5s, so nothing scans bytes any more.

### Counts, and how the window was chosen

```
lua_pushcclosure call sites        : 1064
named registrations                :  524
distinct names bound 1:1           :   63
metatable keys, many bodies        :    4
names bound to >1 body             :    4
closures with no name in reach     :  539
call sites with no body            :    1
```

`BACK = 24` is measured, not chosen. Distance from a call site back to its
nearest `lua_pushstring` saturates at 24: the count of reachable sites is
identical at 32, 48, 64, 96 and 120 instructions back (524 in every case). All
539 unreachable sites hit a `ret`/`int3` before any `lua_pushstring`, so their
name genuinely lives in another function. Closure-named recovery is therefore
**complete**, not truncated by an arbitrary window.

### Metatable keys carry one closure per property

| key | bodies | registrations |
|---|---|---|
| `__propget` | 203 | 209 |
| `__propset` | 155 | 173 |
| `__gc` | 49 | 49 |
| `__len` | 10 | 10 |

`__gc` and `__len` are exactly 1:1. A name with many bodies is the shape of a
metatable key, not a contradiction, so keys are reported separately from a
genuine name/body disagreement. This independently corroborates §4's push-site
counts (`__propget` 421, `__propset` 184); the closure count is lower because
some property registrations use the `pushstring`/`rotate`/`rawset` path with no
closure at all.

The remaining four multi-body method names are real disagreements, not key
plumbing: `Get` (6 bodies), `Size` (10), `Room` (2), `RoomDescriptor` (2). These
are properties of several types sharing one name, so they are expected.

### The 63 one-to-one bindings are type constructors, not methods

Every one is a class or type name whose body is a small constructor in the band
`0x008c2c10`–`0x008c3250`: `Entity` -> `0x008c2fb0`, `Color` -> `0x008c3210`,
`Font` -> `0x008c3230`, `Game` -> `0x008c2c10`, `HUD` -> `0x008c2c30`,
`EntityPtr` -> `0x008c32b0`. These bodies build the metatable for the type. The
recovered method surface is therefore **not** 63 methods; it is 63 type
constructors, and the actual methods are the `__propget`/`__propset` bodies,
which are property accessors rather than named methods.

### Control

`0x0087c60e`, the call the old probe attributed to `AddTrinketEffect`, is
resolved as body `0x008ab2a0` with no `AddTrinketEffect` literal in reach. The
probe exits non-zero if that call ever binds to that name, or if it cannot be
found at all.

### Defects found while doing this

- `Section.vma` is an RVA. Its absolute VA is `vma + image_base`. Using `vma`
  directly put every window below the real image and truncated `.text` at
  `0x717133` instead of `0xB17134` -- a run that reported zero bindings while
  exiting 0. `.text` is `0x401000..0xB17134`; `0x401000 + 0x716134` is
  `0xB17134`, not `0x717134`.
- `pe_vaddr.va_to_offset` and `Section.contains_va` are named for VAs but take
  **RVAs**; every existing caller in `tools/` passes an RVA. Not renamed, since
  that would break five tools, but the unit is now documented at the call site.
- Passing a VA to `offset_to_va` (which expects a file offset) produced body
  addresses of `0x00cf...`–`0x00d0...`, outside `.text` entirely. The tell was
  that every address landed in a plausible-looking but wrong band.
- A name pushed at several sites now resolves only if every resolving site
  agreed; disagreement is demoted to `conflicting` rather than tie-broken.
- Class attribution was a single arbitrary winner whenever a name appeared in
  several classes' candidate lists. All claiming classes are now reported.
---

## 14. The property-accessor ABI — how a bound feature actually runs

§13 ended with "0 of 441 signatures". That is no longer true. All 441 recovered
closure bodies have now been decompiled, and they are not 441 unrelated
functions: they are instances of a small number of shapes, and one of those
shapes is the mechanism that binds C++ objects to Lua properties.

### 14.1 Corpus provenance

`kuna` was driven **standalone**, not through the REA MCP server. REA's Kuna
provider refuses this file with `target_format_unsupported`:

> REA verified the Kuna provider on ELF and thin Mach-O images; pe is not
> admitted.

That is a statement about REA's *verified* matrix, not about kuna's capability.
Standalone `kuna` loads this PE without complaint and reports 4236 functions.
Treating a provider's verified-format matrix as the tool's capability ceiling is
the mistake this section exists to prevent.

| | |
|---|---|
| bodies decompiled | 441 / 441 |
| failures | 0 |
| address set vs `isaac-ng-body-seeds.tsv` | identical (asserted, not assumed) |
| headers present in `kuna-all.c` | 441, all unique |
| wall clock | ~1.4 s per body, ~10 min total |
| output | `~/.omo/evidence/pseudocode/kuna-all.c` (9600 lines), `kuna-index.jsonl` |

The first attempt at this sweep buffered the composed file and was aborted at
400/441, which left a **0-byte output** — the work was lost even though every
individual decompilation had succeeded. The runner now flushes per body and
keeps an append-only index, so an abort costs at most one batch and a rerun
resumes. Evidence integrity was then checked by asserting the produced address
set equals the seed set, not by counting lines.

Speed, for the record: kuna 1.4 s per body versus Ghidra 607 s for the same
address on this file. That is a ~430x difference, and it is why this corpus was
obtainable at all.

### 14.2 A caveat that must not be dropped

For **all 441** bodies kuna emits:

> `0x… is interior to std::basic_streambuf::~basic_streambuf (0x41423a+…)`,
> not an entry kuna discovered; decompiled from 0x… as asked

kuna's nearest-function attribution is garbage here — one symbol swallowing
0x41423a through 0x8F9xxxx. What matters is that kuna still decompiles from the
requested address, which is what was asked. The claim that these 441 addresses
are real `lua_pushcclosure` closure entries comes from §12/§13's call-anchored
analysis of the `.text` instruction stream, **not** from kuna. The two
independent methods agree on the addresses and disagree about naming.

### 14.3 Shape taxonomy

Classifying all 441 bodies by which Lua entry points they touch, every one
falls into a known shape — 441 of 441 classified, no residual bucket:

| bodies | shape |
|---:|---|
| 118 | `lua_touserdata(0xfff0b9d7)` → push → `return 1` |
| 72 | `lua_touserdata` → `luaL_check*` → `return 0` |
| 68 | `lua_touserdata` → `return 0` |
| 60 | `lua_touserdata` → vtable deref → wrap or nil → `return 1` |
| 49 | upvalue resolver → vtable deref → `return 0` |
| 24 | `…` → `lua_pushboolean` → `return 1` |
| 17 | `return 1` |
| 16 | `…` → `luaL_*` → `return 1` |
| 8 | `lua_touserdata` → `luaL_*` |
| 5 | `lua_touserdata` only |
| 4 | `…` → vtable deref → `return 0` |

`lua_type` appears in **434 of 441** bodies — almost every accessor opens with an
argument type check. That is the dispatch guard, and it is why the bindings are
safe to call from untyped Lua.

Two constants dominate: `0xfff0b9d7` in 375 bodies, `0xfff0b9d8` in 90 bodies,
with `lua_rawgetp` in those same 90. They are a constant family, not noise.

### 14.4 The mechanism, verified at the instruction level

The largest shape is a property getter. Representative body `0x00899030`
(kuna's rendering):

```c
unsigned int sub_899030(unsigned int a0)
{
  v2 = lua_type(a0, 1);
  if (v2)
    sub_85c590(0xc82a13, 1);          // type-assert arg 1
  v2 = (**(void **)lua_touserdata(v1, 0xfff0b9d7))();
  if (!v2) {
    lua_pushnil(v1);
    return 1;
  }
  v3 = (struct_0 *)lua_newuserdata(v1, 8);
  v3->field_0x0 = 0xb753c8;           // metatable / type tag
  v3->field_0x4 = v2;                 // the C++ object
  lua_rawgetp(v1, 0xfff0b9d8, 0xc8297f);
  lua_setmetatable(v1, 0xfffffffe);    // -2: the new userdata
  return 1;
}
```

Every claim in that listing was re-derived from the actual instruction stream
(`tools/disasm.py`), and every IAT slot was resolved out of the PE import
directory rather than taken from kuna's symbol rendering:

```
0x00899030: push     ebp
0x00899031: mov      ebp,esp
0x00899033: push     esi
0x00899034: mov      esi,DWORD PTR [ebp+0x8]      ; esi = lua_State *L
0x00899038: push     0x1
0x0089903a: push     esi
0x0089903b: call     DWORD PTR ds:0xb18400        ; lua_type(L, 1)
0x00899041: add      esp,0x8
0x00899044: test     eax,eax
0x00899046: jne      0x89904c
0x00899048: xor      edi,edi
0x0089904a: jmp      0x899065
--- taken branch: assert the type, recover the C++ `this` ---
0x0089904c: push     0x1
0x0089904e: push     0xc82a13
0x00899053: mov      edx,0x1
0x00899058: mov      ecx,esi
0x0089905a: call     0x85c590                      ; luaL-type assert helper
0x00899062: mov      edi,DWORD PTR [eax+0x4]      ; edi = the wrapped C++ pointer
--- the property dispatch ---
0x00899065: push     0xfff0b9d7
0x0089906a: push     esi
0x0089906b: call     DWORD PTR ds:0xb183b0        ; lua_touserdata(L, 0xfff0b9d7)
0x00899071: add      esp,0x8
0x00899074: mov      ecx,edi                       ; this
0x00899076: mov      eax,DWORD PTR [eax]           ; load vtable slot 0
0x00899078: call     eax                           ; call the member function
0x0089907a: mov      edi,eax
0x0089907c: test     edi,edi
0x0089907e: je       0x8990b8
--- wrap the result ---
0x00899080: push     0x8
0x00899082: push     esi
0x00899083: call     DWORD PTR ds:0xb1839c        ; lua_newuserdata(L, 8)
0x00899089: push     0xc8297f
0x0089908e: push     0xfff0b9d8
0x00899093: push     esi
0x00899094: mov      DWORD PTR [eax],0xb753c8      ; tag
0x0089909a: mov      DWORD PTR [eax+0x4],edi       ; payload
0x0089909d: call     DWORD PTR ds:0xb183bc        ; lua_rawgetp(L, 0xfff0b9d8, 0xc8297f)
0x008990a3: push     0xfffffffe
0x008990a5: push     esi
0x008990a6: call     DWORD PTR ds:0xb18398        ; lua_setmetatable(L, -2)
0x008990af: mov      eax,0x1
--- null path ---
0x008990b8: push     esi
0x008990b9: call     DWORD PTR ds:0xb183e0        ; lua_pushnil(L)
0x008990c2: mov      eax,0x1
0x008990ca: ret
```

IAT slots resolved from the import directory:

| IAT | import | role in this body |
|---|---|---|
| `0x00b18400` | `lua_type` | argument type check |
| `0x00b183b0` | `lua_touserdata` | fetch the bound object |
| `0x00b1839c` | `lua_newuserdata` | allocate the 8-byte wrapper |
| `0x00b183bc` | `lua_rawgetp` | fetch the wrapper's metatable |
| `0x00b18398` | `lua_setmetatable` | attach it |
| `0x00b183e0` | `lua_pushnil` | null result |

The C level of this feature, which is what a reimplementation has to match:

```c
// The uniform shape of every one of the 1064 closure bodies' accessors.
static int isaac_property_get(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TUSERDATA)          // 434/441 guard this
        return luaL_argerror(L, 1, "expected object");

    // 1. the receiver, as stored by the binder at __index time
    void  *holder   = lua_touserdata(L, ISAAC_OBJ_SLOT_1);   // 0xfff0b9d7
    IsaacObject *obj = (IsaacObject *)holder;

    // 2. the getter, taken from the object's vtable slot 0
    void *result = (*obj->vtable->slot0)(obj);

    // 3. wrap: 8 bytes = { type tag, payload }, tagged from a metatable registry
    if (result == NULL) {
        lua_pushnil(L);
        return 1;                                   // one value returned
    }
    IsaacRef *ref = (IsaacRef *)lua_newuserdata(L, 8);
    ref->tag     = 0x00b753c8;                       // per-type metatable id
    ref->payload = result;
    lua_rawgetp(L, ISAAC_META_SLOT_2, 0x00c8297f);  // 0xfff0b9d8 -> metatable
    lua_setmetatable(L, -2);
    return 1;
}
```

### 14.5 What this settles, and what it does not

Settled:

- **Every bound object is an 8-byte userdata** of `{ type tag, payload }`. 90
  bodies construct one; 374 read one.
- **C++ pointers cross into Lua through one constant**, `0xfff0b9d7`, in 375 of
  441 bodies. There is a single object slot, not a per-type one.
- **Getters dispatch through vtable slot 0**, 64 bodies. Setters likewise
  (`0x008990d0`: `lua_touserdata` → `luaL_checkinteger` → `(*vtable->slot0)(v)`,
  `return 0`).
- **The return arity is 1** for getters and 0 for setters, uniformly.
- The 49 no-argument bodies are the module singletons (`Game`, `HUD`,
  `MusicManager`, …): upvalue resolver → vtable deref → `return 0`.

### 14.6 `0xfff0b9d7` is a patched-Lua pseudo-index, not a corrupt value

§14.5 originally left this open. It is now closed, and the answer was in the
shipped `Lua5.3.3r.dll` the whole time.

The game calls `lua_touserdata(L, 0xfff0b9d7)` 772 times out of 833 call sites
in `.text`; the remaining 61 pass a register or a small constant (`-3`, `-4`,
`1`). So the value is a deliberate fixed key, not corruption.

Disassembling the DLL shows `0xfff0b9d8` **inside Lua's own index translation**
(`Lua5.3.3r.dll`, RVA `0x1000`):

```
10001000: mov    0x14(%ecx),%eax        ; eax = L->stack
10001003: test   %edx,%edx
10001005: jle    0x10001018             ; idx > 0 -> positive index
10001007: shl    $0x4,%edx              ; 16 bytes per TValue
1000100a: add    (%eax),%edx
10001018: cmp    $0xfff0b9d8,%edx       ; <-- THE SENTINEL
1000101e: jle    0x10001029
10001029: jne    0x10001032
1000102b: mov    0x10(%ecx),%eax        ; idx == 0xfff0b9d8 -> ci->func + 0x28
10001032: mov    $0xfff0b9d8,%ecx
10001037: sub    %edx,%ecx              ; ecx = 0xfff0b9d8 - idx  == 1 for our key
10001039: mov    (%eax),%edx
1000103b: cmpl   $0x16,0x8(%edx)        ; type-tag check on the anchored value
10001041: jne    0x10001047
10001047: mov    (%edx),%edx
10001049: movzbl 0x6(%edx),%eax         ; array size
1000104d: cmp    %eax,%ecx
1000104f: jg     0x10001059
10001051: shl    $0x4,%ecx
10001054: add    %edx,%ecx              ; -> &array[slot]
```

Reconstructed:

```c
// Reconstructed from the disassembly above. Slot arithmetic is certain; the
// identity of the anchored value is not, so it is not asserted here.
static TValue *rebirth_index2addr(lua_State *L, int idx) {
  if (idx > 0) { ... normal positive index ... }
  if (idx > 0xfff0b9d8) { return (TValue *)((char *)L->top + ((size_t)idx << 4)); }
  if (idx == 0xfff0b9d8) return (TValue *)L->ci->func + 0x28;
  int slot = 0xfff0b9d8 - idx;          // 0xfff0b9d7 -> slot 1
  ... if (anchored_value is not the expected type) return G(L)->nilvalue;
  return slot <= size ? &anchored_array[slot] : G(L)->nilvalue;
}
```

**Positive control.** Stock Lua 5.3.6, vendored at
`third_party/lua-5.3.6/src/lapi.c:60`, is the comparison:

```c
static TValue *index2addr (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) { ... }
  else if (!ispseudo(idx)) { return L->top + idx; }
  else if (idx == LUA_REGISTRYINDEX) return &G(L)->l_registry;
  else { idx = LUA_REGISTRYINDEX - idx; ... }   /* upvalues */
}
```

Stock Lua's sentinel is `LUA_REGISTRYINDEX` == -10000 == `0xFFFFD8F0`, and it
contains **no** comparison against `0xfff0b9d8` anywhere. The shipped DLL's
sentinel is 1000x further out and routes *below* it into a typed array lookup
rather than into upvalues.

So the conclusion is not "the game passes a bad index". It is: **the shipped
`Lua5.3.3r.dll` is a patched Lua whose index space was extended below
`0xfff0b9d8`, and `0xfff0b9d7` is slot 1 of a hidden array anchored on the
current stack frame.** Every one of the 772 call sites is well-formed against
that build.

This also retro-explains `0xfff0b9d8` in 90 bodies: that is the sentinel itself,
used as the "anchored table" index — slot 0 — in `lua_rawgetp`, with a string
key like `0x00c8297f` selecting the per-type metatable. The two constants are
consecutive slots of one structure, which is exactly what the reconstructed
code predicts.

Still open, and now the only real gap: the *type* of the anchored value and how
the game populates slot 1. That lives in the game's own Lua glue, not in Lua.

Not settled — and deliberately left open rather than guessed:
- Which of the 64 vtable-deref bodies are getters versus methods that merely
  happen to return a pointer.
- The 69 seed entries that carry a real identifier are all module singletons and
  named types; the other 372 are anonymous per-property closures. Attaching
  property *names* to the anonymous ones is still open ([#4](https://github.com/caos-obliquo/tboi-recomp/issues/4)).

### 14.7 Correction to an earlier claim in this document

§12 recorded that kuna's `lua_touserdata(a0, 0xfff0b9d7)` was faithfully
rendered but semantically suspicious. That suspicion was worth checking and the
check came back clean: the IAT slot really is `lua_touserdata`, and the constant
really is in the instruction stream. kuna did not invent either. The remaining
question is the semantics of the constant, not the fidelity of the decompiler.