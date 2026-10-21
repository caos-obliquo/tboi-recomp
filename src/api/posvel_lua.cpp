// The Lua face of the PosVel class. See include/isaec/api/posvel_lua.hpp for
// what comes from the binary and what does not.

#include "isaec/api/posvel_lua.hpp"

#include "isaec/api/lua_value.hpp"
#include "isaec/api/vector_lua.hpp"

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

// The class name in the registry. It is the name the generated spec uses, so
// the class table the spec created is the one the dispatch looks in.
constexpr const char* kPosVelClassName = "PosVel";

#if ISAEC_HAS_LUA

lua_State* as_state(void* state) { return static_cast<lua_State*>(state); }

// The PosVel at `index`, or a Lua error. The original checks the receiver
// type before it reads the field, and so does this.
PosVel* check_posvel(lua_State* L, int index) {
    return static_cast<PosVel*>(ValueClass::check(L, index, kPosVelClassName));
}

// The Vector at `index`, or a Lua error.
const Vector* check_vector_arg(lua_State* L, int index) {
    return static_cast<const Vector*>(ValueClass::check(L, index, "Vector"));
}

// The two property getters. The original uses one closure body for both
// fields and reads the offset from an upvalue; here the two are separate
// functions and the offset is a compile time constant.
int posvel_get_position(lua_State* L) {
    const PosVel* self = check_posvel(L, 1);
    push_vector(L, self->position);
    return 1;
}

int posvel_get_velocity(lua_State* L) {
    const PosVel* self = check_posvel(L, 1);
    push_vector(L, self->velocity);
    return 1;
}

// The two property setters. The original copies the eight bytes of the
// argument (the payload of the Vector userdata) into the field.
int posvel_set_position(lua_State* L) {
    PosVel* self = check_posvel(L, 1);
    const Vector* value = check_vector_arg(L, 2);
    self->position = *value;
    return 0;
}

int posvel_set_velocity(lua_State* L) {
    PosVel* self = check_posvel(L, 1);
    const Vector* value = check_vector_arg(L, 2);
    self->velocity = *value;
    return 0;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void install_posvel_class(Binding& binding) {
#if ISAEC_HAS_LUA
    if (!binding.attached()) return;
    auto* L = as_state(binding.state());

    // The metatable with the read and the write dispatch is shared with the
    // other value classes.
    ValueClass::install(L, kPosVelClassName);

    // The two properties. register_property replaces the stub the generated
    // spec installed, in the binding core and in the class table's __propget
    // and __propset tables.
    binding.register_property(kPosVelClassName, "Position",
                               reinterpret_cast<NativeFunction>(posvel_get_position),
                               reinterpret_cast<NativeFunction>(posvel_set_position));
    binding.register_property(kPosVelClassName, "Velocity",
                               reinterpret_cast<NativeFunction>(posvel_get_velocity),
                               reinterpret_cast<NativeFunction>(posvel_set_velocity));
#else
    (void)binding;
#endif
}

void push_posvel(void* state, const PosVel& value) {
#if ISAEC_HAS_LUA
    auto* payload =
        static_cast<PosVel*>(ValueClass::push(state, kPosVelClassName, kPosVelUserdataBytes));
    if (payload != nullptr) {
        *payload = value;
    }
#else
    (void)state;
    (void)value;
#endif
}

}  // namespace isaec::api
