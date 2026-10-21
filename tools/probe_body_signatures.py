#!/usr/bin/env python3
"""Instruction-level signature census over Lua closure bodies.

Counts, per body, the Lua entry points it touches and the pseudo-indices it
uses.  This is the measurement behind the shape taxonomy in
``docs/binding-surface-findings.md`` §14.3 and the reproduction attempt in
§15.10.

Two corpora are measured with the same code:

* the kuna seed file (441 bodies) -- the corpus the document's numbers were
  written against;
* every distinct body recovered by ``tools/probe_property_names.py`` (911).

Extent rule (MSVC layout): a function starts at ``push ebp`` and ends where
the next function starts, i.e. at a ``push ebp`` whose predecessor is a
terminator (``ret`` / ``int3`` / ``jmp``).  Blocks after a tail ``jmp`` are
contiguous in ``.text`` and belong to the same function -- stopping at the
first ``jmp`` truncates the body and drops most pseudo-index uses, which is
why an earlier run of this census reported 29 of 441 instead of 392.

Usage::

    tools/probe_body_signatures.py /path/to/isaac-ng.exe --cache disasm.txt
    tools/probe_body_signatures.py /path/to/isaac-ng.exe \\
        --bodies props.json --json-out sigs.json
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from disasm import Disasm  # noqa: E402
from pe_vaddr import PEImage  # noqa: E402
from probe_iat import parse_imports  # noqa: E402

DEFAULT_SEEDS = Path.home() / ".local/share/rea/seeds/isaac-ng-body-seeds.tsv"

# The two constants that dominate the accessor bodies (see §14.6).
PSEUDO_INDEXES = (0xFFF0B9D7, 0xFFF0B9D8)


def load_seed_bodies(path: Path) -> list[int]:
    """Body addresses from a kuna seed file: ``0xADDR<TAB>kind<TAB>label``."""
    out = []
    for line in path.read_text().strip().splitlines():
        if not line.strip():
            continue
        out.append(int(line.split("\t")[0], 16))
    return out


def load_json_bodies(path: Path) -> list[int]:
    """Distinct body addresses from a probe_property_names.py JSON dump."""
    data = json.loads(path.read_text())
    records = data["records"] if isinstance(data, dict) and "records" in data else data
    return sorted({r["body"] for r in records if r.get("body")})


class Census:
    """The census over one corpus."""

    def __init__(self, exe: str, cache: str | None) -> None:
        self.image = PEImage(exe)
        self.disasm = Disasm(exe, cache)
        self.slot2name = {va: name for _, name, va in parse_imports(self.image)}
        self.slot_va = {name: va for va, name in self.slot2name.items()}
        self.checks = tuple(n for n in self.slot2name.values()
                            if n.startswith("luaL_check") or n.startswith("luaL_opt"))

    def body_insns(self, va: int, limit: int = 600) -> list:
        """Instructions of the function starting at ``va``, by the extent rule."""
        out: list = []
        prev = None
        for insn in self.disasm.forward(va):
            if (insn.mnemonic == "push" and insn.operands.strip() == "ebp"
                    and prev is not None and prev.is_terminator):
                break
            out.append(insn)
            prev = insn
            if len(out) > limit:
                break
        return out

    def called_import(self, insn) -> str | None:
        """The imported function an instruction calls, if it is an IAT call."""
        if insn.mnemonic != "call":
            return None
        for va, name in self.slot2name.items():
            if insn.calls_slot(va):
                return name
        return None

    def count(self, bodies: list[int]) -> Counter:
        c: Counter = Counter()
        for body in bodies:
            insns = self.body_insns(body)
            calls = []
            for insn in insns:
                name = self.called_import(insn)
                if name is not None:
                    calls.append(name)
            operands = " ".join(i.operands for i in insns)
            c["bodies"] += 1
            c["instructions"] += len(insns)
            if "lua_type" in calls:
                c["calls_lua_type"] += 1
            if calls and calls[0] == "lua_type":
                c["first_lua_call_is_lua_type"] += 1
            if calls and calls[0] in ("lua_type", "lua_touserdata"):
                c["first_lua_call_is_a_type_check"] += 1
            if "0xfff0b9d7" in operands:
                c["uses_pseudo_d7"] += 1
            if "0xfff0b9d8" in operands:
                c["uses_pseudo_d8"] += 1
            if any(name in calls for name in self.checks):
                c["uses_luaL_check"] += 1
            if any(i.mnemonic == "ret" for i in insns):
                c["has_ret"] += 1
        return c


def report(label: str, c: Counter) -> None:
    print(f"== {label}")
    for key in ("bodies", "instructions", "calls_lua_type",
                "first_lua_call_is_lua_type", "first_lua_call_is_a_type_check",
                "uses_pseudo_d7", "uses_pseudo_d8", "uses_luaL_check", "has_ret"):
        print(f"   {key:32s} {c[key]}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("--cache", help="objdump text cache for the binary")
    ap.add_argument("--seeds", default=str(DEFAULT_SEEDS),
                    help="kuna seed file (default: %(default)s)")
    ap.add_argument("--bodies", help="JSON dump from probe_property_names.py; "
                                     "when given, the seed file is not read")
    ap.add_argument("--json-out", help="write the counts as JSON")
    a = ap.parse_args(argv)

    census = Census(a.exe, a.cache)
    out: dict = {}
    if a.bodies:
        bodies = load_json_bodies(Path(a.bodies))
        report(f"{len(bodies)} bodies from {a.bodies}", census.count(bodies))
        out["bodies"] = census.count(bodies)
    else:
        seeds = load_seed_bodies(Path(a.seeds))
        report(f"{len(seeds)} seed bodies", census.count(seeds))
        out["seeds"] = census.count(seeds)

    if a.json_out:
        Path(a.json_out).write_text(json.dumps(out, indent=2, default=dict))
        print(f"wrote {a.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
