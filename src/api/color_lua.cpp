// The Lua face of the Color class. See include/isaec/api/color_lua.hpp for
// what is recovered from the binary and what is not.

#include "isaec/api/color_lua.hpp"

#include "isaec/api/binding.hpp"
#include "isaec/api/color.hpp"
#include "isaec/api/lua_value.hpp"

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
constexpr const char* kColorClassName = "Color";

#if ISAEC_HAS_LUA

// The getter of the field at `Offset`.
//
// The original's getter checks the receiver, reads the field through the
// accessor in its upvalue and pushes the float. The recomp does the same
// with the offset it recovered.
template <std::size_t Offset>
int color_get(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    lua_pushnumber(L, static_cast<lua_Number>(color_field(*color, Offset)));
    return 1;
}

// The setter of the field at `Offset`.
//
// The original's setter checks the receiver, reads the value with
// luaL_checknumber and stores it through the accessor. It returns nothing.
template <std::size_t Offset>
int color_set(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    const auto value = static_cast<f32>(luaL_checknumber(L, 2));
    color_set_field(*color, Offset, value);
    return 0;
}

// The getter and the setter of one field, chosen by the offset.
struct FieldAccessors {
    NativeFunction getter;
    NativeFunction setter;
};

FieldAccessors accessors_for(std::size_t offset) {
    switch (offset) {
        case kColorOffsetR:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetR>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetR>)};
        case kColorOffsetG:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetG>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetG>)};
        case kColorOffsetB:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetB>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetB>)};
        case kColorOffsetA:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetA>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetA>)};
        case kColorOffsetRO:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetRO>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetRO>)};
        case kColorOffsetGO:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetGO>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetGO>)};
        case kColorOffsetBO:
            return {reinterpret_cast<NativeFunction>(&color_get<kColorOffsetBO>),
                    reinterpret_cast<NativeFunction>(&color_set<kColorOffsetBO>)};
        default:
            return {nullptr, nullptr};
    }
}

// -- methods ---------------------------------------------------------------

int color_reset_lua(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    color_reset(*color);
    return 0;
}

int color_set_tint_lua(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    const f32 r = static_cast<f32>(luaL_checknumber(L, 2));
    const f32 g = static_cast<f32>(luaL_checknumber(L, 3));
    const f32 b = static_cast<f32>(luaL_checknumber(L, 4));
    const f32 a = static_cast<f32>(luaL_checknumber(L, 5));
    color_set_tint(*color, r, g, b, a);
    return 0;
}

int color_set_colorize_lua(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    const f32 r = static_cast<f32>(luaL_checknumber(L, 2));
    const f32 g = static_cast<f32>(luaL_checknumber(L, 3));
    const f32 b = static_cast<f32>(luaL_checknumber(L, 4));
    const f32 a = static_cast<f32>(luaL_checknumber(L, 5));
    color_set_colorize(*color, r, g, b, a);
    return 0;
}

int color_set_offset_lua(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    const f32 r = static_cast<f32>(luaL_checknumber(L, 2));
    const f32 g = static_cast<f32>(luaL_checknumber(L, 3));
    const f32 b = static_cast<f32>(luaL_checknumber(L, 4));
    color_set_offset(*color, r, g, b);
    return 0;
}

// Color:Lerp(goal, t) returns a new colour; the oracle host predates the
// method, so this follows the reference binary's native routine.
int color_lerp_lua(lua_State* L) {
    const auto* color = static_cast<const Color*>(ValueClass::check(L, 1, kColorClassName));
    const auto* goal = static_cast<const Color*>(ValueClass::check(L, 2, kColorClassName));
    const f32 t = static_cast<f32>(luaL_checknumber(L, 3));
    push_color(L, color_lerp(*color, *goal, t));
    return 1;
}

// -- metamethods ------------------------------------------------------------

// Color * Color mutates and returns the left operand, which the oracle
// confirmed by watching the left colour change and the returned value alias
// it.
int color_meta_mul(lua_State* L) {
    auto* color = static_cast<Color*>(ValueClass::check(L, 1, kColorClassName));
    const auto* other = static_cast<const Color*>(ValueClass::check(L, 2, kColorClassName));
    color_multiply(*color, *other);
    lua_pushvalue(L, 1);
    return 1;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void install_color_class(Binding& binding) {
#if ISAEC_HAS_LUA
    if (!binding.attached()) return;
    auto* L = static_cast<lua_State*>(binding.state());

    // The metatable with the read and the write dispatch. The class table
    // has to exist first: apply the generated spec before this call.
    ValueClass::install(L, kColorClassName);

    // The multiplication metamethod mutates the left operand.
    ValueClass::push_metatable(L, kColorClassName);
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, color_meta_mul);
        lua_setfield(L, -2, "__mul");
    }
    lua_pop(L, 1);

    struct Method {
        const char* name;
        lua_CFunction fn;
    };
    static const Method kMethods[] = {
        {"Lerp", color_lerp_lua},          {"Reset", color_reset_lua},
        {"SetColorize", color_set_colorize_lua}, {"SetOffset", color_set_offset_lua},
        {"SetTint", color_set_tint_lua},
    };
    for (const auto& method : kMethods) {
        binding.register_method(kColorClassName, method.name,
                                reinterpret_cast<NativeFunction>(method.fn));
    }

    // The original exposes the class table as the global `Color`.
    lua_getfield(L, LUA_REGISTRYINDEX, kColorClassName);
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, kColorClassName);
    }
    lua_pop(L, 1);

    struct Property {
        const char* name;
        std::size_t offset;
    };
    static const Property kProperties[] = {
        {"R", kColorOffsetR},   {"G", kColorOffsetG},   {"B", kColorOffsetB},
        {"A", kColorOffsetA},   {"RO", kColorOffsetRO}, {"GO", kColorOffsetGO},
        {"BO", kColorOffsetBO},
    };

    for (const auto& property : kProperties) {
        const FieldAccessors accessors = accessors_for(property.offset);
        if (accessors.getter == nullptr) continue;
        // register_property replaces the stub of the generated spec.
        binding.register_property(kColorClassName, property.name, accessors.getter,
                                  accessors.setter);
    }
#else
    (void)binding;
#endif
}

void push_color(void* state, const Color& color) {
#if ISAEC_HAS_LUA
    auto* payload = static_cast<Color*>(
        ValueClass::push(state, kColorClassName, sizeof(Color)));
    if (payload != nullptr) {
        *payload = color;
    }
#else
    (void)state;
    (void)color;
#endif
}

}  // namespace isaec::api
