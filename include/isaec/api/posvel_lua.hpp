// The Lua face of the PosVel class.
//
// What is recovered from the binary
// ---------------------------------
// * The member names: the class "PosVel" with the properties "Position" and
//   "Velocity" (the generated spec already carries them).
// * The property roles: both have a getter (0x008bde40) and a setter
//   (0x008bdee0); the two are told apart by the field offset in the upvalue,
//   0 for the position and 8 for the velocity.
// * The shape of the call: the getter receives the instance and returns a
//   *new* Vector userdata built from the eight bytes at the offset; the
//   setter receives the instance and a Vector and copies the eight bytes of
//   the argument into the field, returning no value.
//
// What is not recovered
// ---------------------
// Nothing else: this class only moves two vectors in and out of a struct.
// The original reads the receiver through the game's patched pseudo index
// 0xfff0b9d7 (see issue #12); here the receiver is the first argument, the
// way a stock Lua port does it.

#ifndef ISAEC_API_POSVEL_LUA_HPP
#define ISAEC_API_POSVEL_LUA_HPP

#include "isaec/api/binding.hpp"
#include "isaec/api/posvel.hpp"

namespace isaec::api {

// Installs the PosVel class: the read and write dispatch on the metatable
// and the two properties, which replace the stubs the generated spec
// installed. Call it after apply(spec, binding) and before any instance is
// created.
void install_posvel_class(Binding& binding);

// Pushes a PosVel instance on the Lua stack of `state`.
void push_posvel(void* state, const PosVel& value);

}  // namespace isaec::api

#endif  // ISAEC_API_POSVEL_LUA_HPP
