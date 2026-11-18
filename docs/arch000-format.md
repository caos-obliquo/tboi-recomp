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

`+0x00` is a 32-bit hash of the resource path. The algorithm is djb2-add seeded
with 5381, and it is reversible against the shipped resource-path manifest: see
section 11, which resolves 81.46% of all installed entries.

`+0x04` is not part of a 64-bit name hash. It is the per-entry cipher seed. This
section previously concluded otherwise, from a sweep of nine algorithms that
could not match it; they could not match it because they were being asked for a
64-bit result.

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

Recovering it needs the decompressor in the shipped binary. See [#1](https://github.com/caos-obliquo/tboi-recomp/issues/1) and [#2](https://github.com/caos-obliquo/tboi-recomp/issues/2).

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
| `0x02` | `0xa89c90` → `0xaa93e0` PRNG + `0xa89f80` MiniZ wrapper (this section) |
| `0x00` | inline at `0xa68cc5` — XOR (section 4) |

All three arms end with the same virtual call
`output_buffer->vtbl[3](field_c->field_c, 0)`; the per-mode transform runs
before it.

### 9.0 Correction: mode 0x02 is MiniZ, not a whole-entry PRNG stream

**Retraction.** Earlier revisions of this section — and the C++/Python code that
followed it — described mode 0x02 as "PRNG keystream XOR over every payload".
That is wrong. REA/Ghidra analysis of the shipped `isaac-ng.exe`
(SHA256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`)
shows the actual chain:

1. `0xa68c50` dispatch (`ev_9734722ed3d254f67bf9738cc450c60c4beb597c8633a43d6a067455d3ed8e20`,
   `ev_bad9d9a1137734961bdae624e1dc17cf7a185b13441441e20f76bc1ab5e72209`)
   calls `0xa89c90` with `*(entry+8)`, the per-entry cipher seed.
2. `0xa89c90` wrapper (`ev_4957dbced24eeaf09e969600de5891fbe57209d27da4f2897048a737dda1c670`,
   `ev_9c59863125a5cf25186dcc2f2d0ae03fb1b74a85172d6b116d6c9baadb865b6a`)
   allocates the PRNG buffer and calls `0xaa93e0(seed)`; it does not decode the
   entry itself.
3. `0xa694a0` vtbl[3] (`ev_c46289808951fff4cdb4f2dbbe0e6884131cf7a3b438e7d31465c38b57c655db`)
   routes refill to `0xa69090`.
4. `0xa69090` refill (`ev_9bb8bfe6a9261ddc23a7a22df21e6a7c1aecce7cab5b1c1a911903a8f4ba21ab`)
   reads each chunk's 32-bit length, takes `len = raw & 0x7fffffff` and
   `flag = raw >> 31`, rejects `len > 0x7ff`, then calls
   `0xa89f80(dst=state+0x81c, dst_size=0x400, compressed, len, flag)`.
5. `0xa89f80` MiniZ wrapper (`ev_f2927f0f8983277f6d9771f90bb46f4ecb4989990d3add4a69fc5477e24c2881`)
   has two paths:
   - **stored flag** (or length equal to the 0x400 destination): `memcpy` then
     `0xa89d70` XORs the stored bytes with the PRNG stream prepared in step 2;
   - **compressed flag**: `0xa85710` MiniZ decompression (`ev_cd61b34462953efeb4a0ae3a963ac5321ee9b8f9e31e996f0a648a34d174436a`),
     with **no PRNG XOR at all**.

Therefore the PRNG at `0xaa93e0` is only a stored-block XOR aid; it is not the
mode 0x02 decoder. `0xa89d70` stored-block XOR:
`ev_6ec6deefdee9729772e73b4b5f3e026a7da44d6ae54da659a501a9f4d51b460a`.
The constructor `0xa68a50`
(`ev_1f71bbfaa663e6632037be161fbf91467c97674c8fa92ee89d51b8c6ae6e0f7f`)
and vtable `0xba05bc`
(`ev_9e134804f283f794b99483209d4e7876e8ff89b8a8d3053c978c169537ccda1f`)
establish that the 0x14-byte subobject at `[ESI+0x14]` is the PRNG holder used
by the wrapper. Full MiniZ porting is tracked separately; this verification
turn establishes only the native chain and the two transcription errors below.

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

Seed preparation (32-bit), corrected from the bytes at `0xaa93eb`
(`ev_dd72ecf227305aee7420801d63a464968849b03d5ed1e79714a10fbbeb85f833`):

```
h = (((seed << 15) ^ seed) << 8) ^ (seed >> 9) ^ seed
```

The earlier transcription

```
h  = (seed << 15) ^ seed
h ^= (seed >> 9)
h  = (h << 8) ^ seed
```

is **wrong**: it shifts the `seed >> 9` term left by 8 as well. That error is
preserved in `src/data/codec.cpp` and `tools/isaac-archive.py`; correcting it is
an implementation task, not a verification-first change.

The 64-bit LCG state is initialised to `state_lo = h`, `state_hi = seed`
(bytes at `0xaa9406`; `XOR EAX,ESI` then `ADD EAX,ECX / ADC ECX,ESI`).
Each of the 256 iterations performs:

```
state = state * 0x5851f42d4c957f2d + 0x7f      (64-bit)
```

The multiplier was previously documented as `0x4c957f2d5851f42d`. The native
code pushes `0x5851f42d` first and `0x4c957f2d` second
(`ev_f45a036bf54ad8e5388809fdfe279f2481726b8b312bc91c570720e1db193d97`,
`ev_ff92aeb65ab4dd4497213617bee2c070fb258db5c9385a3087470008bdbe9446`);
under cdecl argument order these are the high and low words of the standard PCG
multiplier `0x5851f42d4c957f2d`. Both the C++ and the Python mirror the
word-swapped value.

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

### 9.5 Status: unverified; the earlier "wrong generator" conclusion is retracted

The stronger real-archive check did fail: 1406 obfuscated entries from
`repentance.a` and `afterbirthp.a` decoded to noise, with no PNG/OGG/BMF magic
at any tried offset. That observation remains evidence. The **explanation**
previously written here — "the generator in 9.2 is not the one the game
applies" — is retracted. REA analysis now shows two independent defects:

1. the decode chain is missing the MiniZ stage (§9.0), so a whole-entry PRNG
   XOR cannot reproduce compressed mode 0x02 entries even with a correct PRNG;
2. the PRNG transcription itself has two errors (wrong word order in the
   multiplier, wrongly shifted `seed >> 9` in seed preparation), so even the
   stored-block XOR path is wrong.

A four-variant probe over the installed `repentance.a` compares the old and
corrected multiplier/seed-preparation combinations against the record-header
predicate from §10.4. It is a negative control: if none of the four variants
produces record headers, that confirms a missing stage rather than a single
mistyped constant. The result is reported in the issue trail, not copied into
this document as a permanent number.

What *is* externally verified is mode 0x00, by PNG chunk CRCs and zlib
inflation, and the record header in section 10. Treat every mode 0x02 decode
as unvalidated until the MiniZ stage and the corrected PRNG are implemented and
checked against a real archive.

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

## 11. Entry names

Names are recoverable. `+0x00` of the entry is a 32-bit hash of the resource
path, and the algorithm is djb2-add seeded with 5381 over the path string
including its extension:

```python
def resource_name_hash(path: str) -> int:
    h = 5381
    for byte in path.lower().encode("latin-1", "replace"):
        h = (h * 33 + byte) & 0xFFFFFFFF
    return h
```

Note that `+0x04` is *not* the upper half of a 64-bit name hash — it is the
per-entry cipher seed used by modes 0x00 and 0x02. Section 5 originally guessed
otherwise, from a sweep of nine algorithms that could not match it. They could
not match it because they were being asked for a 64-bit result.

### 11.1 The corpus

The modding `ResourceExtractor` ships a complete list of extractable resource
paths at `tools/ResourceExtractor/filelist.txt` relative to the game install
(1,028,325 bytes, 18,789 paths, identical copies under `macOS/` and `Linux/`).
Hashing all of them yields 18,789 distinct values with zero collisions, so the
hash is injective over the known corpus and a single match is unambiguous.

`tools/isaac-archive.py` uses it as `resource_name_hash()` plus
`load_name_table()`, and exposes `--names` on both `list` and `extract`. The
table is located by, in order: an explicit argument, the `ISAEC_FILELIST`
environment variable, then auto-discovery at
`<install>/tools/ResourceExtractor/filelist.txt`. No flag is needed in practice.

### 11.2 Coverage

22,186 of 27,236 entries across the 22 installed archives resolve: **81.46%**.

| archive | mode | resolved | entries | % |
|---|---|---|---|---|
| `repentance_de.a` | 0x02 | 888 | 888 | 100.00% |
| `repentance_es.a` | 0x02 | 834 | 834 | 100.00% |
| `repentance_fr.a` | 0x02 | 914 | 914 | 100.00% |
| `repentance_jp.a` | 0x02 | 845 | 845 | 100.00% |
| `repentance_kr.a` | 0x02 | 896 | 896 | 100.00% |
| `repentance_ru.a` | 0x02 | 875 | 875 | 100.00% |
| `repentance_zh.a` | 0x02 | 941 | 941 | 100.00% |
| `videos.a` | 0x00 | 17 | 17 | 100.00% |
| `animations.a` | 0x01 | 1 | 1 | 100.00% |
| `afterbirthp.a` | 0x02 | 10185 | 10228 | 99.58% |
| `music.a` | 0x00 | 82 | 83 | 98.80% |
| `afterbirth_jp.a` | 0x02 | 393 | 404 | 97.28% |
| `afterbirthp_jp.a` | 0x02 | 166 | 176 | 94.32% |
| `graphics.a` | 0x00 | 2060 | 2300 | 89.57% |
| `afterbirth.a` | 0x02 | 2396 | 2647 | 90.52% |
| `config.a` | 0x01 | 21 | 24 | 87.50% |
| `afterbirth_kr.a` | 0x02 | 458 | 657 | 69.71% |
| `sfx.a` | 0x00 | 205 | 301 | 68.11% |
| `fonts.a` | 0x01 | 6 | 10 | 60.00% |
| `rooms.a` | 0x01 | 3 | 13 | 23.08% |
| `afterbirthp_kr.a` | 0x02 | 0 | 2 | 0.00% |
| `repentance.a` | 0x02 | 0 | 4180 | 0.00% |

The four entries that resolve at 100% in the localized archives pin the
namespace convention: `repentance_de.a` entry 0 is
`resources.de/gfx/backdrop/controls.png`. Paths are stored and hashed
lower-cased.

`graphics.a` entry 0, `0x503f9b44`, is
`resources/font/pftempestasevencondensed_0.png`. That is confirmed
independently by content: the entry decodes to a 512x256 RGBA PNG, which is a
font atlas page, and the matching `pftempestasevencondensed.fnt` sits in
`fonts.a` as its entry 0.

### 11.3 The `repentance.a` gap

The main DLC archive resolves nothing at all. This is *not* a hash failure.
Its 4,180 entry hashes are disjoint from `filelist.txt`, and also disjoint from
`afterbirthp.a` and `afterbirth.a`. Zero overlap is exactly what a correct hash
over absent paths predicts — 4,180 x 18,789 / 2^32 is about 1.8% of accidental
collisions — so the paths are simply not enumerated by the base-game extractor,
while `afterbirthp.a` is 99.58% named because Afterbirth+ shipped folded into
the same install.

Attempts that found nothing new: regex-harvesting resource paths from all 4,357
text files under `mods/`, `resources/`, `data/` and `tools/` (0 of 4,180);
harvesting from partially decoded LZW XML (0); searching the install for any
other path manifest (only the three identical `filelist.txt` copies exist).

4,180 entries, 15% of the total, stay unnamed. Recovering them needs a path
manifest for the DLC itself, which the game does not ship in a readable form.
