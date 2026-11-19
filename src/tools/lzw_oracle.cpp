// Game-side oracle for mode 0x01 (issue #107).
//
// Decodes every entry of a mode-0x01 ARCH000 archive through the C++ container
// and codec layers, then writes a framed binary stream to stdout:
//
//   repeated: u32 LE entry index, u32 LE decoded size, that many raw bytes
//
// The Python checker (tools/check_lzw_oracle.py) hashes those bytes against
// tools/isaac-archive.py. Keeping the hash out of C++ makes the comparison a
// byte-for-byte check of the decoder rather than of two crypto implementations.
// This tool reads only a path the caller supplies; it ships no game data.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "isaec/data/archive.hpp"
#include "isaec/platform/types.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: isaec_lzw_oracle <archive.a>\n");
        return 2;
    }

    isaec::data::Archive archive;
    if (!archive.open(std::filesystem::path(argv[1]))) {
        std::fprintf(stderr, "cannot open %s: %s\n", argv[1], archive.error().c_str());
        return 2;
    }
    if (archive.mode() != isaec::data::kModeLzw) {
        std::fprintf(stderr, "expected mode 0x01 archive, got 0x%02x\n",
                     static_cast<unsigned>(archive.mode()));
        return 2;
    }

    const std::size_t count = archive.entries().size();
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<isaec::u8> out;
        if (!archive.decode(i, out)) {
            std::fprintf(stderr, "entry %zu: %s\n", i, archive.error().c_str());
            return 1;
        }
        if (out.size() > 0xFFFFFFFFu) {
            std::fprintf(stderr, "entry %zu: decoded size exceeds u32\n", i);
            return 1;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(i);
        const std::uint32_t size = static_cast<std::uint32_t>(out.size());
        if (std::fwrite(&index, sizeof(index), 1, stdout) != 1 ||
            std::fwrite(&size, sizeof(size), 1, stdout) != 1 ||
            (!out.empty() && std::fwrite(out.data(), 1, out.size(), stdout) != out.size())) {
            std::fprintf(stderr, "failed to write framed output\n");
            return 1;
        }
    }
    return 0;
}
