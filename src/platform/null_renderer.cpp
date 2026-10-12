#include "isaec/platform/null_renderer.hpp"

namespace isaec::platform {

bool NullRenderer::initialize(i32 width, i32 height) {
    if (width <= 0 || height <= 0) return false;
    width_ = width;
    height_ = height;
    initialized_ = true;
    return true;
}

void NullRenderer::shutdown() noexcept {
    textures_.clear();
    next_handle_ = 1;
    initialized_ = false;
}

i32 NullRenderer::create_texture(i32 width, i32 height, const u8* rgba, usize byte_count) {
    if (!initialized_ || width <= 0 || height <= 0 || rgba == nullptr) return -1;

    const usize needed = static_cast<usize>(width) * static_cast<usize>(height) * 4u;
    if (byte_count < needed) return -1;

    const i32 handle = next_handle_++;
    textures_.push_back({handle, width, height});
    return handle;
}

void NullRenderer::destroy_texture(i32 texture) noexcept {
    // Handles are dense from 1 and never recycled within a session, so this
    // linear scan is a bounds check in disguise.
    for (auto it = textures_.begin(); it != textures_.end(); ++it) {
        if (it->handle == texture) {
            textures_.erase(it);
            return;
        }
    }
}

void NullRenderer::submit(const engine::DrawCall&) {
    ++draw_calls_;
    ++queued_;
}

void NullRenderer::present() {
    // Nothing survives to a framebuffer: a null backend has no composite target,
    // so pending_draws() is only meaningful between submit and present.
    queued_ = 0;
    ++presented_;
}

}  // namespace isaec::platform
