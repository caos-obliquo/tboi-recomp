// Filesystem-backed data sources.
//
// This is the only translation unit in the core that touches <fstream>, and it
// is compiled with ISAEC_DATA_IO defined (see CMakeLists.txt) so the rest of
// the core stays free of filesystem includes and can build for a platform whose
// storage layer differs.
//
// Only a directory tree of loose files is supported. The game also ships packed
// .a containers, but their entry layout is not yet reversed, so they are not
// offered here rather than being opened as something empty.

#include "isaec/data/source.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>


namespace isaec::data {
namespace {

std::string normalize(std::string_view rel) {
    std::string out;
    out.reserve(rel.size());
    for (const char c : rel) {
        if (c == '\\') {
            out.push_back('/');
        } else {
            out.push_back(c);
        }
    }
    // Leading "./" or "/" would break exact matching against listed paths.
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    if (out.rfind("./", 0) == 0) out.erase(0, 2);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// DirectorySource
// ---------------------------------------------------------------------------

class DirectorySource final : public DataSource {
public:
    explicit DirectorySource(std::string root) : root_(std::move(root)) {
        // Store the root without a trailing separator so joins stay clean.
        while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
    }

    [[nodiscard]] std::string describe() const override { return "directory:" + root_; }

    [[nodiscard]] bool exists(std::string_view rel_path) const override {
        const std::string rel = normalize(rel_path);
        if (rel.empty()) return true;
        std::error_code ec;
        return std::filesystem::is_regular_file(std::filesystem::path(root_) / rel, ec);
    }

    bool read(std::string_view rel_path, std::vector<u8>& out) const override {
        const std::string rel = normalize(rel_path);
        if (rel.empty()) return false;

        std::ifstream in(std::filesystem::path(root_) / rel, std::ios::binary);
        if (!in) return false;

        in.seekg(0, std::ios::end);
        const auto end = in.tellg();
        if (end < 0) return false;
        in.seekg(0, std::ios::beg);

        out.resize(static_cast<usize>(end));
        if (end > 0) {
            in.read(reinterpret_cast<char*>(out.data()), end);
            if (!in && !in.eof()) {
                out.clear();
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<std::string> list(std::string_view prefix) const override {
        std::vector<std::string> out;
        const std::string rel_prefix = normalize(prefix);

        std::error_code ec;
        std::filesystem::path base(root_);
        if (!rel_prefix.empty()) base /= rel_prefix;
        if (!std::filesystem::is_directory(base, ec)) return out;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            std::string full = entry.path().string();
            if (full.rfind(root_.size(), 0) == 0) full.erase(0, root_.size());
            while (!full.empty() && full.front() == '/') full.erase(full.begin());
            if (full.rfind("./", 0) == 0) full.erase(0, 2);
            out.push_back(full);
        }

        std::sort(out.begin(), out.end());
        return out;
    }

private:
    std::string root_;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

// Open `root` as a data source. If `root/prefix` exists it is used, otherwise
// `root` itself is treated as the directory. Returns nullptr when nothing
// usable is present. Packed `.a` files are not readable yet: they use a custom
// container whose entry layout is not yet reversed, so a regular file is
// rejected rather than silently opened as empty.
std::unique_ptr<DataSource> open_source(const std::string& root, const std::string& prefix) {
    std::error_code ec;
    std::filesystem::path base(root);

    std::filesystem::path target = prefix.empty() ? base : base / prefix;
    if (prefix.empty() || std::filesystem::exists(target, ec)) base = target;
    else if (!std::filesystem::exists(base, ec)) return nullptr;

    if (std::filesystem::is_directory(base, ec)) {
        return std::make_unique<DirectorySource>(base.string());
    }
    return nullptr;
}

}  // namespace isaec::data
