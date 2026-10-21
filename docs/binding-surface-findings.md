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

> **Retracted — see [§15.9](#15-property-names-are-recovered--and-the-earlier-names-were-off-by-one).
> The class names in this subsection are class-name *lookup keys*, not closure
> names: `probe_closures.py` reached them by walking backwards from the
> `lua_pushcclosure` call and landed on the previous registration's literal.
> 45 of the bodies labelled with a class name are in fact that class's `__gc`
> closure; the rest are ordinary methods.

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

§13 ended with "0 of 441 signatures". That is no longer true, and 441 was not
the right denominator either: §15.8 shows there are 911 closure bodies in
this binary. **All 911 are now decompiled** (441 in the first sweep, the 470
that the corrected census added in a second sweep, 0 failures in both). They
are not 911 unrelated functions: they are instances of a small number of
shapes, and one of those shapes is the mechanism that binds C++ objects to
Lua properties.

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
| bodies decompiled | **911 / 911** (441 in the first sweep, 470 in the second) |
| failures | 0 |
| address set vs `isaac-ng-body-seeds.tsv` | identical (asserted, not assumed) |
| headers present in `kuna-all.c` | 911, all unique |
| wall clock | ~1.3 s per body, ~11 min for the second sweep |
| output | `~/.omo/evidence/pseudocode/kuna-all.c` (23765 lines), `kuna-index.jsonl` |

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

> **Not reproduced at instruction level.** The two constant counts in the next
> sentence reproduce exactly (375 and 90), but no candidate definition of the
> type-guard count reaches 434: calling `lua_type` gives 386, having `lua_type`
> as the first Lua call gives 379, and either gives 392. The definition behind
> 434 is not written down, so the number is neither confirmed nor retracted
> here — see [§15.10](#1510-one-documented-number-that-could-not-be-reproduced).

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

---

## 15. Property names are recovered — and the earlier names were off by one

§13 named 524 registrations by walking **backwards** from each
`lua_pushcclosure` call. That method is structurally incapable of finding a
property name, because the compiler pushes the name *after* the closure. This
section gives the mechanism that replaces it, the new census, and the
retractions that follow from it.

Tool: `tools/probe_property_names.py` (see `tools/README.md`). Every number
below is its output; the command is in `tools/README.md`.

### 15.1 The registration order, at the instruction level

The Lua 5.1 compiler emits a property registration like this:

```
    push   0x1              ; nupvalues
    push   <body>           ; the closure
    push   L
    call   lua_pushcclosure ; the closure now exists on the stack
    push   <name literal>   ; the name arrives AFTER the closure
    push   L
    call   lua_pushstring
    push   0x1
    push   0xfffffffe
    push   L
    call   lua_rotate       ; swap, so the name sits below the value
    push   <table>
    push   L
    call   lua_rawset       ; t[name] = closure
```

`lua_rawset(L, t)` takes the **key** from the second-from-top slot and the
**value** from the top, so the compiler emits `pushstring`/`rotate`/`rawset` to
put the name underneath the value it is about to store. The name literal is
therefore pushed *after* the closure is created, and a backward scan from the
`lua_pushcclosure` call can only see the **previous** registration's literal —
usually a class-name lookup key consumed by `lua_rawget`, or the previous
property's name.

### 15.2 Two hand-checked examples

Site `0x008a4d28`, body `0x008c2fb0` — the class-name case:

| VA | instruction | meaning |
|---|---|---|
| `0x008a4cdb` | `push 0xb706b8` | `"Entity"` — a **class-name lookup key** |
| `0x008a4ce3` | `call lua_pushstring` | pushed for the lookup |
| `0x008a4ceb` | `call lua_rawget` | consumed here |
| `0x008a4d15` | `push 0xb706b8` | pushed again |
| `0x008a4d1a` | `call 0x0085d040` | consumed by the class-registration call |
| `0x008a4d28` | `call lua_pushcclosure` | body `0x008c2fb0` is registered here |
| `0x008a4d3c` | `push 0xb753b4` | `"__gc"` — the **real** name |
| `0x008a4d44` | `call lua_pushstring` | |
| `0x008a4d4f` | `call lua_rotate` | name below value |
| `0x008a4d5d` | `mov ebx,DWORD PTR ds:0xb18408` | `lua_rawset` held in a register |
| `0x008a4d60` | `call ebx` | `t["__gc"] = closure` |

`probe_closures.py` reports the name at `0x008a4d28` as **"Entity"**. The
closure is stored under **`__gc`**. The body `0x008c2fb0` is a destructor
shape — a lookup, one virtual call, then `xor eax,eax; pop ebp; ret` — so
`__gc` is the correct reading and "Entity" is not.

Site `0x0087a18a`, body `0x008c3630` — the class-name-again case: the literal
`0x00b6d1ac` (`"Size"`) is pushed at `0x0087a111` and `0x0087a141`, *before*
the closure is created, and the closure is stored under `0x00b70f1c`
(`"__len"`) by `pushstring` at `0x0087a1a9` → `rotate` at `0x0087a1b4` →
`rawset` at `0x0087a1bf`. `probe_closures.py` reports **"Size"**.

### 15.3 The rule that replaces the backward walk

Scan **forward** from the `lua_pushcclosure` call and accept a `lua_pushstring`
as *this* closure's name only when all of the following hold:

1. a `lua_rotate` follows within 12 instructions, and a `lua_rawset` within 16
   more. The `rawset` is frequently indirect — `mov ebx,DWORD PTR
   ds:0xb18408` then `call ebx` — so the tool resolves register-held imports
   rather than only direct `call DWORD PTR ds:<slot>` forms.
2. no other `lua_pushcclosure` occurs in between. Without this rule the scan
   steals the *next* closure's name.
3. the string argument is a resolvable literal: pushed directly, passed as a
   stack parameter of the enclosing helper (then the literal comes from the
   helper's **caller**), or staged into a register by the caller.

Rule 1 alone is not enough — it also matches a name pushed for a lookup — and
rule 2 alone is not enough — it will happily attribute the next
registration's name. Both are required, and the tool fails its self-test
without them.

### 15.4 Three name shapes

| shape | how the literal is found | example |
|---|---|---|
| literal in the helper | `push 0x00b73d98` before `lua_pushstring` | `GetRandomTileIndex` |
| stack parameter | `push DWORD PTR [ebp+0x8]`; the helper's **caller** pushes the literal | `GetType` — caller `0x0086c36d` pushes `0x00b7306c` |
| register argument | `mov edx,0x00b74cb0` in the caller, `push edx` in the helper | `ResetPitch` — caller `0x0086dc6e` |

The second shape is why one helper can register many properties: helper
`0x00875230` takes the name and the upvalue as parameters, and each of its
callers registers a different property. The tool emits one record per caller
for such helpers, which is why 1064 call sites produce 2010 records.

### 15.5 The upvalue, and why (name, upvalue) is the key

`lua_pushcclosure(L, fn, 1)` pops one upvalue, stored immediately before the
call as `mov DWORD PTR [eax],<src>`. The source is either a **code pointer** —
the per-property implementation, e.g. `0x0040c3a0`, which disassembles to
`mov eax,DWORD PTR [ecx]; ret`, a field reader — or an **integer field
offset**, e.g. `0` for `Vector.X` and `4` for `Vector.Y`.

One generic body serves several properties: `X` and `Y` both come from the
registration helper `0x00875230`, distinguished only by the upvalue. The
discriminating key is therefore **(name, upvalue)**, not the body.

### 15.6 The new census

```
python -u tools/probe_property_names.py "<isaac-ng.exe>" \
    --cache disasm.txt --json-out props.json
```

| quantity | value |
|---|---:|
| `lua_pushcclosure` call sites | 1064 |
| registration records (one per caller for multi-caller helpers) | 2010 |
| records with a name | 2010 |
| call sites with no name | **0** |
| distinct names | 1289 |
| property-style names | 1885 records / 1275 distinct |
| metamethod records | 125, over 14 distinct metamethod names |
| distinct closure bodies | 911 |
| distinct bodies that carry a name | **911** |
| bodies serving more than one name | 323 |
| records with an upvalue | 1870 / 1402 distinct |
| distinct (name, upvalue) pairs | 1577 |

Metamethod breakdown: `__gc` 98 records over 49 bodies, `__len` 11, `__call`
3, `__tostring` 2, `__mul` 2, and one each of `__add`, `__sub`, `__div`,
`__unm`, `__eq`, `__bnot`, `__band`, `__bor`, `__bxor`.

Real Isaac API names are now attached to bodies, not just to classes:
`Ref`, `X`, `Y`, `GetPlayer`, `FindInRadius`, `FindByType`, `CountEntities`,
`ShowItemText`, `ShowFortuneText`, `GetType`, `ResetPitch`,
`GetRandomTileIndex`, `GetGridEntityFromPos`, `TearFlags`, `Language`.

### 15.7 Retraction: the backward names are off by one registration

`probe_closures.py` named 524 sites; the forward scan names all 1064. Of the
524 sites the two methods both name, **73 agree and 451 differ**:

| | probe_closures (backward) | forward scan |
|---|---:|---:|
| sites with a name | 524 | 1064 |
| sites without | 540 | **0** |
| names in agreement | 73 | |
| names in disagreement | **451** | |

The 73 agreements are mostly luck: 49 of them are `__gc`, where the previous
registration happened to be another `__gc`, and a handful are names that are
genuinely registered twice in a row (`ToBomb`, `ToFamiliar`, `ToPlayer`,
`ToPickup`, `ToPoop`, `Get`). The bulk of the 451 disagreements are the
metatable-key labels: **all 209** sites that `probe_closures.py` labelled
`__propget` are in fact real property names (**355 distinct**), and **all 173**
sites it labelled `__propset` are real property names too (**305 distinct**).
A further 10 were labelled `__len` and are not.

### 15.8 Retraction: there are 911 closure bodies, not 441

The "441" is not a count of closure bodies. It is the number of bodies
`probe_closures.py` could *reach*: 63 "bound", 417 metatable-key and 20
shared-name bodies. Those three categories **overlap** -- 63 + 417 + 20
is 500, not 441 -- so they are a cover, not a partition. Its own categories are a partition of the same 441 the kuna seed
file uses, which splits them 69 identifier-labelled / 372 generic — two
different partitions of one set.

Over all 1064 call sites there are **911 distinct bodies**, every one of them
preceded by a terminator (verified: 911 of 911), and every one of them now
carries a name. Consequences:

- The seed file now covers **all 911** bodies: it was regenerated from the
  code-derived attribution table (`tools/gen_body_seeds.py`) and the 470
  bodies it added have been decompiled, 0 failures.
- Of the 372 seed bodies that were labelled generically, **368 now carry a real
  property name** (524 distinct names across them) and 4 remain
  metamethod-only.
- Of the 69 seed bodies carrying a real identifier, **45 are `__gc` closures**
  (the class's destructor) and the other 24 are genuine method names —
  `Get`, `Language`, and 19 `To*` conversions. The document's claim that the
  class-named bodies are *type constructors* is therefore wrong: see
  §15.9.

### 15.9 Retraction: the "63 type constructors" claim

§13 stated that the 63 one-to-one name bindings are type constructors whose
bodies build a type's metatable. They are not. They are the per-class `__gc`
closures — destructors — for 45 of them, and ordinary methods for the rest.
The example §13 gave, `Entity` → `0x008c2fb0`, is a `__gc` closure: the
disassembly at that address performs a lookup, one virtual call and returns 0.

The metatable-key bodies of §13 are the property accessors, and their
per-property names are now known — which is what §13 and §14 could not
distinguish.

### 15.10 One documented number that could not be reproduced

§14.3 states that `lua_type` appears in **434 of 441** bodies. The sibling
numbers in the same paragraph reproduce exactly at instruction level over the
same 441 seed addresses, measured with `tools/probe_body_signatures.py`:

```
tools/probe_body_signatures.py "<isaac-ng.exe>" --cache disasm.txt
```

| quantity (441 seed bodies) | §14.3 | measured | reproduced |
|---|---:|---:|:--:|
| bodies using `0xfff0b9d7` | 375 | 375 | yes |
| bodies using `0xfff0b9d8` | 90 | 90 | yes |
| bodies calling `lua_type` | — | 386 | — |
| bodies whose first Lua call is `lua_type` | — | 379 | — |
| bodies calling `lua_type` **or** using a pseudo-index | — | 392 | — |

The same measurement over the **whole** corpus, now that the seed file
covers all 911 bodies (the counts are over every body, and every one of
them has a `ret`):

| quantity (911 bodies) | measured |
|---|---:|
| bodies calling `lua_type` | 800 |
| bodies whose first Lua call is `lua_type` | 783 |
| bodies whose first Lua call is a type check | 858 |
| bodies using `0xfff0b9d7` | 825 |
| bodies using `0xfff0b9d8` | 238 |
| bodies calling `luaL_check*` | 287 |
| bodies with a `ret` | 911 |
| bodies truncated by the extent guard | 0 |
| bodies whose first Lua call is a type check | — | 391 | — |
| bodies calling `lua_type` | 434 | 386 | **no** |

No candidate definition reproduces 434. The figure is most likely a
decompiler-level classification that this document does not define; the
definition needs to be pinned before the number can be trusted or corrected.
Recorded as an open question, not as a retraction.

Over all 911 bodies the same measurement gives: `lua_type` called by 800,
first Lua call is `lua_type` in 783, `0xfff0b9d7` in 825, `0xfff0b9d8` in 238,
`luaL_check*` in 287.

### 15.11 What this does not establish

- **No body is implemented.** This is naming only. The 911 bodies are located
  and named; 441 of them are decompiled; 0 of them run.
- **Names are not per-class attribution.** The tool knows *which name* a
  closure is registered under, not *which class* it belongs to. Attaching a
  name to a class still requires the class-registration call to be resolved.
- **A name is not a signature.** Knowing that `TearFlags` is registered under
  the upvalue `0x38` says which field it touches; it does not say what it
  returns.
- **The 911 is a count of distinct closure bodies**, not of functions in the
  binary, and not of Lua-visible methods. One body can be registered under
  several names (323 do), and one name can be registered on many bodies.
- **Nothing here is cross-checked against an independent source.** The names
  are self-consistent and hand-verified, but they come from one binary; an
  independent check against REPENTOGON's `libzhl` is still open.

## 16. Per-class attribution: which class owns which closure

§15 named every closure. It did not say which class a closure belongs to,
because the registration site alone does not carry the class. This section
closes that gap, from the code.

Tool: `tools/probe_class_attribution.py` (see `tools/README.md`).

### 16.1 The class-registration routine

Every game class is registered by a dedicated routine that receives the class
name as a C string argument, builds the class table, and installs its
metatable. In this build that routine is at `0x0085d040`.

**66** call sites of it exist, and each passes a `.rdata` string literal.
Those literals are the class names, and they yield **64 distinct classes** --
`Room` and `RoomDescriptor` are each registered twice, in two places.

The routines that call it are per class and hardcode their own name. The
routine for `Vector` is `0x008a7c00`, and it pushes `0x00b701dc` ("Vector")
three times -- to look the class up, to create it, and to install its `__gc`
closure:

```
  8a7c5b:  push   0xb701dc        ; "Vector"
  8a7c95:  push   0xb701dc        ; "Vector" -- create the class
  8a7c9a:  call   0x85d040        ; the class-registration routine
  8a7cbc:  push   0xb753b4        ; "__gc"
```

That per-class literal is what makes the attribution evidence-based rather
than a guess: the class name is in the code of the registrar itself, not only
at the call site.

### 16.2 The master function and the scope rule

All 66 call sites live in one function, `0x00866960`, in class order. Each
class block has the same shape:

```
  866a48:  call   0x0085dab0      ; construct the scope's table
  866a4e:  push   0x00b701dc      ; "Vector"
  866a5d:  call   0x008a7c00      ; the class-registration routine
  ...
  866c81:  push   0x00b6a748      ; "X"
  866c86:  call   0x00875230      ; the property registrar
  866c8f:  push   0x00b6d134      ; "Y"
  866c96:  call   0x00875230
  ...
  866ce1:  call   0x0085dab0      ; the next scope's table
```

`0x0085dab0` constructs a scope's table and `0x0085d980` destroys it. The
scope rules are:

1. **The last class-registration call before a registration names the
   class** -- as long as no scope construct happened after it.
2. **A construct with no class-registration call after it opens a namespace
   scope**, not a class scope. The tail of the master function does exactly
   this: it registers the game's Lua globals.
3. **A registration before the namespace is named belongs to the plain global
   scope** -- the table is constructed first and named later.

The anchor is the class-registration call, *not* the construct: the twelve
entity classes (`Entity`, `EntityTear`, `EntityPlayer`, `EntityPickup`,
`EntityEffect`, `EntityFamiliar`, `EntityProjectile`, `EntityNPC`,
`EntityBomb`, `EntityLaser`, `EntityKnife`) all live under a single
construct at `0x00868909` and each has its own registration call.

Rule 2 is what keeps the globals out of the classes. The last class before
the global block is `MusicManager`; without rule 2 the 56 global functions
would be attributed to `MusicManager`, which is what the first version of this
tool did. The self-test now pins that with negative controls.

Three shapes of registration have to be attributed, and all three are covered:

1. **Inside the class's own registrar** -- the `__gc` closure, for instance.
   The class is that routine's class.
2. **In a shared helper called from the master function** -- the usual case.
   The class is the scope that contains the *call site* of the helper.
3. **In a shared helper called from several class scopes** -- then the closure
   really is registered on several classes, and the tool emits one record per
   class. A property that exists on several classes is a fact about this API,
   not a contradiction.

### 16.3 The result

| quantity | value |
|---|---:|
| class-registration routines | 66 |
| distinct classes | **64** |
| global namespaces (not classes) | **3** |
| attributed registration records | **2012** |
| distinct (class, property) pairs | **1538** |
| distinct bodies attributed to a class | **911** |
| method / property / metamethod records | **1026 / 861 / 125** |

The 64 classes, their member counts, and how many distinct closure bodies
implement them. A class can share a body with another class and with its own
sibling members, so the body count is never a method count:

| class | methods | properties | metamethods | distinct bodies |
|---|---:|---:|---:|---:|
| `Entity` | 63 | 37 | 1 | 57 |
| `Item` | 5 | 30 | 1 | 17 |
| `EntityLaser` | 15 | 26 | 0 | 30 |
| `EntityPlayer` | 249 | 26 | 0 | 95 |
| `RoomDescriptor` | 1 | 26 | 3 | 16 |
| `ProjectileParams` | 0 | 23 | 2 | 14 |
| `EntityTear` | 6 | 19 | 0 | 19 |
| `EntityFamiliar` | 22 | 17 | 0 | 20 |
| `Room` | 116 | 16 | 2 | 58 |
| `EntityNPC` | 22 | 15 | 0 | 30 |
| `EntityProjectile` | 6 | 14 | 0 | 6 |
| `GridEntityDoor` | 16 | 14 | 0 | 23 |
| `EntityEffect` | 5 | 13 | 0 | 14 |
| `Card` | 3 | 11 | 1 | 10 |
| `PillEffect` | 1 | 10 | 1 | 11 |
| `EntityPickup` | 8 | 9 | 0 | 9 |
| `Costume` | 0 | 8 | 1 | 9 |
| `EntityKnife` | 7 | 8 | 0 | 10 |
| `EntityRef` | 0 | 8 | 1 | 11 |
| `GridEntityDesc` | 0 | 8 | 1 | 6 |
| `ActiveItemDesc` | 0 | 7 | 1 | 5 |
| `Color` | 4 | 7 | 2 | 7 |
| `Sprite` | 37 | 7 | 1 | 25 |
| `TearParams` | 0 | 7 | 1 | 9 |
| `Game` | 80 | 6 | 1 | 47 |
| `GridEntity` | 23 | 5 | 1 | 27 |
| `Level` | 59 | 5 | 1 | 28 |
| `Spawn` | 1 | 5 | 1 | 9 |
| `EntityBomb` | 1 | 4 | 0 | 7 |
| `Entry` | 0 | 4 | 1 | 7 |
| `KColor` | 0 | 4 | 1 | 3 |
| `GridEntityPoop` | 2 | 3 | 0 | 7 |
| `GridEntityPressurePlate` | 1 | 3 | 0 | 7 |
| `GridEntityRock` | 4 | 3 | 0 | 8 |
| `QueueItemData` | 0 | 3 | 1 | 7 |
| `TemporaryEffect` | 0 | 3 | 1 | 5 |
| `BitSet128` | 2 | 2 | 8 | 10 |
| `PosVel` | 0 | 2 | 1 | 3 |
| `Vector` | 15 | 2 | 7 | 15 |
| `CardConfigList` | 1 | 1 | 2 | 3 |
| `CostumeConfigList` | 1 | 1 | 2 | 3 |
| `EffectList` | 1 | 1 | 2 | 3 |
| `EntityList` | 1 | 1 | 2 | 3 |
| `EntityPtr` | 1 | 1 | 1 | 3 |
| `GridEntityPit` | 3 | 1 | 0 | 5 |
| `GridEntitySpikes` | 0 | 1 | 0 | 2 |
| `GridEntityTNT` | 0 | 1 | 0 | 2 |
| `ItemConfigList` | 1 | 1 | 2 | 3 |
| `PillConfigList` | 1 | 1 | 2 | 3 |
| `RoomConfigEntries` | 1 | 1 | 2 | 3 |
| `RoomConfigSpawns` | 1 | 1 | 2 | 3 |
| `VectorList` | 1 | 1 | 2 | 3 |
| `intValues` | 1 | 1 | 2 | 3 |
| `Config` | 12 | 0 | 1 | 9 |
| `Font` | 9 | 0 | 1 | 9 |
| `FontRenderSettings` | 12 | 0 | 1 | 9 |
| `HUD` | 11 | 0 | 1 | 7 |
| `ItemPool` | 16 | 0 | 1 | 15 |
| `MusicManager` | 19 | 0 | 1 | 11 |
| `PathFinder` | 13 | 0 | 1 | 11 |
| `RNG` | 5 | 0 | 2 | 7 |
| `SFXManager` | 9 | 0 | 1 | 8 |
| `Seeds` | 27 | 0 | 1 | 18 |
| `TemporaryEffects` | 17 | 0 | 1 | 8 |

The three global namespaces are not classes -- they are the game's Lua
globals. They are listed separately so they are never mistaken for a class
with an unusually large API:

| namespace | methods | properties | metamethods | distinct bodies |
|---|---:|---:|---:|---:|
| `global:Options` | 0 | 27 | 0 | 6 |
| `global:Input` | 9 | 0 | 0 | 4 |
| `global:Isaac` | 56 | 0 | 0 | 29 |

`global:Isaac` is the game's Lua global namespace -- `Isaac.GetPlayer()`,
`Isaac.CountEntities()`, `Isaac.FindInRadius()` and so on. `global:Input` and
`global:Options` are the other two. They are not classes.

The surface this section produces is written to
`bindings/code-derived-surface.json` (format 2) and drives
`bindings/isaac-api-spec.json` and the generated header through
`tools/gen-api-spec.py`.

### 16.4 The contamination is gone

§13's surface was built by string adjacency and carried a documented
caveat: `HUD` and `Isaac` were "known-contaminated" with animation,
layer-fade and table-config field names. The code-derived attribution
settles it:

| class | old bucket (§13, adjacency) | code-derived (§16) |
|---|---:|---:|
| `HUD` | 42 | **12** |
| `Isaac` | 76 | **0** -- there is no such class; the global namespace is `global:Isaac` |
| `Entity` | 261 | **101** |
| `KColor` | 18 | **5** |
| `EntityPlayer` | 77 | **275** |
| `Sprite` | 33 | **45** |
| `Vector` | 22 | **24** |
| `Font` | 8 | **10** |

`HUD` now carries the methods the binary gives it, and no others. `Entity`
gained `IsVulnerableEnemy`, which the adjacency method had filed under `HUD`;
the mod-data functions (`HasModData`, `LoadModData`, `RemoveModData`,
`SaveModData`) are `global:Isaac` functions and not `global:Input` ones; and
`__mul` belongs to `Color`, not to `Sprite`.

The old surface also had only 9 classes where the code has 64, and it had
`Input` as a class, which it is not.

### 16.5 Method, property or metamethod

Every record is classified by the helper that performs the registration, not
by the name. The helper is the function containing the `lua_pushcclosure` call
site, and the classification is read from the metatable key that the helper
pushes into the class table:

| pushed key | meaning | records |
|---|---|---:|
| `__propget` / `__propset` / `__newindex` | **property** | **861** |
| `__gc`, `__len`, `__call`, `__tostring`, `__add`, ... | **metamethod** | **125** |
| none of the above | **method** | **1026** |

The role of a property is read per call site, because one helper can install
both sides. The generic property registrar `0x875230` pushes `__propget`
(`0xb6e994`) and installs the reader `0x008bb2e0`, then pushes `__propset`
(`0xb6ea38`) and installs the writer `0x008bb340`; a whole-helper scan reported
both as getters. Per call site, `Vector.X` carries
`getter 0x008bb2e0 / setter 0x008bb340 / upvalue 0x00000000` and `Vector.Y` the
same bodies with `upvalue 0x00000004` -- the field offset travels as the
upvalue, so a body address is not an identity.

The classification was itself a regression check: switching from a
frame-pointer-only function extent to a leaf-aware extent moved 273 records to
a different `func` and, with a recursive class resolution, kept every class
count (64 classes + 3 namespaces, 2012 records, 1538 pairs, 911
bodies).

### 16.6 What this does not establish

- **No body is implemented.** This is a table, not code. 0 of 911 bodies run.
- **Inheritance is not modelled.** A property that a class inherits is
  registered on its parent's table, so it is attributed to the parent. The
  `__parent` link is a Lua table taken from the stack in the registration
  routine, not a name literal, so the parent *name* is not recoverable from
  the binary; the eight known `Entity` subclasses are curated in
  `tools/gen-api-spec.py`.
- **A property is not a signature.** `TearFlags` is on `Entity`; what it
  returns is still unknown.
- **Metamethods are recorded but not wired.** `apply()` registers methods and
  properties; the 125 metamethods in the spec have no runtime registration
  path yet.
- **The counts are per registration, not per method.** 1538 (class, property)
  pairs over 64 classes, from 2012 records; the same body appears in several
  classes, and the same name appears in many. A property with a getter and a
  setter is counted twice, which is correct for a registration table and
  wrong for a method count.
- **Function extents are heuristic.** A function starts at a `push ebp`
  prologue, or at the first instruction after an `int3` run that follows a
  terminator. Helpers that neither push `ebp` nor follow padding are folded
  into the function that encloses them; `jmp` is deliberately not treated as a
  function start, because a loop back-edge is not one. The class attribution
  resolves a helper through its call sites instead, so it does not depend on
  the extent rule.
- **One binary.** No independent cross-check yet.

## 17. Shape census: how many arguments a body sees, and what it returns

Phase 4 of the plan in `~/.omo/plans/tboi-655-bodies.md`. A closure body
carries no prototype, but it does touch the Lua stack, and the slots it
touches say how many arguments it can see and how many values it hands back.

### 17.1 What is measured, and what the number is

`tools/probe_body_arity.py` walks every one of the 911 bodies and reports two
numbers per body:

- **`argc`** — the highest Lua stack index the body touches, minus one (the
  receiver lives at index 1). It is an **upper bound** on the number of
  arguments the member can see, not a count of the arguments it uses: the body
  may ignore the values it reads, and the game may pass arguments the body
  never looks at.
- **`returns`** — the number of values the body leaves on the Lua stack, read
  as the value in `eax` at the `ret` (`mov eax,1` → 1, `xor eax,eax` → 0).

`argc_evidence` says where the number came from: `index` (the body reads the
slot itself), `helper` (a helper the body calls reads it — the helper's own
reads are followed recursively), `receiver-only` (the body touches only the
receiver) or `none`. `argc_evidence: none` means nothing was measured, and
`argc` is then 0 with `argc_measured` false in the generated spec.

### 17.2 Why the recursion matters

The first version of the census looked only at the body itself and reported
`Vector:Clamped` as taking no argument. The body is:

```
0x008a8680: ... call 0x8ba890 ...
```

and `0x8ba890` reads four numbers out of the stack:

```
0x8ba890: push 0x5; push edi; call luaL_checknumber
          push 0x4; push edi; call luaL_checknumber
          push 0x3; push edi; call luaL_checknumber
          push 0x2; push edi; call luaL_checknumber
```

so the body can see four arguments even though it passes only `L`. The census
now follows helper calls — recursively, to depth 3, and only into helpers
shorter than 200 instructions — and reports the bound. That is the difference
between "the body reads no argument" and "the body can see four arguments".

### 17.3 The census over all 911 bodies

```
$ python3 tools/probe_body_arity.py isaac-ng.exe --cache disasm.txt --json-out arity.json
== 911 seed bodies
   argc_0                       465
   argc_1                       297
   argc_2                        60
   argc_3                        39
   argc_4                        21
   argc_5                        14
   argc_6                         8
   argc_7                         4
   argc_8                         3
   argc_without_evidence         52
   returns_0                    359
   returns_1                    534
   returns_unknown               18
   bodies                       911
```

Read through the class attribution, the same census gives the shape of every
member of the surface: **1003 methods** — argc 0 → 539, 1 → 273, 2 → 85,
3 → 46, 4 → 28, 5 → 14, 6 → 7, 7 → 4, 8 → 3, unmeasured → 4; returns 1 → 587,
0 → 395, unmeasured → 21. **461 properties**: the getter reads no argument in
459 cases and returns one value in 453.

### 17.4 Hand-checked members

| member | body | argc | returns | why |
| --- | --- | --- | --- | --- |
| `Vector:Cross` | `0x008a8270` | 1 | 1 | takes the other vector |
| `Vector:Lerp` | `0x008a8310` | 3 | 1 | factor plus two bounds |
| `Vector:Length` | `0x008a8510` | 0 | 1 | reads only the receiver |
| `Vector:Normalize` | `0x008a8180` | 0 | 0 | mutates in place, returns nothing |
| `Vector:Resized` | `0x008a8430` | 1 | 1 | returns a new vector |
| `Vector:Clamped` | `0x008a8680` | 4 (`helper`) | 1 | the helper reads four numbers |
| `Vector.X` getter | `0x008bb2e0` | 0 | 1 | reads the receiver's field |
| `Vector.X` setter | `0x008bb340` | 1 | 0 | takes the new value |

`Vector:Lerp` reading three numbers and `Vector:Normalize` reading none match
the argument counts the game documents for those members. That is a
consistency check on the measurement, not a proof of the semantics: the bodies
call the game's own C++ virtuals, so what the numbers *mean* is not in the
binary at all.

### 17.5 What this does not establish

- **A bound is not a signature.** `argc` is the number of arguments the body
  can *see*. The number of arguments the game passes, and the number the
  member uses, can both be smaller: `Vector:Clamped` is documented with two
  arguments, and the body reaches four slots because a shared helper reads
  four numbers.
- **The body does not say what the arguments mean.** The census measures the
  ABI, not the semantics. A body that reads one number and a body that reads
  one table look the same here.
- **Unmeasured stays unmeasured.** 52 of the 911 bodies have no index
  evidence and 18 have an unmeasurable return; the generated spec keeps
  `argc_measured: false` / `returns_measured: false` for them instead of
  inventing a zero.
- **No body is implemented.** The census is a table. 0 of 911 bodies run.
