# tools/

Research and codegen tools. Everything here is committed so a contributor can
re-derive the recovered surface instead of trusting it.

No tool in this directory ships, embeds, or downloads Nicalis content. They read
a binary **you** already own, from a path **you** pass in.

| tool | what it does | needs the game binary? |
|---|---|---|
| `extract-binding-tables.py` | Surveys the native→Lua binding surface from `isaac-ng.exe`. Hard-fails if its class anchors do not validate. | yes |
| `gen-api-spec.py` | Turns the code-derived surface into `bindings/isaac-api-spec.json` + a generated C++ header (methods, properties, metamethods). | no |
| `gen_body_seeds.py` | Regenerates the REA/Ghidra seed list of closure bodies (911) from the code-derived attribution table. | no |
| `pe_vaddr.py` | PE section reader. Translates addresses and **refuses to guess** when a VA is ambiguous. | yes |
| `probe_metatable_keys.py` | Locates the nine recovered metatable keys in `.rdata`. | yes |
| `probe_xrefs.py` | Counts `.text` references to each metatable key address. | yes |
| `probe_iat.py` | Walks the PE import directory, maps every IAT slot to its imported name, and finds each `call dword ptr [slot]` site. | yes |
| `probe_property_names.py` | Recovers the name and upvalue of every Lua closure registration, by scanning forward from each `lua_pushcclosure` call site. | yes |
| `probe_body_signatures.py` | Counts the Lua entry points and patched-Lua pseudo-indices each closure body touches. | yes |
| `probe_class_attribution.py` | Attributes every closure registration to the game class that owns it, classifies it (method / property / metamethod) and writes `bindings/code-derived-surface.json`. | yes |
| `negctl-baseline.py` | Negative control for the test suite. Proves a green baseline can go red. | no |

Current findings from these probes are written up in
[`../docs/binding-surface-findings.md`](../docs/binding-surface-findings.md).
That document is the place to look first for measured addresses and call-site
counts; this file stays the map of what each tool does.

## `pe_vaddr.py`

```sh
tools/pe_vaddr.py --self-test /path/to/isaac-ng.exe
```

A library, not a script — the probes import it. The name has an underscore
because a hyphen makes it unimportable.

**It has no default.** `va_to_offset` returns every section that claims an
address and refuses to translate when more than one does. Callers must either
name the section or assert the candidate list was a singleton. In `isaac-ng.exe`
`.rdata` and `.data` sit at different bases (`0x00718000` vs `0x007f8000`), so a
single-base helper silently resolves one and emits garbage for the other. `.data`
also has a virtual tail that is not on disk, and the reader says so rather than
returning an offset into unrelated bytes.

Addresses are RVAs internally, with `rva_to_va`/`va_to_rva` for the absolute
form binutils prints. Mixing the two is the other easy way to be wrong, and it
already caused two disassemblies to be aimed at unrelated code.

`--self-test` validates the parse against `objdump -h` rather than hard-coded
expectations, and pins `.text`'s first byte to the VMA binutils prints.

## `probe_metatable_keys.py`

```sh
tools/probe_metatable_keys.py /path/to/isaac-ng.exe
```

Exact NUL-terminated search for each recovered metatable key. Carries a canary
control (a key that cannot exist must return zero hits) and a reachability
control (a key known to exist must return at least one). Those controls earned
their keep: an off-by-one in the scan was dropping every string followed by
ordinary data and reporting four of nine keys as absent.

## `probe_xrefs.py`

```sh
tools/probe_xrefs.py /path/to/isaac-ng.exe
```

Counts `.text` occurrences of each key's absolute address as an immediate. The
near-universal idiom is `push <key-address>` / `call dword ptr [IAT]`.

**A byte match is not a finding.** It proves the value is materialised in
`.text`, not that the instruction intends a metatable lookup. This tool reports
candidates.

The counts are diagnostic: the property keys dominate because property
registration is the most common operation, while the rare keys pin down the
metatable core.

## `probe_iat.py`

```sh
tools/probe_iat.py /path/to/isaac-ng.exe
tools/probe_iat.py /path/to/isaac-ng.exe lua_pushcclosure
tools/probe_iat.py /path/to/isaac-ng.exe --dll=Lua5.3.3r.dll
```

Walks the PE import directory directly rather than scraping `objdump -p`, then
finds each call site by matching the six-byte x86-32 indirect-call encoding
`ff 15 <slot:le32>` in one pass over `.text`.

This is the tool that removes the guesswork: `isaac-ng.exe` imports Lua from
`Lua5.3.3r.dll` and every slot is named, so binding code reads with exact
semantics. `lua_pushcclosure` (1064 call sites) is where a native method becomes
a Lua closure; `lua_createtable` (8 call sites) is effectively one function that
builds the metatables.

## `probe_property_names.py`

```sh
tools/probe_property_names.py /path/to/isaac-ng.exe
tools/probe_property_names.py /path/to/isaac-ng.exe --json-out props.json
tools/probe_property_names.py /path/to/isaac-ng.exe --compare closures.json
tools/probe_property_names.py /path/to/isaac-ng.exe --self-test
```

Recovers the **name** each Lua closure is registered under, by scanning
*forward* from every `lua_pushcclosure` call site.

The reason a forward scan is needed: the Lua 5.1 compiler pushes the closure
first and the name afterwards —

```
call lua_pushcclosure   ; the closure exists
push <name literal>
call lua_pushstring
call lua_rotate         ; swap: name below value
call lua_rawset         ; t[name] = closure
```

— so `probe_closures.py`, which walks *backwards* from the call, can only see
the previous registration's literal. `probe_property_names.py` therefore
accepts a `lua_pushstring` as a site's name only when a `lua_rotate` follows
within 12 instructions and a `lua_rawset` within 16 more (both resolved through
register-held imports), and no other `lua_pushcclosure` intervenes.

It also recovers the closure's **upvalue** — the per-property implementation
staged as `mov DWORD PTR [eax],<src>` before the call — because one generic
body serves several properties: `Vector.X` and `Vector.Y` come from the same
registration helper and differ only by the upvalue. The discriminating key is
`(name, upvalue)`, not the body.

### What it establishes

- A name for every `lua_pushcclosure` call site in the binary (1064 of 1064),
  plus the metatable key when the name is a metamethod, plus the upvalue.
- One record per *caller* for registration helpers that take the name as a
  parameter, because such a helper registers a different property for each
  caller.

### What it does **not** establish

- **Not a class attribution.** The tool says *which name* a closure is
  registered under. It does not say *which class* owns it; that still needs the
  class-registration call to be resolved.
- **Not a signature.** The upvalue identifies the field a property touches, not
  what the body returns.
- **Not an implementation.** It reads the binary; it does not reimplement
  anything.
- **Not independent of the binary.** The names come from one binary and are
  self-consistent; they have not been cross-checked against another source.
- `--compare` against `probe_closures.json` is a **disagreement report**, not a
  vote. The forward scan is the one with the mechanism behind it (§15 of
  `docs/binding-surface-findings.md`); the backward scan cannot find a property
  name at all.

### Self-test

`--self-test` runs positive and negative controls against known sites and
exits non-zero if any of them fails: the `Ref` registration must resolve to
name `Ref` with body `0x008a0600` and upvalue `0x0040c3a0`; the `X`/`Y`
helper must resolve to two names over four records; the site at `0x008a4d28`
must resolve to `__gc` and the one at `0x0087a18a` to `__len` — both of which
the backward scan gets wrong.

## `probe_body_signatures.py`

```sh
tools/probe_body_signatures.py /path/to/isaac-ng.exe --cache disasm.txt
tools/probe_body_signatures.py /path/to/isaac-ng.exe \
    --bodies props.json --json-out sigs.json
```

Counts, per closure body, which Lua entry points it calls and which patched-Lua
pseudo-index it uses. This is the measurement behind the shape taxonomy in
`docs/binding-surface-findings.md` §14.3, and the reproduction attempt for the
`434/441` figure in §15.10.

By default it measures the 441-body kuna seed file, which is the corpus the
document's numbers were written against. With `--bodies` it measures the
distinct bodies of a `probe_property_names.py` dump (911 of them).

The extent rule matters and is easy to get wrong: a function ends where the
**next** function starts — a `push ebp` whose predecessor is a terminator —
not at the first `jmp`. A tail `jmp` is followed by more blocks of the same
function, which are contiguous in `.text`; stopping there truncates the body
and drops most pseudo-index uses (a run that did so reported 29 of 441 instead
of 392).

### What it establishes

- Instruction-level counts: bodies calling `lua_type`, bodies whose first Lua
  call is `lua_type`, bodies using `0xfff0b9d7` / `0xfff0b9d8`, bodies
  calling `luaL_check*`.

### What it does **not** establish

- **Not a classification.** It counts entry points; it does not decide which
  of the 11 shapes of §14.3 a body belongs to.
- **Not a signature.** "Calls `lua_type`" is not "takes a userdata".
- **Not an argument count.** The Lua-visible argument count needs the stack
  index actually used, which is a separate measurement.

## `probe_class_attribution.py`

```sh
tools/probe_class_attribution.py /path/to/isaac-ng.exe
tools/probe_class_attribution.py /path/to/isaac-ng.exe --json-out attrib.json
tools/probe_class_attribution.py /path/to/isaac-ng.exe \
    --surface-out bindings/code-derived-surface.json
tools/probe_class_attribution.py /path/to/isaac-ng.exe --self-test
```

Attributes each closure registration to the game class that owns it, classifies
it as a method, a property or a metamethod, and writes the resulting table.

### Mechanism

Every class is registered by a routine that takes the class name as an
argument and installs the class metatable (`0x0085d040` in this build). Its 66
call sites all live in one master function (`0x00866960`), in class order, and
each passes the class name as a `.rdata` literal. The registrar routine for a
class hardcodes that class's own name, so the name is in the code, not only at
the call site.

The scope is opened by the table constructor (`0x0085dab0`) and closed by the
destructor (`0x0085d980`); neither is a class registrar, and the destructor is
not a scope boundary in this tool -- the class comes from the **last
class-registration call** before the record, unless a construct happened after
it, in which case the record belongs to the *namespace* opened by that
construct. That is how the game's Lua globals (`Isaac`, `Input`, `Options`) are
kept out of the classes: without the construct rule the 56 global functions
land on `MusicManager`, the class that happens to precede them. Twelve entity
classes share one construct, so the anchor is the registrar call, not the
construct.

Function extents are leaf-aware (`push ebp` prologues, and the first instruction
after an `int3` run that follows a terminator), so a small helper is not folded
into its caller. The classification of a registration therefore depends on
*which* helper performs it, and a shared helper is attributed through its call
sites with a recursive resolution.

### Classification

| pushed metatable key | kind |
|---|---|
| `__propget`, `__propset`, `__newindex` | property |
| `__gc`, `__len`, `__call`, `__tostring`, `__add`, ... | metamethod |
| none | method |

The property **role** (getter vs setter) is read per call site, because one
helper can install both sides: `0x00875230` pushes `__propget` and installs the
reader `0x008bb2e0`, then pushes `__propset` and installs the writer
`0x008bb340`, so `Vector.X` carries `getter 0x008bb2e0 / setter 0x008bb340 /
upvalue 0x00000000` and `Vector.Y` the same bodies with `upvalue 0x00000004`.
The field offset travels as the upvalue, so a body address is not an identity.

### Result

**64 classes** and 3 global namespaces, **2012 attributed records**,
**1538 (class, property) pairs**, **911 distinct bodies** attributed, kinds
**method 1026 / property 861 / metamethod 125**. `--surface-out` writes
`bindings/code-derived-surface.json` (format 2), which drives
`bindings/isaac-api-spec.json` and the generated header through
`tools/gen-api-spec.py`: 64 classes + 3 namespaces, 938 class methods, 434
class properties, 76 class metamethods, 65 namespace methods, 27 namespace
properties.

### What it establishes

- The class list, from the code: 64 classes, not the 9 the string-adjacency
  surface claimed.
- The class/property table: 1538 pairs. `HUD` has 12 properties, not the 42
  the adjacency surface claimed; `Isaac` does not exist as a class, so the
  documented HUD/Isaac contamination is resolved.
- The kind of every member, from the metatable key its helper pushes.
- The class of every record is a *name* from the code, with the address that
  produced it, so every row is checkable by hand.

### What it does **not** establish

- **Not an implementation.** It is a table.
- **Not the class hierarchy.** A property a class inherits is registered on
  its parent's table, so it is attributed to the parent; the `__parent` link is
  a Lua table taken from the stack, never a name literal, so no parent *name* is
  recoverable from the binary. The eight known `Entity` subclasses are curated
  in `tools/gen-api-spec.py`.
- **Not a signature.** Which field a property touches is a separate question.
- **Not a method count.** A property with a getter and a setter is two
  registrations but one member.
- **Not runtime wiring.** Metamethods are in the spec but have no registration
  path in the binding yet.

## `negctl-baseline.py`

```sh
tools/negctl-baseline.py                 # break, expect red
tools/negctl-baseline.py --no-break      # control mode, expect green
```

Copies the tree to scratch, applies exactly one surgical behavioural break (the
bogocrypt1 word-rotation mask), and asserts the suite reports precisely the two
expected failures and no others. The break must compile — a syntax error is not
a control. The copy's mtime is forced forward, because `tar` and `shutil.copy2`
preserve timestamps and `make` would otherwise skip the recompile and report a
phantom pass against the unpatched object file.

## `extract-binding-tables.py`

```sh
tools/extract-binding-tables.py "/path/to/isaac-ng.exe" > survey.json
```

Exits **2** if the binary is missing. Outputs a JSON region survey to stdout and
a one-line summary to stderr, so you can redirect the survey to a file.

### What it actually produces — read this before trusting it

A **string-region survey, not per-class membership.** This is a measured
limitation, not a caveat to be papered over.

Class names appear in the binary as NUL-terminated strings, and the method
table for a class follows. String adjacency alone cannot tell you which table
belongs to which class, because neighbouring tables are equally dense: with the
measured inter-token gap rule applied, `Font` reports **531** candidate tokens
while it has **8** real methods.

Attributing a name to a class requires the code that builds each metatable, so
this is blocked on the same disassembly pass as recovering the 655 method
bodies (see [#3](https://github.com/caos-obliquo/tboi-recomp/issues/3)).

### Measured constants, and how they were measured

These are in the script as named constants. They were derived by dumping byte
gaps, not chosen.

- `MAX_GAP = 3`. Inter-token gaps inside a real binding table were measured on
  `Entity` (first 14 gaps: `[1,2,0,0,0,2,2,1,0,3,0,2,0,0]`; histogram over
  gap≤12: `{(0,53),(1,58),(2,42),(3,53),(7,1)}`), with the same shape for
  `EntityPlayer`, `Sprite`, and `Room`. Gaps of 0–3 are the `\x00` separators;
  4+ is a run boundary.
- `CLASS_ANCHORS` holds 23 file offsets. The script **verifies** that the
  NUL-terminated name at each offset equals the expected class name and raises
  `SystemExit` otherwise, so a stale offset cannot silently produce garbage.

Three earlier attempts failed and are recorded here so they are not repeated:
scanning to the next anchor (buckets ran to the next anchor up to 42 KB away);
starting the scan at the anchor instead of after its NUL terminator (immediate
zero-method results); and treating a fixed byte span as a table boundary.

## `gen-api-spec.py`

```sh
python3 tools/gen-api-spec.py
```

Reads `bindings/code-derived-surface.json` — committed, so a fresh checkout can
regenerate. That file is written by `probe_class_attribution.py --surface-out`
(format 2: per class `parent`, `methods`, `properties`, `metamethods`, plus the
`namespaces` table for `Isaac`, `Input` and `Options`).

Idempotent, and it writes **both** the JSON and a C++ header so a malformed
member name fails at compile time instead of at load time. CI enforces this
with a `git diff --exit-code` job (`generated-spec-is-current`), so the surface,
the JSON and the header must change in the same commit.

Every method is currently `argc 0 / returns 0 / documented false`. Those are
placeholders until signatures are recovered; they are not claims about the real
API. Properties carry the getter and setter closure addresses as provenance, and
metamethods are recorded in the spec but have no runtime registration path yet.

The class hierarchy is **curated**, not recovered: the eight known `Entity`
subclasses are listed in `PARENTS` in this file. The `__parent` link is a Lua
table taken from the stack in the registration routine, never a name literal, so
no parent name is recoverable from the binary.

`bindings/recovered-binding-surface.json` is the earlier, string-adjacency
surface and is kept as the historical record of the contaminated attempt. It is
not read by this generator.

## `gen_body_seeds.py`

```sh
python3 tools/gen_body_seeds.py attrib.json --out seeds.tsv
```

Regenerates the REA/Ghidra seed list — one line per closure body, with a name:

```
0x008bb2e0	function	Vector.X
```

The input is the JSON written by `probe_class_attribution.py --json-out`. The
old list had 441 lines, because it was built from the bodies the *backward*
scan could name; the code-derived table knows all **911** bodies, and all 911
are named.

The name is the first (class, member) that uses the body, with `+N` when more
than one member shares it. A body address is not an identity: `Vector.X` and
`Vector.Y` share `0x008bb2e0` and differ only in their upvalue, and one body
serves up to 36 members.

### What it establishes

- The address set the decompiler is pointed at, and it is reproducible from the
  committed attribution table.

### What it does **not** establish

- **Not a body inventory.** A body that the attribution cannot place still gets
  a name here, because the seed file must list every address the decompiler
  should look at.
- **Not a signature.** The name is a label, not a claim about behaviour.

## `decompile_corpus.py`

Decompiles the 441 recovered closure bodies in **small, flushed, resumable
batches**. This exists because the first attempt at exactly this work lost all
of it, and the failure was invisible.

```
python3 tools/decompile_corpus.py \
  --seeds ~/.local/share/rea/seeds/isaac-ng-body-seeds.tsv \
  --out /tmp/corpus.c --batch 25
```

### Why it is shaped this way

The earlier sweep ran the decompiler once per address and composed the output
file **at the end**. It reached 400 of 441 bodies — 400 successful, 0 failures,
547 seconds — and was aborted. The output file was **0 bytes**. Every
decompilation had already been paid for and discarded, and the run had invoked
the decompiler a *second* time per address while composing, so it did twice the
work for one result. From the process's point of view it looked fine right up to
the abort.

Three properties are therefore enforced rather than hoped for:

- **Written as produced.** Each body is flushed to the output the moment the
  decompiler returns. Nothing accumulates in memory.
- **Resumable.** An append-only JSONL index records completed addresses and is
  the resume point. Re-running continues.
- **Batch size is an argument.** An abort costs at most `--batch` bodies rather
  than the whole run.

### Verified

| check | result |
|---|---|
| fresh run, `--batch 2 --limit 4` | 4 bodies, 0 failed |
| identical rerun | `already_done=4`, 4 more, no duplicates |
| headers in output | 8 unique, 8 records — no address recorded twice |
| corrupt index line | `ValueError`, exit 1 — **not** silently skipped |

That last row is a negative control. A resume mechanism that silently drops an
unreadable index line would quietly redo or skip work, which is the failure this
script exists to prevent.

### Completeness is the caller's job

This script counts bodies and reports failures. It does not decide a corpus is
correct. Assert the produced address set against the seed set — 441/441 equal was
checked that way, not by counting lines.

### kuna, and why not through REA

REA's Kuna provider refuses this PE with `target_format_unsupported`: *"REA
verified the Kuna provider on ELF and thin Mach-O images; pe is not admitted."*
That is true about REA's **verified matrix** and false about kuna, which loads
the file standalone and reports 4236 functions. Treating a wrapper's allowlist as
the tool's capability ceiling is [#14](https://github.com/caos-obliquo/tboi-recomp/issues/14).

kuna reports every one of the 441 bodies as *"interior to
std::basic_streambuf::~basic_streambuf"*, which is its nearest-symbol attribution
and is wrong. The bodies are real `lua_pushcclosure` closure entries by
`probe_closures.py`'s independent, call-anchored analysis. Two methods agreeing on
addresses and disagreeing about naming is the useful case; neither is trusted for
addresses here.

## Dead ends — do not re-mine these

Recorded so the next person does not repeat them. Every one of these was
actually executed and measured.

- **String-VA hunting for the archive loader.** Validated that `.text` does
  reference 17,931 distinct `.rdata` VAs, so the method works — but the three
  copies of `GameData wrong file header (%.16s)` and `resources/packed/config.a`
  are **not** in the referenced set. Not an anchor.
- **The `.data` archive manifest.** 17 records of `{const char* path; u32*
  entries; u32 count}` plus a master pointer array at VA `0x00C02E60`. That
  array has **zero** occurrences anywhere in the 9.36 MB binary, and no record
  or path string is referenced from `.text`. It is build-pipeline residue,
  never read at runtime. The runtime manifest is built by enumerating
  `resources/packed/` with `FindFirstFileA/W`.
- **`fopen`/`fseek`/`fread` call-site cluster (`0xa52500`–`0xa52880`).** A
  generic file-handle wrapper, not the archive reader. The archive reader is a
  *caller* of it.
- **Filename hashing for archive entries.** 9 algorithms (fnv1a32, fnv1_32,
  djb2, djb2x, sdbm, jenkins one-at-a-time, crc32, adler32, murmur3_32) × 5
  case/separator variants × 1,127 filenames harvested from shipped Lua → 0
  hits. Entry arrays are unsorted with uniform bit distribution, so they are not
  a sorted lookup table. This is the *archive-name* problem and is already
  solved at 81.46% by djb2-5381; do not confuse it with method names.
- **CRC32 over payload** for `animations.a` (count 1): no contiguous byte range
  of the file produces the recorded value.
- **Assuming `.rdata` and `.data` overlap in VA.** They do not — the ranges are
  disjoint in this binary. The hazard is that their bases differ, which
  `tools/pe_vaddr.py` now makes impossible to hit silently.

### Three traps that cost real time

1. `.rdata` and `.data` **share no base**: `0x00718000` vs `0x007f8000`. A
   single-base VA→file helper silently works for one and produces garbage for
   the other. Branch on the section, or use `tools/pe_vaddr.py`.
2. objdump prints two different operand dialects depending on `-M intel`.
   A regex written for `DWORD PTR [0x...]` matches nothing under the AT&T form
   and looks like a negative result. Use plain substring search plus a positive
   control.
3. `.text` VAs include `image_base`. Disassembling with a *file offset* as a
   `--start-address` aims at unrelated code and produces a plausible-looking
   disassembly of the wrong function. Convert through `tools/pe_vaddr.py`.

## `tools/probe_body_arity.py` — the shape census of every closure body

Reads all 911 closure bodies and reports, per body, how many Lua stack slots
the body touches and how many values it returns to Lua.

```
python3 tools/probe_body_arity.py isaac-ng.exe --cache disasm.txt \
    --json-out arity.json
```

**What it establishes**

- `argc` — the highest Lua stack index the body touches, minus one. It is an
  **upper bound** on the number of arguments the member can see.
- `returns` — the number of values the body leaves on the Lua stack, read from
  the value in `eax` at the `ret`.
- `argc_evidence` — where the number came from: `index` (the body reads the
  slot), `helper` (a helper it calls reads the slot, followed recursively),
  `receiver-only` or `none`.

**What it does NOT establish**

- The number of arguments the game actually passes, or the number the member
  uses: a body can read slots it ignores, and a shared helper can read more
  slots than the member needs. `Vector:Clamped` is documented with two
  arguments and the body reaches four slots because `0x8ba890` reads four
  numbers.
- What the arguments mean. The bodies call the game's own C++ virtuals, so the
  semantics of the API are not in the binary.
- That every body was measured: 52 bodies have no index evidence and 18 have
  an unmeasurable return; those stay unmeasured in the generated spec.

## Conventions for reporting findings

`CONTRIBUTING.md` has the full rules. The short version:

- State **how you measured it**. A finding without its method is not a finding.
- Negative results are valuable. "I searched for X and it is not there" saves
  someone a day — say what search space you covered.
- Commit the **tool**, not its raw output. Do not commit disassembly dumps or
  extracted game bytes.
- Do all address arithmetic as expressions over the PE section table. Never
  hand-computed hex in a script or an issue.
- Every search that can return nothing carries a **positive control** that
  proves it can return something.
