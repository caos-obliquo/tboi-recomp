#!/usr/bin/env python3
"""Regenerate the REA/Ghidra seed list for the closure bodies.

The seed file tells the decompiler which addresses are the interesting
function bodies, and gives each one a name. It used to be hand-built from the
441 bodies that the *backward* scan could name; the code-derived table
(`tools/probe_class_attribution.py`) knows all 911 bodies, so this tool
regenerates the file from that table.

    tools/gen_body_seeds.py attrib.json --out seeds.tsv

Input: the JSON written by `probe_class_attribution.py --json-out`, a list of
records with `class`, `name`, `kind` and `body` fields.

Output: a TSV with one line per distinct body:

    0x008bb2e0	function	Vector.X

The name is the first (class, member) that uses the body. A body that several
members share is marked with the number of extra users, because a body
address is not an identity: `Vector.X` and `Vector.Y` share `0x008bb2e0` and
differ only in their upvalue.
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path


def load_bodies(records: list[dict]) -> dict[int, dict]:
    """address -> {"users": [(class, name, kind)], "dunder": [names]}"""
    out: dict[int, dict] = {}
    for rec in records:
        body = rec.get("body")
        if not body:
            continue
        addr = int(body, 16) if isinstance(body, str) else int(body)
        entry = out.setdefault(addr, {"users": [], "dunder": []})
        name = rec.get("name") or ""
        cls = rec.get("class") or "?"
        if name.startswith("__"):
            if name not in entry["dunder"]:
                entry["dunder"].append(name)
        else:
            entry["users"].append((cls, name, rec.get("kind") or ""))
    return out


def label(entry: dict) -> str:
    """A stable, readable symbol name for one body."""
    users = sorted(set(entry["users"]))
    if users:
        cls, name, _kind = users[0]
        base = f"{cls}.{name}"
        if len(users) > 1:
            base += f"+{len(users) - 1}"
        return base
    dunder = sorted(entry["dunder"])
    if dunder:
        base = dunder[0]
        if len(dunder) > 1:
            base += f"+{len(dunder) - 1}"
        return base
    return ""


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("attrib", help="JSON from probe_class_attribution.py --json-out")
    ap.add_argument("--out", help="output TSV (default: stdout)")
    ap.add_argument("--extra", action="append", default=[],
                    help="additional seed file to merge in (address<TAB>kind<TAB>name)")
    args = ap.parse_args(argv)

    records = json.loads(Path(args.attrib).read_text())
    bodies = load_bodies(records)

    # Merge an older seed file so a body that the new table cannot attribute
    # keeps its hand-written name.
    extra_names: dict[int, str] = {}
    for path in args.extra:
        for line in Path(path).read_text().splitlines():
            if not line.strip():
                continue
            parts = line.split("\t")
            try:
                addr = int(parts[0], 16)
            except ValueError:
                continue
            if len(parts) > 2 and parts[2]:
                extra_names[addr] = parts[2]

    lines = []
    for addr in sorted(bodies):
        name = label(bodies[addr]) or extra_names.get(addr, "")
        if not name:
            name = f"anon_{addr:08x}"
        lines.append(f"0x{addr:08x}\tfunction\t{name}")

    text = "\n".join(lines) + "\n"
    if args.out:
        Path(args.out).write_text(text)
        print(f"wrote {len(lines)} seeds to {args.out}")
    else:
        print(text, end="")

    # Report what the new table could not name, so the gap stays visible.
    anon = [a for a in bodies if not label(bodies[a])]
    users = collections.Counter(len(set(v["users"])) for v in bodies.values())
    print(f"bodies={len(bodies)} unnamed={len(anon)}")
    print("users per body: " + ", ".join(f"{k}:{v}" for k, v in sorted(users.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
