// The classes whose Lua members are plain fields of a flat game object.
//
// Where this comes from
// --------------------
// The recovered surface shows that most properties of the game classes are
// not computed: they read a field of the game object at a fixed offset. The
// offset is the property's upvalue (a small constant) and the type is the
// type the getter pushes (an int, a float or a bool). The tool
// tools/probe_field_layout.py measures both, and this header turns the
// measurement into code.
//
// The original installs one closure per property, and the closure reads the
// field through the offset in its upvalue. The port installs one getter and
// one setter per class instead: the dispatch passes the property name, and
// the getter looks the field up in the table below. The difference is
// internal to the port -- the Lua visible behaviour is the same -- but it
// means the port's property getter takes the name as a second argument. See
// docs/binding-surface-findings.md for the layout that was recovered.
//
// A field of type Object is *not* implemented here: such a field is a
// pointer into the game's own heap, and the port has no game objects. Those
// properties keep the loud stub of the generated spec.
//
// Nothing in this header includes Lua: the tables and the accessors are
// usable (and testable) without an interpreter.

#ifndef ISAEC_API_FIELD_CLASS_HPP
#define ISAEC_API_FIELD_CLASS_HPP

#include <cstddef>

#include "isaec/platform/types.hpp"

namespace isaec::api {

class Binding;

// The type of a plain field.
enum class FieldType {
    Int,    // a 32 bit signed integer
    Int64,  // a 64 bit signed integer
    Int16,  // a 16 bit signed integer
    UInt8,  // a one byte unsigned integer
    Float,  // a 32 bit float
    Bool,   // a byte that is zero or non-zero
};

// One plain field of a game object.
struct FieldSpec {
    const char* name;      // the name the Lua API uses
    usize offset;          // the offset of the field in the object
    FieldType type;        // the type the getter pushes
};

// A class whose members are plain fields of one flat object.
//
// The instance is a userdata whose payload is the object itself: a flat byte
// buffer of `object_size` bytes. The original keeps a pointer to the game
// object in the userdata; the port owns the bytes, so the game side that
// would fill them is still missing.
class FieldClass {
 public:
    // Installs the class: the metatable with the read and write dispatch,
    // and a property getter and setter for every field. The getter and the
    // setter are the same function for every field of every class; the name
    // tells them which field to work on.
    //
    // The class table has to exist before: apply the generated spec first.
    static void install(Binding& binding, const char* class_name, usize object_size,
                        const FieldSpec* fields, usize field_count);

    // Creates an instance with a zeroed payload of `object_size` bytes and
    // returns the pointer to the payload.
    static void* create(void* state, const char* class_name, usize object_size);

    // The payload of an existing instance, or nullptr when the value is not
    // an instance of the class. Used by the port and by the tests.
    static void* payload(void* state, int index, const char* class_name);
};

}  // namespace isaec::api

#endif  // ISAEC_API_FIELD_CLASS_HPP
