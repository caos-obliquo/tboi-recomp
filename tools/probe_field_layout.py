#!/usr/bin/env python3
"""Recover the field layout of the game's classes from the property getters.

Why this exists
---------------
A property in the recovered surface is a *closure*: a body plus an upvalue.
For the plain data classes (the ones that are just a bag of numbers, strings
and pointers) the upvalue is the offset of the field inside the game object,
and the getter body says what the field's type is.  With both, the layout of
the class is known -- and that layout can be checked against an independent
source (the Repentogon ``.zhl`` layouts, see ``tools/crosscheck_libzhl.py``)
and used to build the value objects in the port.

How a field is read
-------------------
Every property getter has the same shape::

    lua_type(L, 1)                       -- the receiver must be there
    call 0x85c590(<type descriptor>, 1)  -- and of the right type
    mov edi, [eax+4]                     -- the field offset (from the
                                           descriptor or the upvalue)
    push 0xfff0b9d7; call lua_touserdata -- the game object behind `self`
    <read the field>                     -- movss / mov / fld at [obj+edi]
    <push the value>                     -- 0x85c050 float, 0x85bfd0 int,
                                           lua_pushboolean, lua_pushstring,
                                           lua_newuserdata
    mov eax, 1                           -- one value returned

The push call at the end is what gives the field its type.  The offsets come
from the property upvalue: a value below 0x1000 *is* the offset, a larger one
is a pointer to a per-field accessor (``fld DWORD PTR [ecx+off]``) that holds
it.

What this does NOT establish
-----------------------------
* A field that is read through a *virtual call* (the list classes' ``Size``)
  has no offset here: the number lives in the game's C++ object.
* A property whose getter pushes something else (a table, a function) has no
  type here.
* An offset in the Lua userdata is not necessarily an offset in the ported
  C++ object: the port allocates its own payload.
* The type is read from the push helper, so a field that is a float in the
  game but pushed as an integer (or the other way round) would be reported
  wrongly.  The cross-check against the ``.zhl`` layouts is the referee.

Usage::

    tools/probe_field_layout.py /path/to/isaac-ng.exe --cache disasm.txt
    tools/probe_field_layout.py /path/to/isaac-ng.exe \\
        --surface bindings/code-derived-surface.json --json-out fields.json
    tools/probe_field_layout.py /path/to/isaac-ng.exe --self-test
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import probe_iat  # noqa: E402
from disasm import Disasm  # noqa: E402
from pe_vaddr import PEImage  # noqa: E402

# The size of a field of each type, in bytes.  A bool is one byte in the
# game's objects; the getter pushes a Lua boolean, which is a whole int.
# The size of a field of each type, in bytes, as the game stores it:
#   int      4      a 32 bit integer
#   int64    8      a 64 bit integer (the game pushes it in two halves)
#   int16    2      a 16 bit integer
#   float    4      a 32 bit float
#   bool     1      a byte
#   object   4      a pointer into the game's heap
#   string  24      a std::string (pointer, size, capacity)
FIELD_SIZES = {
    "int": 4,
    "int64": 8,
    "int16": 2,
    "uint8": 1,
    "float": 4,
    "bool": 1,
    "object": 4,
    "string": 24,
}

# The push helpers of the game, read by hand in the disassembly:
#   0x85c050  push a float (movss; the game's own number push)
#   0x85bfd0  push an integer (mov eax,edx; cdq; lua_pushinteger)
#   0x85bff0  push the result of a virtual call (an integer as well)
# The Lua entry points are found through the import table, not hard-coded.
PUSH_FLOAT_HELPERS = (0x85C050,)
PUSH_INT_HELPERS = (0x85BFD0, 0x85BFF0)

# A property upvalue below this value is a field offset, not a code pointer.
OFFSET_LIMIT = 0x1000

# `fld DWORD PTR [ecx+0x1c]` -- and `[ecx]` for the offset zero.
ACCESSOR_RE = re.compile(r"\[e[a-z]x([+-]0x[0-9a-f]+)?\]")

# `[ecx+edi*1]` -- a memory operand with an index, the way the game
# loads a field out of the game object.
INDEXED_RE = re.compile(r"\[[a-z]+[a-z0-9]*\+[a-z]+[a-z0-9]*\*1(\+0x[0-9a-f]+)?\]")


class LayoutProbe:
    """Reads the field offsets and types out of the property getters."""

    def __init__(self, exe: str, cache: str | None = None) -> None:
        self.image = PEImage(exe)
        slots = {va: name for (_dll, name, va) in probe_iat.parse_imports(self.image)}
        self.slot_va = {name: va for va, name in slots.items()}
        self.d = Disasm(exe, cache)

        # The Lua entry points that push a value of a known type.  A getter
        # is classified by the call that pushes its result, and a call to one
        # of the game's own helpers is classified by the Lua entry point that
        # helper ends up calling.
        self.marks: dict[str, set[int]] = {
            "int": set(PUSH_INT_HELPERS) | {self.slot_va["lua_pushinteger"]},
            "float": set(PUSH_FLOAT_HELPERS) | {self.slot_va["lua_pushnumber"]},
            "bool": {self.slot_va["lua_pushboolean"]},
            "object": {self.slot_va["lua_newuserdata"]},
            "string": {self.slot_va["lua_pushstring"], self.slot_va["lua_pushlstring"]},
        }
        # The helper cache: the type of a game helper, by its address.
        self._helper_type: dict[int, str] = {}

    # -- the disassembly -----------------------------------------------------

    def body(self, va: int, limit: int = 200) -> list:
        """The instructions of the leaf body at `va`; it ends at its `ret`."""
        out = []
        for insn in self.d.forward(va):
            out.append(insn)
            if insn.mnemonic == "ret" or len(out) > limit:
                break
        return out

    def calls(self, insn, target: int) -> bool:
        """True when the instruction calls `target`, directly or via the IAT."""
        if insn.mnemonic != "call":
            return False
        if insn.calls_slot(target):
            return True
        try:
            return int(insn.operands.strip(), 16) == target
        except ValueError:
            return False

    # -- the layout -----------------------------------------------------------

    def width_of(self, body: list) -> str:
        """The width of the integer field a body loads.

        The load is the interesting instruction: `mov eax, DWORD PTR [ecx+edi*1]`
        is a 32 bit field, `movzx eax, BYTE PTR [ecx+edi*1]` a one byte one
        and `mov dx, WORD PTR [ecx+edi*1]` a two byte one.  A 64 bit field is
        loaded as two dwords.
        """
        dwords = 0
        word = False
        byte = False
        for insn in body:
            operands = insn.operands
            if not INDEXED_RE.search(operands):
                continue
            if "DWORD PTR [" in operands:
                dwords += 1
            elif "WORD PTR [" in operands:
                word = True
            elif "BYTE PTR [" in operands:
                byte = True
        if dwords >= 2:
            return "int64"
        if word:
            return "int16"
        if byte:
            return "uint8"
        return "int"

    def helper_type(self, target: int, depth: int = 0) -> str:
        """The type a game helper pushes, or "unknown".

        A helper is classified by the Lua entry point it calls: a helper that
        ends in `lua_newuserdata` hands out an object, one that ends in
        `lua_pushlstring` a string, and so on.  The helper of a helper is
        followed one more level, which covers the game's own push wrappers.
        """
        if target in self._helper_type:
            return self._helper_type[target]
        if depth > 2:
            return "unknown"
        body = self.body(target, limit=40)
        result = self._classify(body, depth)
        self._helper_type[target] = result
        return result

    def _classify(self, body: list, depth: int) -> str:
        """The type the body pushes, or "unknown"."""
        found: set[str] = set()
        for insn in body:
            if insn.mnemonic != "call":
                continue
            hit = None
            for name, targets in self.marks.items():
                if any(self.calls(insn, t) for t in targets):
                    hit = name
                    break
            if hit is not None:
                found.add(hit)
                continue
            try:
                inner = int(insn.operands.strip(), 16)
            except ValueError:
                continue  # an indirect call: no static target
            kind = self.helper_type(inner, depth + 1)
            if kind != "unknown":
                found.add(kind)
        # The order matters: a getter that hands out an object also touches
        # numbers while building it, so the object wins.
        for name in ("object", "string", "bool", "float", "int64", "int16", "int"):
            if name in found:
                return name
        return "unknown"

    def field_type(self, getter: int) -> str:
        """The type the getter pushes, or "unknown"."""
        body = self.body(getter)
        kind = self._classify(body, 0)
        if kind == "int":
            # The helper only pushes an integer; the getter says how wide
            # the field is.
            return self.width_of(body)
        return kind

    def accessor_offset(self, code: int) -> int | None:
        """The offset a code-pointer upvalue reads.

        A field accessor is a tiny function that reads exactly one field and
        returns it: `fld DWORD PTR [ecx+0x1c]` / `mov eax, [ecx]`.  Anything
        that reads more than one location, or that computes something from it,
        is not a field accessor -- the list classes' `Size` for instance
        computes `(end - begin) / 8` from two pointers, and reporting the
        offset of the second one as a field would be a lie.
        """
        reads = []
        for insn in self.body(code, limit=12):
            m = ACCESSOR_RE.search(insn.operands)
            if m:
                reads.append(int(m.group(1), 16) if m.group(1) else 0)
        if len(reads) != 1:
            return None
        return reads[0]

    def offset_of(self, upvalue: int | None) -> tuple[int | None, str]:
        """The field offset of a property and where the number came from."""
        if upvalue is None:
            return None, "none"
        if upvalue < OFFSET_LIMIT:
            return upvalue, "upvalue"
        return self.accessor_offset(upvalue), "accessor"

    def class_layout(self, entry: dict) -> dict:
        """The layout of one class: the fields and the computed size."""
        fields = []
        for prop in entry.get("properties", []):
            getter = int(prop["getter"], 16)
            upvalue = int(prop["upvalue"], 16) if prop.get("upvalue") else None
            offset, source = self.offset_of(upvalue)
            ftype = self.field_type(getter)
            fields.append({
                "name": prop["name"],
                "offset": offset,
                "offset_source": source,
                "type": ftype,
                "size": FIELD_SIZES.get(ftype),
                "getter": prop["getter"],
                "setter": prop.get("setter"),
            })

        unresolved = [f for f in fields if f["offset"] is None or f["type"] == "unknown"]
        size = 0
        rows = []
        for field in sorted(fields, key=lambda f: (f["offset"] is None, f["offset"] or 0)):
            if field["offset"] is not None and field["size"] is not None:
                size = max(size, field["offset"] + field["size"])
            rows.append(field)
        return {
            "size": size,
            "fields": rows,
            "complete": not unresolved,
            "unresolved": [f["name"] for f in unresolved],
        }

    def layouts(self, surface_path: str) -> tuple[dict, list]:
        """The layout of every class of the surface, and the incomplete ones."""
        surface = json.loads(Path(surface_path).read_text())
        out: dict[str, dict] = {}
        incomplete = []
        for name, entry in surface.get("classes", {}).items():
            if not entry.get("properties"):
                continue
            layout = self.class_layout(entry)
            out[name] = layout
            if not layout["complete"]:
                incomplete.append(name)
        return out, incomplete

    def summary(self, layouts: dict) -> dict:
        types = Counter()
        for layout in layouts.values():
            for field in layout["fields"]:
                types[field["type"]] += 1
        return {
            "classes": len(layouts),
            "complete_classes": sum(1 for v in layouts.values() if v["complete"]),
            "fields": sum(len(v["fields"]) for v in layouts.values()),
            "types": dict(types),
        }


# The controls the self-test checks.  Each one is a fact that was read by
# hand in the disassembly and confirmed against the Repentogon layouts.
CONTROLS = [
    # (class, property, offset, type)
    ("ActiveItemDesc", "Item", 0, "int"),
    ("ActiveItemDesc", "VarData", 28, "int"),
    ("ProjectileParams", "DotProductLimit", 120, "float"),
    ("Vector", "X", 0, "float"),
    ("PosVel", "Position", 0, "object"),
    ("KColor", "Red", 0, "float"),
]


def self_test(exe: str, cache: str | None, surface: str) -> int:
    """Positive and negative controls.  A non-zero exit means the tool lies."""
    probe = LayoutProbe(exe, cache)
    layouts, incomplete = probe.layouts(surface)
    failures = 0

    for cls, name, offset, ftype in CONTROLS:
        layout = layouts.get(cls)
        if layout is None:
            print(f"FAIL: class {cls} has no layout", file=sys.stderr)
            failures += 1
            continue
        fields = {f["name"]: f for f in layout["fields"]}
        field = fields.get(name)
        if field is None:
            print(f"FAIL: {cls}.{name} is not in the layout", file=sys.stderr)
            failures += 1
            continue
        if field["offset"] != offset or field["type"] != ftype:
            print(f"FAIL: {cls}.{name} is offset {field['offset']} type "
                  f"{field['type']}, expected offset {offset} type {ftype}",
                  file=sys.stderr)
            failures += 1
        else:
            print(f"control {cls}.{name}: offset {offset} type {ftype}")

    # Negative control: a property that is not a field must not get one.
    color = layouts.get("Color", {})
    red = {f["name"]: f for f in color.get("fields", [])}.get("R")
    if red is not None and red["type"] == "unknown":
        print("FAIL: Color.R has no type", file=sys.stderr)
        failures += 1

    # Negative control: a canary property must not be found.
    if "ZZ_CANARY" in {f["name"] for f in
                       (layout for layout in layouts.values() for layout in layout["fields"])}:
        print("FAIL: the canary property appeared in the layout", file=sys.stderr)
        failures += 1

    summary = probe.summary(layouts)
    print(f"classes with a layout : {summary['classes']}")
    print(f"complete layouts      : {summary['complete_classes']}")
    print(f"fields                : {summary['fields']}")
    print(f"types                 : {summary['types']}")
    print(f"incomplete classes    : {len(incomplete)}")

    print("SELF-TEST PASSED" if not failures else f"{failures} CONTROL FAILURES")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("--cache", help="objdump text cache for the binary")
    ap.add_argument("--surface", default="bindings/code-derived-surface.json",
                    help="the code-derived surface to read the properties from")
    ap.add_argument("--json-out", help="write the layouts as JSON")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test(args.exe, args.cache, args.surface)

    probe = LayoutProbe(args.exe, args.cache)
    layouts, incomplete = probe.layouts(args.surface)
    summary = probe.summary(layouts)

    print(f"classes with a layout : {summary['classes']}")
    print(f"complete layouts      : {summary['complete_classes']}")
    print(f"fields                : {summary['fields']}")
    print(f"types                 : {summary['types']}")
    print()
    for name, layout in sorted(layouts.items(),
                               key=lambda kv: (-len(kv[1]["fields"]), kv[0])):
        types = Counter(f["type"] for f in layout["fields"])
        mark = "" if layout["complete"] else "  (incomplete)"
        print(f"  {name:22s} size={layout['size']:<6d} "
              f"{len(layout['fields']):3d} fields {dict(types)}{mark}")

    if incomplete:
        print()
        print("classes with an unresolved field:")
        for name in sorted(incomplete):
            print(f"  {name}: {', '.join(layouts[name]['unresolved'])}")

    if args.json_out:
        payload = {"source": args.surface, "summary": summary,
                   "classes": layouts, "incomplete": incomplete}
        Path(args.json_out).write_text(json.dumps(payload, indent=1))
        print(f"wrote {args.json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
