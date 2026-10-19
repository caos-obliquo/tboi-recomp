#!/usr/bin/env python3
"""Decompile the recovered closure bodies with kuna, in small flushed batches.

The first attempt at this ran all 441 bodies and buffered the composed file, so
the abort at 400/441 left a 0-byte output. This version writes each body the
moment it is produced and records an append-only index, so an abort costs at
most one batch and a rerun resumes.

kuna is driven standalone rather than through the REA MCP server: REA's Kuna
provider refuses this target with `target_format_unsupported` ("REA verified the
Kuna provider on ELF and thin Mach-O images; pe is not admitted"), which is a
statement about REA's verified matrix, not about kuna's capability. Standalone
kuna loads this PE and reports 4236 functions.
"""
from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

EXE = "/home/caos/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth/isaac-ng.exe"
SEEDS = Path("/home/caos/.local/share/rea/seeds/isaac-ng-body-seeds.tsv")
OUTDIR = Path("/home/caos/.omo/evidence/pseudocode")
OUT = OUTDIR / "kuna-all.c"
INDEX = OUTDIR / "kuna-index.jsonl"


def load_bodies() -> list[tuple[int, str, str]]:
    """(address, symbol-kind, name) for every seeded body, in seed order."""
    out: list[tuple[int, str, str]] = []
    for line in SEEDS.read_text().splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        addr = int(parts[0], 16)
        kind = parts[1] if len(parts) > 1 else "function"
        name = parts[2] if len(parts) > 2 else ""
        out.append((addr, kind, name))
    return out


def load_done() -> set[int]:
    if not INDEX.exists():
        return set()
    done: set[int] = set()
    for line in INDEX.read_text().splitlines():
        if line.strip():
            done.add(json.loads(line)["address"])
    return done


def decompile(addr: int) -> tuple[str, bool, str]:
    """Return (text, ok, note). kuna's note line is separated from the code."""
    proc = subprocess.run(
        ["kuna", "decompile", EXE, hex(addr)],
        capture_output=True,
        text=True,
        timeout=180,
    )
    blob = (proc.stdout or "") + (proc.stderr or "")
    body_lines, note_lines = [], []
    for line in blob.splitlines():
        if line.startswith("note:"):
            note_lines.append(line.strip())
        elif line.startswith("kuna decompile") or line.startswith("head -"):
            continue
        else:
            body_lines.append(line)
    body = "\n".join(body_lines).strip()
    return body, proc.returncode == 0 and bool(body), " ".join(note_lines)


def main() -> int:
    batch_size = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    bodies = load_bodies()
    done = load_done()
    todo = [b for b in bodies if b[0] not in done]
    OUTDIR.mkdir(parents=True, exist_ok=True)

    print(f"bodies={len(bodies)} already_done={len(done)} todo={len(todo)} batch={batch_size}")
    if not todo:
        print("nothing to do")
        return 0

    started = time.time()
    failed: list[int] = []
    with OUT.open("a") as out, INDEX.open("a") as idx:
        for i in range(0, len(todo), batch_size):
            batch = todo[i : i + batch_size]
            ok_in_batch = 0
            for addr, kind, name in batch:
                body, ok, note = decompile(addr)
                if not ok:
                    failed.append(addr)
                    print(f"  FAIL 0x{addr:08x}", flush=True)
                    continue
                out.write(f"\n/* 0x{addr:08x}  {name or '(anonymous)'}  [{kind}] */\n")
                out.write(f"/* kuna note: {note} */\n" if note else "")
                out.write(body + "\n")
                out.flush()  # cost of one flush per body is negligible at 1.4s each
                idx.write(
                    json.dumps({"address": addr, "name": name, "kind": kind, "note": note}) + "\n"
                )
                idx.flush()
                ok_in_batch += 1
            print(
                f"batch {i // batch_size + 1}: {ok_in_batch}/{len(batch)} ok "
                f"({len(failed)} failed total, {time.time() - started:.0f}s elapsed, "
                f"{INDEX.stat().st_size} bytes index)",
                flush=True,
            )

    print(f"done: failed={len(failed)} {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())