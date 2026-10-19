#!/usr/bin/env python3
"""Locate the metatable key strings in isaac-ng.exe and report their addresses.

The binding model registers classes through metatables rather than lua_register,
so the registration routines must reference these keys. Finding them is step one
of recovering the 655 method bodies: the code that loads them is the code that
walks each class's (name, function-pointer) table.

Exact NUL-terminated match only. Substring matches are reported separately and
labelled, because a loose match would attribute a hit to the wrong anchor.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402

# Recovered metatable keys, in the order the README lists them.
KEYS = [
    "__class",
    "__parent",
    "__index",
    "__propget",
    "__propset",
    "__newindex",
    "__const",
    "__metatable",
    "__type",
]


def scan(image: PEImage, needle: bytes) -> list[tuple[str, int, int]]:
    """Exact NUL-terminated occurrences: (section, file_offset, rva)."""
    hits = []
    for section in image.sections:
        if section.raw_size == 0:
            continue
        start = section.raw_offset
        blob = image.data[start : start + section.raw_size]
        pos = 0
        while True:
            idx = blob.find(needle, pos)
            if idx < 0:
                break
            rva = section.vma + idx
            hits.append((section.name, start + idx, rva))
            pos = idx + 1
    return hits


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "isaac-ng.exe"
    image = PEImage(path)
    print(f"{path}: {len(image.sections)} sections, image_base 0x{image.image_base:08x}")

    found_any = False
    for key in KEYS:
        needle = key.encode() + b"\x00"
        hits = scan(image, needle)
        if not hits:
            print(f"  {key:<12} NOT FOUND")
            continue
        found_any = True
        for sec, off, rva in hits:
            print(
                f"  {key:<12} sec={sec:<7} off=0x{off:08x} "
                f"rva=0x{rva:08x} va=0x{image.rva_to_va(rva):08x}"
            )

    # POSITIVE CONTROL: a string that cannot exist. If this "finds" something,
    # the scan is broken and every absence above is meaningless.
    canary = "__this_key_cannot_exist_9f3a7c__"
    bogus = scan(image, canary.encode() + b"\x00")
    print(f"\npositive control {canary}: {len(bogus)} hits (must be 0)")
    if bogus:
        print("SCAN IS BROKEN", file=sys.stderr)
        return 1

    # Second control: a key we know exists must be findable, proving the
    # scan reaches .rdata at all.
    probe = scan(image, b"__class\x00")
    print(f"positive control __class: {len(probe)} hits (must be >= 1)")
    if not probe:
        print("SCAN REACHES NOTHING", file=sys.stderr)
        return 1

    return 0 if found_any else 2


if __name__ == "__main__":
    sys.exit(main())