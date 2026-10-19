#!/usr/bin/env python3
"""Disambiguate method bodies by reading the lua_pushcclosure argument order.

The previous probe took any .text pointer within 48 bytes of a name's push site
as the candidate body, which left 249 of 376 ambiguous. That is a guess about
argument position.

This uses the API instead. `lua_pushcclosure(L, fn, n)` is called through a known
IAT slot, so for each name reference the probe walks forward to the next
`lua_pushcclosure` call and records the pushes in between. The value that becomes
its second argument is the body, because __stdcall pushes arguments right to
left: the last push before the call is `n`, the one before that is `fn`.

Evidence classes, kept strictly separate:
  confirmed  exactly one .text push between the name and the call
  ambiguous  two or more .text pushes, or none
  no_call    no lua_pushcclosure within the window
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402
from probe_iat import parse_imports  # noqa: E402

WINDOW = 96
CALL_DWORD_PTR = b"\xff\x15"


def push_sites_for(image: PEImage, section) -> dict[int, list[int]]:
    blob = image.data[section.raw_offset : section.raw_offset + section.raw_size]
    out: dict[int, list[int]] = {}
    pos = 0
    while True:
        i = blob.find(b"\x68", pos)
        if i < 0:
            return out
        if i + 5 <= len(blob):
            (v,) = struct.unpack_from("<I", blob, i + 1)
            out.setdefault(v, []).append(section.raw_offset + i)
        pos = i + 1


def pushes_between(image: PEImage, lo: int, hi: int) -> list[int]:
    """Immediates pushed by `push imm32` or `mov r32,imm32` in [lo, hi)."""
    out = []
    for i in range(lo, hi - 4):
        op = image.data[i]
        if op == 0x68 or 0xB8 <= op <= 0xBF:
            (v,) = struct.unpack_from("<I", image.data, i + 1)
            out.append((i, v))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("spec")
    ap.add_argument("--window", type=int, default=WINDOW)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    image = PEImage(args.exe)
    rdata, text = image.section(".rdata"), image.section(".text")
    imports = {n: va for _d, n, va in parse_imports(image)}
    slot = imports.get("lua_pushcclosure")
    if slot is None:
        print("lua_pushcclosure not imported", file=sys.stderr)
        return 2
    slot_bytes = struct.pack("<I", slot)

    spec = json.loads(Path(args.spec).read_text())
    names = {}
    for cls, val in spec.items():
        if cls.startswith("_") or not isinstance(val, dict):
            continue
        for m in val.get("api", []) or []:
            if isinstance(m, str):
                names[m] = cls

    name_pushes = push_sites_for(image, text)
    text_end = text.raw_offset + text.raw_size

    confirmed, ambiguous, no_call, missing = [], [], [], []
    for nm, cls in names.items():
        off = rdata.raw_offset
        idx = image.data.find(nm.encode() + b"\x00", off, off + rdata.raw_size)
        if idx < 0:
            missing.append(nm)
            continue
        sites = name_pushes.get(image.offset_to_va(idx), [])
        if not sites:
            missing.append(nm)
            continue
        verdicts = []
        for s in sites:
            hi = min(s + args.window, text_end)
            call = image.data.find(CALL_DWORD_PTR + slot_bytes, s + 5, hi)
            if call < 0:
                verdicts.append(None)
                continue
            cands = [v for _i, v in pushes_between(image, s + 5, call)
                     if text.contains_va(image.va_to_rva(v))]
            verdicts.append(cands)
        good = [v for v in verdicts if v is not None and len(v) == 1]
        if good:
            confirmed.append({"name": nm, "class": cls,
                              "body": good[0][0],
                              "body_va": image.offset_to_va(good[0][0]),
                              "sites": len(sites)})
        elif any(v for v in verdicts):
            ambiguous.append({"name": nm, "class": cls})
        else:
            no_call.append(nm)

    print(f"names considered      : {len(names)}")
    print(f"confirmed single body : {len(confirmed)}")
    print(f"ambiguous body        : {len(ambiguous)}")
    print(f"no pushcclosure in win: {len(no_call)}")
    print(f"no name reference     : {len(missing)}\n")

    if confirmed:
        by_class: dict[str, int] = {}
        for c in confirmed:
            by_class[c["class"]] = by_class.get(c["class"], 0) + 1
        print("confirmed per class:")
        for cls, n in sorted(by_class.items(), key=lambda kv: -kv[1]):
            total = sum(1 for v in names.values() if v == cls)
            print(f"  {cls:<18} {n:>4}/{total}")

        print("\nsample confirmed (name -> body va):")
        for c in confirmed[:20]:
            print(f"  {c['name']:<26} {c['class']:<12} 0x{c['body_va']:08x} "
                  f"(rva 0x{image.va_to_rva(c['body_va']):08x})")

    uniq = len({c["body"] for c in confirmed})
    print(f"\ndistinct body addresses among confirmed: {uniq} "
          f"(from {len(confirmed)} names)")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(
            {"source": args.exe, "window": args.window,
             "confirmed": confirmed,
             "ambiguous": sorted(a["name"] for a in ambiguous),
             "no_call": sorted(no_call),
             "missing": sorted(missing)},
            indent=2, sort_keys=True))
        print(f"wrote {args.json_out}")
    return 0 if confirmed else 1


if __name__ == "__main__":
    sys.exit(main())