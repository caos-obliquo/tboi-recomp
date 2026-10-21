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
// Not recovered: what the three overlay channels (RO, GO, BO) mean, and the
// sixteen bytes at offsets 16..31 that no recovered accessor reads. The
// names come from the binary; the meaning of the fields does not.
constexpr std::size_t kColorOffsetR = 0;
constexpr std::size_t kColorOffsetG = 4;
constexpr std::size_t kColorOffsetB = 8;
constexpr std::size_t kColorOffsetA = 12;
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
    f32 reserved[4];  // offsets 16..31, read by no recovered accessor
    f32 ro;
    f32 go;
    f32 bo;
};

// The float at `offset` of the colour.
[[nodiscard]] f32 color_field(const Color& color, std::size_t offset);

// Writes `value` at `offset` of the colour.
void color_set_field(Color& color, std::size_t offset, f32 value);

}  // namespace isaec::api
