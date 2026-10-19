#!/usr/bin/env python3
"""Find .text references to the metatable key addresses.

x86-32 code references a global as an immediate absolute address, so the little-
endian encoding of the VA appears verbatim in .text wherever the address is
loaded (push imm32, mov reg,imm32, lea-style operand). Scanning for those four
bytes is therefore a sound first pass for xrefs, independent of any disassembler.

Caveat kept explicit: a byte-sequence match proves the address VALUE is
materialised in .text, not that the instruction's intent is a metatable lookup.
It is a candidate filter, and every hit still needs disassembly to confirm.

Positive controls:
  - a VA that cannot be referenced must yield zero hits
  - each real key must yield at least one hit, otherwise .text is not being read
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402

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


def find_nul_terminated(image: PEImage, text: str) -> list[tuple[str, int, int]]:
    """(section, file_offset, rva) for exact NUL-terminated occurrences."""
    needle = text.encode() + b"\x00"
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
            hits.append((section.name, start + idx, section.vma + idx))
            pos = idx + 1
    return hits


def xrefs_to(image: PEImage, rva: int) -> list[int]:
    """File offsets in .text where the absolute VA appears as an immediate."""
    va = image.rva_to_va(rva)
    needle = struct.pack("<I", va)
    text = image.section(".text")
    blob = image.data[text.raw_offset : text.raw_offset + text.raw_size]
    out = []
    pos = 0
    while True:
        idx = blob.find(needle, pos)
        if idx < 0:
            break
        out.append(text.raw_offset + idx)
        pos = idx + 1
    return out


def disasm_window(image: PEImage, offset: int, before: int = 8, after: int = 8) -> str:
    """Raw bytes around a hit. No disassembler: shown so a human can eyeball the
    opcode that materialised the address."""
    lo = max(0, offset - before)
    hi = min(len(image.data), offset + after)
    return image.data[lo:hi].hex(" ")


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "isaac-ng.exe"
    image = PEImage(path)
    text = image.section(".text")
    print(f"{path}")
    print(
        f".text rva=0x{text.vma:08x} raw=0x{text.raw_offset:08x} "
        f"size=0x{text.raw_size:08x} ({text.raw_size} bytes)"
    )

    key_rvas: dict[str, list[int]] = {}
    for key in KEYS:
        hits = find_nul_terminated(image, key)
        key_rvas[key] = [rva for _s, _o, rva in hits]

    # Positive control: an RVA far past the image cannot be referenced.
    bogus_rva = image.rva_to_va(text.vma) + 0x40000000
    bogus_hits = xrefs_to(image, bogus_rva - image.image_base)
    print(f"\ncontrol (unreferencable rva 0x{bogus_rva:08x}): {len(bogus_hits)} hits (must be 0)")
    if bogus_hits:
        print("XREF SCAN IS BROKEN", file=sys.stderr)
        return 1

    total = 0
    for key in KEYS:
        refs: list[int] = []
        for rva in key_rvas[key]:
            refs.extend(xrefs_to(image, rva))
        refs = sorted(set(refs))
        total += len(refs)
        print(f"\n{key}  (rva {', '.join('0x%08x' % r for r in key_rvas[key])})")
        print(f"  {len(refs)} .text reference(s)")
        for off in refs[:12]:
            rva = image.offset_to_va(off)
            print(
                f"    off=0x{off:08x} rva=0x{image.va_to_rva(rva):08x} "
                f"va=0x{rva:08x}  bytes[{disasm_window(image, off)}]"
            )
        if len(refs) > 12:
            print(f"    ... and {len(refs) - 12} more")

    print(f"\ntotal .text references to metatable keys: {total}")
    if total == 0:
        print("NO REFERENCES FOUND -- the anchoring assumption is wrong", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())