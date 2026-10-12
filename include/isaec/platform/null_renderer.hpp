#pragma once

#include "isaec/engine/renderer.hpp"
#include "isaec/platform/types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace isaec::platform {

// Headless renderer. Draws nothing; counts everything.
//
// This exists so the engine, the binding layer and the test suite can run on a
// machine with no display and no GPU, and so a platform backend has a concrete
// example of the smallest thing that satisfies Renderer.
class NullRenderer final : public engine::Renderer {
public:
    std::string_view backend_name() const noexcept override { return "null"; }

    bool initialize(i32 width, i32 height) override;
    void shutdown() noexcept override;
    i32 create_texture(i32 width, i32 height, const u8* rgba, usize byte_count) override;
    void destroy_texture(i32 texture) noexcept override;
    void submit(const engine::DrawCall&) override;
    void present() override;
    i32 viewport_width() const noexcept override { return width_; }
    i32 viewport_height() const noexcept override { return height_; }

    [[nodiscard]] u64 draw_calls() const noexcept { return draw_calls_; }
    [[nodiscard]] u64 presented() const noexcept { return presented_; }
    [[nodiscard]] usize live_textures() const noexcept { return textures_.size(); }
    [[nodiscard]] usize pending_draws() const noexcept { return queued_; }

private:
    struct Entry {
        i32 handle;
        i32 width;
        i32 height;
    };

    i32 width_ = 0;
    i32 height_ = 0;
    i32 next_handle_ = 1;
    bool initialized_ = false;

    std::vector<Entry> textures_;
    u64 draw_calls_ = 0;
    u64 presented_ = 0;
    usize queued_ = 0;
};

}  // namespace isaec::platform
