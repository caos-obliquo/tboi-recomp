# tools/

Research and codegen tools. Everything here is committed so a contributor can
re-derive the recovered surface instead of trusting it.

No tool in this directory ships, embeds, or downloads Nicalis content. They read
a binary **you** already own, from a path **you** pass in.

| tool | what it does | needs the game binary? |
|---|---|---|
| `extract-binding-tables.py` | Surveys the native→Lua binding surface from `isaac-ng.exe`. Hard-fails if its class anchors do not validate. | yes |
| `gen-api-spec.py` | Turns committed recovered data into `bindings/isaac-api-spec.json` + a generated C++ header. | no |
| `pe_vaddr.py` | PE section reader. Translates addresses and **refuses to guess** when a VA is ambiguous. | yes |
| `probe_metatable_keys.py` | Locates the nine recovered metatable keys in `.rdata`. | yes |
| `probe_xrefs.py` | Counts `.text` references to each metatable key address. | yes |
| `probe_iat.py` | Walks the PE import directory, maps every IAT slot to its imported name, and finds each `call dword ptr [slot]` site. | yes |
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

Reads `bindings/recovered-binding-surface.json` — committed, so a fresh checkout
can regenerate. Keys beginning with `_` are reserved metadata (`_provenance`,
`_caveat`) and are skipped by the class loop.

Idempotent, and it writes **both** the JSON and a C++ header so a malformed
method name fails at compile time instead of at load time. CI enforces this
with a `git diff --exit-code` job (`spec-freshness`).

Every method is currently `argc 0 / returns 0 / documented false`. Those are
placeholders until signatures are recovered; they are not claims about the real
API.

`bindings/recovered-binding-surface.json` carries a `_caveat` key: the `HUD` and
`Isaac` buckets are **known-contaminated** with animation, layer-fade, and
table-config field names that were not filtered out. Treat those two buckets as
provisional.

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