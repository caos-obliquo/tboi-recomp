# The REA toolchain: what is installed, what was broken, and why

This records the reverse-engineering toolchain state, one root cause that cost
real time, and the settings that fix it. Addresses are authoritative in
`binding-surface-findings.md`; this file is about the tools.

## Installed and verified

```
rea-agents            6.7.0     (was 6.4.0; 207 MCP tools, up from 170)
  skill               reverse-engineer-anything v42 (was v37)
  MCP registration    ~/.config/opencode/opencode.json -> mcp.rea
  node                v26.10.0  (supported: 22.x>=22.19, 24.x>=24.11, 26+)
ghidra                12.1.4_PUBLIC
  install             ~/.local/share/rea/ghidra/ghidra_12.1.4_PUBLIC
  analyzeHeadless     present
  native decompiler   os/linux_x86_64/decompile present
  java                21.0.12.1, 64-bit full JDK (Ghidra 12.1.x needs 21+)
kuna                  1.906
  install             ~/.local/bin/kuna   (single self-contained Rust binary)
  sha256              b703d52d211bcd9ff866cce93625533c77…
hopper                not installed — declined at setup (needs sudo + PGP install)
ida                   blocked — commercial IDA Pro, absent, not installable here
```

`rea doctor --provider ghidra --json` reports **`healthy: true`** with every
Ghidra check passing, and independently reports Kuna 1.906 with its digest.

### The four decompiler providers

There are four, not one. Two are usable here.

| provider | env var | state |
|---|---|---|
| ghidra | `GHIDRA_INSTALL_DIR` | available |
| kuna | `REA_KUNA_COMMAND` | available |
| hopper | `HOPPER_LAUNCHER_PATH` | not installed |
| ida | `REA_IDA_MCP_CONFIG` | needs commercial IDA Pro + an upstream IDA MCP server |

Two working decompilers is deliberate, not luck: a single provider's pseudocode
is a reconstruction, and `AGENTS.md` requires that a claim not rest on one
tool's word. When they disagree, that disagreement is the finding.

IDA cannot be enabled honestly. Writing `REA_IDA_MCP_CONFIG` pointing at
nothing would report `configured: true` and then fail at analysis time, which is
worse than an honest `not_configured`. It is left unset until IDA exists.

---

## The root cause behind every Ghidra timeout

Every Ghidra-backed call on `isaac-ng.exe` timed out — `procedure_pseudo_code`,
`procedure_info`, `list_documents`, `binary_overview`. Two measurements
identified the cause.

**Measurement 1: no Java process existed.** While calls were timing out,
`pgrep -c java` returned **0**. If auto-analysis were merely slow, a JVM would
have been running and consuming CPU. Nothing was.

**Measurement 2: the documented deadline.** REA's release notes state:

> Each verified session uses an ephemeral temporary project … REA passes
> `-readOnly`, `-deleteProject` … Operations begin only after default
> auto-analysis completes. The provider startup deadline is **330,000 ms** by
> default … A client can time out earlier even when Ghidra would complete
> within its startup deadline.

Put together: REA imports the 9,362,440-byte PE into a **throwaway Ghidra
project, runs whole-program auto-analysis, answers one query, then deletes the
project**. Every single request repeats the entire import and analysis. There is
no analysis cache to wait on, so waiting longer never helped — the earlier
`pgrep` observation is the proof, not an inference.

The MCP client gave up before REA's own 330 s deadline expired. That is the
"A client can time out earlier" case, and it is why nothing was ever going to
succeed by retrying.

### The fix

Three settings, applied to `mcp.rea.environment`:

```json
{
  "GHIDRA_INSTALL_DIR":           "~/.local/share/rea/ghidra/ghidra_12.1.4_PUBLIC",
  "JAVA_HOME":                    "/usr/lib/jvm/java-21-openjdk",
  "REA_KUNA_COMMAND":             "~/.local/bin/kuna",
  "REA_GHIDRA_STARTUP_TIMEOUT_MS": "900000",
  "GHIDRA_HEADLESS_MAXMEM":        "6G",
  "REA_GHIDRA_SEED_FILE":          "~/.local/share/rea/seeds/isaac-ng-body-seeds.tsv"
}
```

- **`REA_GHIDRA_STARTUP_TIMEOUT_MS=900000`** raises REA's own deadline from
  330 s to 15 min, which is the side that was actually being hit.
- **`GHIDRA_HEADLESS_MAXMEM=6G`** raises the JVM heap from the 2G default. A
  9.3 MB PE with 2G is tight.
- **`REA_GHIDRA_SEED_FILE`** is the real lever. It seeds
  `address<TAB>kind[<TAB>name]` lines so auto-analysis *defines* functions at
  exact addresses instead of discovering them. We already know 441 body
  addresses from `tools/probe_closures.py`, so those are handed to Ghidra
  directly rather than searched for.

The seed file has 441 `function` entries; 69 carry a real recovered identifier
(`Language`, `ToTNT`, `ToPoops`, …) and 372 are the property-accessor bodies
that have no name of their own. It is regenerated from
`/home/caos/.omo/evidence` output and is not committed — generated evidence
lives outside the repository.

Seed digests are part of the analysis profile, and a session **fails to open**
if the applied seeds differ from the committed profile. So the file must be
stable; regenerate it deliberately, not incidentally.

---

## Two mistakes worth recording

**"The upgrade broke Ghidra."** False. `GHIDRA_INSTALL_DIR` was correct in the
MCP registration from the start. The `unavailable` came from running
`npx rea-agents capabilities` in a bare shell, where that variable does not
exist. The MCP path was always fine; the diagnostic was run in the wrong
environment.

**"No polkit agent, so no dialog appears."** Also false, and the user corrected
it by showing a working prompt. What was actually true: `polkit` is installed
and intact (`pacman -Qkk polkit` → 0 altered files), Arch does not ship
`org.freedesktop.policykit.exec.policy` because that action is built into
`polkitd`, and `pkexec` was rejecting the entered password for reasons that
were never diagnosed. The gap was a missing GUI agent such as `polkit-gnome`,
not a missing prompt. Rerunning setup and declining the privileged step was the
correct route and needed no root at all.

The general lesson, which is the repo's own rule applied to myself: a
diagnosis asserted from partial evidence was corrected only because the
evidence was re-measured instead of defended.

## Kuna reaches this PE directly — and that is the headline

The Ghidra pipeline is configured, healthy and demonstrably working, but it is
the wrong tool for bulk work on this file: 607 seconds per body, which puts 441
bodies at roughly 73 hours.

Kuna does the same job in **1.4 seconds per body** — about 430x faster, roughly
10 minutes for the whole corpus.

The important part is *how* it is reached. REA's Kuna provider refuses this
file:

> REA verified the Kuna provider on ELF and thin Mach-O images; pe is not
> admitted.

That is a true statement about REA's *verified* matrix and a false statement
about kuna. Standalone, `kuna decompile isaac-ng.exe 0x897770` works, and
`kuna functions isaac-ng.exe --json` reports 4236 functions from this PE.

**A provider's verified-format matrix is not the tool's capability ceiling.**
Reading the first as the second would have cost the entire binding-surface
recovery, because 441 decompiled bodies are what turned "0 of 441 signatures"
into a known property-accessor ABI (see `binding-surface-findings.md` §14).

| | Ghidra 12.1.4 via REA | kuna 1.906 standalone |
|---|---|---|
| this PE | yes | yes |
| per body | 607 s | 1.4 s |
| 441 bodies | ~73 h | ~10 min |
| boundary attribution | from seed file | says "interior to …" for all 441, i.e. wrong |
| agreement on `0x8c2fb0` | `undefined4 Entity(void) { p = FUN_0085c3e0(&DAT_00c8297a); (**(code**)*p)(0); return 0; }` | `unsigned int sub_8c2fb0(void) { (*(void *)**(unsigned int **)sub_85c3e0(0xc8297a))(0); return 0; }` |

Both decoders agree on that body, which is the cross-check that
`AGENTS.md` asks for. Neither is trusted for addresses: those come from
`tools/probe_closures.py` and `tools/disasm.py` reading the instruction stream.

The corpus is `~/.omo/evidence/pseudocode/kuna-all.c` (441 bodies) with
`kuna-index.jsonl` alongside it. Generated evidence lives outside the repository.

---

## Status

Proven, not configured-and-hoped:

- `rea doctor --provider ghidra --json` -> `healthy: true`, 0 failed checks
- Ghidra returned real pseudocode for 4 bodies in 10m07s with
  `analysis_seeds.entries.function: 441`
- Kuna returned real pseudocode for **441 of 441** bodies, 0 failures
- The accessor ABI recovered from that corpus was re-derived from the
  instruction stream and every IAT slot was resolved out of the PE import
  directory before it was written down

`0xfff0b9d7` is **resolved**: it is a patched-Lua pseudo-index, and the proof
lives in the shipped DLL rather than in the game. `Lua5.3.3r.dll` compares
against its own sentinel `0xfff0b9d8` in Lua's index translation, which stock
Lua 5.3.6 has no equivalent of. See `binding-surface-findings.md` §14.6.

Still open: the type of the value anchored on the current stack frame, and how
the game populates that slot — which lives in the game's own Lua glue.