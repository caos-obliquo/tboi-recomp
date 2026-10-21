// The colour value. See include/isaec/api/color.hpp for where the field
// offsets come from.

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

}  // namespace isaec::api
