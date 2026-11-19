#pragma once

// Small raw-DEFLATE helper for the archive and codec tests. The mode-0x02
// chunk stream stores ordinary DEFLATE chunks, so tests build them at runtime
// with zlib instead of embedding a generated fixture file.

#include <zlib.h>

#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::test {

inline std::vector<isaec::u8> raw_deflate(const std::vector<isaec::u8>& input) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    std::vector<isaec::u8> out(deflateBound(&stream, input.size()) + 16);
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int status = deflate(&stream, Z_FINISH);
    const isaec::usize written = out.size() - stream.avail_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END) return {};
    out.resize(written);
    return out;
}

}  // namespace isaec::test
