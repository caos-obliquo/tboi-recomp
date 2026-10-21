// The Lua face of the Vector class.
//
// The shipped binary registers Vector through the game's own registration
// routine: it allocates a userdata, installs a metatable and fills the class
// table's __index / __propget / __propset tables. This file does the same
// thing for the Vector class only, and it is the first class in the project
// whose members are real implementations rather than
// not_implemented_trampoline() stubs.
//
// What is recovered and what is not
// ---------------------------------
// Recovered from the binary (docs/binding-surface-findings.md, sections 15
// to 17):
//   * the member names of the class (15 methods, the X and Y properties and
//     the arithmetic metamethods);
//   * the ABI: the receiver is the first argument, the visible arguments
//     start at index 2, a method returns zero or one value, and a method that
//     returns a vector allocates a fresh userdata;
//   * the userdata is 16 bytes and the payload is two floats at offset 0.
//
// NOT recovered:
//   * the arithmetic itself, which in the original lives in the game's C++
//     Vector behind a vtable pointer. The maths in vector.hpp is the
//     documented Isaac API reimplemented from scratch.
//   * the exact text of Vector.__tostring, and the meaning of the fourth
//     argument the original's Vector:Lerp reads. The recomp ignores that
//     argument and does not implement __tostring.
//   * Vector:Clamp and Vector:Clamped: the original's closure reads four
//     numbers from the Lua stack, while the public API documents two. The
//     two members stay loud stubs until the discrepancy is settled.
//
// The original reaches the receiver through a patched pseudo-index
// (0xfff0b9d7) rather than index 1; this port uses the standard index 1.
// See issue #12 for the patched-Lua slot semantics.

#ifndef ISAEC_API_VECTOR_LUA_HPP
#define ISAEC_API_VECTOR_LUA_HPP

#include "isaec/api/binding.hpp"
#include "isaec/api/vector.hpp"

namespace isaec::api {

// Installs the Vector class into `binding`.
//
// Call this *after* the generated spec has been applied: it replaces the
// "not implemented" stubs of the members it implements and leaves every other
// member of the class untouched, so the unimplemented part of the surface
// still refuses loudly.
//
// The interpreter must be attached to the binding; without one this call does
// nothing and the class stays a set of stubs.
void install_vector_class(Binding& binding);

// Pushes a new Vector instance onto the given interpreter stack.
//
// `state` is the same opaque handle Binding uses. The instance carries the
// Vector metatable, so it dispatches through the same __index / __newindex
// path as an instance the game creates.
//
// The original game exposes no Vector constructor (no "Vector" appears among
// the recovered global names), so this port does not add one either: the
// vectors in a running game come from the game, not from Lua. This helper
// exists for the engine and for the tests.
void push_vector(void* state, const Vector& value);

}  // namespace isaec::api

#endif  // ISAEC_API_VECTOR_LUA_HPP
