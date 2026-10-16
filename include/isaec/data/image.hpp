// Decoded raster image.
//
// The archives only carry encoded PNG bytes, but nothing downstream the codec
// should know anything about PNG chunks. This header is the one place that
// crosses that line: the engine, the renderer and the font atlas builder all
// work on the rgba buffer this produces and never see a chunk.
//
// RGBA, non-premultiplied, row-major, first row at the top. That layout is
// accepted unchanged by SDL, OpenGL and Vulkan, so no backend needs a swizzle.

#ifndef ISAEC_DATA_IMAGE_HPP
#define ISAEC_DATA_IMAGE_HPP

#include <optional>
#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::data {

struct Image {
    i32 width = 0;
    i32 height = 0;
    std::vector<u8> rgba;

    [[nodiscard]] usize byte_count() const noexcept { return rgba.size(); }
    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0 || rgba.empty(); }
};

// Per-axis ceiling on accepted dimensions. Not a policy limit: libpng commits
// to the header's width and height before it decodes anything, and the output
// allocation is sized from them, so an unchecked header field turns one corrupt
// archive entry into an allocation of arbitrary size. Nothing in the game comes
// close to this.
inline constexpr i32 kMaxImageDimension = 16384;

// Decode a PNG to RGBA8. Handles greyscale, RGB, palette and RGBA inputs at
// 8-bit depth, interlaced or not, because the archives contain all of them.
//
// Returns nullopt on malformed, truncated, or oversized input. Bad input is
// reported by return value rather than through the libpng error handler, so
// this never aborts the process.
[[nodiscard]] std::optional<Image> decode_png(const u8* data, usize size);

}  // namespace isaec::data

#endif  // ISAEC_DATA_IMAGE_HPP