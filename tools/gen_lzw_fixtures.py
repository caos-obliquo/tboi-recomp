#!/usr/bin/env python3
"""Generate tests/lzw_fixtures.inc from tools/isaac-archive.py.

The expected bytes come from the reference decoder, so the generated C++
fixtures assert byte-for-byte parity with the Python port of the game's LZW
natives -- not self-consistency with the C++ implementation.

Only synthetic cases are emitted. They are deterministic chunk-prefixed blobs;
several carry more than one chunk so that a decoder which rebuilds the
dictionary per chunk diverges from the expected bytes. One case declares a
recorded size shorter than the decoded output, for the container layer.

No game bytes are committed: the fixtures assert parity with the Python port of
the REA-recovered natives, not with a captured archive slice.

Usage: gen_lzw_fixtures.py [repo-root]
"""

import glob
import importlib.util
import os
import struct
import sys


def load_arc(repo_root):
    path = glob.glob(os.path.join(repo_root, 'tools', 'isaac-archive.py'))
    if not path:
        raise SystemExit('cannot find tools/isaac-archive.py under %s' % repo_root)
    spec = importlib.util.spec_from_file_location('isaac_archive', path[0])
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def lcg_blob(size, seed):
    out = bytearray()
    state = seed & 0xFFFFFFFF
    for _ in range(size):
        state = (state * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((state >> 16) & 0xFF)
    return bytes(out)


def chunk(payload):
    return struct.pack('<i', len(payload)) + payload


class LiteralEncoder:
    """Emit literal LZW codes with the dictionary/width the decoder expects.

    Only literal codes are used, so the decoded bytes are the plaintext. State
    survives across chunks while the bit accumulator is per chunk, exactly as
    in 0xa85320. Plaintexts stay under 0x1000 codes so the reset arm is not
    needed here.
    """

    def __init__(self):
        self.next_code = 0x100
        self.width = 8

    def encode_chunk(self, data):
        acc = 0
        bits = 0
        out = bytearray()
        for index, byte in enumerate(data):
            # The decoder grows the width before the read and inserts only
            # after it. Doing either one code early shifts every subsequent
            # bit boundary, so keep the state update in that order.
            if index:
                while (1 << self.width) <= self.next_code:
                    self.width += 1
            acc = (acc << self.width) | byte
            bits += self.width
            while bits >= 8:
                bits -= 8
                out.append((acc >> bits) & 0xFF)
                acc &= (1 << bits) - 1
            if index:
                self.next_code += 1  # decoder inserts after the code
        if bits:
            out.append((acc << (8 - bits)) & 0xFF)
        return bytes(out)

    def encode(self, data, chunk_size):
        blob = bytearray()
        for start in range(0, len(data), chunk_size):
            blob += chunk(self.encode_chunk(data[start:start + chunk_size]))
        assert self.next_code < 0x1000
        return bytes(blob)


def synthetic_cases():
    """(name, compressed blob, recorded decompressed size)."""
    cases = []

    cases.append(('single', chunk(b'A'), None))
    literals = bytes([0, 7, 14, 21, 28, 35, 42, 49, 56, 63, 70, 77, 84, 91,
                      98, 105, 112, 119, 126, 133])
    cases.append(('literals', chunk(literals), None))

    # Long single chunk: width must grow past 8 before each read.
    plain = lcg_blob(3000, 0x5EED)
    cases.append(('wide_single', LiteralEncoder().encode(plain, 3000), None))

    # Three 1024-byte chunks: the dictionary of chunk N is visible to N+1.
    plain = lcg_blob(3072, 0xC0FFEE)
    cases.append(('multi_chunk_1024', LiteralEncoder().encode(plain, 1024), None))

    # Short chunks cross bit boundaries at awkward offsets.
    plain = lcg_blob(2400, 0xA5A50000)
    cases.append(('multi_chunk_200', LiteralEncoder().encode(plain, 200), None))

    # A truncated final chunk must not be decoded; the walk stops on a length
    # prefix that runs past the span.
    blob = LiteralEncoder().encode(lcg_blob(200, 0x1234), 200) + b'\xff\xff\xff\x7f'
    cases.append(('trailing_garbage', blob, None))
    return cases


def emit_bytes(handle, name, data):
    handle.write('static const u8 %s[] = {' % name)
    for i, byte in enumerate(data):
        if i % 24 == 0:
            handle.write('\n    ')
        handle.write('%d,' % byte)
    handle.write('\n};\n')


def main():
    repo_root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    arc = load_arc(repo_root)

    cases = synthetic_cases()
    print('fixture cases: %d' % len(cases))

    chunks = []
    total_in = 0
    total_out = 0
    for name, blob, size in cases:
        expected = arc.decode_lzw(blob, 0, len(blob))
        assert len(expected) > 0, name
        if name == 'multi_chunk_1024':
            assert len(expected) == 3072, len(expected)  # encoder control
        if name == 'multi_chunk_200':
            assert len(expected) == 2400, len(expected)  # encoder control
        if size is None:
            size = len(expected)
        if name == 'multi_chunk_1024':
            # The recorded size need not equal the decoder's full output; the
            # container layer must truncate it. This is the shape of the real
            # entries whose final chunk over-produces.
            size = len(expected) - 17
        assert 0 < size <= len(expected), (name, size, len(expected))
        chunks.append((name, blob, size, expected))
        total_in += len(blob)
        total_out += len(expected)

    # Positive control: a multi-chunk case must cross several boundaries,
    # otherwise a per-chunk-decoder regression would still pass the suite.
    assert any(name.startswith('multi_chunk_') and len(expected) > 1024
               for name, _, _, expected in chunks)

    out_path = os.path.join(repo_root, 'tests', 'lzw_fixtures.inc')
    tmp = out_path + '.tmp'
    with open(tmp, 'w') as handle:
        handle.write('// GENERATED by tools/gen_lzw_fixtures.py -- do not edit by hand.\n')
        handle.write('// Expected outputs come from the reference decoder in\n')
        handle.write('// tools/isaac-archive.py, which was ported from the game natives\n')
        handle.write('// 0xa850f0 / 0xa85090 / 0xa85020 / 0xa85320 (REA/Ghidra).\n')
        handle.write('//\n')
        handle.write('// `input_size` is the COMPRESSED span; `size` is the recorded\n')
        handle.write('// DECOMPRESSED length. They differ in the container-layer case so the\n')
        handle.write('// caller has to truncate the decoded bytes to `size`.\n\n')
        handle.write('struct LzwFixture {\n')
        handle.write('    const char* name;\n')
        handle.write('    const u8* input; usize input_size;\n')
        handle.write('    usize size;\n')
        handle.write('    const u8* expected; usize expected_size;\n')
        handle.write('};\n\n')

        tags = {}
        for name, blob, size, expected in chunks:
            tag = ''.join(ch if ch.isalnum() else '_' for ch in name)
            tags[name] = tag
            emit_bytes(handle, 'kLzwIn_' + tag, blob)
            emit_bytes(handle, 'kLzwEx_' + tag, expected)
            handle.write('\n')

        handle.write('static const LzwFixture kLzwFixtures[] = {\n')
        for name, blob, size, expected in chunks:
            handle.write('    {"%s", kLzwIn_%s, %d, %d, kLzwEx_%s, %d},\n'
                         % (name, tags[name], len(blob), size,
                            tags[name], len(expected)))
        handle.write('};\n\n')
        handle.write('static const usize kLzwFixtureCount = %d;\n' % len(chunks))
    os.replace(tmp, out_path)

    print('wrote %s' % out_path)
    print('  fixture input bytes:  %d' % total_in)
    print('  fixture output bytes: %d' % total_out)
    print('  file size:           %d' % os.path.getsize(out_path))


if __name__ == '__main__':
    main()
