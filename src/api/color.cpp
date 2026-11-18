// The colour value. See include/isaec/api/color.hpp for where the field
// offsets and the method semantics come from.

#include "isaec/api/color.hpp"

#include <cstring>

namespace isaec::api {

f32 color_field(const Color& color, std::size_t offset) {
    f32 value = 0.0f;
    // The offsets are the game's own field offsets, so the value is read the
    // way the accessor routine reads it: as a float at that address.
    std::memcpy(&value, reinterpret_cast<const u8*>(&color) + offset, sizeof(f32));
    return value;
}

void color_set_field(Color& color, std::size_t offset, f32 value) {
    std::memcpy(reinterpret_cast<u8*>(&color) + offset, &value, sizeof(f32));
}

// 0x00682ed0.
void color_reset(Color& color) noexcept {
    color.r = 1.0f;
    color.g = 1.0f;
    color.b = 1.0f;
    color.a = 1.0f;
    for (f32& value : color.colorize) value = 0.0f;
    color.ro = 0.0f;
    color.go = 0.0f;
    color.bo = 0.0f;
}

// 0x00682f20.
void color_set_tint(Color& color, f32 r, f32 g, f32 b, f32 a) noexcept {
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
}

// 0x00682fb0.
void color_set_colorize(Color& color, f32 r, f32 g, f32 b, f32 a) noexcept {
    color.colorize[0] = r;
    color.colorize[1] = g;
    color.colorize[2] = b;
    color.colorize[3] = a;
}

// 0x00682fe0.
void color_set_offset(Color& color, f32 r, f32 g, f32 b) noexcept {
    color.ro = r;
    color.go = g;
    color.bo = b;
}

// 0x006832e0. The reference binary's native routine is
// `out = t * goal + (1 - t) * color` for all eleven floats.
Color color_lerp(const Color& color, const Color& goal, f32 t) noexcept {
    const f32 inverse = 1.0f - t;
    Color result{};
    result.r = t * goal.r + inverse * color.r;
    result.g = t * goal.g + inverse * color.g;
    result.b = t * goal.b + inverse * color.b;
    result.a = t * goal.a + inverse * color.a;
    for (std::size_t i = 0; i < 4; ++i) {
        result.colorize[i] = t * goal.colorize[i] + inverse * color.colorize[i];
    }
    result.ro = t * goal.ro + inverse * color.ro;
    result.go = t * goal.go + inverse * color.go;
    result.bo = t * goal.bo + inverse * color.bo;
    return result;
}

// 0x006831c0.
void color_multiply(Color& color, const Color& other) noexcept {
    color.r *= other.r;
    color.g *= other.g;
    color.b *= other.b;
    color.a *= other.a;

    // The native sum comparison counts a NaN as non-zero: the unordered case
    // jumps to the same branch as "not equal to zero".
    const f32 other_colorize_sum = other.colorize[0] + other.colorize[1] + other.colorize[2];
    if (!(other_colorize_sum == 0.0f)) {
        const f32 colorize_sum = color.colorize[0] + color.colorize[1] + color.colorize[2];
        if (colorize_sum == 0.0f) {
            for (std::size_t i = 0; i < 4; ++i) color.colorize[i] = other.colorize[i];
        } else {
            for (std::size_t i = 0; i < 4; ++i) {
                color.colorize[i] = (color.colorize[i] + other.colorize[i]) * 0.5f;
            }
        }
    }

    color.ro += other.ro;
    color.go += other.go;
    color.bo += other.bo;
}

}  // namespace isaec::api
