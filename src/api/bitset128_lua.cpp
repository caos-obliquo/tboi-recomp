// The Lua face of the BitSet128 class. See include/isaec/api/bitset128.hpp
// for what is recovered from the binary.

#include "isaec/api/bitset128_lua.hpp"

#include "isaec/api/lua_value.hpp"

#include <string>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

constexpr const char* kBitSet128ClassName = "BitSet128";

#if ISAEC_HAS_LUA

lua_State* as_state(void* state) { return static_cast<lua_State*>(state); }

// The BitSet128 at `index`, or a Lua error. The original checks the receiver
// through the shared userdata helper, which rejects a foreign userdata.
BitSet128* check_bitset(lua_State* L, int index) {
    return static_cast<BitSet128*>(ValueClass::check(L, index, kBitSet128ClassName));
}

// BitSet128(low, high): native 0x008bc330. Both integers are required and
// each supplies one 64 bit half, low first.
int bitset_constructor(lua_State* L) {
    const auto low = static_cast<u64>(static_cast<i64>(luaL_checkinteger(L, 2)));
    const auto high = static_cast<u64>(static_cast<i64>(luaL_checkinteger(L, 3)));

    BitSet128 set;
    set.words[0] = static_cast<u32>(low);
    set.words[1] = static_cast<u32>(low >> 32);
    set.words[2] = static_cast<u32>(high);
    set.words[3] = static_cast<u32>(high >> 32);
    push_bitset128(L, set);
    return 1;
}

// Get(index) -> bool. 0x0085e560.
int bitset_get_lua(lua_State* L) {
    const BitSet128* set = check_bitset(L, 1);
    const i32 index = static_cast<i32>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, bitset128_get(*set, index) ? 1 : 0);
    return 1;
}

// Set(index, value). 0x0085e5c0: the value is taken with its Lua truthiness,
// so a missing value clears the bit rather than raising.
int bitset_set_lua(lua_State* L) {
    BitSet128* set = check_bitset(L, 1);
    const i32 index = static_cast<i32>(luaL_checkinteger(L, 2));
    bitset128_set(*set, index, lua_toboolean(L, 3) != 0);
    return 0;
}

// __band / __bor / __bxor. 0x0085e4d0 / 0x0085e4f0 / 0x0085e510.
//
// The shared closure 0x008aa300 fetches the second operand through
// 0x008bac00, which accepts only a BitSet128 and raises otherwise, so a number
// is an error here just as it is in the original.
int bitset_meta_band(lua_State* L) {
    const BitSet128* a = check_bitset(L, 1);
    const BitSet128* b = check_bitset(L, 2);
    push_bitset128(L, bitset128_bit_and(*a, *b));
    return 1;
}

int bitset_meta_bor(lua_State* L) {
    const BitSet128* a = check_bitset(L, 1);
    const BitSet128* b = check_bitset(L, 2);
    push_bitset128(L, bitset128_bit_or(*a, *b));
    return 1;
}

int bitset_meta_bxor(lua_State* L) {
    const BitSet128* a = check_bitset(L, 1);
    const BitSet128* b = check_bitset(L, 2);
    push_bitset128(L, bitset128_bit_xor(*a, *b));
    return 1;
}

// __bnot. 0x00592200: a new value, receiver untouched.
int bitset_meta_bnot(lua_State* L) {
    const BitSet128* set = check_bitset(L, 1);
    push_bitset128(L, bitset128_bit_not(*set));
    return 1;
}

// __eq. 0x0085e530 compares all four words.
int bitset_meta_eq(lua_State* L) {
    const BitSet128* a = check_bitset(L, 1);
    const BitSet128* b = check_bitset(L, 2);
    lua_pushboolean(L, *a == *b ? 1 : 0);
    return 1;
}

// __tostring. 0x0085e490: the native's format is
// "BitSet128: %016llx%016llx" with the high half first.
int bitset_meta_tostring(lua_State* L) {
    const BitSet128* set = check_bitset(L, 1);
    const std::string text = bitset128_to_string(*set);
    lua_pushlstring(L, text.c_str(), text.size());
    return 1;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void install_bitset128_class(Binding& binding) {
#if ISAEC_HAS_LUA
    if (!binding.attached()) return;
    auto* L = as_state(binding.state());

    ValueClass::install(L, kBitSet128ClassName);

    // The instance metatable carries the metamethods. The `l` and `h`
    // properties come from the generated field class, which has to have been
    // installed already.
    ValueClass::push_metatable(L, kBitSet128ClassName);
    struct Metamethod {
        const char* name;
        lua_CFunction fn;
    };
    static const Metamethod kMetamethods[] = {
        {"__band", bitset_meta_band},   {"__bor", bitset_meta_bor},
        {"__bxor", bitset_meta_bxor},   {"__bnot", bitset_meta_bnot},
        {"__eq", bitset_meta_eq},       {"__tostring", bitset_meta_tostring},
    };
    for (const auto& method : kMetamethods) {
        lua_pushcfunction(L, method.fn);
        lua_setfield(L, -2, method.name);
    }
    lua_pop(L, 1);

    struct Method {
        const char* name;
        lua_CFunction fn;
    };
    static const Method kMethods[] = {
        {"Get", bitset_get_lua},
        {"Set", bitset_set_lua},
    };
    for (const auto& method : kMethods) {
        binding.register_method(kBitSet128ClassName, method.name,
                                reinterpret_cast<NativeFunction>(method.fn));
    }

    // The class table is callable in the original: `BitSet128(low, high)`
    // enters the `__call` metamethod, whose native is 0x008bc330. Put that
    // metamethod on the class table and expose the table as the global.
    lua_getfield(L, LUA_REGISTRYINDEX, kBitSet128ClassName);
    if (lua_istable(L, -1)) {
        lua_newtable(L);
        lua_pushcfunction(L, bitset_constructor);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);

        lua_pushvalue(L, -1);
        lua_setglobal(L, kBitSet128ClassName);
    }
    lua_pop(L, 1);
#else
    (void)binding;
#endif
}

void push_bitset128(void* state, const BitSet128& set) {
#if ISAEC_HAS_LUA
    auto* payload = static_cast<BitSet128*>(
        ValueClass::push(state, kBitSet128ClassName, kBitSet128Bytes));
    if (payload != nullptr) *payload = set;
#else
    (void)state;
    (void)set;
#endif
}

}  // namespace isaec::api
