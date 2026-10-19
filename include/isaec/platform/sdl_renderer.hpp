// Windowed renderer over SDL2.
//
// SDL is a portability decision, not a preference. The project's stated goal is
// one source tree that builds for Linux x86_64 and Android arm64; SDL gives a
// window on both, and SDL_Renderer picks its own graphics backend at runtime, so
// this file also covers a Vulkan or GLES host without being edited.
//
// SDL's own types are held as opaque pointers here so that no SDL header
// escapes into a public include. That is deliberate: renderer.hpp documents
// that the core never sees a graphics API type, and leaking SDL_Texture through
// this header would undo that the first time a caller included it.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "isaec/engine/renderer.hpp"
#include "isaec/platform/types.hpp"

namespace isaec::platform {

class SdlRenderer final : public engine::Renderer {
public:
    SdlRenderer(std::string title, i32 width, i32 height);
    ~SdlRenderer() override;

    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;

    std::string_view backend_name() const noexcept override { return "sdl2"; }

    bool initialize(i32 width, i32 height) override;
    void shutdown() noexcept override;
    i32 create_texture(i32 width, i32 height, const u8* rgba, usize byte_count) override;
    void destroy_texture(i32 texture) noexcept override;
    void submit(const engine::DrawCall& call) override;
    void present() override;
    i32 viewport_width() const noexcept override { return width_; }
    i32 viewport_height() const noexcept override { return height_; }

    // Counters mirror NullRenderer so the windowed and headless binaries read as
    // siblings and their output can be compared line for line.
    [[nodiscard]] u64 draw_calls() const noexcept { return draw_calls_; }
    [[nodiscard]] u64 presented() const noexcept { return presented_; }
    [[nodiscard]] usize live_textures() const noexcept;

    // False once the user has closed the window, so a main loop can exit
    // instead of spinning against a surface nobody is looking at.
    [[nodiscard]] bool alive() const noexcept { return alive_; }

private:
    struct Entry {
        void* texture = nullptr;  // SDL_Texture*
        i32 width = 0;
        i32 height = 0;
    };

    void release_all() noexcept;
    void release_video() noexcept;

    std::string title_;
    i32 width_ = 0;
    i32 height_ = 0;
    bool alive_ = false;
    bool owns_video_init_ = false;
    bool frame_started_ = false;

    void* window_ = nullptr;  // SDL_Window*
    void* device_ = nullptr;  // SDL_Renderer*

    std::vector<Entry> textures_;
    u64 draw_calls_ = 0;
    u64 presented_ = 0;
};

}  // namespace isaec::platform