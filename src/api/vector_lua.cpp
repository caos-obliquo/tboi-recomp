// The Lua face of the Vector class. See include/isaec/api/vector_lua.hpp for
// what is recovered from the binary and what is not.

#include "isaec/api/vector_lua.hpp"

#include "isaec/api/binding.hpp"
#include "isaec/api/lua_value.hpp"
#include "isaec/api/vector.hpp"

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

// The class name in the registry. It is the same name the generated spec
// uses, so the class table the spec created is the one the dispatch looks in.
constexpr const char* kVectorClassName = "Vector";

#if ISAEC_HAS_LUA

lua_State* as_state(void* state) { return static_cast<lua_State*>(state); }

// -- the metatable ----------------------------------------------------------

// The Vector at `index`, or a Lua error. A wrong argument is an error here
// rather than a silent zero: the original checks the receiver type too
// (lua_type plus a "nil passed to reference" check in the shared helper).
Vector* check_vector(lua_State* L, int index) {
    return static_cast<Vector*>(ValueClass::check(L, index, kVectorClassName));
}

// -- dispatch ----------------------------------------------------------------

// -- methods ----------------------------------------------------------------

int vector_length(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    lua_pushnumber(L, vector_length(*self));
    return 1;
}

int vector_length_squared(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    lua_pushnumber(L, vector_length_squared(*self));
    return 1;
}

int vector_dot(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const Vector* other = check_vector(L, 2);
    lua_pushnumber(L, vector_dot(*self, *other));
    return 1;
}

int vector_cross(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const Vector* other = check_vector(L, 2);
    lua_pushnumber(L, vector_cross(*self, *other));
    return 1;
}

int vector_distance(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const Vector* other = check_vector(L, 2);
    lua_pushnumber(L, vector_distance(*self, *other));
    return 1;
}

int vector_distance_squared(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const Vector* other = check_vector(L, 2);
    lua_pushnumber(L, vector_distance_squared(*self, *other));
    return 1;
}

// Vector:Lerp(other, t). The original also reads a fourth argument, a
// boolean, and passes it to the game's own vector routine; its meaning is not
// known, so this port ignores it.
int vector_lerp(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const Vector* other = check_vector(L, 2);
    const f32 t = static_cast<f32>(luaL_checknumber(L, 3));
    push_vector(L, vector_lerp(*self, *other, t));
    return 1;
}

// Vector:Normalize() normalises the receiver in place and returns nothing.
int vector_normalize(lua_State* L) {
    Vector* self = check_vector(L, 1);
    *self = vector_normalized(*self);
    return 0;
}

// Vector:Normalized() returns a new, normalised vector.
int vector_normalized(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    push_vector(L, vector_normalized(*self));
    return 1;
}

// Vector:Resize(scale) scales the receiver in place and returns nothing.
int vector_resize(lua_State* L) {
    Vector* self = check_vector(L, 1);
    const f32 factor = static_cast<f32>(luaL_checknumber(L, 2));
    *self = vector_resized(*self, factor);
    return 0;
}

// Vector:Resized(scale) returns a new, scaled vector.
int vector_resized(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const f32 factor = static_cast<f32>(luaL_checknumber(L, 2));
    push_vector(L, vector_resized(*self, factor));
    return 1;
}

// Vector:Rotated(degrees) returns a new, rotated vector.
int vector_rotated(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    const f32 degrees = static_cast<f32>(luaL_checknumber(L, 2));
    push_vector(L, vector_rotated(*self, degrees));
    return 1;
}

// Vector:GetAngleDegrees() returns the direction of the vector in degrees.
int vector_get_angle_degrees(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    lua_pushnumber(L, vector_angle_degrees(*self));
    return 1;
}

// -- properties -------------------------------------------------------------

// The X and Y properties. The original reaches the field through a per
// property upvalue (the field offset: 0 for X, 4 for Y) and the same closure
// body serves both; here the two are separate functions.
int vector_get_x(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    lua_pushnumber(L, self->x);
    return 1;
}

int vector_set_x(lua_State* L) {
    Vector* self = check_vector(L, 1);
    self->x = static_cast<f32>(luaL_checknumber(L, 2));
    return 0;
}

int vector_get_y(lua_State* L) {
    const Vector* self = check_vector(L, 1);
    lua_pushnumber(L, self->y);
    return 1;
}

int vector_set_y(lua_State* L) {
    Vector* self = check_vector(L, 1);
    self->y = static_cast<f32>(luaL_checknumber(L, 2));
    return 0;
}

// -- metamethods ------------------------------------------------------------

int vector_meta_add(lua_State* L) {
    const Vector* a = check_vector(L, 1);
    const Vector* b = check_vector(L, 2);
    push_vector(L, vector_add(*a, *b));
    return 1;
}

int vector_meta_sub(lua_State* L) {
    const Vector* a = check_vector(L, 1);
    const Vector* b = check_vector(L, 2);
    push_vector(L, vector_sub(*a, *b));
    return 1;
}

// Vector * number and number * Vector.
int vector_meta_mul(lua_State* L) {
    if (lua_type(L, 1) == LUA_TNUMBER) {
        const f32 factor = static_cast<f32>(lua_tonumber(L, 1));
        const Vector* v = check_vector(L, 2);
        push_vector(L, vector_scale(*v, factor));
        return 1;
    }
    const Vector* v = check_vector(L, 1);
    const f32 factor = static_cast<f32>(luaL_checknumber(L, 2));
    push_vector(L, vector_scale(*v, factor));
    return 1;
}

// Vector / number.
int vector_meta_div(lua_State* L) {
    const Vector* v = check_vector(L, 1);
    const f32 divisor = static_cast<f32>(luaL_checknumber(L, 2));
    push_vector(L, vector_divide(*v, divisor));
    return 1;
}

// -Vector.
int vector_meta_unm(lua_State* L) {
    const Vector* v = check_vector(L, 1);
    push_vector(L, vector_negate(*v));
    return 1;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void install_vector_class(Binding& binding) {
#if ISAEC_HAS_LUA
    if (!binding.attached()) return;
    auto* L = as_state(binding.state());

    // The metatable with the read and the write dispatch is shared with the
    // other value classes.
    ValueClass::install(L, kVectorClassName);

    // The arithmetic metamethods live on the metatable of the instances.
    ValueClass::push_metatable(L, kVectorClassName);
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, vector_meta_add);
        lua_setfield(L, -2, "__add");
        lua_pushcfunction(L, vector_meta_sub);
        lua_setfield(L, -2, "__sub");
        lua_pushcfunction(L, vector_meta_mul);
        lua_setfield(L, -2, "__mul");
        lua_pushcfunction(L, vector_meta_div);
        lua_setfield(L, -2, "__div");
        lua_pushcfunction(L, vector_meta_unm);
        lua_setfield(L, -2, "__unm");
    }
    lua_pop(L, 1);

    // The methods. register_method replaces the stub the generated spec
    // installed, in the binding core and in the class table's __index table.
    struct Method {
        const char* name;
        lua_CFunction fn;
    };
    static const Method kMethods[] = {
        {"Cross", vector_cross},
        {"Distance", vector_distance},
        {"DistanceSquared", vector_distance_squared},
        {"Dot", vector_dot},
        {"GetAngleDegrees", vector_get_angle_degrees},
        {"Length", vector_length},
        {"LengthSquared", vector_length_squared},
        {"Lerp", vector_lerp},
        {"Normalize", vector_normalize},
        {"Normalized", vector_normalized},
        {"Resize", vector_resize},
        {"Resized", vector_resized},
        {"Rotated", vector_rotated},
    };
    for (const auto& method : kMethods) {
        binding.register_method(kVectorClassName, method.name,
                                 reinterpret_cast<NativeFunction>(method.fn));
    }

    // The X and Y properties. The original installs a getter under __propget
    // and a setter under __propset, with the same closure body for both
    // coordinates and the field offset in an upvalue.
    binding.register_property(kVectorClassName, "X", reinterpret_cast<NativeFunction>(vector_get_x),
                               reinterpret_cast<NativeFunction>(vector_set_x));
    binding.register_property(kVectorClassName, "Y", reinterpret_cast<NativeFunction>(vector_get_y),
                               reinterpret_cast<NativeFunction>(vector_set_y));
#else
    (void)binding;
#endif
}

void push_vector(void* state, const Vector& value) {
#if ISAEC_HAS_LUA
    auto* payload =
        static_cast<Vector*>(ValueClass::push(state, kVectorClassName, kVectorUserdataBytes));
    if (payload != nullptr) {
        *payload = value;
    }
#else
    (void)state;
    (void)value;
#endif
}

}  // namespace isaec::api
