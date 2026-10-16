// Windowed milestone demo.
//
// The headless binary (null_main.cpp) counts frames and draws nothing. This one
// opens a real window and draws an image. That is the whole point: it proves
// the Renderer abstraction in engine/renderer.hpp is satisfied by something
// other than the null backend, which is the condition for every later milestone
// to be visible at all.
//
// Two source modes, picked by the first argument's extension:
//
//   isaec_window <image.png> [frames] [width] [height]
//   isaec_window <archive.a> <entry-index> [frames] [width] [height]
//
// The second form runs the whole pipeline inside the engine: read the ARCH000
// container, apply the entry's codec, hand the bytes to libpng, upload to the
// GPU. Nothing shells out to tools/ and nothing is pre-extracted.
//
// Argument handling mirrors null_main.cpp positionally rather than adding a
// parser, so the two binaries stay diffable.

#include "isaec/data/archive.hpp"
#include "isaec/data/image.hpp"
#include "isaec/engine/renderer.hpp"
#include "isaec/platform/sdl_renderer.hpp"
#include "isaec/platform/types.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string next_arg(int argc, char** argv, int& index, const char* fallback) {
    if (index >= argc) return fallback;
    return argv[index++];
}

bool ends_with(const std::string& text, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

bool read_file(const char* path, std::vector<isaec::u8>& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (size <= 0) {
        std::fprintf(stderr, "%s is empty\n", path);
        std::fclose(file);
        return false;
    }

    out.resize(static_cast<isaec::usize>(size));
    const std::size_t read = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);

    if (read != out.size()) {
        std::fprintf(stderr, "short read on %s\n", path);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    int cursor = 1;
    const std::string source = next_arg(argc, argv, cursor, "");
    if (source.empty()) {
        std::fprintf(stderr,
                     "usage: isaec_window <image.png|archive.a> [entry-index] [frames] "
                     "[width] [height]\n");
        return 2;
    }

    const bool from_archive = ends_with(source, ".a");

    std::string index_arg;
    std::string frames_arg;
    std::string width_arg;
    std::string height_arg;
    if (from_archive) {
        index_arg = next_arg(argc, argv, cursor, "0");
        frames_arg = next_arg(argc, argv, cursor, "600");
    } else {
        frames_arg = next_arg(argc, argv, cursor, "600");
    }
    width_arg = next_arg(argc, argv, cursor, "1280");
    height_arg = next_arg(argc, argv, cursor, "720");

    isaec::i32 width = static_cast<isaec::i32>(std::strtol(width_arg.c_str(), nullptr, 10));
    isaec::i32 height = static_cast<isaec::i32>(std::strtol(height_arg.c_str(), nullptr, 10));
    const auto frame_budget =
        static_cast<isaec::u64>(std::strtoull(frames_arg.c_str(), nullptr, 10));
    if (frame_budget == 0) {
        std::fprintf(stderr, "frame budget must be positive\n");
        return 2;
    }

    // From here on the source is the only thing that differs: both modes hand
    // back encoded image bytes plus a label for them.
    std::vector<isaec::u8> encoded;
    std::string label = source;
    std::string origin;

    if (from_archive) {
        const auto entry_index =
            static_cast<isaec::usize>(std::strtoull(index_arg.c_str(), nullptr, 10));
        isaec::data::Archive archive;
        if (!archive.open(source)) {
            std::fprintf(stderr, "%s\n", archive.error().c_str());
            return 1;
        }
        if (!archive.decode(entry_index, encoded)) {
            std::fprintf(stderr, "%s: %s\n", source.c_str(), archive.error().c_str());
            return 1;
        }
        const isaec::data::ArchiveEntry& entry = archive.entries()[entry_index];

        // describe() covers mode and entry count; the stored marker and the name
        // hash are the two facts that explain what decode() did with the entry.
        origin = archive.describe();
        if (archive.is_stored(entry_index)) origin += " stored";
        origin += "  hash=" + [&] {
            char hash[16];
            std::snprintf(hash, sizeof hash, "0x%08x", entry.name_hash);
            return std::string(hash);
        }();

        label = "entry " + std::to_string(entry_index);
    } else if (!read_file(source.c_str(), encoded)) {
        return 1;
    }

    const auto decoded = isaec::data::decode_png(encoded.data(), encoded.size());
    if (!decoded) {
        std::fprintf(stderr, "not a decodable PNG: %s\n", label.c_str());
        return 1;
    }

    isaec::platform::SdlRenderer renderer("isaec", width, height);
    if (!renderer.initialize(width, height)) {
        std::fprintf(stderr, "renderer initialize failed\n");
        return 1;
    }

    const isaec::i32 texture =
        renderer.create_texture(decoded->width, decoded->height, decoded->rgba.data(),
                                decoded->rgba.size());
    if (texture < 0) {
        std::fprintf(stderr, "texture upload failed\n");
        renderer.shutdown();
        return 1;
    }

    std::printf("backend   %.*s\n", static_cast<int>(renderer.backend_name().size()),
                renderer.backend_name().data());
    if (!origin.empty()) std::printf("archive   %s\n", origin.c_str());
    std::printf("image     %s %d x %d\n", label.c_str(), decoded->width, decoded->height);
    std::printf("viewport  %d x %d\n", width, height);
    std::fflush(stdout);

    const isaec::engine::DrawCall call{
        texture,
        {0, 0, decoded->width, decoded->height},
        static_cast<isaec::f32>((width - decoded->width) / 2),
        static_cast<isaec::f32>((height - decoded->height) / 2),
        1.0f,
        1.0f,
        1.0f,
    };

    // A fixed frame budget rather than an event loop: this binary has to be
    // runnable unattended as a check, so it terminates on its own.
    constexpr auto kFrameTime = std::chrono::microseconds(16666);
    const auto started = std::chrono::steady_clock::now();
    isaec::u64 frames = 0;

    while (frames < frame_budget && renderer.alive()) {
        const auto frame_started = std::chrono::steady_clock::now();

        renderer.submit(call);
        renderer.present();
        ++frames;

        const auto spent = std::chrono::steady_clock::now() - frame_started;
        if (spent < kFrameTime) std::this_thread::sleep_for(kFrameTime - spent);
    }

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    renderer.destroy_texture(texture);
    renderer.shutdown();

    std::printf("frames    %lld\n", static_cast<long long>(frames));
    std::printf("elapsed   %.3f s\n", elapsed);
    std::printf("draws     %lld\n", static_cast<long long>(renderer.draw_calls()));
    std::printf("presented %lld\n", static_cast<long long>(renderer.presented()));
    std::printf("textures  %lld live\n", static_cast<long long>(renderer.live_textures()));
    return 0;
}