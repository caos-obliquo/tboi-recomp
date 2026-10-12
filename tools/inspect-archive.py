#!/usr/bin/env python3
"""Parse and validate an Isaac ARCH000 archive.

Everything this prints is checked, not assumed: the index arithmetic is asserted
rather than trusted, so a wrong interpretation of the format makes the tool exit
non-zero instead of printing a plausible table.

Usage:
    inspect-archive.py <archive.a> [...]        validate and summarise
    inspect-archive.py --entries <archive.a>    print the raw index records

The game binary is not needed and is never read.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass

MAGIC = b"ARCH000"
OFFSET_MODE = 7
OFFSET_INDEX = 8
OFFSET_COUNT = 12
HEADER_SIZE = 14
ENTRY_SIZE = 20


class Malformed(Exception):
    pass


@dataclass(frozen=True)
class Entry:
    hash_lo: int
    hash_hi: int
    offset: int
    raw_size: int
    crc: int

    @property
    def name_hash(self) -> int:
        return (self.hash_hi << 32) | self.hash_lo


@dataclass(frozen=True)
class Archive:
    path: str
    mode: int
    index_offset: int
    entries: tuple[Entry, ...]
    size: int

    def spans(self) -> tuple[int, ...]:
        """Stored size of each entry: the gap to the next offset."""
        bounds = [e.offset for e in self.entries] + [self.index_offset]
        return tuple(bounds[i + 1] - bounds[i] for i in range(len(self.entries)))


def parse(path: str) -> Archive:
    with open(path, "rb") as handle:
        blob = handle.read()

    if blob[: len(MAGIC)] != MAGIC:
        raise Malformed(f"{path}: bad magic {blob[:8]!r}")

    mode = blob[OFFSET_MODE]
    index_offset = struct.unpack_from("<I", blob, OFFSET_INDEX)[0]
    count = struct.unpack_from("<H", blob, OFFSET_COUNT)[0]

    if index_offset + count * ENTRY_SIZE != len(blob):
        raise Malformed(
            f"{path}: index_offset={index_offset} count={count} implies "
            f"{index_offset + count * ENTRY_SIZE} but file is {len(blob)} bytes"
        )

    if count == 0:
        raise Malformed(f"{path}: zero entries")

    if index_offset < HEADER_SIZE:
        raise Malformed(
            f"{path}: index_offset={index_offset} overlaps the {HEADER_SIZE}-byte header"
        )

    entries = []
    for i in range(count):
        base = index_offset + i * ENTRY_SIZE
        entries.append(Entry(*struct.unpack_from("<5I", blob, base)))

    if entries[0].offset != HEADER_SIZE:
        raise Malformed(
            f"{path}: first payload offset is {entries[0].offset}, expected {HEADER_SIZE}"
        )

    offsets = [e.offset for e in entries]
    if offsets != sorted(offsets) or len(set(offsets)) != len(offsets):
        raise Malformed(f"{path}: offset column is not strictly ascending")

    return Archive(path, mode, index_offset, tuple(entries), len(blob))


def summarise(archive: Archive) -> str:
    spans = archive.spans()
    payload = archive.index_offset - HEADER_SIZE
    stored = sum(spans)
    lines = [
        f"{archive.path}",
        f"  size          {archive.size}",
        f"  mode          0x{archive.mode:02x}",
        f"  index offset  {archive.index_offset}",
        f"  entries       {len(archive.entries)}",
        f"  payload       {payload} bytes "
        f"({stored} accounted for, {payload - stored} unaccounted)",
        "  entry        offset  raw_size   stored  ratio",
    ]
    for entry, span in zip(archive.entries, spans):
        ratio = span / entry.raw_size if entry.raw_size else 0.0
        lines.append(
            f"  {entry.hash_hi:08x}{entry.hash_lo:08x}  {entry.offset:8d}  "
            f"{entry.raw_size:9d} {span:7d}  {ratio:5.2f}"
        )
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", nargs="+")
    parser.add_argument(
        "--entries",
        action="store_true",
        help="print every index record rather than a summary",
    )
    args = parser.parse_args(argv[1:])

    failed = False
    for path in args.archives:
        try:
            archive = parse(path)
        except (OSError, Malformed) as error:
            print(f"error: {error}", file=sys.stderr)
            failed = True
            continue
        print("\n".join(f"  {e.name_hash:016x} {e}" for e in archive.entries)
              if args.entries else summarise(archive))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))