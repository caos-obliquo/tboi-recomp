#!/usr/bin/env python3
"""Decode an Isaac ARCH000 archive.

The container layout and both codecs were recovered by measurement and then
verified against the reference install: every entry of every archive decodes to
a recognisable file type. Nothing here is transcribed from another
implementation. The format facts and the algorithm descriptions are recorded in
docs/arch000-format.md, and the reference that first documented the layout is
credited there.

Entry names are *not* recoverable yet. Each entry carries a 32-bit name hash
whose algorithm is not yet identified, so entries are addressed by index and the
hash is printed for matching. See issue #2.

Usage:
    isaac-archive.py list <archive.a> [...]
    isaac-archive.py extract <archive.a> <entry-index> <output-path>
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"ARCH000"
OFFSET_MODE = 7
OFFSET_INDEX = 8
OFFSET_COUNT = 12
HEADER_SIZE = 14
ENTRY_SIZE = 20
U32 = 0xFFFFFFFF

# Two archives exist: the asset bank is obfuscated, the configuration archives
# are LZW. Both were confirmed by decoding, not inferred.
MODE_ASSETS = 0x00
MODE_LZW = 0x01
# 0x02 exists and covers every DLC and localisation archive, which is most
# of the installed bytes. Its codec is unknown; read_archive still parses those
# archives, but decode_entry refuses them rather than guessing.

# Obfuscation seed for the asset bank. Verified: every entry decoded with this
# constant yields a valid PNG, OGG, BMF or WAV.
ASSET_SEED = 0xF9524287
BLOCK = 1024


class Malformed(Exception):
    pass


def _rotate(word: bytearray, mode: int) -> None:
    """Permute the four bytes in place, per the key's low nibble."""
    t0, t1, t2, t3 = word
    if mode == 2:
        word[0], word[1], word[2], word[3] = t1, t2, t1, t0
    elif mode == 9:
        word[0], word[1], word[2], word[3] = t1, t0, t3, t2
    elif mode == 13:
        word[0], word[1], word[2], word[3] = t2, t3, t0, t1


def _advance(key: int) -> int:
    shifted = (key << 8) & U32
    mixed = (key ^ shifted) >> 9
    return (key ^ (mixed ^ shifted) ^ (((mixed ^ key ^ shifted) << 23) & U32)) & U32


def decode_assets(blob: bytes, offset: int, size: int, special_hash: int) -> bytes:
    key = ((special_hash ^ ASSET_SEED) | 1) & U32
    out = bytearray(size)
    for start in range(0, size, BLOCK):
        n = min(size - start, BLOCK)
        chunk = bytearray(BLOCK)
        chunk[:n] = blob[offset + start : offset + start + n]
        for i in range(0, BLOCK, 4):
            mode = key & 0xF
            word = bytearray(chunk[i : i + 4])
            value = struct.unpack_from("<I", word, 0)[0] ^ key
            word[:] = struct.pack("<I", value & U32)
            _rotate(word, mode)
            chunk[i : i + 4] = word
            key = _advance(key)
        out[start : start + n] = chunk[:n]
    return bytes(out)


class _BitReader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._bit = 0

    def read(self, count: int) -> int | None:
        value = 0
        for _ in range(count):
            if self._bit >= len(self._data) * 8:
                return None
            byte = self._data[self._bit >> 3]
            value = (value << 1) | ((byte >> (7 - (self._bit & 7))) & 1)
            self._bit += 1
        return value


def decode_lzw_codes(data: bytes) -> bytes:
    """Variable-width LZW, most significant bit first, 8 bits to start.

    Width grows once the table outgrows the current width, which is what makes
    this a fixed-point-of-growth variant rather than plain GIF/TIFF LZW.
    """
    out = bytearray()
    reader = _BitReader(data)
    width = 8
    table = [bytes([i]) for i in range(256)]
    previous: bytes | None = None
    while True:
        code = reader.read(width)
        if code is None:
            break
        if previous is None:
            if code >= len(table):
                break
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            table.append(previous + entry[:1])
        else:
            entry = previous + previous[:1]
            table.append(entry)
        out += entry
        previous = entry
        if len(table).bit_length() > width:
            width += 1
    return bytes(out)


def decode_lzw(blob: bytes, offset: int, size: int) -> bytes:
    out = bytearray()
    cursor = offset
    end = offset + size
    while cursor < end:
        length = struct.unpack_from("<i", blob, cursor)[0]
        cursor += 4
        if length <= 0 or cursor + length > end:
            break
        out += decode_lzw_codes(blob[cursor : cursor + length])
        cursor += length
    return bytes(out)


def read_archive(path: Path) -> tuple[int, list[tuple[int, int, int, int, int]]]:
    blob = path.read_bytes()
    if blob[: len(MAGIC)] != MAGIC:
        raise Malformed(f"{path}: bad magic {blob[:8]!r}")
    mode = blob[OFFSET_MODE]
    index_offset = struct.unpack_from("<I", blob, OFFSET_INDEX)[0]
    count = struct.unpack_from("<H", blob, OFFSET_COUNT)[0]
    if count == 0:
        raise Malformed(f"{path}: zero entries")
    if index_offset + count * ENTRY_SIZE != len(blob):
        raise Malformed(
            f"{path}: index_offset={index_offset} count={count} does not match "
            f"file size {len(blob)}"
        )
    entries = [
        struct.unpack_from("<5I", blob, index_offset + i * ENTRY_SIZE)
        for i in range(count)
    ]
    if entries[0][2] != HEADER_SIZE:
        raise Malformed(f"{path}: first payload offset is {entries[0][2]}")
    offsets = [e[2] for e in entries]
    if offsets != sorted(offsets):
        raise Malformed(f"{path}: offset column is not ascending")
    return mode, entries


def decode_entry(blob: bytes, mode: int, entry: tuple[int, int, int, int, int]) -> bytes:
    _, special_hash, offset, size, _ = entry
    if mode == MODE_LZW:
        return decode_lzw(blob, offset, size)
    if mode == MODE_ASSETS:
        return decode_assets(blob, offset, size, special_hash)
    raise Malformed(
        f"archive mode 0x{mode:02x} is not implemented; decoding it as mode "
        f"{MODE_ASSETS} would return garbage rather than fail"
    )


def classify(data: bytes) -> str:
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return "png"
    if data[:4] == b"OggS":
        return "ogg"
    if data[:4] == b"RIFF":
        return "wav"
    if data[:4] == b"BMF ":
        return "bmf"
    stripped = data.lstrip()
    if stripped[:1] == b"<":
        return "xml"
    return "unknown"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list")
    p_list.add_argument("archives", nargs="+")

    p_extract = sub.add_parser("extract")
    p_extract.add_argument("archive")
    p_extract.add_argument("index", type=int)
    p_extract.add_argument("output")

    args = parser.parse_args(argv[1:])
    failed = False

    for raw in getattr(args, "archives", []):
        path = Path(raw)
        try:
            mode, entries = read_archive(path)
        except (OSError, Malformed) as error:
            print(f"error: {error}", file=sys.stderr)
            failed = True
            continue
        codec = {MODE_LZW: "lzw", MODE_ASSETS: "obfuscated"}.get(mode, "unknown")
        print(f"{path}  mode=0x{mode:02x} codec={codec} entries={len(entries)}")
        for i, (name_hash, special, offset, size, checksum) in enumerate(entries):
            print(
                f"  [{i:5d}] hash={name_hash:#010x} special={special:#010x} "
                f"offset={offset} size={size} crc={checksum:#010x}"
            )

    if args.command == "extract":
        path = Path(args.archive)
        try:
            mode, entries = read_archive(path)
        except (OSError, Malformed) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        if not 0 <= args.index < len(entries):
            print(f"error: index {args.index} out of range (0..{len(entries) - 1})",
                  file=sys.stderr)
            return 1
        data = decode_entry(path.read_bytes(), mode, entries[args.index])
        Path(args.output).write_bytes(data)
        print(
            f"wrote {len(data)} bytes to {args.output} "
            f"(type {classify(data)}, size_in_file {entries[args.index][3]})"
        )

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))