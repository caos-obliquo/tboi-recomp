#!/usr/bin/env python3
"""Attribute method names to native function addresses.

Established so far, in order:

  - All 654 recoverable committed method names exist as NUL-terminated strings
    in .rdata, densely packed with single-NUL separators and NO interleaved
    pointer array. So the name->body mapping cannot be read off a table; it has
    to come from the code that references each name.
  - 472 of 654 names are referenced by a `push imm32` in .text. The remaining
    182 are registered some other way and this probe does not explain how.
  - For those 472, a .text pointer near the push site is the candidate body.

Reports CANDIDATES with an explicit denominator. A nearby pointer is not proof
of which argument it is; confirmation needs disassembly, and the acceptance gate
is the Font calibration plus the delta against the committed surface.

Controls:
  - canary name must be absent from the binary and from the located set
  - the located-name count is printed as a fraction of the committed total, so a
    regression is visible rather than silent
  - names with no code reference are listed, not dropped
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402

CANARY = "__no_such_method_9f3a7c__"
NEAR_WINDOW = 48
MIN_BODY_VA = 0x00401000  # sanity floor, .text VMA range checked properly below


def committed(spec_path: str) -> dict[str, str]:
    spec = json.loads(Path(spec_path).read_text())
    out: dict[str, str] = {}
    for cls, val in spec.items():
        if cls.startswith("_") or not isinstance(val, dict):
            continue
        for m in val.get("api", []) or []:
            if isinstance(m, str):
                out[m] = cls
    return out


def find_in_section(image: PEImage, needle: bytes, section) -> int | None:
    lo = section.raw_offset
    idx = image.data.find(needle, lo, lo + section.raw_size)
    return idx if idx >= 0 else None


def push_index(image: PEImage, section) -> dict[int, list[int]]:
    """.text file offsets of every `push imm32`, grouped by immediate."""
    blob = image.data[section.raw_offset : section.raw_offset + section.raw_size]
    out: dict[int, list[int]] = defaultdict(list)
    pos = 0
    while True:
        i = blob.find(b"\x68", pos)
        if i < 0:
            return out
        if i + 5 <= len(blob):
            (v,) = struct.unpack_from("<I", blob, i + 1)
            out[v].append(section.raw_offset + i)
        pos = i + 1


def is_text_pointer(image: PEImage, val: int, text) -> bool:
    try:
        return text.contains_va(image.va_to_rva(val))
    except ValueError:
        return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("spec")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    image = PEImage(args.exe)
    rdata, text = image.section(".rdata"), image.section(".text")
    names = committed(args.spec)

    print(f"exe     : {args.exe}")
    print(f"committed: {len(names)} names across "
          f"{len(set(names.values()))} classes")
    print(f".rdata  : raw=0x{rdata.raw_offset:08x} size=0x{rdata.raw_size:08x}")
    print(f".text   : raw=0x{text.raw_offset:08x} size=0x{text.raw_size:08x}\n")

    canary_hits = find_in_section(image, CANARY.encode() + b"\x00", rdata)
    print(f"control: canary {CANARY!r} in .rdata: {canary_hits} (must be None)")
    if canary_hits is not None:
        print("SCAN BROKEN", file=sys.stderr)
        return 1

    located: dict[str, tuple[int, int]] = {}
    for nm in names:
        off = find_in_section(image, nm.encode() + b"\x00", rdata)
        if off is not None:
            located[nm] = (off, image.offset_to_va(off))
    print(f"located in .rdata: {len(located)}/{len(names)}")

    pushes = push_index(image, text)
    print(f"distinct push immediates in .text: {len(pushes)} "
          f"({sum(len(v) for v in pushes.values())} sites)\n")

    referenced, unreferenced, candidates = [], [], {}
    for nm, (off, va) in located.items():
        sites = pushes.get(va, [])
        if not sites:
            unreferenced.append(nm)
            continue
        referenced.append(nm)
        lo = max(text.raw_offset, min(sites) - NEAR_WINDOW)
        hi = min(text.raw_offset + text.raw_size, max(sites) + NEAR_WINDOW)
        bodies = []
        for o in range(lo, hi, 4):
            if o + 4 > len(image.data):
                break
            (v,) = struct.unpack_from("<I", image.data, o)
            if o not in sites and is_text_pointer(image, v, text):
                bodies.append(v)
        if bodies:
            candidates[nm] = {
                "class": names[nm],
                "name_va": va,
                "push_sites": [image.offset_to_va(s) for s in sites],
                "bodies": sorted(set(bodies)),
            }

    print(f"names with a push reference : {len(referenced)}/{len(located)}")
    print(f"names with NO push reference: {len(unreferenced)}/{len(located)}")
    print(f"candidate (name -> body)    : {len(candidates)}/{len(located)} "
          f"({100.0 * len(candidates) / max(1, len(located)):.1f}%)\n")

    if unreferenced:
        print(f"unreferenced sample ({len(unreferenced)} total): "
              f"{sorted(unreferenced)[:12]}\n")

    multi = {nm: d for nm, d in candidates.items() if len(d["bodies"]) > 1}
    print(f"candidates with an ambiguous body: {len(multi)} "
          f"(these need disassembly before use)")

    by_class: dict[str, int] = defaultdict(int)
    for d in candidates.values():
        by_class[d["class"]] += 1
    print("\ncandidates per class:")
    for cls, n in sorted(by_class.items(), key=lambda kv: -kv[1]):
        total = sum(1 for v in names.values() if v == cls)
        print(f"  {cls:<18} {n:>4}/{total}")

    print("\nsample candidates:")
    for nm, d in list(sorted(candidates.items()))[:20]:
        bodies = ", ".join(f"0x{b:08x}" for b in d["bodies"][:3])
        print(f"  {nm:<26} {d['class']:<12} bodies=[{bodies}]")

    if args.json_out:
        payload = {
            "source": args.exe,
            "committed_names": len(names),
            "located": len(located),
            "referenced": len(referenced),
            "unreferenced": sorted(unreferenced),
            "candidates": candidates,
        }
        Path(args.json_out).write_text(json.dumps(payload, indent=2, sort_keys=True))
        print(f"\nwrote {args.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())