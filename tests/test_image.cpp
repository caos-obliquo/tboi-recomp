// PNG decoding tests.
//
// The embedded image is generated, not transcribed: a 2x2 8-bit RGBA PNG with
// four distinct, checkable pixels (red, green, blue, white). Hand-writing PNG
// bytes is not a thing anyone should attempt, and a fixture that cannot be
// regenerated cannot be trusted when a failure needs diagnosing.

#include "isaec/data/image.hpp"

#include "test_harness.hpp"

#include <cstddef>

using isaec::u8;
using isaec::data::decode_png;
using isaec::data::Image;

namespace {

const u8 kTwoByTwoRgba[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x0c, 0x81, 0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0x03,
    0xd9, 0x64, 0xf1, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82,
};

const u8 kGarbage[] = {
    0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
};

}  // namespace

ISAEC_TEST(image_decode_png_produces_expected_dimensions) {
    const auto image = decode_png(kTwoByTwoRgba, sizeof(kTwoByTwoRgba));
    REQUIRE(image.has_value());

    CHECK(image->width == 2);
    CHECK(image->height == 2);
    CHECK(image->rgba.size() == 16);
    CHECK(!image->empty());
}

// Checks the actual pixels, not just the shape. A decoder that returned the
// right number of zero bytes would pass a size-only test while producing a
// fully black sprite, which is the failure this milestone exists to rule out.
ISAEC_TEST(image_decode_png_returns_expected_pixels) {
    const auto image = decode_png(kTwoByTwoRgba, sizeof(kTwoByTwoRgba));
    REQUIRE(image.has_value());

    const u8 expected[16] = {255, 0,   0,   255, 0,   255, 0,   255,
                             0,   0,   255, 255, 255, 255, 255, 255};
    for (std::size_t i = 0; i < sizeof(expected); ++i) {
        CHECK(image->rgba[i] == expected[i]);
    }
}

// Cut mid-stream so the IDAT is short. This is the shape a truncated archive
// entry actually takes, and it must be a clean failure rather than a crash or a
// partially filled buffer.
ISAEC_TEST(image_decode_png_rejects_truncated_input) {
    const std::size_t truncated = 40;  // header plus part of the IDAT
    CHECK(truncated < sizeof(kTwoByTwoRgba));
    CHECK(!decode_png(kTwoByTwoRgba, truncated).has_value());
}

ISAEC_TEST(image_decode_png_rejects_garbage) {
    CHECK(!decode_png(kGarbage, sizeof(kGarbage)).has_value());
}

ISAEC_TEST(image_decode_png_rejects_empty_input) {
    CHECK(!decode_png(kTwoByTwoRgba, 0).has_value());
    CHECK(!decode_png(nullptr, 0).has_value());
    // Below the 8-byte PNG signature. Nothing here is a valid file, and the
    // point is that it is rejected without entering libpng at all.
    CHECK(!decode_png(kTwoByTwoRgba, 4).has_value());
}