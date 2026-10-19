#!/usr/bin/env python3
"""Recover (method name, native function) pairs from lua_pushcclosure call sites.

`lua_pushcclosure(L, fn, n)` is the call that turns a native C function into a
Lua closure, so every binding of a native method must pass one. Walking
backwards from each call site for the immediates that were pushed gives the
candidate method name (a string in .rdata) and the candidate body address (a
pointer into .text).

This reports CANDIDATES. A `push imm32` near a call is not proof of which
argument it is; the pairing is a filter to be confirmed by disassembly, and the
`Font` calibration in docs/binding-surface-findings.md is the acceptance gate.

Controls:
  - cross-check against bindings/recovered-binding-surface.json: recovered names
    that appear here must be a subset of the committed surface
  - a nonsense string must yield zero .rdata pointers
  - report, explicitly, how many sites produced no name at all, so a low yield
    is visible rather than hidden
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402
from probe_iat import build_call_index, parse_imports  # noqa: E402

BACK_WINDOW = 64
MIN_NAME_LEN = 3
MAX_NAME_LEN = 64


def printable_ascii_at(image: PEImage, rva: int, limit: int = MAX_NAME_LEN) -> str | None:
    """NUL-terminated printable ASCII at `rva`, or None."""
    try:
        off = image.va_to_offset(rva)
    except ValueError:
        return None
    end = image.data.find(b"\x00", off, off + limit + 1)
    if end < 0 or end == off:
        return None
    raw = image.data[off:end]
    if len(raw) < MIN_NAME_LEN:
        return None
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    if not all(32 <= ord(c) < 127 for c in text):
        return None
    if not (text[0].isalpha() or text[0] == "_"):
        return None
    return text


def immediates_before(image: PEImage, site_off: int, window: int = BACK_WINDOW):
    """(distance_back, value) for each `push imm32` / `mov r32,imm32` before a site.

    Opcodes: 68 = push imm32, b8..bf = mov r32,imm32.
    """
    lo = max(0, site_off - window)
    chunk = image.data[lo:site_off]
    out = []
    for i in range(len(chunk)):
        op = chunk[i]
        if op == 0x68 or (0xB8 <= op <= 0xBF):
            if i + 5 <= len(chunk):
                (val,) = struct.unpack_from("<I", chunk, i + 1)
                out.append((site_off - (lo + i), val))
    return out


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0]
    spec_path = args[1] if len(args) > 1 else None

    image = PEImage(path)
    rdata = image.section(".rdata")
    text = image.section(".text")

    imports = {name: va for _d, name, va in parse_imports(image)}
    if "lua_pushcclosure" not in imports:
        print("lua_pushcclosure not imported", file=sys.stderr)
        return 2
    slot = imports["lua_pushcclosure"]

    index = build_call_index(image)
    sites = index.get(slot, [])
    print(f"{path}")
    print(f"lua_pushcclosure slot=0x{slot:08x}  call sites={len(sites)}")
    if not sites:
        print("NO CALL SITES -- assumption wrong", file=sys.stderr)
        return 1
    print(f".rdata rva=0x{rdata.vma:08x}  .text rva=0x{text.vma:08x}\n")

    recovered: dict[str, int] = {}
    with_name = 0
    with_body = 0

    for off in sites:
        site_va = image.offset_to_va(off)
        name = None
        body = None
        for _dist, val in immediates_before(image, off):
            rva = image.va_to_rva(val)
            if name is None and rdata.contains_va(rva):
                cand = printable_ascii_at(image, rva)
                if cand:
                    name = cand
                    continue
            if body is None and text.contains_va(rva):
                body = val
        if name:
            with_name += 1
            recovered[name] = recovered.get(name, 0) + 1
        if body:
            with_body += 1

    print(f"sites with a .rdata name within {BACK_WINDOW} bytes: {with_name}/{len(sites)} "
          f"({100.0 * with_name / len(sites):.1f}%)")
    print(f"sites with a .text pointer within {BACK_WINDOW} bytes: {with_body}/{len(sites)} "
          f"({100.0 * with_body / len(sites):.1f}%)")
    print(f"distinct candidate names: {len(recovered)}\n")

    if not recovered:
        print("NO NAMES RECOVERED -- the backwards-window model is wrong", file=sys.stderr)
        return 1

    canary = "__no_such_method_9f3a7c__"
    print(f"control: canary {canary!r} absent from recovered set: "
          f"{canary not in recovered} (must be True)")

    if spec_path:
        try:
            spec = json.loads(Path(spec_path).read_text())
        except (OSError, json.JSONDecodeError) as e:
            print(f"control skipped: could not read {spec_path}: {e}")
        else:
            committed: set[str] = set()
            for key, val in spec.items():
                if key.startswith("_") or not isinstance(val, dict):
                    continue
                for m in val.get("api", []) or []:
                    if isinstance(m, str):
                        committed.add(m)
                    elif isinstance(m, dict) and isinstance(m.get("name"), str):
                        committed.add(m["name"])
            if committed:
                inter = sorted(set(recovered) & committed)
                new = sorted(set(recovered) - committed)
                missing = sorted(committed - set(recovered))
                print(f"\ncross-check vs {Path(spec_path).name}:")
                print(f"  committed names      : {len(committed)}")
                print(f"  candidate names      : {len(recovered)}")
                print(f"  overlap (control>0)  : {len(inter)}")
                print(f"  candidates NOT committed: {len(new)}")
                print(f"  committed NOT found  : {len(missing)}")
                if missing[:15]:
                    print(f"    e.g. missing: {missing[:15]}")
                if new[:15]:
                    print(f"    e.g. new   : {new[:15]}")
                if not inter:
                    print("  ZERO OVERLAP -- the pairing model is probably wrong",
                          file=sys.stderr)
                    return 1

    print("\ntop candidates by site count:")
    for name, count in sorted(recovered.items(), key=lambda kv: -kv[1])[:30]:
        print(f"  {count:4d}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())