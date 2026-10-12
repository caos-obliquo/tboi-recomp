// Engine lifecycle.
//
// Engine owns a Renderer (injected, never created here) and a data source. It
// knows nothing about Lua or graphics APIs; the binding layer attaches to it
// separately. This is the entry point a platform backend drives.

#ifndef ISAEC_ENGINE_ENGINE_HPP
#define ISAEC_ENGINE_ENGINE_HPP

#include <memory>
#include <string>
#include <string_view>

#include "isaec/data/source.hpp"
#include "isaec/engine/clock.hpp"
#include "isaec/engine/renderer.hpp"
#include "isaec/platform/types.hpp"

namespace isaec::engine {

struct EngineConfig {
    i32 viewport_width = 1280;
    i32 viewport_height = 720;
    bool vsync = true;
    // Cap on frame time. A long stall (app backgrounded, breakpoint) is clamped
    // so physics does not fast-forward on resume.
    f64 max_frame_seconds = 1.0 / 15.0;
};

class Engine {
public:
    // `renderer` is borrowed, not owned: the platform backend keeps its own
    // Renderer and outlives the Engine.
    Engine(Renderer& renderer, const EngineConfig& config, Clock& clock);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Point the engine at the user's own game install. No data ships with this
    // repo; the path is supplied at runtime.
    bool mount(std::string game_root);

    // Bring up the renderer. mount() must have succeeded first.
    bool start();
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return running_; }

    // Advance one frame: update timing, run the binding layer's frame hook,
    // present the renderer.
    void tick();

    [[nodiscard]] Renderer& renderer() noexcept { return *renderer_; }
    [[nodiscard]] const Renderer& renderer() const noexcept { return *renderer_; }
    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }

    // Null until mount() succeeds.
    [[nodiscard]] const data::DataSource* data() const noexcept;

    [[nodiscard]] std::uint64_t frame_count() const noexcept { return frames_; }
    [[nodiscard]] const std::string& game_root() const noexcept { return game_root_; }

private:
    std::unique_ptr<data::DataSource> data_source_;

    Renderer* renderer_ = nullptr;
    Clock* clock_ = nullptr;
    EngineConfig config_;

    std::string game_root_;
    std::uint64_t frames_ = 0;
    f64 last_frame_ = 0.0;
    bool running_ = false;
};

}  // namespace isaec::engine

#endif  // ISAEC_ENGINE_ENGINE_HPP
