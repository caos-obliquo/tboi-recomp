#include "isaec/data/image.hpp"

#include <png.h>

#include <cstring>

namespace isaec::data {

std::optional<Image> decode_png(const u8* data, usize size) {
    if (data == nullptr || size == 0) return std::nullopt;
    // The signature alone is 8 bytes. libpng validates the header itself, so
    // this only avoids a pointless call and documents the floor.
    if (size < 8) return std::nullopt;

    // The simplified API rather than the classic one: it needs no setjmp target
    // and no per-callback plumbing, it converts any input type to the requested
    // output format including interlace, and it reports failure by return value
    // with the reason left in image.message. That last part is what lets this
    // function stay exception-free and abort-free.
    png_image image;
    std::memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;

    if (png_image_begin_read_from_memory(&image, data, size) == 0) {
        // The manual requires this on the failure path to release anything
        // begin_read_from_memory had taken before it gave up.
        png_image_finish_read(&image, nullptr, nullptr, 0, nullptr);
        png_image_free(&image);
        return std::nullopt;
    }

    // Bound the dimensions before allocating. libpng has already committed to
    // these from the header, so this is the only point where the size is still
    // a number rather than a reservation.
    if (image.width <= 0 || image.height <= 0 ||
        image.width > static_cast<png_uint_32>(kMaxImageDimension) ||
        image.height > static_cast<png_uint_32>(kMaxImageDimension)) {
        png_image_free(&image);
        return std::nullopt;
    }

    // Set before sizing: PNG_IMAGE_SIZE derives the stride from the format, so
    // the allocation would come out one byte per pixel short if the format were
    // still the default.
    image.format = PNG_FORMAT_RGBA;

    Image out;
    out.width = static_cast<i32>(image.width);
    out.height = static_cast<i32>(image.height);
    // Both axes are capped above, so width * height * 4 cannot wrap size_t.
    out.rgba.resize(PNG_IMAGE_SIZE(image));

    const int ok = png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr);
    png_image_free(&image);
    if (ok == 0) return std::nullopt;

    return out;
}

}  // namespace isaec::data