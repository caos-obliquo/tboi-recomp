// Abstract renderer.
//
// No graphics API type appears in this header. A platform backend implements
// Renderer; the engine core only ever calls through this interface. That is
// what keeps one source tree buildable for Android arm64 (Vulkan or GLES
// backend) and Linux x86_64 (Vulkan, GL, or a headless null) without
// conditional compilation leaking into the core.

#ifndef ISAEC_ENGINE_RENDERER_HPP
#define ISAEC_ENGINE_RENDERER_HPP

#include <string>
#include <string_view>

#include "isaec/platform/types.hpp"

namespace isaec::engine {

// A source rectangle, in texels.
struct Rect {
    i32 x = 0;
    i32 y = 0;
    i32 w = 0;
    i32 h = 0;
};

// A draw request in logical pixels. Coordinates are top-left origin.
struct DrawCall {
    i32 texture = 0;      // renderer-assigned texture handle
    Rect src;             // source rect in the texture
    f32 x = 0.0f;         // destination, logical px
    f32 y = 0.0f;
    f32 scale_x = 1.0f;
    f32 scale_y = 1.0f;
    f32 alpha = 1.0f;
};

class Renderer {
public:
    virtual ~Renderer() = default;

    // Name of the backend, for logs. Static, e.g. "null" or "vulkan".
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

    // Bring up a drawing context. `width`/`height` are logical pixels.
    virtual bool initialize(i32 width, i32 height) = 0;
    virtual void shutdown() noexcept = 0;

    // Upload pixel data into a texture slot. Returns a handle, or a negative
    // value on failure.
    virtual i32 create_texture(i32 width, i32 height, const u8* rgba, usize byte_count) = 0;
    virtual void destroy_texture(i32 texture) noexcept = 0;

    // Queue a draw. Order of calls is the submission order.
    virtual void submit(const DrawCall& call) = 0;

    // Present everything queued since the last present.
    virtual void present() = 0;

    [[nodiscard]] virtual i32 viewport_width() const noexcept = 0;
    [[nodiscard]] virtual i32 viewport_height() const noexcept = 0;

protected:
    Renderer() = default;
    // Non-copyable: a renderer owns device state.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
};

}  // namespace isaec::engine

#endif  // ISAEC_ENGINE_RENDERER_HPP
