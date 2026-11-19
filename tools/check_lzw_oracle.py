#!/usr/bin/env python3
"""Game-side oracle for the C++ mode-0x01 LZW decoder (issue #107).

Runs build/isaec_lzw_oracle against a REAL mode-0x01 archive supplied at
runtime, then compares every decoded entry byte-for-byte with the Python
reference in tools/isaac-archive.py. Nothing from the game is committed: the
archive path is an argument and the C++ tool writes only a framed binary
stream to stdout (u32 LE index, u32 LE decoded size, raw bytes).

Usage: tools/check_lzw_oracle.py [repo-root] [path/to/config.a]

Exit status 0 means every entry matches; 1 means at least one mismatch or a
decode failure. A negative control (e.g. restoring the old per-chunk LZW
dictionary in src/data/codec.cpp) must make this script fail.
"""

import hashlib
import importlib.util
import os
import pathlib
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET


def load_reference(repo_root):
    path = os.path.join(repo_root, 'tools', 'isaac-archive.py')
    spec = importlib.util.spec_from_file_location('isaac_archive', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_stream(stream):
    records = {}
    offset = 0
    while offset < len(stream):
        if offset + 8 > len(stream):
            raise ValueError('truncated record header at offset %d' % offset)
        index, size = struct.unpack_from('<II', stream, offset)
        offset += 8
        if offset + size > len(stream):
            raise ValueError('truncated payload for entry %d' % index)
        if index in records:
            raise ValueError('duplicate record for entry %d' % index)
        records[index] = stream[offset:offset + size]
        offset += size
    return records


def main():
    repo_root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    archive = pathlib.Path(
        sys.argv[2] if len(sys.argv) > 2 else
        os.path.expanduser('~/.local/share/Steam/steamapps/common/'
                           'The Binding of Isaac Rebirth/resources/packed/config.a'))
    if not archive.is_file():
        raise SystemExit('archive not found: %s' % archive)

    oracle = os.path.join(repo_root, 'build', 'isaec_lzw_oracle')
    if not os.path.isfile(oracle):
        raise SystemExit('oracle not built: %s (run cmake --build build)' % oracle)

    arc = load_reference(repo_root)
    blob = archive.read_bytes()
    mode, entries = arc.read_archive(archive)
    if mode != arc.MODE_LZW:
        raise SystemExit('%s is mode 0x%02x, expected 0x01' % (archive, mode))

    process = subprocess.run([oracle, str(archive)], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
    if process.returncode != 0:
        raise SystemExit('oracle exited %d: %s' %
                         (process.returncode, process.stderr.decode('utf-8', 'replace')))
    if process.stderr:
        print('oracle stderr:', process.stderr.decode('utf-8', 'replace'))

    actual = parse_stream(process.stdout)
    print('archive: %s' % archive)
    print('entries: %d (C++ records: %d)' % (len(entries), len(actual)))
    print('%-6s %-9s %-9s %-10s %s' % ('index', 'expected', 'actual', 'sha256', 'status'))

    failures = 0
    for index, entry in enumerate(entries):
        span = arc.asset_span(entries, index, len(blob))
        expected = arc.decode_entry(blob, mode, entry, span)
        got = actual.get(index)
        digest = hashlib.sha256(expected).hexdigest()[:10]
        if got is None:
            status = 'MISSING'
        elif len(got) != len(expected):
            status = 'SIZE %d != %d' % (len(got), len(expected))
        elif got != expected:
            first = next(i for i in range(len(expected)) if got[i] != expected[i])
            status = 'BYTE %d' % first
        else:
            status = 'ok'
            if expected.lstrip().startswith(b'<?xml'):
                try:
                    ET.fromstring(expected)
                except ET.ParseError as exc:
                    status = 'XML %s' % exc
                if b'The Sad Onion' in expected:
                    if len(expected) != 51228 or b'id="1"' not in expected:
                        status = 'ITEMS'
        if status != 'ok':
            failures += 1
        print('%-6d %-9d %-9d %-10s %s' % (
            index, len(expected), len(got) if got is not None else -1, digest, status))

    if set(actual) != set(range(len(entries))):
        failures += 1
        print('record index set differs from the entry table')

    print('result: %d/%d entries match' % (len(entries) - failures, len(entries)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
