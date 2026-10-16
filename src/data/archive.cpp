#include "isaec/data/archive.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>

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

    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("cannot open " + path.string());

    blob_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) return fail("read error on " + path.string());

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
    if (static_cast<usize>(entry.offset) + size > blob_.size()) {
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

    if (mode_ == kModeLzw) {
        return fail("mode 0x01 LZW diverges on real archives; refusing to return a "
                    "partial decode");
    }
    if (mode_ == kModePrng) {
        return fail("mode 0x02 PRNG keystream is unverified against real archives; "
                    "refusing to return noise");
    }
    if (mode_ != kModeAssets) {
        return fail("unknown codec mode " + std::to_string(mode_));
    }

    const usize span = cipher_span(index);
    std::vector<u8> buffer(blob_.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                           blob_.begin() +
                               static_cast<std::ptrdiff_t>(entry.offset + span));
    if (!ArchiveCodec::decode(buffer, mode_, entry.special, span)) {
        return fail("codec rejected entry " + std::to_string(index));
    }
    buffer.resize(size);
    out = std::move(buffer);
    return true;
}

}  // namespace isaec::data