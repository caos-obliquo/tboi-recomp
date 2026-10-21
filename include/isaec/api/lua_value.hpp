#pragma once

#include <cstddef>

namespace isaec::api {

// Support for Lua value classes.
//
// A value class is a userdata type with a metatable. Its members do not live
// in the metatable: the game keeps the property accessors in the class
// table's __propget table and the methods in its __index table, and one
// __index function on the instance's metatable decides which of the two a
// name resolves to. The recomp keeps that shape, so the generated spec can
// drive every class the same way.
//
// The class table has to exist in the registry under the class name before an
// instance is created: apply the generated spec first, then install the value
// class, then create instances.
class ValueClass {
 public:
    // Creates the metatable of `class_name` (if it does not exist yet) and
    // installs the read and write dispatch on it. The metatable is kept in a
    // registry table, so one metatable per interpreter state and per class.
    static void install(void* state, const char* class_name);

    // Pushes the metatable of `class_name` on the Lua stack. The caller pops
    // it. A class that adds metamethods needs it after install().
    static void push_metatable(void* state, const char* class_name);

    // Checks that the value at `index` is an instance of `class_name` and
    // returns the pointer to its payload. A value of another type raises a
    // Lua error, the way the original refuses a wrong receiver.
    static void* check(void* state, int index, const char* class_name);

    // Pushes a new instance of `class_name` with `bytes` bytes of payload and
    // returns the pointer to the payload. The payload is zeroed.
    static void* push(void* state, const char* class_name, std::size_t bytes);
};

}  // namespace isaec::api
