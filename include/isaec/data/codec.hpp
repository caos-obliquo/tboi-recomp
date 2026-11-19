// Archive codec for ARCH000 containers.
//
// Three modes exist:
//   - 0x00: Bogocrypt1 (XOR + conditional swaps + key schedule)
//   - 0x01: LZW decompression (carries no keystream)
//   - 0x02: chunked DEFLATE, four-byte length headers between chunks
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
    // `cipher_span` is how many payload bytes the codec must cover. For both
    // ciphered modes it must be the entry's span -- the next entry's offset
    // minus this entry's offset -- and not the entry's recorded `size` field.
    //
    // Mode 0x00 borrows trailing bytes to fill the final 1024-byte block; 1982
    // of the 2300 graphics.a entries get a corrupt IEND CRC if you pass the
    // size instead. Bytes at or beyond `cipher_span` are left untouched.
    //
    // Mode 0x01 has no keystream: `span` is the COMPRESSED byte length and
    // bounds the walk over 4-byte-prefixed chunks. `size` is the DECOMPRESSED
    // length; walking `size` bytes runs into the next entry and over-produces
    // (#52). The result replaces the whole buffer, so callers must truncate
    // to `size` after decoding.
    //
    // Pass 0 to use the whole buffer as the span.
    //
    // Returns false for an unknown mode or a span larger than the buffer.
    static bool decode(std::vector<u8>& data, u8 mode, u32 seed, usize cipher_span = 0);

    // Decode one mode-0x02 chunk stream, appending to `out`.
    //
    // The stream is a sequence of chunks: a little-endian u32 header, then
    // `header & 0x7fffffff` bytes. The top bit of the header is set on the
    // final chunk of every entry inspected in the retail archives (1155 of
    // 1155); it is not a stored flag and is ignored here.
    //
    // A chunk length of exactly 0x400 (1024) is a stored chunk and is copied
    // verbatim. Every other chunk is an independent raw-DEFLATE stream.
    // Lengths above 0x7ff are rejected. Decoding stops once `expected` bytes
    // have been produced; a final chunk may overshoot and is truncated.
    //
    // `span` must be at least the total chunk sizes. Returns false on a
    // truncated header, a truncated chunk, an invalid DEFLATE stream, or
    // running out of input before `expected` bytes exist.
    static bool decode_chunked(const u8* data, usize span, usize expected,
                               std::vector<u8>& out);

    // Get the codec name for a mode.
    static const char* codec_name(u8 mode);
};

}  // namespace isaec::data

#endif  // ISAEC_DATA_CODEC_HPP
