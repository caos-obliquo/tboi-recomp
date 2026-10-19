#!/usr/bin/env python3
"""Generate tests/bogocrypt1_fixtures.inc.

Expected outputs come from the reference decoder in tools/isaac-archive.py, so
the generated fixtures assert that the C++ Bogocrypt1 port is byte-identical
to the Python reference -- not merely self-consistent.

Two families of cases are emitted:

  synthetic  Deterministic blobs at the block boundaries the format cares
             about. 1024-byte block padding is the whole reason this port was
             ever wrong, so the sizes straddle 1024 and 2048 deliberately, and
             several cases carry a cipher span larger than the asset size --
             the shape of the 318 graphics.a entries whose IEND CRC came out
             corrupt before the span rule was found.

  real       Actual entries lifted out of the shipped archives, so the fixtures
             are anchored to game data rather than only to synthetic blobs.

Usage: gen_bogocrypt1_fixtures.py [repo-root]
"""

import glob
import importlib.util
import os
import struct
import sys

BLOCK = 1024
MAX_REAL_BYTES = 1500
REAL_TARGET = 6


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


def synthetic_cases():
    """(name, special_hash, blob, size, span)."""
    cases = []

    # Sub-block: the tail word is partial, so the old unguarded swap wrote past
    # the end of the buffer and the old flat walk used a different key schedule
    # than the padded one.
    cases.append(('partial_word', 0x9BB3F301, lcg_blob(37, 0xC0FFEE), 37, 37))
    cases.append(('partial_word_single_byte', 0x00000001, lcg_blob(1, 0x1234), 1, 1))
    cases.append(('three_bytes', 0xDEADBEEF, lcg_blob(3, 0x5678), 3, 3))

    # span > size: exactly the graphics.a entry-0 shape that needs the gap byte.
    cases.append(('span_one_over_size', 0x9BB3F301, lcg_blob(39, 0xC0FFEE), 37, 39))
    cases.append(('span_three_over_size', 0x12345678, lcg_blob(44, 0xABCD), 41, 44))

    # Exact block boundaries.
    cases.append(('one_byte_short_of_block', 0xA5A5A5A5, lcg_blob(1023, 0x11), 1023, 1023))
    cases.append(('exactly_one_block', 0xA5A5A5A5, lcg_blob(1024, 0x22), 1024, 1024))
    cases.append(('one_past_block', 0xA5A5A5A5, lcg_blob(1027, 0x33), 1027, 1027))
    cases.append(('two_blocks_exact', 0x5A5A5A5A, lcg_blob(2048, 0x44), 2048, 2048))
    cases.append(('two_blocks_and_a_tail', 0x5A5A5A5A, lcg_blob(2051, 0x55), 2049, 2051))

    return cases


def real_cases(repo_root, arc):
    packed = os.path.expanduser(
        '~/.local/share/Steam/steamapps/common/'
        'The Binding of Isaac Rebirth/resources/packed')
    if not os.path.isdir(packed):
        print('note: %s absent, skipping real fixtures' % packed)
        return []

    out = []
    for name in sorted(os.listdir(packed)):
        if not name.endswith('.a'):
            continue
        path = os.path.join(packed, name)
        try:
            mode, entries = arc.read_archive(__import__('pathlib').Path(path))
        except Exception as exc:                      # noqa: BLE001
            print('note: skipping %s (%s)' % (name, exc))
            continue
        if mode != arc.MODE_ASSETS:
            continue

        blob = open(path, 'rb').read()
        for index, entry in enumerate(entries):
            _, special, offset, size, _ = entry
            if size > MAX_REAL_BYTES or size == 0:
                continue
            span = arc.asset_span(entries, index, len(blob))
            if span != size and span != size + 1:
                # Only the interesting shapes: exact fit, or one borrowed byte.
                continue
            out.append(('%s_%d' % (name[:-2], index), special,
                        blob[offset:offset + span], size, span))
            if len(out) >= REAL_TARGET:
                return out
    return out


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
    real = real_cases(repo_root, arc)
    print('synthetic cases: %d' % len(cases))
    print('real cases:      %d' % len(real))

    total_in = 0
    total_out = 0
    chunks = []
    for name, special, blob, size, span in cases + real:
        assert len(blob) == span, (name, len(blob), span)
        decoded = arc.decode_assets(blob, 0, size, special, span)
        assert len(decoded) == size, (name, len(decoded), size)
        chunks.append((name, special, blob, size, span, decoded))
        total_in += span
        total_out += size

    out_path = os.path.join(repo_root, 'tests', 'bogocrypt1_fixtures.inc')
    tmp = out_path + '.tmp'
    with open(tmp, 'w') as handle:
        handle.write('// GENERATED by tools/gen_bogocrypt1_fixtures.py -- do not edit by hand.\n')
        handle.write('// Expected outputs come from the reference decoder in\n')
        handle.write('// tools/isaac-archive.py, so these assert the C++ port is byte-identical\n')
        handle.write('// to the Python reference rather than merely self-consistent.\n')
        handle.write('//\n')
        handle.write('// `span` is how many bytes the cipher must cover and is always equal to\n')
        handle.write('// input_size; `size` is the asset length and may be shorter. Bogocrypt1\n')
        handle.write('// pads every block to a whole number of words, so decoding with `size`\n')
        handle.write('// instead of `span` corrupts the tail of the asset.\n\n')
        handle.write('struct Bogocrypt1Fixture {\n')
        handle.write('    const char* name;\n')
        handle.write('    u32 special_hash;\n')
        handle.write('    const u8* input; usize input_size; usize size;\n')
        handle.write('    const u8* expected;\n')
        handle.write('};\n')

        tags = {}
        for name, special, blob, size, span, decoded in chunks:
            tag = ''.join(ch if ch.isalnum() else '_' for ch in name)
            tags[name] = tag
            emit_bytes(handle, 'kBc1In_' + tag, blob)
            emit_bytes(handle, 'kBc1Ex_' + tag, decoded)

        handle.write('\nstatic const Bogocrypt1Fixture kBogocrypt1Fixtures[] = {\n')
        for name, special, blob, size, span, decoded in chunks:
            tag = tags[name]
            handle.write('    {"%s", 0x%08Xu, kBc1In_%s, %d, %d, kBc1Ex_%s},\n'
                         % (name, special, tag, span, size, tag))
        handle.write('};\n')
    os.replace(tmp, out_path)

    print('wrote %s' % out_path)
    print('  fixture input bytes:  %d' % total_in)
    print('  fixture output bytes: %d' % total_out)
    print('  file size:           %d' % os.path.getsize(out_path))


if __name__ == '__main__':
    main()