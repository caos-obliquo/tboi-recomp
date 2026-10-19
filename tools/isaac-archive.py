#!/usr/bin/env python3
"""Decode an Isaac ARCH000 archive.

The container layout and both codecs were recovered by measurement and then
verified against the reference install: every entry of every archive decodes to
a recognisable file type. Nothing here is transcribed from another
implementation. The format facts and the algorithm descriptions are recorded in
docs/arch000-format.md, and the reference that first documented the layout is
credited there.

Mode 0x02 entries carry a nine-byte record header in front of the payload. When
that header declares a stored entry the payload is plaintext and needs no cipher
at all; this is how 965 PNGs in repentance.a and afterbirthp.a were recovered.

Entry names are recovered by hashing the full lowercased resource path with
djb2-add (seed 5381, multiplier 33). The hash was confirmed by 22186 matches
against the ResourceExtractor filelist that ships with the game, which resolves
81.46% of every entry across all 22 archives. The list is found automatically
next to the archives; --names overrides it. Unresolved entries stay addressable
by index and their raw hash is still printed.

Usage:
    isaac-archive.py list <archive.a> [...]
    isaac-archive.py extract <archive.a> <entry-index> <output-path>
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path
from typing import NamedTuple

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
# of the installed bytes. Its codec is the 64-bit LCG keystream below,
# recovered from disassembly at VA 0xaa93e0 and reached via the wrapper at
# 0xa89c90, which seeds it with each entry's specialHash.
MODE_PRNG = 0x02

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
        word[0], word[1], word[2], word[3] = t3, t2, t1, t0
    elif mode == 9:
        word[0], word[1], word[2], word[3] = t1, t0, t3, t2
    elif mode == 13:
        word[0], word[1], word[2], word[3] = t2, t3, t0, t1


def _advance(key: int) -> int:
    shifted = (key << 8) & U32
    mixed = (key ^ shifted) >> 9
    return (key ^ (mixed ^ shifted) ^ (((mixed ^ key ^ shifted) << 23) & U32)) & U32


def decode_assets(
    blob: bytes, offset: int, size: int, special_hash: int, span: int | None = None
) -> bytes:
    """Decode one obfuscated asset entry.

    ``size`` is the recorded asset length and is what the caller wants. ``span``
    is how many bytes the cipher actually consumed, which can exceed ``size``:
    the obfuscator worked in whole 1024-byte blocks, so the last block of an
    entry that is not block-aligned borrows bytes from the gap in front of the
    next entry. Those gap bytes are padding, not payload, so the result is
    always truncated back to ``size``.

    Measured over all 2300 entries of graphics.a, using ``span`` in place of
    ``size`` takes every chunk CRC of every entry from 1982/2300 to 2300/2300.
    """
    key = ((special_hash ^ ASSET_SEED) | 1) & U32
    limit = size if span is None else span
    out = bytearray(size)
    for start in range(0, limit, BLOCK):
        n = min(limit - start, BLOCK)
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
        keep = min(n, size - start)
        if keep > 0:
            out[start : start + keep] = chunk[:keep]
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


class PrngStream:
    """The 64-bit LCG keystream generator at VA 0xaa93e0.

    Disassembled, not transcribed: the arm reached by mode 0x02 allocates two
    buffers and calls this generator with the entry's specialHash as the seed,
    then XORs the keystream over the payload. It is a plain linear congruential
    generator and NOT ISAAC, despite an earlier revision of this file asserting
    otherwise. Details in docs/arch000-format.md section 9.
    """

    MULT = 0x4C957F2D5851F42D
    INC = 0x7F

    def __init__(self, seed: int) -> None:
        h = ((seed << 15) ^ seed) & U32
        h ^= seed >> 9
        h = ((h << 8) ^ seed) & U32
        self.state = (((seed & U32) << 32) | h) & 0xFFFFFFFFFFFFFFFF

    def next(self) -> int:
        self.state = (self.state * self.MULT + self.INC) & 0xFFFFFFFFFFFFFFFF
        hi = (self.state >> 32) & U32
        v = ((self.state >> 27) & U32) ^ (hi >> 13)
        n = hi >> 27
        # The generator derives 32-n with neg/and, so n == 0 would ask for a
        # shift by 32. That is undefined on a 32-bit register in the C++ port;
        # masking keeps the two implementations byte-identical rather than
        # relying on the trailing & U32 to happen to absorb it here.
        return ((v >> n) | (v << ((32 - n) & 31))) & U32


def decode_prng(
    blob: bytes, offset: int, size: int, special_hash: int
) -> bytes:
    out = bytearray(blob[offset : offset + size])
    prng = PrngStream(special_hash)
    # The loop advances by whole words but consumes a word whenever any byte of
    # it remains, so a payload whose length is not a multiple of four still has
    # its tail XORed by the leading bytes of the next word. Rounding the range
    # down to the last whole word would leave that tail in plaintext, which the
    # differential fixture test rejects.
    for i in range(0, len(out), 4):
        word = prng.next()
        for byte_index in range(4):
            if i + byte_index < len(out):
                out[i + byte_index] ^= (word >> (8 * byte_index)) & 0xFF
    return bytes(out)


def asset_span(
    entries: list[tuple[int, int, int, int, int]], index: int, blob_size: int
) -> int:
    """How many bytes the asset cipher consumed for entry ``index``.

    Always measured as the distance to the next entry's recorded offset (or to
    the end of the blob for the last entry), never merely this entry's recorded
    size. Entries are not contiguous: measured gaps across the installed
    archives are 0, 1, 2 and 3 bytes, and every entry whose gap was non-zero
    decoded with a corrupt trailing chunk CRC until the gap bytes were included.
    Those bytes are padding consumed by the final block; they are never asset
    payload, so the decoded result is truncated back to the recorded size.

    Falls back to the recorded size when the table is not ordered by offset,
    which happens for the LZW archives.
    """
    offset = entries[index][2]
    limit = entries[index + 1][2] if index + 1 < len(entries) else blob_size
    if limit <= offset:
        return entries[index][3]
    return limit - offset


RECORD_HEADER_SIZE = 9


class RecordHeader(NamedTuple):
    """The nine bytes that precede a mode-0x02 entry's payload.

    Measured across repentance.a, afterbirthp.a and afterbirth.a; see
    docs/arch000-format.md section 10.
    """

    declared_total: int  # [0:2] little endian, equals size + 5
    marker: int          # [2]
    kind: int            # [3]; 0x80 marks a plaintext ("stored") entry
    flavour: int         # [4]
    declared_size: int   # [5:7] little endian, equals size
    length_check: int    # [7], equals ~size & 0xFF
    trailer: int         # [8]


def record_header(blob: bytes, offset: int) -> RecordHeader | None:
    """Parse the record header at ``offset``, or None if it is not there."""
    raw = blob[offset : offset + RECORD_HEADER_SIZE]
    if len(raw) < RECORD_HEADER_SIZE:
        return None
    return RecordHeader(
        declared_total=struct.unpack_from("<H", raw, 0)[0],
        marker=raw[2],
        kind=raw[3],
        flavour=raw[4],
        declared_size=struct.unpack_from("<H", raw, 5)[0],
        length_check=raw[7],
        trailer=raw[8],
    )


def is_stored(blob: bytes, entry: tuple[int, int, int, int, int]) -> bool:
    """True when this entry's payload sits behind the header in the clear.

    ``declared_total == size + 5`` is a perfect discriminator on the installed
    archives: it selects 378/4180 entries of repentance.a, 587/10228 of
    afterbirthp.a and 0/2647 of afterbirth.a, which are exactly the counts of
    entries holding a PNG magic number. Those payloads need no cipher at all.
    """
    _, _, offset, size, _ = entry
    head = record_header(blob, offset)
    if head is None or head.declared_total != size + 5 or head.declared_size != size:
        return False
    return head.length_check == (~size) & 0xFF



def decode_entry(
    blob: bytes,
    mode: int,
    entry: tuple[int, int, int, int, int],
    span: int | None = None,
) -> bytes:
    _, special_hash, offset, size, _ = entry
    if mode == MODE_LZW:
        return decode_lzw(blob, offset, size)
    if mode == MODE_PRNG:
        if is_stored(blob, entry):
            return blob[
                offset + RECORD_HEADER_SIZE : offset + RECORD_HEADER_SIZE + size
            ]
        return decode_prng(blob, offset, size, special_hash)
    if mode == MODE_ASSETS:
        return decode_assets(blob, offset, size, special_hash, span)
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


def resource_name_hash(path: str) -> int:
    """Hash a resource path the way the archives do.

    djb2-add seeded with 5381 and multiplier 33, over the whole path including
    the extension, lowercased. Confirmed against 22186 shipped entries; the
    ResourceExtractor filelist hashes to 18789 distinct values with no
    collisions, so a match is unambiguous.
    """
    h = 5381
    for byte in path.lower().encode("latin-1", "replace"):
        h = (h * 33 + byte) & U32
    return h


def load_name_table(source, archive):
    """Map name hash -> resource path from a ResourceExtractor filelist.

    Without a source, looks for <install>/tools/ResourceExtractor/filelist.txt,
    which is where the game keeps it relative to resources/packed/. ISAEC_FILELIST
    overrides the search.
    """
    if source is None:
        source = os.environ.get("ISAEC_FILELIST")
    if source is None:
        guess = archive.parent.parent.parent / "tools" / "ResourceExtractor" / "filelist.txt"
        source = str(guess) if guess.is_file() else None
    if source is None:
        print("note: no name table; pass --names <filelist.txt>", file=sys.stderr)
        return {}
    try:
        lines = Path(source).read_text(errors="replace").splitlines()
    except OSError as error:
        print(f"note: {error}", file=sys.stderr)
        return {}
    table = {}
    for line in lines:
        text = line.strip()
        if text:
            table.setdefault(resource_name_hash(text), text)
    return table


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list")
    p_list.add_argument("archives", nargs="+")
    p_list.add_argument("--names", help="ResourceExtractor filelist.txt")

    p_extract = sub.add_parser("extract")
    p_extract.add_argument("archive")
    p_extract.add_argument("index", type=int)
    p_extract.add_argument("output")
    p_extract.add_argument("--names", help="ResourceExtractor filelist.txt")

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
        codec = {
            MODE_LZW: "lzw",
            MODE_ASSETS: "obfuscated",
            MODE_PRNG: "prng",
        }.get(mode, "unknown")
        print(f"{path}  mode=0x{mode:02x} codec={codec} entries={len(entries)}")
        blob_size = path.stat().st_size
        blob = path.read_bytes() if mode == MODE_PRNG else b""
        stored = 0
        names = load_name_table(args.names, path)
        named = 0
        for i, (entry_hash, special, offset, size, checksum) in enumerate(entries):
            state = ""
            if mode == MODE_PRNG:
                state = "stored" if is_stored(blob, entries[i]) else "prng"
                stored += state == "stored"
            resolved = names.get(entry_hash)
            named += resolved is not None
            print(
                f"  [{i:5d}] hash={entry_hash:#010x} special={special:#010x} "
                f"offset={offset} size={size} crc={checksum:#010x} "
                f"span={asset_span(entries, i, blob_size)} {state} "
                f"{resolved if resolved else '-'}"
            )
        if mode == MODE_PRNG:
            print(f"  {stored}/{len(entries)} entries are stored in the clear")
        if names:
            print(f"  {named}/{len(entries)} named ({100.0 * named / len(entries):.2f}%)")

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
        blob = path.read_bytes()
        entry = entries[args.index]
        span = asset_span(entries, args.index, len(blob))
        data = decode_entry(blob, mode, entry, span)[: entry[3]]
        Path(args.output).write_bytes(data)
        stored = mode == MODE_PRNG and is_stored(blob, entry)
        how = "stored (no cipher)" if stored else f"cipher_span {span}"
        resolved = load_name_table(args.names, path).get(entry[0], "-")
        print(
            f"wrote {len(data)} bytes to {args.output} ({resolved}, "
            f"type {classify(data)}, size_in_file {entry[3]}, {how})"
        )

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))