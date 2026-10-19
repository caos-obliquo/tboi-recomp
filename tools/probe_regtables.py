#!/usr/bin/env python3
"""Find the (method name, native function) registration tables in .rdata.

The backwards-window scan around lua_pushcclosure recovers class names and the
property machinery (__propget / __propset closures) but almost none of the 655
methods -- only 36 of 655 overlapped the committed surface. That is the expected
shape if the methods are not pushed one at a time but walked out of a static
table by a loop.

A registration table has an unmistakable signature: a run of 8-byte pairs whose
first word points into .rdata (a NUL-terminated name) and whose second word
points into .text (the native body). This scans .rdata for runs of those pairs.

Reported as candidates. A pointer pair that looks right is not yet a binding;
the acceptance gate is the cross-check against the committed surface plus the
Font calibration.

Controls:
  - the string side must actually decode as printable ASCII
  - a run must reach MIN_RUN pairs, and every pair in it must satisfy both
    constraints, so noise cannot accumulate into a table
  - cross-check against bindings/recovered-binding-surface.json and report the
    overlap in both directions
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402

MIN_RUN = 3
MAX_NAME = 64


def parse_stride(argv: list[str]) -> int:
    for a in argv:
        if a.startswith("--stride="):
            return int(a.split("=", 1)[1])
    return 8


def name_at(image: PEImage, rva: int) -> str | None:
    try:
        off = image.va_to_offset(rva)
    except ValueError:
        return None
    end = image.data.find(b"\x00", off, off + MAX_NAME + 1)
    if end <= off:
        return None
    raw = image.data[off:end]
    if not (2 <= len(raw) <= MAX_NAME):
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


def committed_names(spec_path: str) -> set[str]:
    spec = json.loads(Path(spec_path).read_text())
    names: set[str] = set()
    for key, val in spec.items():
        if key.startswith("_") or not isinstance(val, dict):
            continue
        for m in val.get("api", []) or []:
            if isinstance(m, str):
                names.add(m)
    return names


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0]
    spec_path = args[1] if len(args) > 1 and not sys.argv[1].startswith("--") else None
    stride = parse_stride(sys.argv[1:])

    image = PEImage(path)
    rdata, text = image.section(".rdata"), image.section(".text")
    print(f"{path}")
    print(f".rdata rva=0x{rdata.vma:08x} raw=0x{rdata.raw_offset:08x} size=0x{rdata.raw_size:08x}")
    print(f".text  rva=0x{text.vma:08x}")
    print(f"stride={stride}\n")

    base = rdata.raw_offset
    blob = image.data[base : base + rdata.raw_size]

    tables = []
    i = 0
    limit = len(blob) - stride
    while i <= limit:
        str_va = struct.unpack_from("<I", blob, i)[0]
        fn_va = struct.unpack_from("<I", blob, i + 4)[0]
        str_rva = image.va_to_rva(str_va)
        fn_rva = image.va_to_rva(fn_va)
        if rdata.contains_va(str_rva) and text.contains_va(fn_rva):
            pairs = []
            j = i
            while j <= limit:
                s = struct.unpack_from("<I", blob, j)[0]
                if not rdata.contains_va(image.va_to_rva(s)):
                    break
                nm = name_at(image, image.va_to_rva(s))
                if nm is None:
                    break
                fns = []
                for k in range(1, stride // 4):
                    f = struct.unpack_from("<I", blob, j + 4 * k)[0]
                    fns.append(f if text.contains_va(image.va_to_rva(f)) else None)
                if not any(fns):
                    break
                pairs.append((nm, fns))
                j += stride
            if len(pairs) >= MIN_RUN:
                tables.append((rdata.raw_offset + i, pairs))
                i = j
                continue
        i += 4

    print(f"candidate registration tables (run >= {MIN_RUN} pairs): {len(tables)}")
    total_pairs = sum(len(p) for _o, p in tables)
    print(f"total pairs across them: {total_pairs}\n")

    if not tables:
        print("NO TABLES FOUND -- the pair signature is wrong for this stride",
              file=sys.stderr)
        return 1

    for off, pairs in sorted(tables, key=lambda t: -len(t[1]))[:15]:
        print(f"  file off 0x{off:08x}  rva 0x{image.va_to_rva(image.offset_to_va(off)):08x}  "
              f"{len(pairs)} pairs")
        for nm, fns in pairs[:6]:
            pretty = ", ".join(f"0x{f:08x}" if f else "-" for f in fns)
            print(f"      {nm:<32} fns=[{pretty}]")
        if len(pairs) > 6:
            print(f"      ... and {len(pairs) - 6} more")

    if spec_path:
        committed = committed_names(spec_path)
        found: dict[str, list[list]] = {}
        for _off, pairs in tables:
            for nm, fns in pairs:
                found.setdefault(nm, []).append(fns)
        inter = sorted(set(found) & committed)
        print(f"\ncross-check vs committed surface:")
        print(f"  committed names : {len(committed)}")
        print(f"  table names     : {len(found)}")
        print(f"  overlap         : {len(inter)}")
        print(f"  table-only      : {len(set(found) - committed)}")
        print(f"  committed-only  : {len(committed - set(found))}")
        if inter[:20]:
            print(f"  e.g. overlapping: {inter[:20]}")
        if not inter:
            print("  ZERO OVERLAP -- signature is probably wrong", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())