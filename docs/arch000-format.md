# The `ARCH000` archive format

Measured from the user's own copy of the game. Everything below was verified by a
script, not inferred. Reproduction: `tools/inspect-archive.py`.

## 1. File layout

```
0x00  8 bytes   magic: "ARCH000" + one mode byte
0x08  u32       offset of the entry index
0x0C  u16       entry count
0x0E  ...       payload, entry 0 first
index  count * 20 bytes, at the end of the file
```

The index offset is exact, not approximate. For every archive measured,
`index_offset + count * 20 == file_size`:

| file | size | index offset | count | count × 20 |
|---|---|---|---|---|
| `animations.a` | 660 301 | 660 281 | 1 | 20 |
| `fonts.a` | 15 061 | 14 861 | 10 | 200 |
| `rooms.a` | 655 906 | 655 646 | 13 | 260 |
| `config.a` | 130 681 | 130 201 | 24 | 480 |

The header is therefore exactly **14 bytes** and the payload begins at offset 14.
This is independently confirmed: the bytes at offset 14 *are* the first bytes of
entry 0's payload, so a naive read of a `u16` at offset 14 yields entry 0's first
two payload bytes rather than a header field.

Entry 0's offset column is `14` in every archive measured.

## 2. Entry layout (20 bytes, little-endian)

| offset | field | evidence |
|---|---|---|
| +0x00 | `hash_lo` | high-entropy; unknown algorithm |
| +0x04 | `hash_hi` | high-entropy; unknown algorithm |
| +0x08 | `offset` | monotone ascending, first is 14, all below the index offset |
| +0x0C | `stored_size` | size of the *stored* bytes |
| +0x10 | `crc` | high-entropy |

The `offset` column is strictly increasing across entries in every archive
measured, which is what identifies it. The other four columns are not monotone
and their values are uniformly distributed over 32 bits.

This column is the **stored** size, not the decompressed size. An earlier version
of this document had it backwards, which produced a convincing but wrong reading
of the compression ratio. The tell was `graphics.a` entry 0: stored size 12 327
against a 12 328-byte span, a one-byte gap that is padding, not a ratio of 1.00.

Stored size is implicit: `offsets[i+1] - offsets[i]`, less one padding byte, with
the last entry ending exactly at the index offset. For `fonts.a` the implied sizes sum to
14 847, which equals `index_offset - 14` exactly.

## 3. What the offsets imply about ordering

Entries are ordered by something other than payload position. `fonts.a` entry 0
starts at 14 and is 1 963 bytes long by the difference rule, yet entry 1 starts at
1 977 — inside that span. The index is sorted; the payloads are not laid out in
index order. Any implementation must sort by offset before streaming.

## 4. The mode byte

| byte | archives | behaviour |
|---|---|---|
| `0x01` | `config`, `fonts`, `rooms`, `animations` | real compression headroom |
| `0x00` | `graphics`, `music`, `sfx`, `videos`, `afterbirth*` | stored span ≈ raw size |

Compression ratios by mode, measured on entry 0:

| file | mode | stored span | `raw_size` | ratio |
|---|---|---|---|---|
| `config.a` | `0x01` | 8 806 | 25 894 | 0.34 |
| `fonts.a` | `0x01` | 1 963 | 4 147 | 0.47 |
| `graphics.a` | `0x00` | 12 328 | 12 327 | 1.00 |
| `music.a` | `0x00` | 6 502 620 | 6 502 617 | 1.00 |

The `0x00` archives hold PNG and OGG payloads, which are already compressed, so a
ratio near 1.0 is expected from any compressor and is not evidence of *absence* of
compression. What the mode byte does select, in the game, is a read path: the
file-handle object stores a mode word that picks between two different block-read
routines.

## 5. Names

There are no plaintext filenames anywhere. Measured: zero `.png` or `gfx`
substrings in the 17.5 MB `graphics.a`, and `resources/` contains only `packed/`
and `scripts/`, so there is no loose copy to diff against.

`+0x00` and `+0x04` are almost certainly a 64-bit name hash. That is consistent
with an earlier failed sweep of nine 32-bit algorithms: a 64-bit result cannot be
produced by any of them. The algorithm is still unknown.

## 6. The codec is not zlib or deflate

Ruled out by measurement, not by assumption:

- No `ESC Lua` signature appears in any of the 23 archives (1.5 GB scanned).
- Decompressing at every 16-byte-aligned offset of the first 64 KB finds no
  stream, for `config.a`, `rooms.a` or `animations.a`.
- Brute-forcing a start offset across the first 80 bytes at `wbits` of
  `-15, -9, 9, 15, 31` decompresses nothing.
- LZMA-alone, raw LZMA and XZ all fail.

It is also not a repeating-key XOR. Comparing a known plaintext against the
ciphertext of several `config.a` entries yields a different key per entry, so the
keystream is per-entry. An autocorrelation test over periods 1–256 finds no key
period; a strong single-byte skew (period-1 bias 36.9 against 0.13 at period 256)
combined with an entropy ramp from 7.07 in the first 256 bytes to 7.93 over the
whole payload points at a custom LZ variant rather than a cipher.

Recovering it needs the decompressor in the shipped binary. See issues #1 and #2.

## 7. A build-pipeline leftover, now explained

A 17-record table in `.data` stores `{const char* path, u32* entries, u32 count}`
for each archive. Its `entries` arrays are byte-identical to the `crc` column of
the file index. That is why a 32-bit name-hash sweep found nothing: the arrays
are checksums, not hashes. The table is never read at runtime — the master pointer
array at `.data + 0x2E60` has zero references anywhere in the binary — so the
loader enumerates `resources/packed/` itself at startup.
## 8. Provenance and licensing

The codec details in sections 4 and 6 were cross-checked against
`github.com/diamondo25/tbar-archive-unpacker`, whose own `LICENSE` is MIT
(Copyright 2014 Erwin Oegema). That project ships a vendored `SharpLZW`
directory whose files carry the GNU GPL; **none of it was copied into this
repository.** The container parser, the mode `0x00` obfuscation and the LZW
decoder here are independent implementations written from the algorithm
descriptions. That project's format does not document mode `0x02`, which predates
it.

## 9. Mode 0x02 decoder (recovered from disassembly)

The mode dispatch lives at VA `0xa68c50`. Byte 7 of the header selects the arm:

| mode | path |
|---|---|
| `0x01` | `0xa850f0` — LZW (section 6) |
| `0x02` | `0xa89c90` — PRNG keystream (this section) |
| `0x00` | inline at `0xa68cc5` — XOR (section 4) |

All three arms end with the same virtual call
`output_buffer->vtbl[3](field_c->field_c, 0)`; the per-mode transform runs
before it.

### 9.1 Wrapper at `0xa89c90`

Allocates a buffer, calls `0xaef15c` (a thin forwarder to `0xa0f500`, an
SEH-protected allocator at `0xa648b0`), allocates a second buffer, then calls
the PRNG at `0xaa93e0` with the entry's `specialHash` as the seed.

### 9.2 PRNG at `0xaa93e0`

This is a 64-bit LCG, **not** ISAAC. An earlier revision of this document and
of the C++ implementation both claimed ISAAC; that mistake is what allowed the
mode dispatch to be wired to the wrong algorithm while 34 tests passed green,
because those tests were written from the implementation rather than from the
disassembly.

Seed preparation (32-bit):

```
h  = (seed << 15) ^ seed
h ^= (seed >> 9)
h  = (h << 8) ^ seed
```

The 64-bit LCG state is initialised to `state_lo = h`, `state_hi = seed`.
Each of the 256 iterations performs:

```
state = state * 0x4c957f2d5851f42d + 0x7f      (64-bit)
```

and emits one 32-bit word:

```
n   = state_hi >> 27
v   = ((state >> 27) & 0xffffffff) ^ (state_hi >> 13)
out = rotate_right(v, n)
```

The rotation amount is the 5-bit field `state_hi >> 27`, **not**
`state & 31` as an earlier note in this file claimed — the disassembly at
`aa9450`–`aa9467` computes `32 - n` with `neg`/`and`, shifts left by that and
right by `n`, and ORs the halves, which is a right rotation.

The function returns 256 dwords (1024 bytes) per call. The caller is expected
to invoke it repeatedly for inputs longer than 1024 bytes.

Because the LCG state persists across calls, a payload longer than 1024 bytes is
XORed against one continuous keystream — not 1024 bytes of one stream followed
by a fresh stream restarting at the initial state.

### 9.3 Applying the keystream to the payload

The generated word is XORed into the entry payload four bytes at a time, in
little-endian order. If the payload length is not a multiple of 4, the final
group still consumes one keystream word and XORs only the bytes that fit:

```
for i in range(0, size, 4):        # NOT range(0, size - size % 4, 4)
    word = prng.next()
    for b in range(4):
        if i + b < size:
            payload[i + b] ^= (word >> (8 * b)) & 0xFF
```

Dropping the trailing partial group instead — the superficially equivalent
`size - size % 4` — diverges from the real decoder on precisely those payloads.
The differential test below was written because of that bug.

A `specialHash` of exactly 0 makes the first emitted word `0x00000000` (the seed
prep leaves `h` at 0 and the first state is `0x7f`), so such an entry would have
its first four payload bytes unobfuscated. Real name hashes are non-zero.

### 9.4 Implementations

| implementation | file |
|---|---|
| production C++ | `src/data/codec.cpp` (`ArchiveCodec::decode`, `PrngStream`) |
| reference Python | `tools/isaac-archive.py` (`decode_prng`, `PrngStream`) |

Both implementations are exercised differentially: `tests/test_codec.cpp`
contains `codec_lzw_matches_reference_decoder` and
`codec_prng_matches_reference_decoder`, which feed identical fixtures to the C++
and Python decoders and compare outputs byte-for-byte. Expected bytes in
`tests/lzw_fixtures.inc` and `tests/prng_fixtures.inc` are generated by the
Python side. Regenerate them with `tools/gen_bogocrypt1_fixtures.py` and the sibling
generators in `tools/` rather than hand-editing. Note the limitation
those fixtures inherit: they pin the two implementations to each other,
not to the game. Section 9.5 is what that gap costs.

Keeping two independent implementations matters here specifically because the
single-implementation mistake described in 9.2 passed its own tests. Agreement
with a second implementation written from the same disassembly is the available
check; a real packed archive from a game install would be the stronger one.

### 9.5 Status: unverified, and the stronger check has now failed

That stronger check has been run. The mode 0x02 decoder does not reproduce real
payloads.

Over 1406 decrypted entries drawn from `repentance.a` and `afterbirthp.a`:

- the first decoded byte is uniformly distributed, the commonest value appearing
  13 times where uniform would predict about 5;
- `decoded[0:4] == raw[0:4]` holds for 0 of 1406 entries, so the keystream is
  never accidentally inert;
- no decoded entry opens with a PNG, OGG or BMF magic, at any of four tried
  payload offsets (`offset`, `offset + size`, `offset + 9`, `offset + 9 + span`).

The output is indistinguishable from noise, so the generator in 9.2 is not the
one the game applies. The C++ and the Python agree because both transcribe the
same disassembly reading — the exact weakness 9.2 records, now demonstrated on
real data instead of merely anticipated. What *is* externally verified is mode
0x00, by PNG chunk CRCs and zlib inflation, and the record header in section 10.
Treat every mode 0x02 decode as unvalidated until this is resolved.

## 10. The mode 0x02 record header

Mode 0x02 entries do not begin with their payload. Every one carries a nine-byte
header, and for a subset the header declares the payload to be *stored* —
present in the archive in the clear, with no cipher applied at all.

### 10.1 Layout

```
[0:2]  u16 LE   size + 5
[2]    0x00
[3]    0x80 when stored, 0x00 when obfuscated
[4]    variant selector
[5:7]  u16 LE   size
[7]    ~size & 0xFF
[8]    0xfd or 0xfc
```

`size` is the recorded entry size. For a stored entry the payload is exactly
`size` bytes starting at `offset + 9`. Verified on 13 entries: the PNG chunk walk
ends at `offset + 9 + size` every time.

### 10.2 The stored predicate

`header[0:2] == size + 5` selects stored entries exactly. Counting it over the
installed archives:

| archive | mode | entries | stored | PNG magics present in the blob |
|---|---|---|---|---|
| `repentance.a` | 0x02 | 4180 | 378 | 378 |
| `afterbirthp.a` | 0x02 | 10228 | 587 | 587 |
| `afterbirth.a` | 0x02 | 2647 | 0 | 0 |

The counts agree, so this is the flag rather than a heuristic. Two independent
signals agree with it: `header[3]` is bimodal over the same entries (3691 zeroes
against 489 set bytes in `repentance.a`), and `header[7] == ~size & 0xFF` held
for every stored entry inspected.

### 10.3 What it buys

965 PNGs across `repentance.a` and `afterbirthp.a` are recoverable with no codec
involved at all. Decoding the first 400 stored entries of each archive, 378/378
and 399/400 came out with every chunk CRC valid, an IDAT that inflates cleanly,
and a PNG length equal to the recorded `size`.

`tools/isaac-archive.py` exposes this as `RECORD_HEADER_SIZE`,
`record_header()` and `is_stored()`; `extract` on a stored entry reports
`stored (no cipher)` and `list` summarises the stored count per archive.
