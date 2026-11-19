// Archive codec implementation.
//
// Mode dispatch follows docs/arch000-format.md section 9 (recovered from the
// original binary at VA 0xa68c50); byte 7 of an entry header selects the arm:
//
//   0x00 -> XOR         (inline at 0xa68cc5, section 4)
//   0x01 -> LZW         (0xa850f0, section 6)
//   0x02 -> chunked DEFLATE (0xa89f80 -> miniz at 0xa85710, section 9)
//
// Bogocrypt1 (section 4) follows the Gibbed.Rebirth reference
//   projects/Gibbed.Rebirth.FileFormats/ArchiveFile.cs
// The mode-0x02 stream is *not* a whole-entry keystream: the earlier PRNG
// model was retracted in section 9.0 and disproved by decoding the retail
// archives (see section 10).

#include "isaec/data/codec.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace isaec::data {
namespace {

// Mode 0x02 chunk length mask. The top bit is the end-of-entry flag observed
// on the final chunk; lengths above 0x7ff never occur in the retail archives
// and are rejected rather than trusted.
constexpr u32 kChunkLengthMask = 0x7FFFFFFFu;
constexpr usize kMaxChunkLength = 0x7FF;
constexpr usize kStoredChunkLength = 0x400;

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

// ---------------------------------------------------------------------------
// LZW (mode 0x01): faithful port of the game's natives, not a fresh algorithm.
//
// REA/Ghidra on isaac-ng.exe (SHA256 3bdfc8bae0dc7...), pseudocode evidence:
//   0xa850f0  ev_2ba7fae00f476b26809b438919f98d5f79ea9ab52bb30d9d99b40a3ff9939edd
//   0xa85090  ev_022dd8441b68bb8c05295291c706d5d4c3ce7141b14d471b0290afe8c337b690
//   0xa85020  ev_7e10cbc6d1e68acef69eb4da7e34bfa2845aa5793a7b77352d107fc3a0414a99
//   0xa85320  ev_8f84fe3f86c05c3868f6e941f6039201a6865ca0993510ef59215363fb12152e
//
// The dictionary PERSISTS across the 4-byte-prefixed chunks of one entry;
// only the bit reader is per-chunk. Growing the width before the read and
// aborting on an out-of-range code are both native behaviour. The previous
// C++ decoder rebuilt a fresh byte-vector table per chunk and grew the width
// after insertion, which is why it produced garbage from byte 1024 on.
// ---------------------------------------------------------------------------

constexpr usize kLzwSlots = 0x1000;

struct LzwEntry {
    i32 prefix = -1;         // native slot 0
    i32 chain_head = -1;     // native slot 1
    i32 next_in_chain = -1;  // native slot 2
    u8 first = 0;            // native slot 3
};

class LzwState {
public:
    LzwState() {
        for (usize i = 0; i < kLzwSlots; ++i) {
            entries_[i].first = static_cast<u8>(i & 0xFF);
        }
    }

    void reset() {
        // Native 0xa85320 clears the chain heads only; stale prefixes are
        // harmless because insertion overwrites them as next_code grows.
        for (usize i = 0; i < kLzwSlots; ++i) {
            entries_[i].chain_head = -1;
        }
        next_code_ = 0x100;
        width_ = 8;
    }

    // 0xa85020: walk the prefix chain backwards, then reverse it into `out`.
    i32 emit(i32 code, std::vector<u8>& out) {
        u8 scratch[kLzwSlots];
        usize count = 0;
        while (code != -1 && count < kLzwSlots) {
            scratch[count++] = entries_[static_cast<usize>(code)].first;
            code = entries_[static_cast<usize>(code)].prefix;
        }
        for (usize i = count; i > 0; --i) {
            out.push_back(scratch[i - 1]);
        }
        // The scratch holds the string backwards; after reversing, its first
        // element is the ROOT's first byte. That is what the native returns and
        // what a KwKwK insert consumes.
        return count != 0 ? scratch[count - 1] : 0;
    }

    void insert(i32 prefix, u8 first_char) {
        if (prefix < 0 || prefix >= static_cast<i32>(kLzwSlots) ||
            next_code_ > 0xFFF) {
            return;
        }
        LzwEntry& slot = entries_[static_cast<usize>(next_code_)];
        slot.prefix = prefix;
        slot.chain_head = -1;
        slot.first = first_char;
        slot.next_in_chain = entries_[static_cast<usize>(prefix)].chain_head;
        entries_[static_cast<usize>(prefix)].chain_head =
            static_cast<i32>(next_code_);
        ++next_code_;
    }

    i32 next_code() const { return next_code_; }
    usize width() const { return width_; }
    void set_width(usize width) { width_ = width; }

private:
    std::array<LzwEntry, kLzwSlots> entries_;
    i32 next_code_ = 0x100;
    usize width_ = 8;
};

// 0xa85090: byte-accumulator, MSB-first, capped at 32 accumulated bits.
class LzwBitReader {
public:
    LzwBitReader(const u8* data, usize size) : data_(data), size_(size) {}

    i32 read(usize count) {
        while (bits_ < count) {
            if (pos_ >= size_) return -1;
            acc_ = (acc_ << 8) | data_[pos_++];
            bits_ += 8;
        }
        bits_ -= count;
        return static_cast<i32>((acc_ >> bits_) & ((1u << count) - 1u));
    }

private:
    const u8* data_;
    usize size_;
    usize pos_ = 0;
    u32 acc_ = 0;
    u32 bits_ = 0;
};

void decode_lzw_chunk(LzwState& state, const u8* chunk, usize size,
                      std::vector<u8>& out) {
    LzwBitReader reader(chunk, size);
    i32 code = reader.read(state.width());
    if (code < 0 || code >= static_cast<i32>(kLzwSlots)) return;

    i32 first_char = state.emit(code, out);
    i32 prev = code;
    while (true) {
        if (state.next_code() + 1 >= static_cast<i32>(kLzwSlots)) {
            state.reset();
            code = reader.read(8);
            if (code < 0) break;
            first_char = state.emit(code, out);
            prev = code;
        }
        while ((1u << state.width()) <= static_cast<u32>(state.next_code())) {
            state.set_width(state.width() + 1);
        }
        code = reader.read(state.width());
        if (code < 0 || code > state.next_code()) break;
        if (code == state.next_code()) {
            state.insert(prev, static_cast<u8>(first_char));
            first_char = state.emit(code, out);
        } else {
            first_char = state.emit(code, out);
            state.insert(prev, static_cast<u8>(first_char));
        }
        prev = code;
    }
}

void decode_lzw(std::vector<u8>& data, usize span) {
    if (span > data.size() || span < 4) return;

    std::vector<u8> out;
    LzwState state;
    usize cursor = 0;
    while (cursor + 4 <= span) {
        i32 length = static_cast<i32>(data[cursor]) |
                     (static_cast<i32>(data[cursor + 1]) << 8) |
                     (static_cast<i32>(data[cursor + 2]) << 16) |
                     (static_cast<i32>(data[cursor + 3]) << 24);
        cursor += 4;
        if (length <= 0 || cursor + static_cast<usize>(length) > span) break;
        decode_lzw_chunk(state, data.data() + cursor,
                         static_cast<usize>(length), out);
        cursor += static_cast<usize>(length);
    }
    data = std::move(out);
}

}  // namespace

// ---------------------------------------------------------------------------
// ArchiveCodec public interface
// ---------------------------------------------------------------------------

bool ArchiveCodec::decode_chunked(const u8* data, usize span, usize expected,
                                 std::vector<u8>& out) {
    out.clear();
    if (expected == 0) return true;
    out.reserve(expected);

    usize at = 0;
    while (out.size() < expected) {
        if (span < at + 4) return false;
        const u32 raw = static_cast<u32>(data[at]) |
                        static_cast<u32>(data[at + 1]) << 8 |
                        static_cast<u32>(data[at + 2]) << 16 |
                        static_cast<u32>(data[at + 3]) << 24;
        at += 4;

        const usize length = raw & kChunkLengthMask;
        if (length > kMaxChunkLength) return false;
        if (span - at < length) return false;

        if (length == kStoredChunkLength) {
            out.insert(out.end(), data + at, data + at + length);
            at += length;
            continue;
        }

        z_stream stream{};
        if (inflateInit2(&stream, -15) != Z_OK) return false;
        stream.next_in = const_cast<Bytef*>(data + at);
        stream.avail_in = static_cast<uInt>(length);

        u8 scratch[512];
        while (out.size() < expected) {
            stream.next_out = scratch;
            stream.avail_out = sizeof scratch;
            const int status = inflate(&stream, Z_NO_FLUSH);
            const usize produced = sizeof scratch - stream.avail_out;
            out.insert(out.end(), scratch, scratch + produced);
            if (status == Z_STREAM_END) break;
            if (status != Z_OK && status != Z_BUF_ERROR) {
                inflateEnd(&stream);
                return false;
            }
            // Native miniz is driven by the output buffer: a chunk that runs out
            // of input is not an error by itself, and does not have to end with a
            // deflate end marker. No progress with input left is a real failure.
            if (produced == 0 && stream.avail_in == 0) break;
            if (produced == 0) {
                inflateEnd(&stream);
                return false;
            }
        }
        inflateEnd(&stream);

        at += length;
    }

    out.resize(expected);
    return true;
}

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
            // LZW decompress over the entry's COMPRESSED span, not its
            // recorded DECOMPRESSED size. Dictionary state is per entry and
            // survives across its chunks; the seed is unused by the native arm.
            decode_lzw(data, span);
            return true;
        }
        case 0x02: {
            // Mode 0x02 is not a keystream and cannot be decoded without the
            // entry's expected output length. Callers use decode_chunked().
            return false;
        }
        default:
            return false;
    }
}

const char* ArchiveCodec::codec_name(u8 mode) {
    switch (mode) {
        case 0x00: return "xor";
        case 0x01: return "lzw";
        case 0x02: return "chunked";
        default: return "unknown";
    }
}

}  // namespace isaec::data
