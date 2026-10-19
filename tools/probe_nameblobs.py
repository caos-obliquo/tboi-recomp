#!/usr/bin/env python3
"""Locate the method-name blobs and test for a parallel function-pointer array.

Scanning .rdata for (name, fn) pointer pairs finds only Lua's own luaL_openlibs
table, at every stride tried. That is consistent with the committed surface's own
_provenance, which says the names were recovered from NUL-separated *string
regions* rather than from pointer arrays. So the layout is probably a contiguous
blob of NUL-terminated names plus a separate array of body pointers, zipped by a
registration loop.

This locates the blobs by searching for known committed names, measures how many
strings each blob holds, and then searches the neighbourhood for a run of .text
pointers whose length matches, which is the signature of the parallel array.

Controls:
  - a nonsense name must not be found anywhere
  - every reported blob must contain at least one committed name, so a blob of
    unrelated strings cannot be reported as a method table
  - a parallel-array hit is only claimed when the run length is within 4 of the
    blob's string count
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pe_vaddr import PEImage  # noqa: E402

NEIGHBOURHOOD = 0x4000
MAX_NAME = 64


def load_committed(spec_path: str) -> dict[str, set[str]]:
    spec = json.loads(Path(spec_path).read_text())
    out: dict[str, set[str]] = {}
    for key, val in spec.items():
        if key.startswith("_") or not isinstance(val, dict):
            continue
        out[key] = {m for m in (val.get("api", []) or []) if isinstance(m, str)}
    return out


def find_all(image: PEImage, needle: bytes, section) -> list[int]:
    """Occurrences of `needle` inside one section only.

    Searching the whole file matches bytes in the PE header and in unrelated
    tables, which produced a false blob built out of OpenGL entry-point names.
    """
    lo = section.raw_offset
    hi = lo + section.raw_size
    out, pos = [], lo
    while True:
        idx = image.data.find(needle, pos, hi)
        if idx < 0:
            return out
        out.append(idx)
        pos = idx + 1


def count_strings(image: PEImage, start: int, blob_end: int) -> tuple[int, list[str]]:
    """Walk NUL-terminated printable strings from `start` up to `blob_end`."""
    off, names = start, []
    while off < blob_end:
        end = image.data.find(b"\x00", off, blob_end)
        if end < 0:
            break
        raw = image.data[off:end]
        if raw:
            try:
                text = raw.decode("ascii")
                if all(32 <= ord(c) < 127 for c in text) and 2 <= len(text) <= MAX_NAME:
                    names.append(text)
                    off = end + 1
                    continue
            except UnicodeDecodeError:
                pass
        break
    return len(names), names


def text_pointer_runs(image: PEImage, lo: int, hi: int, text) -> list[tuple[int, int]]:
    """Runs of >=4 consecutive dwords that all point into .text."""
    runs, off = [], lo
    while off + 4 <= hi:
        (v,) = struct.unpack_from("<I", image.data, off)
        if text.contains_va(image.va_to_rva(v)):
            start, n = off, 0
            while off + 4 <= hi:
                (w,) = struct.unpack_from("<I", image.data, off)
                if not text.contains_va(image.va_to_rva(w)):
                    break
                off += 4
                n += 1
            if n >= 4:
                runs.append((start, n))
        else:
            off += 4
    return runs


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path, spec_path = args[0], args[1]
    image = PEImage(path)
    rdata, text = image.section(".rdata"), image.section(".text")
    committed = load_committed(spec_path)
    all_names = {n for s in committed.values() for n in s}
    print(f"{path}")
    print(f"committed classes: {len(committed)}  names: {len(all_names)}")
    print(f".rdata raw=0x{rdata.raw_offset:08x} size=0x{rdata.raw_size:08x}\n")

    canary = b"__no_such_method_9f3a7c__\x00"
    hits = find_all(image, canary, rdata)
    print(f"control: canary occurrences in the image = {len(hits)} (must be 0)")
    if hits:
        print("SCAN BROKEN", file=sys.stderr)
        return 1

    found_any = 0
    for cls in sorted(committed):
        names = committed[cls]
        anchors = {}
        for nm in sorted(names):
            for off in find_all(image, nm.encode() + b"\x00", rdata):
                anchors.setdefault(off, []).append(nm)
        if not anchors:
            print(f"  {cls:<22} no committed names located")
            continue
        offsets = sorted(anchors)
        # Walk anchors into contiguous regions.
        regions, cur = [], [offsets[0]]
        for o in offsets[1:]:
            if o - cur[-1] <= 256:
                cur.append(o)
            else:
                regions.append(cur)
                cur = [o]
        regions.append(cur)

        for reg in regions[:3]:
            lo_off = reg[0]
            hi_off = reg[-1] + MAX_NAME
            n_str, strs = count_strings(image, lo_off, min(hi_off + 4096, rdata.file_end))
            hit = [nm for nm in strs if nm in names]
            if not hit:
                continue
            found_any += 1
            try:
                lo_va = image.offset_to_va(lo_off)
            except ValueError:
                continue
            print(f"  {cls:<22} blob at file 0x{lo_off:08x} (va 0x{lo_va:08x}) "
                  f"rva 0x{image.va_to_rva(lo_va):08x}")
            print(f"      committed names in blob: {len(hit)}/{len(names)}  "
                  f"contiguous strings from here: {n_str}")
            print(f"      first strings: {strs[:8]}")
            lo2 = max(rdata.raw_offset, lo_off - NEIGHBOURHOOD)
            hi2 = min(rdata.file_end, hi_off + NEIGHBOURHOOD)
            runs = text_pointer_runs(image, lo2, hi2, text)
            close = [(o, c) for o, c in runs if abs(c - n_str) <= 4]
            print(f"      .text pointer runs within +/-{NEIGHBOURHOOD:#x}: {len(runs)}; "
                  f"length-matching (+/-4): {len(close)}")
            for o, c in close[:5]:
                va = image.offset_to_va(o)
                print(f"        file 0x{o:08x} va 0x{va:08x} rva 0x{image.va_to_rva(va):08x} "
                      f"count={c} (blob strings {n_str})")
            break

    print(f"\nclasses with a located blob: {found_any}/{len(committed)}")
    return 0 if found_any else 2


if __name__ == "__main__":
    sys.exit(main())