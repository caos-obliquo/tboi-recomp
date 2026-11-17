// Windowed milestone demo.
//
// The headless binary (null_main.cpp) counts frames and draws nothing. This one
// opens a real window and draws an image. That is the whole point: it proves
// the Renderer abstraction in engine/renderer.hpp is satisfied by something
// other than the null backend, which is the condition for every later milestone
// to be visible at all.
//
// Three source modes, picked by the first argument:
//
//   isaec_window <image.png> [frames] [width] [height]
//   isaec_window <archive.a> <entry-index> [frames] [width] [height]
//   isaec_window slice [width] [height] [frames]
//
// The archive form runs the whole pipeline inside the engine: read the ARCH000
// container, apply the entry's codec, hand the bytes to libpng, upload to the
// GPU. Nothing shells out to tools/ and nothing is pre-extracted.
//
// The slice form is the playable white-box (the issue #79 playtesting box):
// an empty room (floor, four walls) with a controllable marker square — move
// it with arrows/WASD or the stick of XInput-shaped pad 0. Drawn with one
// procedural texture, paced to 60 Hz, Esc exits. It reads no game data and
// draws nothing Nicalis-derived. A frames argument of 0 runs until the
// window closes (the actual playtest mode); the default budget of 300 frames
// exists so headless smoke runs self-terminate.
//
// Argument handling mirrors null_main.cpp positionally rather than adding a
// parser, so the two binaries stay diffable.

#include "isaec/data/archive.hpp"
#include "isaec/data/image.hpp"
#include "isaec/engine/renderer.hpp"
#include "isaec/engine/sandbox.hpp"
#include "isaec/platform/sdl_input.hpp"
#include "isaec/platform/sdl_renderer.hpp"
#include "isaec/platform/types.hpp"

#include <SDL_keyboard.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
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

int run_slice(isaec::i32 width, isaec::i32 height, isaec::u64 frame_budget) {
    isaec::platform::SdlRenderer renderer("isaec-slice", width, height);
    if (!renderer.initialize(width, height)) return 1;

    // 64x64 white texture, scaled down by the renderer's 64x clamp means any
    // destination under 4096 logical px stays exactly integer-scaled.
    const std::vector<isaec::u8> white(64 * 64 * 4, 255);
    const isaec::i32 tex = renderer.create_texture(64, 64, white.data(), white.size());
    if (tex < 0) {
        renderer.shutdown();
        return 1;
    }

    const isaec::engine::Rect full{0, 0, 64, 64};
    const isaec::f32 room_x = width * 0.15f, room_y = height * 0.15f;
    const isaec::f32 room_w = width * 0.70f, room_h = height * 0.65f;
    const isaec::f32 wall = 16.0f, marker = 32.0f;
    // Playtest sandbox state: the entity is a square moved by keyboard or
    // the first XInput-shaped pad, constrained to the room's inner bounds.
    isaec::engine::SandboxPos entity{room_x + (room_w - marker) * 0.5f,
                                     room_y + (room_h - marker) * 0.5f};
    const isaec::engine::SandboxBounds bounds{room_x + wall, room_y + wall,
                                              room_w - 2.0f * wall, room_h - 2.0f * wall};
    constexpr isaec::f32 kSpeedPxPerSec = 240.0f;
    constexpr isaec::f32 kDt = 1.0f / 60.0f;  // fixed step; matches the 60 Hz pacing below
    isaec::platform::SdlGamepadSource pads;

    const isaec::engine::DrawCall floor_call{
        tex, full, room_x, room_y, room_w / 64.0f, room_h / 64.0f, 0.35f};
    const isaec::engine::DrawCall wall_left{tex, full, room_x, room_y, wall / 64.0f,
                                            room_h / 64.0f, 1.0f};
    const isaec::engine::DrawCall wall_right{tex, full, room_x + room_w - wall, room_y,
                                             wall / 64.0f, room_h / 64.0f, 1.0f};
    const isaec::engine::DrawCall wall_top{tex, full, room_x, room_y, room_w / 64.0f,
                                           wall / 64.0f, 1.0f};
    const isaec::engine::DrawCall wall_bottom{tex, full, room_x, room_y + room_h - wall,
                                              room_w / 64.0f, wall / 64.0f, 1.0f};
    isaec::engine::DrawCall marker_call{tex, full, entity.x, entity.y,
                                        marker / 64.0f, marker / 64.0f, 1.0f};

    std::printf("backend   %.*s\n", static_cast<int>(renderer.backend_name().size()),
                renderer.backend_name().data());
    std::printf("scene     white-box playtest room\n");
    std::printf("controls  arrows/WASD move; stick 0 moves (XInput); Space does nothing; Esc quits\n");
    std::printf("viewport  %d x %d\n", width, height);
    std::fflush(stdout);

    constexpr auto kFrameTime = std::chrono::microseconds(16666);
    const auto started = std::chrono::steady_clock::now();
    isaec::u64 frames = 0;
    double busy_ms = 0.0;

    while (frames < frame_budget && renderer.alive()) {
        const auto frame_started = std::chrono::steady_clock::now();

        // Sample input and advance the entity before drawing. The keyboard
        // state is edge-triggered by SDL's event pump in present() of the
        // previous frame (one frame of lag); arrows and WASD both work.
        const auto* keys = SDL_GetKeyboardState(nullptr);
        const bool kb_left = keys[SDL_SCANCODE_LEFT] != 0 || keys[SDL_SCANCODE_A] != 0;
        const bool kb_right = keys[SDL_SCANCODE_RIGHT] != 0 || keys[SDL_SCANCODE_D] != 0;
        const bool kb_up = keys[SDL_SCANCODE_UP] != 0 || keys[SDL_SCANCODE_W] != 0;
        const bool kb_down = keys[SDL_SCANCODE_DOWN] != 0 || keys[SDL_SCANCODE_S] != 0;
        pads.poll();
        const isaec::engine::GamepadState& pad = pads.states()[0];  // player 1
        const isaec::engine::SandboxMove move =
            isaec::engine::sandbox_axes(kb_left, kb_right, kb_up, kb_down, pad);
        entity = isaec::engine::sandbox_step(entity, move, bounds, marker,
                                             kSpeedPxPerSec, kDt);
        marker_call.x = entity.x;
        marker_call.y = entity.y;

        renderer.submit(floor_call);
        renderer.submit(wall_left);
        renderer.submit(wall_right);
        renderer.submit(wall_top);
        renderer.submit(wall_bottom);
        renderer.submit(marker_call);
        renderer.present();
        ++frames;

        const auto spent = std::chrono::steady_clock::now() - frame_started;
        busy_ms += std::chrono::duration<double, std::milli>(spent).count();
        if (spent < kFrameTime) std::this_thread::sleep_for(kFrameTime - spent);
    }

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    renderer.destroy_texture(tex);
    renderer.shutdown();

    std::printf("frames    %lld\n", static_cast<long long>(frames));
    std::printf("elapsed   %.3f s\n", elapsed);
    std::printf("pacing    avg=%.3f ms est=%.2f Hz\n", busy_ms / static_cast<double>(frames),
                static_cast<double>(frames) / elapsed);
    std::printf("draws     %lld\n", static_cast<long long>(renderer.draw_calls()));
    std::printf("presented %lld\n", static_cast<long long>(renderer.presented()));
    std::printf("textures  %lld live\n", static_cast<long long>(renderer.live_textures()));
    return 0;
}

int main(int argc, char** argv) {
    int cursor = 1;
    const std::string source = next_arg(argc, argv, cursor, "");
    if (source.empty()) {
        std::fprintf(stderr,
                     "usage: isaec_window <image.png|archive.a> [entry-index] "
                     "[frames] [width] [height]  |  isaec_window slice "
                     "[width] [height] [frames]  (frames 0 = play until Esc)\n");
        return 2;
    }

    if (source == "slice") {
        const isaec::i32 sw = static_cast<isaec::i32>(
            std::strtol(next_arg(argc, argv, cursor, "1280").c_str(), nullptr, 10));
        const isaec::i32 sh = static_cast<isaec::i32>(
            std::strtol(next_arg(argc, argv, cursor, "720").c_str(), nullptr, 10));
        isaec::u64 slice_frames = static_cast<isaec::u64>(
            std::strtoull(next_arg(argc, argv, cursor, "300").c_str(), nullptr, 10));
        // A zero frame budget means "play until Esc/close" — the playtesting
        // mode. A default budget keeps headless smoke runs self-terminating.
        if (slice_frames == 0) slice_frames = ~isaec::u64{0};
        return run_slice(sw, sh, slice_frames);
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

        // The official modding ResourceExtractor ships the resource manifest at
        // <install>/tools/ResourceExtractor/filelist.txt and the archives sit at
        // <install>/resources/packed/*.a, so three parent_path() hops reach the
        // install root. Missing manifest is not an error: names are a nicety.
        std::filesystem::path manifest =
            std::filesystem::path(source).parent_path().parent_path().parent_path() /
            "tools" / "ResourceExtractor" / "filelist.txt";
        const bool named = archive.load_names(manifest);
        if (named) {
            origin = std::string(archive.describe()) + "  " +
                     std::to_string(archive.named_count()) + "/" +
                     std::to_string(archive.entries().size()) + " named";
        } else {
            origin = archive.describe();
        }
        if (archive.is_stored(entry_index)) origin += " stored";
        origin += "  hash=" + [&] {
            char hash[16];
            std::snprintf(hash, sizeof hash, "0x%08x", entry.name_hash);
            return std::string(hash);
        }();

        const std::string resource = named ? archive.name(entry_index) : std::string{};
        label = resource.empty() ? std::string("entry ") + std::to_string(entry_index)
                                 : "entry " + std::to_string(entry_index) + " " + resource;
        if (!resource.empty()) origin += "  " + resource;
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