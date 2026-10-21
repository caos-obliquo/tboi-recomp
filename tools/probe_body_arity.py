#!/usr/bin/env python3
"""Argument count and return shape of every recovered Lua closure body.

Phase 4 of the 655-bodies plan: the *shape* of a body, i.e. how many visible
arguments it reads and how many values it returns to Lua.  Together with the
receiver convention this is the part of a native signature that the binary
itself records, so it can be measured rather than guessed.

Two conventions are established by the disassembly (see
``docs/binding-surface-findings.md`` §17):

* the receiver is the userdata at stack index 1, and the game's own accessors
  reach it through the pseudo-index ``0xfff0b9d7``;
* a visible argument *n* is stack index ``n + 1``, so the visible argument
  count is ``max(index) - 1``.

A body's argument reads come from two places:

* a direct Lua API call -- ``lua_type(L, idx)``, ``luaL_checknumber(L, idx)``
  and friends, where the index is the second argument, and
* a game helper that takes the index in ``edx`` and forwards it to a Lua API
  (``0x8baa50`` is ``lua_type`` with the index in ``edx``; ``0x85c590`` is the
  game's "check this argument" helper).  Those helpers are *learned* here: a
  helper counts as an index helper when its own body forwards a register
  loaded from ``edx`` into a Lua index API.

The measured value is a lower bound when a body reads an argument through a
helper the tool has not learned, so the report states how the count was
reached instead of presenting it as exact.

Usage::

    tools/probe_body_arity.py /path/to/isaac-ng.exe --cache disasm.txt
    tools/probe_body_arity.py /path/to/isaac-ng.exe --cache disasm.txt \\
        --json-out arity.json --self-test
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

# The receiver, reached through the game's pseudo-index instead of index 1.
RECEIVER_PSEUDO = 0xFFF0B9D7
# A table in the game's own registry, not a Lua stack index.
TABLE_PSEUDO = 0xFFF0B9D8

# Lua APIs whose *second* argument is a stack index.  The C prototypes are
# ``int f(lua_State *L, int idx)``, so the index is the second-to-last push
# before the call (cdecl pushes right to left: the last push is ``L``).
INDEX_APIS = frozenset({
    "lua_absindex", "lua_getfield", "lua_getmetatable", "lua_gettable",
    "lua_isuserdata", "lua_len", "lua_objlen", "lua_rawget", "lua_rawgeti",
    "lua_rawgetp", "lua_setfield", "lua_settable", "lua_setmetatable",
    "lua_toboolean", "lua_tointeger", "lua_tolstring", "lua_tonumber",
    "lua_tostring", "lua_touserdata", "lua_type", "luaL_checkany",
    "luaL_checkinteger", "luaL_checklstring", "luaL_checknumber",
    "luaL_checkoption", "luaL_checkudata", "luaL_tolstring",
})

# An address that exists nowhere in the binary: the negative control.
CANARY_BODY = 0x00BFFFFF


def load_seed_bodies(path: Path) -> list[int]:
    """Body addresses from a kuna seed file: ``0xADDR<TAB>kind<TAB>label``."""
    out: list[int] = []
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


class Arity:
    """The shape census over one corpus of closure bodies."""

    def __init__(self, exe: str, cache: str | None) -> None:
        self.image = PEImage(exe)
        self.d = Disasm(exe, cache)
        self.slot2name = {va: name for _, name, va in parse_imports(self.image)}
        # Cache of "does this helper take a stack index in edx".
        self._index_helper: dict[int, bool] = {}
        # Cache of "what is the highest stack index this helper reads".
        self._helper_max: dict[int, int | None] = {}

    # -- instruction helpers ---------------------------------------------

    def body_insns(self, va: int, limit: int = 600) -> list:
        """The instructions of the closure body at ``va``.

        Same extent rule as ``tools/probe_body_signatures.py``: a function
        starts at ``push ebp`` and ends where the next one starts.  A body
        that hits the limit is reported as truncated instead of silently
        shortening the measurement.
        """
        out: list = []
        prev = None
        for insn in self.d.forward(va):
            if (insn.mnemonic == "push" and insn.operands.strip() == "ebp"
                    and prev is not None and prev.is_terminator):
                break
            out.append(insn)
            prev = insn
            if len(out) > limit:
                break
        return out

    def helper_insns(self, va: int, limit: int = 400) -> list:
        """The instructions of the helper at ``va``, up to its first ``ret``."""
        out: list = []
        for insn in self.d.forward(va):
            out.append(insn)
            if insn.mnemonic == "ret":
                break
            if len(out) > limit:
                break
        return out

    def called_import(self, insn) -> str | None:
        """The imported function an instruction calls, if any."""
        if insn.mnemonic != "call":
            return None
        for va, name in self.slot2name.items():
            if insn.calls_slot(va):
                return name
        return None

    @staticmethod
    def direct_target(insn) -> int | None:
        """The address of a direct ``call 0xADDR``, else None."""
        if insn.mnemonic != "call":
            return None
        ops = insn.operands.strip()
        if not ops.startswith("0x"):
            return None
        try:
            return int(ops, 16)
        except ValueError:
            return None

    def is_index_helper(self, target: int) -> bool:
        """True when the helper forwards an index taken from ``edx``.

        The pattern, verified by hand on ``0x8baa50`` and ``0x85c590``:

            mov  esi,edx        ; the index
            mov  edi,ecx        ; the Lua state
            push esi
            push edi
            call lua_type

        so the caller's ``mov edx, N`` is the stack index the helper reads.
        """
        cached = self._index_helper.get(target)
        if cached is not None:
            return cached
        result = False
        edx_regs: set[str] = set()
        pending: list = []
        for insn in self.helper_insns(target):
            m = insn.mnemonic
            if m == "mov" and insn.operands.strip().startswith("edx,"):
                pass  # a write to edx does not create a reader
            if m == "mov" and "," in insn.operands:
                dst, src = (insn.operands.replace(" ", "").split(",", 1))
                if src == "edx" and dst in ("eax", "ebx", "ecx", "edx",
                                            "esi", "edi", "ebp"):
                    edx_regs.add(dst)
            if m == "push":
                pending.append(insn)
                continue
            if m == "call":
                name = self.called_import(insn)
                if name in INDEX_APIS and len(pending) >= 2:
                    idx = pending[-2].operands.strip()
                    if idx in edx_regs:
                        result = True
                        break
                pending = []
                continue
            if m in ("ret", "int3"):
                break
        self._index_helper[target] = result
        return result

    def helper_max_index(self, target: int, depth: int = 0) -> int | None:
        """The highest constant stack index read inside a helper.

        A body often does not touch the Lua stack itself: it calls a helper
        that does.  ``0x8ba890`` (used by ``Vector:Clamped``) reads
        ``luaL_checknumber(L,5)``, ``(L,4)``, ``(L,3)`` and ``(L,2)``, so the
        body can see four arguments even though it passes only ``L``.

        The answer is therefore a *bound* on the number of arguments the
        body can see, not a count of the arguments it uses.  The recursion
        stops at depth 3 and at helpers longer than 200 instructions, so a
        call into the engine cannot drag in an unrelated graph.
        """
        if target in self._helper_max:
            return self._helper_max[target]
        if depth > 2:
            return None
        insns = self.helper_insns(target)
        if len(insns) > 200:
            self._helper_max[target] = None
            return None
        best: int | None = None
        pending: list = []
        for insn in insns:
            m = insn.mnemonic
            if m == "push":
                pending.append(insn)
                continue
            if m == "call":
                name = self.called_import(insn)
                if name in INDEX_APIS and len(pending) >= 2:
                    imm = pending[-2].immediate()
                    if imm is not None and 2 <= imm <= 16:
                        best = imm if best is None else max(best, imm)
                elif name is None:
                    sub = self.direct_target(insn)
                    if sub is not None and sub != target:
                        nested = self.helper_max_index(sub, depth + 1)
                        if nested is not None:
                            best = nested if best is None else max(best, nested)
                pending = []
                continue
            if m in ("ret", "int3"):
                break
        self._helper_max[target] = best
        return best

    # -- the measurement -------------------------------------------------

    def analyze(self, body: int) -> dict:
        """The shape of one body."""
        insns = self.body_insns(body)
        pending: list = []
        edx_val: int | None = None
        indices: set[int] = set()
        helper_bounds: set[int] = set()
        helpers: set[int] = set()
        returns: set = set()
        eax_val: object = None
        truncated = len(insns) > 600

        for insn in insns:
            m = insn.mnemonic
            ops = insn.operands.replace(" ", "")
            if m == "push":
                pending.append(insn)
            elif m == "mov" and "," in insn.operands:
                dst, src = insn.operands.replace(" ", "").split(",", 1)
                if dst == "edx":
                    edx_val = insn.immediate()
                elif dst == "eax":
                    imm = insn.immediate()
                    eax_val = imm if imm is not None else "unknown"
            elif m == "xor" and ops == "eax,eax":
                eax_val = 0
            elif m == "call":
                name = self.called_import(insn)
                if name in INDEX_APIS and len(pending) >= 2:
                    imm = pending[-2].immediate()
                    if imm is not None:
                        indices.add(imm)
                elif name is None:
                    target = self.direct_target(insn)
                    if target is not None and self.is_index_helper(target):
                        helpers.add(target)
                        if edx_val is not None:
                            indices.add(edx_val)
                    elif target is not None:
                        # A helper that reads the stack itself: the body can
                        # see the arguments the helper reads.
                        bound = self.helper_max_index(target)
                        if bound is not None:
                            helper_bounds.add(bound)
                            helpers.add(target)
                pending = []
            elif m == "ret":
                returns.add(eax_val)
                eax_val = "unknown"
            if m in ("ret", "int3"):
                pending = []

        visible = sorted(i for i in indices if 2 <= i <= 16)
        bounds = sorted(i for i in helper_bounds if 2 <= i <= 16)
        # The number of arguments the body can *see*.  This is a bound: the
        # body may ignore the arguments it reads, and the game may pass
        # arguments the body never looks at.
        seen = sorted(set(visible) | set(bounds))
        argc = (max(seen) - 1) if seen else 0
        # The receiver: index 1 or the pseudo-index.
        receiver = (1 in indices) or (RECEIVER_PSEUDO in indices)

        if returns == {0}:
            rets = 0
        elif returns == {1}:
            rets = 1
        else:
            rets = None

        if visible and bounds:
            evidence = "index+helper"
        elif visible:
            evidence = "index"
        elif bounds:
            evidence = "helper"
        elif receiver:
            evidence = "receiver-only"
        else:
            evidence = "none"

        return {
            "body": body,
            "argc": argc,
            "argc_bound": True,
            "max_stack_index": (max(seen) if seen else None),
            "argc_evidence": evidence,
            "reads_receiver": receiver,
            "returns": rets,
            "return_evidence": ("constant" if len(returns) == 1
                                else "multiple" if returns else "none"),
            "indexes_seen": sorted(indices),
            "helper_bounds": bounds,
            "index_helpers": sorted(helpers),
            "truncated": truncated,
        }

    def census(self, bodies: list[int]) -> tuple[list[dict], Counter]:
        rows = []
        c: Counter = Counter()
        for body in bodies:
            row = self.analyze(body)
            rows.append(row)
            c["bodies"] += 1
            c[f"argc_{row['argc']}"] += 1
            if row["returns"] is None:
                c["returns_unknown"] += 1
            else:
                c[f"returns_{row['returns']}"] += 1
            if row["truncated"]:
                c["truncated"] += 1
            if row["argc_evidence"] == "none":
                c["argc_without_evidence"] += 1
        return rows, c


def report(label: str, rows: list[dict], c: Counter) -> None:
    print(f"== {label}")
    for key in sorted(c):
        if key == "bodies":
            continue
        print(f"   {key:28s} {c[key]}")
    print(f"   {'bodies':28s} {c['bodies']}")


def self_test(exe: str, cache: str | None) -> int:
    """Positive and negative controls.  Non-zero exit means the tool is wrong."""
    a = Arity(exe, cache)
    failures = 0

    def fail(msg: str) -> None:
        nonlocal failures
        failures += 1
        print(f"FAIL: {msg}", file=sys.stderr)

    # Positive control 1: the Vector property pair.  Read by hand out of the
    # disassembly: 0x8bb2e0 is the reader (returns one value, reads no
    # argument), 0x8bb340 is the writer (reads argument 1, returns nothing).
    cases = {
        0x008BB2E0: (0, 1, "Vector.X getter"),
        0x008BB340: (1, 0, "Vector.X setter"),
        0x008A8270: (1, 1, "Vector.Cross"),
        0x008A8430: (1, 1, "Vector.Resized"),
    }
    for body, (argc, rets, what) in cases.items():
        row = a.analyze(body)
        print(f"control {what:18s} body 0x{body:08x}: argc={row['argc']} "
              f"returns={row['returns']} evidence={row['argc_evidence']}")
        if row["argc"] != argc:
            fail(f"{what}: argc {row['argc']} != {argc}")
        if row["returns"] != rets:
            fail(f"{what}: returns {row['returns']} != {rets}")

    # Positive control 2: the index helper must be recognised, otherwise the
    # argument of Vector.Cross (read through 0x8baa50) would be invisible.
    if not a.is_index_helper(0x008BAA50):
        fail("0x8baa50 was not recognised as an index helper")
    if a.is_index_helper(0x0085D040):
        fail("0x85d040 was wrongly recognised as an index helper")

    # Positive control 3: a body whose arguments are read by a helper.
    # Vector:Clamped (0x008a8680) passes only L to 0x8ba890, and that helper
    # reads luaL_checknumber(L,5..2): the body can see four arguments, so
    # the bound is 4 even though the body itself reads no index.
    row = a.analyze(0x008A8680)
    print(f"control {'Vector.Clamped':18s} body 0x008a8680: "
          f"argc={row['argc']} evidence={row['argc_evidence']} "
          f"helper_bounds={row['helper_bounds']}")
    if row["argc"] != 4:
        fail(f"Vector.Clamped: helper bound {row['argc']} != 4")
    if row["argc_evidence"] != "helper":
        fail(f"Vector.Clamped: evidence {row['argc_evidence']!r} != 'helper'")

    # Negative control: an address that is not an instruction boundary.
    row = a.analyze(CANARY_BODY)
    if row["argc"] != 0 or row["returns"] is not None:
        fail("the canary body produced a shape")
    print(f"control canary 0x{CANARY_BODY:08x}: no shape reported")

    print("SELF-TEST PASSED" if not failures else f"{failures} CONTROL FAILURES")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("--cache", help="objdump text cache for the binary")
    ap.add_argument("--seeds", default=str(DEFAULT_SEEDS),
                    help="kuna seed file (default: %(default)s)")
    ap.add_argument("--bodies", help="JSON dump from probe_property_names.py; "
                                     "when given, the seed file is not read")
    ap.add_argument("--json-out", help="write the per-body shapes as JSON")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)

    if a.self_test:
        return self_test(a.exe, a.cache)

    arity = Arity(a.exe, a.cache)
    if a.bodies:
        bodies = load_json_bodies(Path(a.bodies))
        label = f"{len(bodies)} bodies from {a.bodies}"
    else:
        bodies = load_seed_bodies(Path(a.seeds))
        label = f"{len(bodies)} seed bodies"
    rows, c = arity.census(bodies)
    report(label, rows, c)

    if a.json_out:
        Path(a.json_out).write_text(json.dumps(
            {"bodies": len(bodies), "shapes": rows}, indent=2))
        print(f"wrote {a.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
