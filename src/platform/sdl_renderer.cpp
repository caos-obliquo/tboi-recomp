#include "isaec/platform/sdl_renderer.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace isaec::platform {

namespace {

// A DrawCall scale and alpha are hints carried by data, not physical
// quantities. Clamping keeps a corrupt or NaN value from becoming an infinite
// destination rectangle, which SDL2 rejects and which would otherwise pass
// unnoticed as a silently dropped draw.
constexpr f32 kMaxScale = 64.0f;

f32 clamp_scale(f32 value) noexcept {
    // The negated comparison, not value < 0: it is false for NaN, so NaN falls
    // through to the clamp and is reported as zero scale rather than passing
    // the check and poisoning the rectangle.
    if (!(value > 0.0f)) return 0.0f;
    return std::min(value, kMaxScale);
}

f32 clamp_alpha(f32 value) noexcept {
    if (!(value > 0.0f)) return 0.0f;
    return std::min(value, 1.0f);
}

}  // namespace

SdlRenderer::SdlRenderer(std::string title, i32 width, i32 height)
    : title_(std::move(title)), width_(width), height_(height) {}

SdlRenderer::~SdlRenderer() { release_all(); }

void SdlRenderer::release_all() noexcept {
    for (auto& entry : textures_) {
        if (entry.texture != nullptr) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(entry.texture));
        }
    }
    textures_.clear();

    // Renderer before window: the renderer holds a reference to the window's
    // surface and destroying them the other way round is a use-after-free.
    if (device_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(device_));
        device_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }

    frame_started_ = false;
    alive_ = false;
}

void SdlRenderer::release_video() noexcept {
    if (owns_video_init_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        owns_video_init_ = false;
    }
}

bool SdlRenderer::initialize(i32 width, i32 height) {
    if (device_ != nullptr) return true;
    if (width <= 0 || height <= 0) return false;

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "sdl: video subsystem: %s\n", SDL_GetError());
            return false;
        }
        owns_video_init_ = true;
    }

    SDL_Window* window = SDL_CreateWindow(
        title_.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "sdl: CreateWindow %dx%d: %s\n", width, height, SDL_GetError());
        release_video();
        return false;
    }

    SDL_Renderer* device =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (device == nullptr) {
        // The software fallback is not defensive padding. The project's target
        // hardware includes a low-power handheld and an integrated laptop GPU,
        // and a host that offers no accelerated path should still get a window
        // rather than a failure the user cannot act on.
        device = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (device == nullptr) {
        std::fprintf(stderr, "sdl: CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        release_video();
        return false;
    }

    window_ = window;
    device_ = device;
    width_ = width;
    height_ = height;

    // DrawCall coordinates are viewport pixels. Pinning the logical size keeps
    // that meaning fixed when the user resizes the window, instead of letting a
    // drag silently change what an x of 40 means.
    SDL_RenderSetLogicalSize(device, width, height);
    SDL_SetRenderDrawColor(device, 0, 0, 0, 255);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    alive_ = true;
    return true;
}

void SdlRenderer::shutdown() noexcept {
    release_all();
    release_video();
}

usize SdlRenderer::live_textures() const noexcept {
    usize count = 0;
    for (const auto& entry : textures_) {
        if (entry.texture != nullptr) ++count;
    }
    return count;
}

i32 SdlRenderer::create_texture(i32 width, i32 height, const u8* rgba, usize byte_count) {
    SDL_Renderer* device = static_cast<SDL_Renderer*>(device_);
    if (device == nullptr || width <= 0 || height <= 0 || rgba == nullptr) return -1;

    const usize needed = static_cast<usize>(width) * static_cast<usize>(height) * 4u;
    if (byte_count < needed) return -1;

    // Wrap the caller's buffer rather than copying it twice. The surface is
    // freed on the line below, but only after CreateTextureFromSurface has
    // taken its own copy, so the borrow never outlives the call.
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<u8*>(rgba), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
    if (surface == nullptr) {
        std::fprintf(stderr, "sdl: CreateRGBSurface %dx%d: %s\n", width, height, SDL_GetError());
        return -1;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(device, surface);
    SDL_FreeSurface(surface);
    if (texture == nullptr) {
        std::fprintf(stderr, "sdl: CreateTextureFromSurface %dx%d: %s\n", width, height,
                     SDL_GetError());
        return -1;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    // The handle is the slot index, and slots are never reused within a session.
    // That keeps a handle valid for as long as it exists, which matters because
    // destroy_texture leaves the slot empty rather than compacting: compacting
    // would renumber every handle above the freed one and silently redirect
    // every later draw.
    const auto handle = static_cast<i32>(textures_.size());
    textures_.push_back({texture, width, height});
    return handle;
}

void SdlRenderer::destroy_texture(i32 texture) noexcept {
    // Negative and out-of-range handles are expected rather than exceptional:
    // the engine is allowed to destroy a texture whose upload already failed,
    // and this is noexcept so it must not assert.
    if (texture < 0 || static_cast<usize>(texture) >= textures_.size()) return;

    SDL_Texture* handle = static_cast<SDL_Texture*>(textures_[static_cast<usize>(texture)].texture);
    if (handle == nullptr) return;  // already destroyed; idempotent
    SDL_DestroyTexture(handle);
    textures_[static_cast<usize>(texture)].texture = nullptr;
}

void SdlRenderer::submit(const engine::DrawCall& call) {
    SDL_Renderer* device = static_cast<SDL_Renderer*>(device_);
    if (device == nullptr) return;

    const auto index = static_cast<usize>(call.texture);
    if (call.texture < 0 || index >= textures_.size()) return;
    SDL_Texture* texture = static_cast<SDL_Texture*>(textures_[index].texture);
    if (texture == nullptr) return;  // slot was destroyed; drop the draw

    // SDL requires a clear before the first draw of a frame, but Renderer has no
    // frame-begin hook to do it in. Clearing lazily on the first submit
    // satisfies that without widening the interface for one backend's
    // bookkeeping. A frame that submits nothing keeps the previous contents,
    // which is the behaviour a frame counter wants anyway.
    if (!frame_started_) {
        SDL_SetRenderDrawColor(device, 0, 0, 0, 255);
        SDL_RenderClear(device);
        frame_started_ = true;
    }

    // A default-constructed Rect means "the whole texture". Passing a literal
    // SDL_RECT_NULL says that to SDL directly, where a zero-size rect would be
    // an error instead.
    SDL_Rect source{call.src.x, call.src.y, call.src.w, call.src.h};
    const SDL_Rect* source_ptr =
        (call.src.x == 0 && call.src.y == 0 && call.src.w == textures_[index].width &&
         call.src.h == textures_[index].height)
            ? nullptr
            : &source;

    SDL_FRect dest{call.x, call.y, static_cast<f32>(call.src.w) * clamp_scale(call.scale_x),
                   static_cast<f32>(call.src.h) * clamp_scale(call.scale_y)};

    SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(clamp_alpha(call.alpha) * 255.0f));
    if (SDL_RenderCopyF(device, texture, source_ptr, &dest) != 0) return;

    ++draw_calls_;
}

void SdlRenderer::present() {
    SDL_Renderer* device = static_cast<SDL_Renderer*>(device_);
    if (device == nullptr) return;

    // The queue is drained here because present is the only per-frame call the
    // interface guarantees, and a closed window that is never noticed turns the
    // main loop into a busy loop forever.
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
            alive_ = false;
        }
    }

    frame_started_ = false;
    if (!alive_) return;

    SDL_RenderPresent(device);
    ++presented_;
}

}  // namespace isaec::platform