// Archive codec implementation.
//
// Mode dispatch follows docs/arch000-format.md section 9 (recovered from the
// original binary at VA 0xa68c50); byte 7 of an entry header selects the arm:
//
//   0x00 -> XOR         (inline at 0xa68cc5, section 4)
//   0x01 -> LZW         (0xa850f0, section 6)
//   0x02 -> PRNG stream (0xa89c90 -> PRNG at 0xaa93e0, section 9)
//
// Bogocrypt1 (section 4) follows the Gibbed.Rebirth reference
//   projects/Gibbed.Rebirth.FileFormats/ArchiveFile.cs
// The mode-0x02 keystream is the 64-bit LCG at 0xaa93e0 -- NOT ISAAC.

#include "isaec/data/codec.hpp"

#include <algorithm>
#include <cstring>

namespace isaec::data {
namespace {

// ---------------------------------------------------------------------------
// Bogocrypt2 table removed: not a documented mode arm.
// Mode 0x02 is the PRNG keystream below; see docs/arch000-format.md section 9.
// ---------------------------------------------------------------------------

// Mode 0x02 (VA 0xa89c90 -> PRNG at 0xaa93e0). The wrapper seeds the PRNG with
// the entry's specialHash, then XORs the payload with the emitted keystream.
// The PRNG yields 256 dwords (1024 bytes) per Generate(); callers re-invoke it
// for longer payloads, so the LCG state persists across calls.
class PrngStream {
public:
    explicit PrngStream(u32 seed) {
        u32 h = (seed << 15) ^ seed;
        h ^= (seed >> 9);
        h = (h << 8) ^ seed;

        state_ = (static_cast<u64>(seed) << 32) | h;
    }

    u32 next() {
        state_ = state_ * 0x4c957f2d5851f42dULL + 0x7fULL;

        const u32 hi = static_cast<u32>(state_ >> 32);
        const u32 v = static_cast<u32>(state_ >> 27) ^ (hi >> 13);
        const u32 n = hi >> 27;
        return (v >> n) | (v << ((32 - n) & 31));
    }

private:
    u64 state_;
};

void prng_keystream_xor(std::vector<u8>& data, u32 seed) {
    PrngStream prng(seed);

    const usize size = data.size();
    for (usize i = 0; i < size; i += 4) {
        const u32 value = prng.next();
        data[i + 0] ^= static_cast<u8>(value & 0xFF);
        if (i + 1 < size) data[i + 1] ^= static_cast<u8>((value >> 8) & 0xFF);
        if (i + 2 < size) data[i + 2] ^= static_cast<u8>((value >> 16) & 0xFF);
        if (i + 3 < size) data[i + 3] ^= static_cast<u8>((value >> 24) & 0xFF);
    }
}

// ---------------------------------------------------------------------------
// Bogocrypt1 (mode-0x00): XOR + conditional swaps + key schedule
// ---------------------------------------------------------------------------

// The cipher walks the payload in fixed 1024-byte blocks. Every block is
// zero-padded to a whole number of 4-byte words and the key schedule advances
// exactly 256 times per block, so the keystream is a function of the block
// index rather than of a raw byte counter. A payload whose length is not a
// multiple of 1024 therefore does NOT decode the same as a straight
// byte-stream walk: the padding shifts every subsequent word.
// Verified against tools/isaac-archive.py on all 2300 entries of graphics.a.
constexpr usize kBogocrypt1BlockBytes = 1024;

void bogocrypt1_swap(u8* bytes, usize i, usize j) {
    std::swap(bytes[i], bytes[j]);
}

u32 bogocrypt1(u8* bytes, usize offset, usize count, u32 key) {
    if (count == 0) return key;

    u8 block[kBogocrypt1BlockBytes];
    for (usize start = 0; start < count; start += kBogocrypt1BlockBytes) {
        const usize n = std::min(kBogocrypt1BlockBytes, count - start);

        // Zero the pad so the cipher always sees whole words, then discard the
        // pad on write-back. Working on a private block also removes any need
        // to bounds-check the swap cases against `count`.
        std::memset(block, 0, kBogocrypt1BlockBytes);
        std::memcpy(block, bytes + offset + start, n);

        for (usize word = 0; word < kBogocrypt1BlockBytes / 4; ++word) {
            const usize i = word * 4;
            block[i + 0] ^= static_cast<u8>((key >> 0) & 0xFF);
            block[i + 1] ^= static_cast<u8>((key >> 8) & 0xFF);
            block[i + 2] ^= static_cast<u8>((key >> 16) & 0xFF);
            block[i + 3] ^= static_cast<u8>((key >> 24) & 0xFF);

            switch (key & 15) {
                case 2:
                    bogocrypt1_swap(block, i + 0, i + 3);
                    bogocrypt1_swap(block, i + 1, i + 2);
                    break;
                case 9:
                    bogocrypt1_swap(block, i + 0, i + 1);
                    bogocrypt1_swap(block, i + 2, i + 3);
                    break;
                case 13:
                    bogocrypt1_swap(block, i + 0, i + 2);
                    bogocrypt1_swap(block, i + 1, i + 3);
                    break;
                default:
                    break;
            }

            key ^= ((key ^ (key << 8)) >> 9) ^ (key << 8) ^
                   ((((key ^ (key << 8)) >> 9) ^ key ^ (key << 8)) << 23);
        }

        std::memcpy(bytes + offset + start, block, n);
    }
    return key;
}

void decode_lzw_codes(const u8* data, usize size, std::vector<u8>& out);

void decode_lzw(std::vector<u8>& data) {
    const usize size = data.size();
    if (size < 4) return;

    std::vector<u8> out;
    usize cursor = 0;

    while (cursor + 4 <= size) {
        i32 length = static_cast<i32>(data[cursor]) |
                     (static_cast<i32>(data[cursor + 1]) << 8) |
                     (static_cast<i32>(data[cursor + 2]) << 16) |
                     (static_cast<i32>(data[cursor + 3]) << 24);
        cursor += 4;

        if (length <= 0 || cursor + static_cast<usize>(length) > size) {
            break;
        }

        decode_lzw_codes(data.data() + cursor, static_cast<usize>(length), out);
        cursor += static_cast<usize>(length);
    }

    data = std::move(out);
}

class BitReader {
public:
    BitReader(const u8* data, usize size) : data_(data), size_(size), bit_(0) {}

    i32 read(usize count) {
        i32 value = 0;
        for (usize i = 0; i < count; ++i) {
            if (bit_ >= size_ * 8) return -1;
            const usize byte = bit_ >> 3;
            const usize shift = 7 - (bit_ & 7);
            value = (value << 1) | static_cast<i32>((data_[byte] >> shift) & 1);
            ++bit_;
        }
        return value;
    }

private:
    const u8* data_;
    usize size_;
    usize bit_;
};

// Significant-bit count, matching Python's int.bit_length() which the
// reference decoder (tools/isaac-archive.py) uses to drive width growth.
usize bit_length(usize value) {
    usize bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

void decode_lzw_codes(const u8* data, usize size, std::vector<u8>& out) {
    BitReader reader(data, size);
    usize width = 8;

    std::vector<std::vector<u8>> table;
    table.reserve(4096);
    for (int i = 0; i < 256; ++i) {
        table.push_back({static_cast<u8>(i)});
    }

    std::vector<u8> previous;
    bool have_previous = false;

    while (true) {
        const i32 code = reader.read(width);
        if (code < 0) break;

        // Copied by value: the table is appended to below, which can
        // reallocate and invalidate a reference into it.
        std::vector<u8> entry;
        if (!have_previous) {
            if (static_cast<usize>(code) >= table.size()) break;
            entry = table[static_cast<usize>(code)];
        } else if (static_cast<usize>(code) < table.size()) {
            entry = table[static_cast<usize>(code)];
            table.push_back(previous);
            table.back().push_back(entry[0]);
        } else {
            // KwKwK: the encoder emitted a code it had not defined yet, which
            // by construction is `previous` followed by its own first byte.
            entry = previous;
            entry.push_back(previous[0]);
            table.push_back(entry);
        }

        out.insert(out.end(), entry.begin(), entry.end());
        previous = entry;
        have_previous = true;

        // Runs after every code, including the first. With 256 initial
        // entries bit_length() is 9, so the width steps to 9 immediately
        // rather than tracking table size exactly.
        if (bit_length(table.size()) > width) {
            ++width;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// ArchiveCodec public interface
// ---------------------------------------------------------------------------

bool ArchiveCodec::decode(std::vector<u8>& data, u8 mode, u32 seed, usize cipher_span) {
    const usize span = cipher_span == 0 ? data.size() : cipher_span;
    if (span > data.size()) return false;

    switch (mode) {
        case 0x00: {
            // Bogocrypt1: XOR + swaps + key schedule. `span` must be the
            // entry's cipher span (next entry's offset minus this entry's
            // offset), not its recorded size -- see kBogocrypt1BlockBytes.
            const u32 key = ((seed ^ 0xF9524287u) | 1u) & 0xFFFFFFFFu;
            bogocrypt1(data.data(), 0, span, key);
            return true;
        }
        case 0x01: {
            // LZW decompress only. The original arm (0xa850f0) does not
            // obfuscate; mode 0x01 carries no keystream.
            decode_lzw(data);
            return true;
        }
        case 0x02: {
            // PRNG keystream XOR, seeded with the entry's specialHash.
            prng_keystream_xor(data, seed);
            return true;
        }
        default:
            return false;
    }
}

const char* ArchiveCodec::codec_name(u8 mode) {
    switch (mode) {
        case 0x00: return "xor";
        case 0x01: return "lzw";
        case 0x02: return "prng";
        default: return "unknown";
    }
}

}  // namespace isaec::data
