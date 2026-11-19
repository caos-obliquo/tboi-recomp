#!/usr/bin/env python3
"""Probe mode 0x02 chunk framing on a real ARCH000 archive.

The framing is recovered from Ghidra on the owned reference binary and checked
against the installed archives, not copied from another implementation. A
chunked entry has no entry-level header; its payload is a sequence of:

    u32 header = raw & 0x7fffffff -> chunk length
    raw >> 31                     -> final-chunk marker (observed only there)
    len > 0x7ff                   -> malformed

A chunk whose length is exactly 0x400 is stored verbatim. Every other chunk is
an independent raw-DEFLATE stream (miniz tinfl at VA 0xa85710) and is inflated
with ``wbits=-15``. Output stops at the entry's declared size; the chunk lengths
must never be summed to find the end, because that walks into the next entry.

The mode 0x02 stored-record arm (nine-byte header, plaintext payload at
``offset + 9``) is counted and used as a positive control, and is not decoded
here. This is a probe, not an extractor: no game bytes are written.

Exit status is 0 only when the positive control holds and at least one entry
decoded. Use ``--json`` for machine output.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
import zlib
from pathlib import Path

LEN_MASK = 0x7FFFFFFF
FLAG_SHIFT = 31
MAX_CHUNK = 0x7FF
STORED_CHUNK = 0x400
HEADER_SIZE = 4
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def load_reference() -> object:
    path = Path(__file__).with_name("isaac-archive.py")
    spec = importlib.util.spec_from_file_location("isaac_archive", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def decode_chunked(
    blob: bytes, offset: int, size: int, limit: int
) -> tuple[bytes, dict[str, int]]:
    """Copy 0x400 chunks, inflate the rest, stop at ``size`` output bytes.

    ``limit`` is the next entry's offset: a chunk may not run past it. Without
    that bound a malformed or overlapping entry silently reads the next entry's
    table as chunk data.
    """
    out = bytearray()
    stats = {
        "chunks": 0,
        "stored": 0,
        "compressed": 0,
        "final_flag": 0,
        "oversize": 0,
        "overrun": 0,
        "truncated": 0,
        "inflate_error": 0,
    }
    cursor = offset
    while len(out) < size:
        if cursor + HEADER_SIZE > limit:
            stats["truncated"] += 1
            break
        raw = struct.unpack_from("<I", blob, cursor)[0]
        length = raw & LEN_MASK
        flag = raw >> FLAG_SHIFT
        stats["chunks"] += 1
        if length > MAX_CHUNK:
            stats["oversize"] += 1
            break
        start = cursor + HEADER_SIZE
        end = start + length
        if end > limit:
            stats["overrun"] += 1
            break
        cursor = end
        chunk = blob[start:end]
        if length == STORED_CHUNK:
            stats["stored"] += 1
            out.extend(chunk)
            continue
        stats["compressed"] += 1
        if flag:
            stats["final_flag"] += 1
        try:
            # Buffer-driven decode: miniz stops when the output buffer is full,
            # so a chunk does not have to end with a deflate end marker.
            inflater = zlib.decompressobj(wbits=-15)
            out.extend(inflater.decompress(chunk))
        except zlib.error:
            stats["inflate_error"] += 1
            break
    stats["short"] = int(len(out) < size)
    return bytes(out[:size]), stats


def plausible(data: bytes) -> str | None:
    head = data[:32]
    if head.startswith(PNG_MAGIC):
        return "png"
    if head.startswith(b"OggS"):
        return "ogg"
    if head.startswith(b"RIFF"):
        return "riff"
    if head.startswith(b"BMF "):
        return "bmf"
    if head.startswith(b"<?xml") or head.startswith(b"<"):
        return "xml"
    if b"\x00" not in head and all(
        32 <= byte < 127 or byte in (9, 10, 13) for byte in head
    ):
        return "text"
    return None


def probe(path: Path, limit: int, all_entries: bool, wants_json: bool) -> int:
    ref = load_reference()
    mode, entries = ref.read_archive(path)
    blob = path.read_bytes()
    if mode != ref.MODE_PRNG:
        raise SystemExit(f"{path}: mode 0x{mode:02x}, expected mode 0x02")

    stored = [i for i, e in enumerate(entries) if ref.is_stored(blob, e)]
    positive = sum(
        1
        for i in stored
        if blob[entries[i][2] + ref.RECORD_HEADER_SIZE :].startswith(PNG_MAGIC)
    )

    stored_set = set(stored)
    candidates = [i for i in range(len(entries)) if i not in stored_set]
    if not all_entries:
        candidates = candidates[:limit]

    score = 0
    kinds: dict[str, int] = {}
    totals = {
        "chunks": 0,
        "stored": 0,
        "compressed": 0,
        "final_flag": 0,
        "oversize": 0,
        "truncated": 0,
        "inflate_error": 0,
    }
    failures = 0
    decoded = 0
    unrecognised = 0
    examples = []
    for i in candidates:
        _, _, offset, size, _ = entries[i]
        limit = entries[i + 1][2] if i + 1 < len(entries) else len(blob)
        data, stats = decode_chunked(blob, offset, size, limit)
        for key in totals:
            if key in stats:
                totals[key] += stats[key]
        if stats["short"]:
            failures += 1
            continue
        decoded += 1
        kind = plausible(data)
        if kind is None:
            unrecognised += 1
            continue
        score += 1
        kinds[kind] = kinds.get(kind, 0) + 1
        if len(examples) < 5:
            examples.append(
                {"index": i, "kind": kind, "head": data[:24].decode("ascii", "replace")}
            )

    result = {
        "archive": str(path),
        "entries": len(entries),
        "stored_entries": len(stored),
        "stored_png_positive_control": positive,
        "sample_size": len(candidates),
        "decoded": decoded,
        "decoded_plausible": score,
        "decoded_unrecognised": unrecognised,
        "failed_short": failures,
        "kinds": kinds,
        "chunk_totals": totals,
        "examples": examples,
    }
    if wants_json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"archive={path}")
        print(f"entries={len(entries)} stored={len(stored)} png_control={positive}")
        print(f"sample={len(candidates)} decoded={decoded} failed_short={failures}")
        print(f"plausible={score} unrecognised={unrecognised}")
        print(f"kinds={kinds}")
        print(f"chunks={totals}")
        for item in examples:
            print(f"  #{item['index']} {item['kind']}: {item['head']!r}")

    if positive == 0:
        raise SystemExit("positive control failed: no stored PNG header found")
    if score == 0:
        raise SystemExit(
            "no plausible decoded entries; do not implement on this evidence"
        )
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--limit", type=int, default=200)
    parser.add_argument("--all", action="store_true", dest="all_entries")
    parser.add_argument("--json", action="store_true", dest="wants_json")
    args = parser.parse_args()
    raise SystemExit(probe(args.archive, args.limit, args.all_entries, args.wants_json))


if __name__ == "__main__":
    main()
