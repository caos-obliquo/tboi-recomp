#include "isaec/engine/clock.hpp"
#include "isaec/engine/engine.hpp"
#include "isaec/engine/renderer.hpp"
#include "isaec/platform/null_renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string next_arg(int argc, char** argv, int& index, const char* fallback) {
    if (index >= argc) return fallback;
    return argv[index++];
}

}  // namespace

int main(int argc, char** argv) {
    int cursor = 1;
    const std::string root = next_arg(argc, argv, cursor, ".");
    const std::string frames_arg = next_arg(argc, argv, cursor, "600");
    const std::string width_arg = next_arg(argc, argv, cursor, "1280");
    const std::string height_arg = next_arg(argc, argv, cursor, "720");

    isaec::engine::EngineConfig config;
    config.viewport_width = static_cast<isaec::i32>(std::strtol(width_arg.c_str(), nullptr, 10));
    config.viewport_height = static_cast<isaec::i32>(std::strtol(height_arg.c_str(), nullptr, 10));

    const auto frame_budget = static_cast<isaec::u64>(std::strtoull(frames_arg.c_str(), nullptr, 10));
    if (frame_budget == 0) {
        std::fprintf(stderr, "frame budget must be positive\n");
        return 2;
    }

    isaec::platform::NullRenderer renderer;
    isaec::engine::SteadyClock clock;
    isaec::engine::Engine engine(renderer, config, clock);

    if (!engine.mount(root)) {
        std::fprintf(stderr, "no data source under %s\n", root.c_str());
        return 1;
    }

    std::printf("backend   %.*s\n",
                static_cast<int>(renderer.backend_name().size()),
                renderer.backend_name().data());
    std::printf("data      %s\n", engine.data()->describe().c_str());
    std::printf("viewport  %d x %d\n", config.viewport_width, config.viewport_height);

    if (!engine.start()) {
        std::fprintf(stderr, "engine start failed\n");
        return 1;
    }

    while (engine.frame_count() < frame_budget && engine.running()) {
        engine.tick();
    }

    engine.stop();

    std::printf("frames    %lld\n", static_cast<long long>(engine.frame_count()));
    std::printf("elapsed   %.3f s\n", clock.elapsed_seconds());
    std::printf("draws     %lld\n", static_cast<long long>(renderer.draw_calls()));
    std::printf("presented %lld\n", static_cast<long long>(renderer.presented()));
    std::printf("textures  %lld live\n", static_cast<long long>(renderer.live_textures()));
    return 0;
}
