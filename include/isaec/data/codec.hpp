// Archive codec for ARCH000 containers.
//
// Three modes exist:
//   - 0x00: Bogocrypt1 (XOR + conditional swaps + key schedule)
//   - 0x01: LZW decompression (carries no keystream)
//   - 0x02: 64-bit LCG PRNG keystream XOR, seeded with the entry specialHash
//
// The mode byte is at offset 7 of the archive header.

#ifndef ISAEC_DATA_CODEC_HPP
#define ISAEC_DATA_CODEC_HPP

#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::data {

class ArchiveCodec {
public:
    // Decode a buffer in-place using the specified mode.
    // `seed` is the entry's specialHash field.
    //
    // `cipher_span` is how many payload bytes the cipher must cover. For mode
    // 0x00 this must be the entry's cipher span -- the next entry's offset
    // minus this entry's offset -- and not the entry's recorded size. The
    // format borrows those trailing bytes to fill the final 1024-byte block,
    // and they change the decoded tail; 1982 of the 2300 graphics.a entries
    // come out with a corrupt IEND CRC if you pass the size instead. Slice
    // `cipher_span` bytes out of the archive, decode, then truncate yourself
    // to the asset length. Bytes at or beyond `cipher_span` are left
    // untouched. Pass 0 to decode the whole buffer.
    //
    // Returns false for an unknown mode or a span larger than the buffer.
    static bool decode(std::vector<u8>& data, u8 mode, u32 seed, usize cipher_span = 0);

    // Get the codec name for a mode.
    static const char* codec_name(u8 mode);
};

}  // namespace isaec::data

#endif  // ISAEC_DATA_CODEC_HPP
