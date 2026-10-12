#include "isaec/engine/engine.hpp"

#include <algorithm>
#include <utility>

#include "isaec/data/source.hpp"

namespace isaec::engine {

Engine::Engine(Renderer& renderer, const EngineConfig& config, Clock& clock)
    : renderer_(&renderer), clock_(&clock), config_(config) {}

Engine::~Engine() = default;

bool Engine::mount(std::string game_root) {
    // Nicalis ships two layouts and the user may have either:
    //   <root>/resources/packed/<name>.a   (Steam, packed)
    //   <root>/resources/                   (some distributions, loose)
    // Try the packed tree first, then the loose one, then the bare root, so a
    // caller can point at the install dir or at `resources` interchangeably.
    static constexpr const char* kPrefixes[] = {
        "resources/packed",
        "resources",
        "",
    };

    for (const char* prefix : kPrefixes) {
        auto source = data::open_source(game_root, prefix);
        if (!source) continue;

        // A directory only counts if it actually holds data. An empty
        // resources/ should fall through to the next candidate rather than
        // silently producing a game that reads nothing.
        if (!source->list().empty()) {
            data_source_ = std::move(source);
            game_root_ = std::move(game_root);
            return true;
        }
    }

    data_source_.reset();
    game_root_.clear();
    return false;
}

bool Engine::start() {
    if (running_) return true;
    if (!data_source_) return false;

    if (!renderer_->initialize(config_.viewport_width, config_.viewport_height)) {
        return false;
    }

    // Prime the frame clock so the first tick does not see a full delta.
    last_frame_ = clock_->elapsed_seconds();
    running_ = true;
    return true;
}

void Engine::stop() noexcept {
    if (!running_) return;
    running_ = false;
    renderer_->shutdown();
}

void Engine::tick() {
    if (!running_) return;

    const f64 now = clock_->elapsed_seconds();
    // Clamp rather than trust the delta: a debugger break or a backgrounded
    // app would otherwise fast-forward every timer in the game on resume.
    const f64 delta = std::clamp(now - last_frame_, 0.0, config_.max_frame_seconds);
    last_frame_ = now;

    (void)delta;  // The binding layer's frame hook consumes this once it exists.

    renderer_->present();
    ++frames_;
}

const data::DataSource* Engine::data() const noexcept {
    return data_source_.get();
}

}  // namespace isaec::engine
