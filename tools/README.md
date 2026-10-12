# tools/

Research and codegen tools. Everything here is committed so a contributor can
re-derive the recovered surface instead of trusting it.

No tool in this directory ships, embeds, or downloads Nicalis content. They read
a binary **you** already own, from a path **you** pass in.

| tool | what it does | needs the game binary? |
|---|---|---|
| `extract-binding-tables.py` | Surveys the native→Lua binding surface from `isaac-ng.exe`. Hard-fails if its class anchors do not validate. | yes |
| `gen-api-spec.py` | Turns committed recovered data into `bindings/isaac-api-spec.json` + a generated C++ header. | no |

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
bodies (see issue #3).

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
  a sorted lookup table.
- **CRC32 over payload** for `animations.a` (count 1): no contiguous byte range
  of the file produces the recorded value.

### Two traps that cost real time

1. `.rdata` and `.data` **share the VA base `0xbf8000`**. A single-base
   VA→file helper silently works for `.data` and produces garbage for `.rdata`.
   Branch on the section. See the section table in
   `docs/re-archive-format.md` when it exists.
2. objdump prints operands as `ds:0x...`, not `DWORD PTR [0x...]`. A regex
   written for the latter matches nothing and looks like a negative result.
   Use plain substring search plus a positive control.

## Conventions for reporting findings

`CONTRIBUTING.md` has the full rules. The short version:

- State **how you measured it**. A finding without its method is not a finding.
- Negative results are valuable. "I searched for X and it is not there" saves
  someone a day — say what search space you covered.
- Commit the **tool**, not its raw output. Do not commit disassembly dumps or
  extracted game bytes.
- Do all address arithmetic as expressions over the PE section table. Never
  hand-computed hex in a script or an issue.