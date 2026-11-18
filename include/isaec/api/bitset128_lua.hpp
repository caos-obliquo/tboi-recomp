// The Lua face of the BitSet128 class. See include/isaec/api/bitset128.hpp
// for the recovered native semantics.

#ifndef ISAEC_API_BITSET128_LUA_HPP
#define ISAEC_API_BITSET128_LUA_HPP

#include "isaec/api/binding.hpp"
#include "isaec/api/bitset128.hpp"

namespace isaec::api {

// Installs the BitSet128 members on top of the generated spec.
//
// Call this after the generated spec has been applied and after the field
// classes have been installed: the field class already provides the `l` and
// `h` properties and the instance metatable, and this call adds Get, Set and
// the six metamethods to them. The class table from the spec must exist, so
// apply() has to have run first.
void install_bitset128_class(Binding& binding);

// Pushes a new BitSet128 instance onto the given interpreter stack.
void push_bitset128(void* state, const BitSet128& set);

}  // namespace isaec::api

#endif  // ISAEC_API_BITSET128_LUA_HPP
