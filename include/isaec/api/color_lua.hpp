#pragma once

#include "isaec/api/binding.hpp"
#include "isaec/api/color.hpp"

namespace isaec::api {

// The Lua face of the Color class.
//
// Recovered from the binary:
//   * the class has seven colour properties -- R, G, B, A and the three
//     overlay channels RO, GO, BO;
//   * every one of them shares one getter body (0x0089f590) and one setter
//     body (0x0089f500), told apart by the upvalue, and the getter's
//     upvalue is a per-field accessor that reads one float at a fixed
//     offset of the object (see color.hpp);
//   * the getter returns one value, the setter returns none;
//   * Reset, SetColorize, SetTint, SetOffset, Lerp and __mul are implemented
//     from their native routines (see color.hpp); Reset, SetTint,
//     SetColorize, SetOffset and Lerp return nothing, and __mul mutates and
//     returns its left operand.
void install_color_class(Binding& binding);

// Pushes a new Color instance on the Lua stack.
void push_color(void* state, const Color& color);

}  // namespace isaec::api
