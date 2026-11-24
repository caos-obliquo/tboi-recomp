#include "isaec/data/archive.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <ios>
#include <limits>
#include <utility>

#include "isaec/data/codec.hpp"

namespace isaec::data {
namespace {

constexpr usize kOffsetMode = 7;
constexpr usize kOffsetIndex = 8;
constexpr usize kOffsetCount = 12;

constexpr char kMagic[] = {'A', 'R', 'C', 'H', '0', '0', '0'};

u16 load_u16(const std::vector<u8>& blob, usize at) {
    return static_cast<u16>(static_cast<u16>(blob[at]) |
                            static_cast<u16>(blob[at + 1]) << 8);
}

u32 load_u32(const std::vector<u8>& blob, usize at) {
    return static_cast<u32>(blob[at]) | static_cast<u32>(blob[at + 1]) << 8 |
           static_cast<u32>(blob[at + 2]) << 16 | static_cast<u32>(blob[at + 3]) << 24;
}

}  // namespace

bool Archive::fail(std::string message) {
    error_ = std::move(message);
    return false;
}

bool Archive::open(const std::filesystem::path& path) {
    blob_.clear();
    entries_.clear();
    error_.clear();
    describe_.clear();
    mode_ = 0;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return fail("cannot open " + path.string());

    // One sized read. The previous form copied through istreambuf_iterator,
    // which advances one character per iterator increment: on a 633 MB
    // container that measured 37.3 s of user CPU before the first byte was
    // parsed (#130). Sizing first also means a truncated read is detected
    // rather than silently producing a short blob.
    const std::streamoff end = in.tellg();
    if (end < 0) return fail("cannot size " + path.string());
    if (static_cast<u64>(end) > std::numeric_limits<usize>::max()) {
        return fail("file is too large to map into memory");
    }
    blob_.resize(static_cast<usize>(end));
    in.seekg(0, std::ios::beg);
    if (!blob_.empty()) {
        in.read(reinterpret_cast<char*>(blob_.data()),
                static_cast<std::streamsize>(blob_.size()));
    }
    if (!in) return fail("read error on " + path.string());

    if (blob_.size() < kArchiveHeaderSize) return fail("file is shorter than the header");
    if (!std::equal(std::begin(kMagic), std::end(kMagic), blob_.begin())) {
        return fail("bad magic, not an ARCH000 container");
    }

    mode_ = blob_[kOffsetMode];
    const u32 index_offset = load_u32(blob_, kOffsetIndex);
    const u16 count = load_u16(blob_, kOffsetCount);
    if (count == 0) return fail("zero entries");
    if (static_cast<usize>(index_offset) +
            static_cast<usize>(count) * kArchiveEntrySize !=
        blob_.size()) {
        return fail("index_offset " + std::to_string(index_offset) + " with count " +
                    std::to_string(count) + " does not match the file size " +
                    std::to_string(blob_.size()));
    }

    entries_.reserve(count);
    for (u16 i = 0; i < count; ++i) {
        const usize at =
            static_cast<usize>(index_offset) + static_cast<usize>(i) * kArchiveEntrySize;
        ArchiveEntry entry;
        entry.name_hash = load_u32(blob_, at + 0);
        entry.special = load_u32(blob_, at + 4);
        entry.offset = load_u32(blob_, at + 8);
        entry.size = load_u32(blob_, at + 12);
        entry.checksum = load_u32(blob_, at + 16);
        entries_.push_back(entry);
    }

    if (entries_.front().offset != kArchiveHeaderSize) {
        return fail("first payload offset is " + std::to_string(entries_.front().offset) +
                    ", expected " + std::to_string(kArchiveHeaderSize));
    }
    if (!std::is_sorted(entries_.begin(), entries_.end(),
                        [](const ArchiveEntry& a, const ArchiveEntry& b) {
                            return a.offset < b.offset;
                        })) {
        return fail("offset column is not ascending");
    }

    char summary[128];
    std::snprintf(summary, sizeof summary, "mode=0x%02x codec=%s entries=%zu", mode_,
                  ArchiveCodec::codec_name(mode_), entries_.size());
    describe_ = summary;
    return true;
}

usize Archive::cipher_span(usize index) const noexcept {
    if (index >= entries_.size()) return 0;
    const ArchiveEntry& entry = entries_[index];
    const usize limit =
        index + 1 < entries_.size() ? entries_[index + 1].offset : blob_.size();
    if (limit <= entry.offset) return entry.size;
    return limit - entry.offset;
}

std::optional<RecordHeader> Archive::record_header(usize index) const {
    if (index >= entries_.size()) return std::nullopt;
    const usize at = entries_[index].offset;
    if (at + kRecordHeaderSize > blob_.size()) return std::nullopt;

    RecordHeader header;
    header.declared_total = load_u16(blob_, at + 0);
    header.reserved = blob_[at + 2];
    header.flags = blob_[at + 3];
    header.kind = blob_[at + 4];
    header.declared_size = load_u16(blob_, at + 5);
    header.length_check = blob_[at + 7];
    header.trailer = blob_[at + 8];
    return header;
}

bool Archive::is_stored(usize index) const noexcept {
    const auto header = record_header(index);
    if (!header) return false;
    const u32 size = entries_[index].size;
    const u8 expected_check = static_cast<u8>((~size) & 0xFFu);
    return header->declared_total == size + 5u && header->declared_size == size &&
           header->length_check == expected_check;
}

bool Archive::decode(usize index, std::vector<u8>& out) {
    out.clear();
    if (index >= entries_.size()) return fail("entry index out of range");

    const ArchiveEntry& entry = entries_[index];
    const usize size = entry.size;
    const usize span = cipher_span(index);
    if (span == 0 || static_cast<usize>(entry.offset) + span > blob_.size()) {
        return fail("entry " + std::to_string(index) + " runs past the end of the file");
    }
    // For mode 0x01 the size column is the DECOMPRESSED length, so it can be
    // larger than the bytes present in the file. Bounding the entry by `size`
    // here would reject every real LZW asset (#52).
    if (mode_ != kModeLzw &&
        static_cast<usize>(entry.offset) + size > blob_.size()) {
        return fail("entry " + std::to_string(index) + " runs past the end of the file");
    }

    if (mode_ == kModePrng && is_stored(index)) {
        // Stored in the clear behind a nine-byte record header.
        const usize at = entry.offset + kRecordHeaderSize;
        if (at + size > blob_.size()) {
            return fail("stored payload runs past the end of the file");
        }
        out.assign(blob_.begin() + static_cast<std::ptrdiff_t>(at),
                   blob_.begin() + static_cast<std::ptrdiff_t>(at + size));
        return true;
    }

    if (mode_ == kModePrng) {
        // Chunked DEFLATE behind four-byte chunk headers. The first chunk
        // starts at the entry offset; there is no nine-byte record header
        // unless is_stored() matched above (stored entries are whole PNGs in
        // the clear, which is what that arm handles).
        const usize span = cipher_span(index);
        std::vector<u8> payload(blob_.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                                blob_.begin() +
                                    static_cast<std::ptrdiff_t>(entry.offset + span));
        if (!ArchiveCodec::decode_chunked(payload.data(), payload.size(), size, out)) {
            return fail("mode 0x02 chunk stream in entry " + std::to_string(index) +
                        " is truncated or not DEFLATE");
        }
        return true;
    }
    if (mode_ != kModeAssets && mode_ != kModeLzw) {
        return fail("unknown codec mode " + std::to_string(mode_));
    }

    std::vector<u8> buffer(blob_.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                           blob_.begin() +
                               static_cast<std::ptrdiff_t>(entry.offset + span));
    if (!ArchiveCodec::decode(buffer, mode_, entry.special, span)) {
        return fail("codec rejected entry " + std::to_string(index));
    }
    if (buffer.size() < size) {
        // Under-producing is as wrong as over-producing: padding the shortfall
        // with zeros would hand the caller plausible-looking but fabricated
        // bytes. rooms.a entry 7 does this on the current port, so it is not a
        // hypothetical (#52).
        return fail("codec produced " + std::to_string(buffer.size()) +
                    " bytes for entry " + std::to_string(index) + ", expected " +
                    std::to_string(size));
    }
    buffer.resize(size);
    out = std::move(buffer);
    return true;
}

u32 resource_name_hash(const std::string& path) {
    u32 hash = 5381u;
    for (const char raw : path) {
        u8 byte = static_cast<u8>(raw);
        if (byte >= 'A' && byte <= 'Z') {
            byte = static_cast<u8>(byte - 'A' + 'a');
        }
        hash = hash * 33u + byte;
    }
    return hash;
}

bool Archive::load_names(const std::filesystem::path& source) {
    names_.clear();
    std::ifstream input(source);
    if (!input) {
        return fail("cannot open name list " + source.string());
    }
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        names_.emplace(resource_name_hash(line), line);
    }
    if (names_.empty()) {
        return fail("name list " + source.string() + " held no paths");
    }
    return true;
}

std::string Archive::name(usize index) const {
    if (index >= entries_.size()) {
        return {};
    }
    const auto found = names_.find(entries_[index].name_hash);
    return found == names_.end() ? std::string{} : found->second;
}

usize Archive::named_count() const noexcept {
    usize named = 0;
    for (const ArchiveEntry& entry : entries_) {
        if (names_.find(entry.name_hash) != names_.end()) {
            ++named;
        }
    }
    return named;
}
}  // namespace isaec::data
