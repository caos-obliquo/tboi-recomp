// The Lua face of the RNG class. See include/isaec/api/rng_lua.hpp for what is
// recovered from the binary and rng.hpp for the native addresses.

#include "isaec/api/rng_lua.hpp"

#include "isaec/api/lua_value.hpp"

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

constexpr const char* kRngClassName = "RNG";

#if ISAEC_HAS_LUA

lua_State* as_state(void* state) { return static_cast<lua_State*>(state); }

Rng* check_rng(lua_State* L, int index) {
    return static_cast<Rng*>(ValueClass::check(L, index, kRngClassName));
}

// The class table's `__call` constructor. The native constructor (0x8bbcf0)
// allocates the payload, installs the default state, and never reads an
// argument.
int rng_constructor(lua_State* L) {
    push_rng(L, rng_default());
    return 1;
}

int rng_get_seed_lua(lua_State* L) {
    const Rng* rng = check_rng(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(rng_get_seed(*rng)));
    return 1;
}

// RNG:SetSeed(seed, shiftIndex). The native routine clamps the index to the
// last row of the shift table and returns nothing.
int rng_set_seed_lua(lua_State* L) {
    Rng* rng = check_rng(L, 1);
    const auto seed = static_cast<u32>(luaL_checkinteger(L, 2));
    const auto shift_index = static_cast<u32>(luaL_checkinteger(L, 3));
    rng_set_seed(*rng, seed, shift_index);
    return 0;
}

int rng_random_int_lua(lua_State* L) {
    Rng* rng = check_rng(L, 1);
    const auto bound = static_cast<u32>(luaL_checkinteger(L, 2));
    lua_pushinteger(L, static_cast<lua_Integer>(rng_random_int(*rng, bound)));
    return 1;
}

int rng_random_float_lua(lua_State* L) {
    Rng* rng = check_rng(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(rng_random_float(*rng)));
    return 1;
}

// RNG:Next() advances once and returns the new seed as an integer.
int rng_next_lua(lua_State* L) {
    Rng* rng = check_rng(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(rng_next(*rng)));
    return 1;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void install_rng_class(Binding& binding) {
#if ISAEC_HAS_LUA
    if (!binding.attached()) return;
    auto* L = as_state(binding.state());

    ValueClass::install(L, kRngClassName);

    struct Method {
        const char* name;
        lua_CFunction fn;
    };
    static const Method kMethods[] = {
        {"GetSeed", rng_get_seed_lua},
        {"Next", rng_next_lua},
        {"RandomFloat", rng_random_float_lua},
        {"RandomInt", rng_random_int_lua},
        {"SetSeed", rng_set_seed_lua},
    };
    for (const auto& method : kMethods) {
        binding.register_method(kRngClassName, method.name,
                                reinterpret_cast<NativeFunction>(method.fn));
    }

    // The class table itself is callable in the original: `RNG(seed)` enters
    // the `__call` metamethod, which ignores the arguments. Put that
    // metamethod on the class table and expose the table as the global `RNG`.
    lua_getfield(L, LUA_REGISTRYINDEX, kRngClassName);
    if (lua_istable(L, -1)) {
        lua_newtable(L);
        lua_pushcfunction(L, rng_constructor);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);

        lua_pushvalue(L, -1);
        lua_setglobal(L, kRngClassName);
    }
    lua_pop(L, 1);
#else
    (void)binding;
#endif
}

void push_rng(void* state, const Rng& rng) {
#if ISAEC_HAS_LUA
    auto* payload = static_cast<Rng*>(ValueClass::push(state, kRngClassName, kRngUserdataBytes));
    if (payload != nullptr) {
        *payload = rng;
    }
#else
    (void)state;
    (void)rng;
#endif
}

}  // namespace isaec::api
