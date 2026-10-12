// Where game data comes from.
//
// Two shapes exist in the wild:
//   - Packed:   resources/packed/<name>.a   (the common Steam layout)
//   - Unpacked: a directory tree of loose files (some distributions ship this)
// DataSource abstracts both so the engine and the binding layer never care
// which they were handed. This is the OpenMW model: the repo ships no assets,
// the user points the engine at their own install.

#ifndef ISAEC_DATA_SOURCE_HPP
#define ISAEC_DATA_SOURCE_HPP

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::data {

class DataSource {
public:
    virtual ~DataSource() = default;

    // Human-readable origin, for logs and error messages.
    [[nodiscard]] virtual std::string describe() const = 0;

    // Normalized forward-slash path relative to the data root, e.g.
    // "gfx/players/isaac.png". Returns false when absent.
    [[nodiscard]] virtual bool exists(std::string_view rel_path) const = 0;

    // Read a whole entry. False when absent or unreadable.
    virtual bool read(std::string_view rel_path, std::vector<u8>& out) const = 0;

    // Read into an owned copy of the payload.
    [[nodiscard]] bool read_copy(std::string_view rel_path, std::vector<u8>& out) const {
        return read(rel_path, out);
    }

    // Every entry path under `prefix` ("" for the whole tree). Sorted.
    [[nodiscard]] virtual std::vector<std::string> list(std::string_view prefix = {}) const = 0;

protected:
    DataSource() = default;
    DataSource(const DataSource&) = delete;
    DataSource& operator=(const DataSource&) = delete;
};

// Open `root` as a data source. If `root/prefix` exists it is used directly,
// otherwise `root` itself. A directory yields a DirectorySource. Returns nullptr
// when nothing usable is present, including for any regular file.
[[nodiscard]] std::unique_ptr<DataSource> open_source(const std::string& root,
                                                     const std::string& prefix);

}  // namespace isaec::data

#endif  // ISAEC_DATA_SOURCE_HPP
