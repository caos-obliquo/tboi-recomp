//   bytes 12..13  entry count
//   then `count` 20-byte entries of (nameHash, specialHash, offset, size, checksum)
//
// Entry names are not stored; only their hashes are. resource_name_hash() and
// NameTable recover them against a shipped resource-path manifest, which
// resolves 81.46% of the entries in the installed archives. See
// docs/arch000-format.md section 11.

#ifndef ISAEC_DATA_ARCHIVE_HPP
#define ISAEC_DATA_ARCHIVE_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::data {

inline constexpr u8 kModeAssets = 0x00;
inline constexpr u8 kModeLzw = 0x01;
inline constexpr u8 kModePrng = 0x02;

inline constexpr usize kArchiveHeaderSize = 14;
inline constexpr usize kArchiveEntrySize = 20;
inline constexpr usize kRecordHeaderSize = 9;

// One row of the archive index. `checksum` is the container's own field; the
// format does not use a CRC-32 variant we could reproduce, so it is carried
// through verbatim and not verified.
struct ArchiveEntry {
    u32 name_hash = 0;
    u32 special = 0;
    u32 offset = 0;
    u32 size = 0;
    u32 checksum = 0;
};

// Nine bytes that precede mode 0x02 payloads. Entries whose header validates
// are stored in the clear; the rest carry a PRNG keystream that has not been
// reproducible from real data (see docs/arch000-format.md 9.5).
//
// The field names describe byte positions, not meaning. In particular the
// "stored in the clear" flag lives in `flags` at [3], not in `reserved` at [2],
// which is always zero in every archive inspected. is_stored() does not consult
// the flag at all; it checks the three length fields agree, which is a strictly
// stronger test.
struct RecordHeader {
    u16 declared_total = 0;  // [0:2] expected size + 5
    u8 reserved = 0;         // [2] always 0x00
    u8 flags = 0;            // [3] 0x80 marks a stored payload
    u8 kind = 0;             // [4] 0x01 for the plaintext kind
    u16 declared_size = 0;   // [5:7] expected payload size
    u8 length_check = 0;     // [7] (~size) & 0xFF
    u8 trailer = 0;          // [8]
};

// djb2-add seeded with 5381, over the resource path including its extension,
// ASCII-lowercased. Matches tools/isaac-archive.py resource_name_hash().
//
// Bytes at or above 0x80 are folded through unchanged rather than rejected,
// matching Python's latin-1 codec: the shipped manifest is pure ASCII so the
// distinction never arises in practice.
u32 resource_name_hash(const std::string& path);

// Maps an entry hash back to its resource path. See docs section 11.
using NameTable = std::unordered_map<u32, std::string>;

// A whole container held in memory. Archives in the shipped game are at most
// a few tens of megabytes, so reading the file eagerly keeps every decode a
// plain memory copy.
class Archive {
public:
    // Reads and validates the container. On failure returns false and leaves a
    // human-readable reason in error(). Reopening replaces any previous state.
    bool open(const std::filesystem::path& path);

    const std::string& error() const noexcept { return error_; }
    const std::string& describe() const noexcept { return describe_; }

    u8 mode() const noexcept { return mode_; }
    const std::vector<ArchiveEntry>& entries() const noexcept { return entries_; }

    // How many bytes the cipher must cover for this entry. That is the gap to
    // the next entry, not the entry's recorded size: the format borrows those
    // trailing bytes to fill the last cipher block. Falls back to the recorded
    // size for the final entry and for tables that are not offset-ordered.
    usize cipher_span(usize index) const noexcept;

    // The nine bytes at the start of this entry, if they are present.
    std::optional<RecordHeader> record_header(usize index) const;

    // True when the record header self-consistently declares a stored payload.
    bool is_stored(usize index) const noexcept;

    // Loads a resource-path manifest: one path per line, blanks ignored.
    // Officially shipped as tools/ResourceExtractor/filelist.txt relative to
    // the game install. An unreadable source is not an error; it just leaves
    // the table empty and reports the count that was loaded.
    bool load_names(const std::filesystem::path& source);
    // The resolved path for this entry, or an empty string when no table is
    // loaded or the hash is unknown. In the shipped archives 18.6% of entries
    // have no known name, so callers must handle the empty case.
    std::string name(usize index) const;

    // How many of this archive's entries resolve to a known path. Scans
    // the table every call, so prefer it for a one-off summary line rather
    // than inside a loop over entries.
    usize named_count() const noexcept;

    // Materialise one entry into `out`, exactly `size` bytes long.
    //
    // Mode 0x02 entries whose header validates are returned verbatim. Mode
    // 0x01 LZW streams decode through the codec (the recorded size is the
    // LZW stream length, and the decoded output is not truncated back to a
    // separate asset size). Non-stored mode 0x02 is still refused: the PRNG
    // keystream is unverified against real archives, and returning it would
    // hand a renderer noise instead of failing.
    //
    // Returns false and fills error() with the reason on any failure.
    bool decode(usize index, std::vector<u8>& out);

private:
    bool fail(std::string message);

    std::vector<u8> blob_;
    std::vector<ArchiveEntry> entries_;
    NameTable names_;
    std::string error_;
    std::string describe_;
    u8 mode_ = 0;
    };

}  // namespace isaec::data

#endif  // ISAEC_DATA_ARCHIVE_HPP