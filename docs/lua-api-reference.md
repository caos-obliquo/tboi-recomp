# The Lua API surface: consultation notes, graded

Scratch pad for anything learned about the Repentance+ Lua API from sources
other than this repository's own binary. Everything in here is a clue to
verify, never an answer to implement. A row marked `unverified` has not been
checked against our binary in this session and must not be used for
implementation, tests, or progress claims.

**Nothing in this file authorizes implementation.** The Progress table in
`README.md` stays honest by command, not by notes.

## What this is not

- Not a copy of any upstream document. Upstream texts (TeamREPENTOGON `.zhl`
  files, IsaacDocs pages, Filloax's autocomplete) are paraphrased and
  cross-checked; their text is never pasted here.
- Not an authoritative address table. Measured addresses live in
  [`binding-surface-findings.md`](binding-surface-findings.md) (§21 cross-check,
  and the per-class attribution sections). Nothing here may contradict §21 —
  and if it does, §21 wins.
- Not a licence grant. REPENTOGON (GPLv2) and IsaacDocs (community
  documentation) are consulted, not vendored. See
  [`reimplementation-references.md`](reimplementation-references.md).

## Provenance grades

| grade | meaning |
|---|---|
| `own-repeat-verified` | Re-derived here from the binary in this or a prior session, with a command that reproduces it. |
| `oracle-agreed` | An external oracle agrees with our own measurement. We still say "oracle agreed" — agreement is not authorship. |
| `oracle-disagreed` | The oracle and our measurement conflict. Reported, never averaged. Use our measurement. |
| `unverified` | Not checked against our binary. Do not implement. |
| `rejected` | Measured wrong here, or legally unusable, or superseded by a later finding. Kept visible as a retraction. |

## Environment of the measurements

- Target binary: `/home/caos/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth/isaac-ng.exe`, Repentance+ v1.9.7.17.J460, 9,362,440 bytes, sha256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`. This is the licensed Steam retail binary the project reads.
- REPENTOGON clone: `TeamREPENTOGON/REPENTOGON`, single-commit checkout, `version.txt` = **1.1.3**, `.zhl` tree `libzhl/functions/` with 244 files.
- Scan method: mask-aware preamble match of each `.zhl` signature line (`?` nibbles = wildcard bytes) against the raw file bytes. Hits are **file offsets**, not VAs — no virtual addresses appear anywhere in this file on purpose. A VA claim requires map-file work, which is out of scope here.

Sha256 of the `.zhl` files read directly:

| file | sha256 (first 16) |
|---|---|
| `Vector2.zhl` | `148eb85018146524` |
| `PosVel.zhl` | `c2558219bb3ec334` |
| `Game.zhl` | `bda2b0eec4fc534d` |
| `EntityPlayer.zhl` | `cba92f01bf5f6ba6` |
| `Global.zhl` | `96b8f35c7eb8325a` |

## Findings

| topic | what the oracle says | this binary says | grade |
|---|---|---|---|
| `Vector` layout | `float x : 0x0, float y : 0x4`, size `0x8` | identical; §21 of binding-surface-findings.md records the same; sizes match our impl | `oracle-agreed` |
| `PosVel` layout | `pos : 0x0`, `vel : 0x8`, size `0x10` | identical; §21; our PosVel is two Vectors, 16 bytes | `oracle-agreed` |
| `Vector::Lerp` | one byte-pattern preamble `558bec8b45??f30f1060`, MSVC `__thiscall Vector::Lerp(Vector* other, float t)` | **NOT FOUND** in this binary (scan returned zero hits for the preamble) | `oracle-disagreed` |
| `Game` preambles | 56 parseable signature lines (of 57 in the file) | 40 of 56 match by masked scan | `unverified` (per-entry) |
| `EntityPlayer` preambles | 193 signature lines | 130 of 193 match | `unverified` (per-entry) |
| `Global` preambles | 10 parseable signature lines (one `\x55…`-escaped entry excluded from scanning) | 4 match | `unverified` (per-entry) |
| IsaacDocs (moddingofisaac.com) | e.g. `Vector(float x, float y)` constructor; Vector arithmetic documented member-by-member | consulted only; member behaviours not re-derived here | `unverified` |
| Filloax autocomplete | EmmyLua annotations for LuaLS; Vector number×vector operators unsupported | not consulted for any measurement here | `unverified` |

### Vector::Lerp disagreement, restated precisely

The scan looked for the masked preamble `55 8b ec 8b 45 ?? f3 0f 10 60` and found zero matches in our `isaac-ng.exe`. Possible causes, in order of likelihood: (1) the oracle's `.zhl` was cut against a different build — TeamREPENTOGON targets the modded Repentance+ line, not necessarily retail `1.9.7.17.J460`; (2) `Vector::Lerp` is inlined at every call site in our build, so no stand-alone preamble exists; (3) the bytes fall on a section boundary the scan tolerated incorrectly (unlikely — the scanner reports matches, it does not hide them). None of these is actionable here. The correct conclusion is the grade: the oracle's claim is **not** usable for our build.

### Why so many `unverified` rows

The 40/56 `Game` and 130/193 `EntityPlayer` masked matches are **coarse hits**: any byte sequence with those wildcards is a possible candidate, and a match says only "this pattern occurs". A preamble hit does not establish calling convention, argument semantics, or even that the site is `Game::<sig topic>`. Promoting one of those hits to a name requires call-site argument inspection — the ordinary §21 discipline — and that is a separate unit of work with its own issue.

## Commands that reproduce the file-offset scan

```sh
# exe must be 9,362,440 bytes, sha256 3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b
python3 - <<'EOF'
import hashlib, re
p = "/home/caos/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth/isaac-ng.exe"
d = open(p, "rb").read()
print(len(d), hashlib.sha256(d).hexdigest())
EOF

# scan(preamble_regex, exe_bytes) -> list of file offsets; see /tmp/opencode/zhl_scan.py
# (throwaway; never commit it)
```

Any result not reproducible by the commands above is not a finding of this project; it is a rumour.

## Standing split reminder

Half 1 of The Browsing of Isaac (chiikabu's own platform/glue, GPLv3) is
adaptable with attribution under this project's licence; half 2 (the lifted
game, `./native/decomp/*_pure_helpers.cpp`, `boot.wasm`, their `decomp/*.json`)
is never usable. REPENTOGON and IsaacDocs are one step further removed —
consulted, graded, never copied.
