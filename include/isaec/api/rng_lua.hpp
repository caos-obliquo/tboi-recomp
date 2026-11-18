#pragma once

#include "isaec/api/binding.hpp"
#include "isaec/api/rng.hpp"

namespace isaec::api {

// The Lua face of the RNG class.
//
// Recovered from the binary:
//   * the class table is callable; its `__call` constructor allocates a
//     16-byte payload and installs seed 0xaa17414f with shift row 35, and it
//     ignores every argument;
//   * the methods are GetSeed, SetSeed, RandomInt, RandomFloat and Next,
//     bound to the native routines named in rng.hpp;
//   * GetSeed and Next push the new seed as a Lua integer, RandomInt pushes
//     an integer, RandomFloat pushes a float, and SetSeed returns nothing.
void install_rng_class(Binding& binding);

// Pushes a new RNG instance on the Lua stack.
void push_rng(void* state, const Rng& rng);

}  // namespace isaec::api
