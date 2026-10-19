#!/usr/bin/env python3
"""Map IAT slots to imported names, then find every indirect call to each.

Why this shape
--------------
x86-32 reaches an imported function through `call dword ptr [slot]`, encoded as
the six bytes `ff 15 <slot:le32>`. That is a far sharper instrument than
searching for the bare address: a four-byte scan finds the address anywhere,
including in data and in unrelated immediates, whereas the six-byte pattern
finds only actual call sites.

The PE import directory is walked directly rather than scraping `objdump -p`,
so the slot-to-name map is derived from the file and nothing is hard-coded.

Usage:
    probe_iat.py <isaac-ng.exe>                 # every imported function
    probe_iat.py <isaac-ng.exe> lua_pushcclosure  # one function, with context
    probe_iat.py <isaac-ng.exe> --dll Lua5.3.3r.dll
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402

IMPORT_DIRECTORY_INDEX = 1
CALL_DWORD_PTR = b"\xff\x15"


def data_directory(image: PEImage, index: int) -> tuple[int, int]:
    """(rva, size) of a data directory entry from the optional header."""
    if image.bits == 32:
        base = image.opt + 96
    else:
        base = image.opt + 112
    return struct.unpack_from("<II", image.data, base + index * 8)


def cstring(image: PEImage, rva: int, limit: int = 512) -> str:
    off = image.va_to_offset(rva, section=None)
    end = image.data.find(b"\x00", off, off + limit)
    if end < 0:
        end = off + limit
    return image.data[off:end].decode("ascii", "replace")


def parse_imports(image: PEImage) -> list[tuple[str, str, int]]:
    """[(dll, function_or_ordinal, iat_va)] in import-table order."""
    imp_rva, imp_size = data_directory(image, IMPORT_DIRECTORY_INDEX)
    if imp_rva == 0:
        return []

    out: list[tuple[str, str, int]] = []
    desc_rva = imp_rva
    while True:
        off = image.va_to_offset(desc_rva)
        ilt_rva, ts, fwd, name_rva, iat_rva = struct.unpack_from("<IIIII", image.data, off)
        if ilt_rva == 0 and name_rva == 0 and iat_rva == 0:
            break
        dll = cstring(image, name_rva)

        thunk_rva = ilt_rva or iat_rva
        idx = 0
        while True:
            t_off = image.va_to_offset(thunk_rva + idx * 4)
            (entry,) = struct.unpack_from("<I", image.data, t_off)
            if entry == 0:
                break
            slot_va = image.rva_to_va(iat_rva + idx * 4)
            if entry & 0x80000000:
                out.append((dll, f"ordinal_{entry & 0xFFFF}", slot_va))
            else:
                hint_off = image.va_to_offset(entry)
                (hint,) = struct.unpack_from("<H", image.data, hint_off)
                end = image.data.find(b"\x00", hint_off + 2, hint_off + 2 + 512)
                if end < 0:
                    end = hint_off + 2 + 512
                name = image.data[hint_off + 2 : end].decode("ascii", "replace")
                out.append((dll, name, slot_va))
            idx += 1
        desc_rva += 20
    return out


def build_call_index(image: PEImage) -> dict[int, list[int]]:
    """slot_va -> [file offsets of `ff 15 <slot>`] in a single pass over .text.

    One pass, because scanning the whole section once per slot is quadratic and
    takes minutes on a 7 MB .text.
    """
    text = image.section(".text")
    blob = image.data[text.raw_offset : text.raw_offset + text.raw_size]
    index: dict[int, list[int]] = {}
    pos = 0
    while True:
        idx = blob.find(CALL_DWORD_PTR, pos)
        if idx < 0:
            break
        if idx + 6 <= len(blob):
            (slot,) = struct.unpack_from("<I", blob, idx + 2)
            index.setdefault(slot, []).append(text.raw_offset + idx)
        pos = idx + 1
    return index


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0] if args else "isaac-ng.exe"
    wanted = args[1] if len(args) > 1 else None
    dll_filter = None
    for a in sys.argv[1:]:
        if a.startswith("--dll="):
            dll_filter = a.split("=", 1)[1]

    image = PEImage(path)
    imports = parse_imports(image)
    print(f"{path}: {len(imports)} imported functions from "
          f"{len({d for d, _, _ in imports})} DLLs")
    print(f"image_base=0x{image.image_base:08x}\n")

    # POSITIVE CONTROL: a slot that is imported but never called must yield no
    # call sites, and a slot we have seen called must yield at least one.
    index = build_call_index(image)
    all_slots = {va for _, _, va in imports}
    called_any = sum(1 for va in all_slots if va in index)
    print(f"control: {len(all_slots)} slots, {called_any} have call sites, "
          f"{len(index)} distinct slots referenced from .text")
    if called_any == 0:
        print("CALL-SITE SCAN IS BROKEN", file=sys.stderr)
        return 1

    # Known-good site from hand disassembly: `call dword ptr ds:0xb18414`
    # (lua_pushstring) sits at VA 0x85d280.
    known = index.get(0xB18414, [])
    if not known:
        print("control FAILED: expected call sites for lua_pushstring 0xb18414", file=sys.stderr)
        return 1
    print(f"control OK: lua_pushstring slot 0xb18414 has {len(known)} call site(s), "
          f"first at file off 0x{known[0]:08x} (VA 0x{image.offset_to_va(known[0]):08x})\n")

    rows = []
    for dll, name, va in imports:
        if dll_filter and dll != dll_filter:
            continue
        if wanted and wanted not in name:
            continue
        rows.append((dll, name, va, index.get(va, [])))

    if wanted and not rows:
        print(f"no import matching {wanted!r}", file=sys.stderr)
        return 2

    rows.sort(key=lambda r: -len(r[3]))
    for dll, name, va, sites in rows:
        print(f"{name:<24} {dll:<24} slot=0x{va:08x}  calls={len(sites)}")
        for off in sites[:8]:
            site_va = image.offset_to_va(off)
            prev = image.data[off - 6 : off]
            print(f"    va=0x{site_va:08x}  preceding={prev.hex(' ')}")
        if len(sites) > 8:
            print(f"    ... and {len(sites) - 8} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())