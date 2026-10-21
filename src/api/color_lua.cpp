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

#endif  // ISAEC_HAS_LUA

}  // namespace

void install_color_class(Binding& binding) {
#if ISAEC_HAS_LUA
    // The metatable with the read and the write dispatch. The class table
    // has to exist first: apply the generated spec before this call.
    ValueClass::install(binding.state(), kColorClassName);

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
