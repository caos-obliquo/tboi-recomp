#!/usr/bin/env python3
"""Decompile the recovered closure bodies in small, flushed, resumable batches.

Why this exists, because the first attempt at this work lost all of it:

  A previous sweep ran the decompiler once per address and composed the output
  file at the end. It reached 400 of 441 bodies, 400 successful, 0 failures --
  and was aborted. The output file was 0 bytes. Every decompilation had already
  been paid for and thrown away, and the run had also invoked the decompiler a
  second time per address while composing, so it did twice the work for one
  result.

  Nothing about that failure was visible from the process. It looked like it was
  working right up to the abort.

So three properties are enforced here rather than hoped for:

  * results are written as they are produced, never accumulated
  * an append-only index records what is done, and is the resume point
  * the batch size is an argument, so an abort costs one batch

The index is also how completeness is checked. This script counts bodies and
reports failures; it does not decide that a corpus is correct. The caller
asserts the address set against its input.

Why kuna standalone rather than through the REA MCP server: REA's Kuna provider
refuses PE targets with `target_format_unsupported`, which is a statement about
REA's verified format matrix and not about kuna. See issue #14 and
docs/binding-surface-findings.md section 14.1.

Usage:
    decompile_corpus.py --seeds SEEDS.tsv --out CORPUS.c [--batch 25]
                        [--exe PATH] [--index PATH]

The seeds file is `address<TAB>kind[<TAB>name]` per line. The index is JSON
lines, one record per completed body. Re-running resumes.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_EXE = (
    "/home/caos/.local/share/Steam/steamapps/common/"
    "The Binding of Isaac Rebirth/isaac-ng.exe"
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--seeds", type=Path, required=True,
                   help="TSV of address<TAB>kind[<TAB>name]")
    p.add_argument("--out", type=Path, required=True,
                   help="output corpus, appended to")
    p.add_argument("--index", type=Path, default=None,
                   help="append-only JSONL index (default: <out>.index.jsonl)")
    p.add_argument("--exe", type=Path, default=Path(DEFAULT_EXE),
                   help="target binary")
    p.add_argument("--decompiler", default="kuna",
                   help="decompiler binary on PATH")
    p.add_argument("--batch", type=int, default=25,
                   help="bodies per batch; an abort costs at most this many")
    p.add_argument("--timeout", type=int, default=180,
                   help="per-body timeout in seconds")
    p.add_argument("--limit", type=int, default=0,
                   help="stop after this many bodies (0 = no limit)")
    return p.parse_args(argv)


def load_bodies(seeds: Path) -> list[tuple[int, str, str]]:
    """(address, kind, name) for every seed line, in file order."""
    bodies: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(seeds.read_text().splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split("\t")
        try:
            addr = int(parts[0], 16)
        except ValueError as exc:
            raise ValueError(f"{seeds}:{lineno}: bad address {parts[0]!r}") from exc
        kind = parts[1] if len(parts) > 1 else "function"
        name = parts[2] if len(parts) > 2 else ""
        bodies.append((addr, kind, name))
    return bodies


def load_done(index: Path) -> set[int]:
    """Addresses already recorded. A malformed line is fatal, not skipped."""
    if not index.exists():
        return set()
    done: set[int] = set()
    for lineno, line in enumerate(index.read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            done.add(int(json.loads(line)["address"]))
        except (json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
            raise ValueError(f"{index}:{lineno}: corrupt index line") from exc
    return done


def decompile(args: argparse.Namespace, addr: int) -> tuple[str, bool, str]:
    """(text, ok, note). The note line is separated from the code."""
    proc = subprocess.run(
        [args.decompiler, "decompile", str(args.exe), hex(addr)],
        capture_output=True, text=True, timeout=args.timeout,
    )
    body_lines: list[str] = []
    notes: list[str] = []
    for line in ((proc.stdout or "") + (proc.stderr or "")).splitlines():
        if line.startswith("note:"):
            notes.append(line.strip())
        elif line.startswith(args.decompiler) or line.startswith("head -"):
            continue  # timing footer, not output
        else:
            body_lines.append(line)
    body = "\n".join(body_lines).strip()
    return body, proc.returncode == 0 and bool(body), " ".join(notes)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.batch < 1:
        print("--batch must be at least 1", file=sys.stderr)
        return 2
    index = args.index or args.out.with_suffix(args.out.suffix + ".index.jsonl")

    bodies = load_bodies(args.seeds)
    done = load_done(index)
    todo = [b for b in bodies if b[0] not in done]
    if args.limit:
        todo = todo[: args.limit]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    print(f"bodies={len(bodies)} already_done={len(done)} todo={len(todo)} "
          f"batch={args.batch}")

    started = time.time()
    failed: list[int] = []
    with args.out.open("a") as out, index.open("a") as idx:
        for i in range(0, len(todo), args.batch):
            batch = todo[i: i + args.batch]
            ok = 0
            for addr, kind, name in batch:
                body, good, note = decompile(args, addr)
                if not good:
                    failed.append(addr)
                    print(f"  FAIL 0x{addr:08x}", flush=True)
                    continue
                out.write(f"\n/* 0x{addr:08x}  {name or '(anonymous)'}  [{kind}] */\n")
                if note:
                    out.write(f"/* kuna note: {note} */\n")
                out.write(body + "\n")
                out.flush()   # the point of the whole exercise
                idx.write(json.dumps({"address": addr, "name": name,
                                      "kind": kind, "note": note}) + "\n")
                idx.flush()
                ok += 1
            print(f"batch {i // args.batch + 1}: {ok}/{len(batch)} ok "
                  f"({len(failed)} failed, {time.time() - started:.0f}s elapsed)",
                  flush=True)

    print(f"done: {len(done) + len(todo) - len(failed)} recorded this session, "
          f"{len(failed)} failed {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))