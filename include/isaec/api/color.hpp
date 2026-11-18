#pragma once

#include <cstddef>

#include "isaec/platform/types.hpp"

namespace isaec::api {

// The layout of the game's colour object.
//
// Recovered from the binary: every colour property shares one getter body
// (0x0089f590) and one setter body (0x0089f500); the two are told apart by
// their upvalue, and the getter's upvalue is a per-field accessor routine
// that reads one float at a fixed offset of the object. The offsets below
// are therefore the game's, not a guess.
//
//   accessor 0x00407240 -> R  at +0    accessor 0x004072b0 -> RO at +32
//   accessor 0x00407250 -> G  at +4    accessor 0x004072c0 -> GO at +36
//   accessor 0x00407260 -> B  at +8    accessor 0x004072d0 -> BO at +40
//   accessor 0x00407270 -> A  at +12
//
// The native methods show what the remaining floats are. SetColorize
// (0x00682fb0) writes four floats at +16, +20, +24 and +28, and the __mul
// routine (0x006831c0) treats them as one RGBA quadruple; they are exposed
// here as `colorize`. The three overlay channels are RO, GO and BO at +32,
// +36 and +40, written by SetOffset (0x00682fe0) and added by __mul.
constexpr std::size_t kColorOffsetR = 0;
constexpr std::size_t kColorOffsetG = 4;
constexpr std::size_t kColorOffsetB = 8;
constexpr std::size_t kColorOffsetA = 12;
constexpr std::size_t kColorOffsetColorize = 16;
constexpr std::size_t kColorOffsetRO = 32;
constexpr std::size_t kColorOffsetGO = 36;
constexpr std::size_t kColorOffsetBO = 40;

// The size of the colour object, the end of the last recovered field.
constexpr std::size_t kColorObjectBytes = 44;

// A colour value.
//
// The recomp stores the value itself in the userdata, where the original
// stores a two-word handle (a metatable pointer and a pointer to the game's
// C++ object). The field offsets are the game's; the representation of the
// handle is not.
struct Color {
    f32 r;
    f32 g;
    f32 b;
    f32 a;
    f32 colorize[4];  // offsets 16, 20, 24, 28
    f32 ro;
    f32 go;
    f32 bo;
};

static_assert(offsetof(Color, r) == kColorOffsetR, "Color::r must match the recovered offset");
static_assert(offsetof(Color, g) == kColorOffsetG, "Color::g must match the recovered offset");
static_assert(offsetof(Color, b) == kColorOffsetB, "Color::b must match the recovered offset");
static_assert(offsetof(Color, a) == kColorOffsetA, "Color::a must match the recovered offset");
static_assert(offsetof(Color, colorize) == kColorOffsetColorize,
              "Color::colorize must match the recovered offset");
static_assert(offsetof(Color, ro) == kColorOffsetRO, "Color::ro must match the recovered offset");
static_assert(offsetof(Color, go) == kColorOffsetGO, "Color::go must match the recovered offset");
static_assert(offsetof(Color, bo) == kColorOffsetBO, "Color::bo must match the recovered offset");
static_assert(sizeof(Color) == kColorObjectBytes, "Color must be exactly the game's 44 bytes");

// The float at `offset` of the colour.
[[nodiscard]] f32 color_field(const Color& color, std::size_t offset);

// Writes `value` at `offset` of the colour.
void color_set_field(Color& color, std::size_t offset, f32 value);

// -- methods ---------------------------------------------------------------

// Color:Reset (0x00682ed0): RGBA becomes opaque white and everything else is
// zeroed.
void color_reset(Color& color) noexcept;

// Color:SetTint (0x00682f20): the four values replace RGBA.
void color_set_tint(Color& color, f32 r, f32 g, f32 b, f32 a) noexcept;

// Color:SetColorize (0x00682fb0): the four values replace the colorize
// quadruple at offsets 16..28.
void color_set_colorize(Color& color, f32 r, f32 g, f32 b, f32 a) noexcept;

// Color:SetOffset (0x00682fe0): the three values replace RO, GO and BO.
void color_set_offset(Color& color, f32 r, f32 g, f32 b) noexcept;

// Color:Lerp (0x006832e0), a method the public API documentation does not
// list. The native code is `out = t * goal + (1 - t) * color` for all eleven
// floats. The Repentogon build used as the oracle predates this method, so it
// is established by the reference binary's disassembly only.
[[nodiscard]] Color color_lerp(const Color& color, const Color& goal, f32 t) noexcept;

// The Color __mul metamethod (0x006831c0), which mutates its receiver:
//
//   * RGBA is multiplied component-wise.
//   * If the other colour's colorize sum is non-zero, the receiver's colorize
//     is copied from it when the receiver's sum is zero, and otherwise the two
//     are averaged component-wise.
//   * RO, GO and BO are added.
//
// The oracle confirmed the Lua-visible result, including that the left operand
// is mutated and returned by reference.
void color_multiply(Color& color, const Color& other) noexcept;

}  // namespace isaec::api
