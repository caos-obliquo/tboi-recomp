// The BitSet128 class: the 128-bit payload, Get/Set and the six metamethods
// translated from the shipped binary.
//
// The boundary rules are the measured ones: Get is false for a negative index
// and for 128 or more, Set is a no-op for both, and the bitwise metamethods
// return a new value without touching the receiver.

#include "isaec/api/binding.hpp"
#include "isaec/api/bitset128.hpp"
#include "isaec/api/bitset128_lua.hpp"
#include "isaec/api/field_class.hpp"
#include "isaec/api/generated/field_classes.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"

#include "test_harness.hpp"

#include <cstdio>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace {

using isaec::api::BitSet128;
using isaec::i32;
using isaec::u32;

// -- the value ---------------------------------------------------------------

ISAEC_TEST(bitset128_get_reads_the_four_words) {
    BitSet128 set;
    set.words[0] = 0x00000001u;
    set.words[1] = 0x00000002u;
    set.words[2] = 0x00000004u;
    set.words[3] = 0x00000008u;

    CHECK(isaec::api::bitset128_get(set, 0));
    CHECK(isaec::api::bitset128_get(set, 33));
    CHECK(isaec::api::bitset128_get(set, 66));
    CHECK(isaec::api::bitset128_get(set, 99));
    CHECK(!isaec::api::bitset128_get(set, 1));
    CHECK(!isaec::api::bitset128_get(set, 32));
}

ISAEC_TEST(bitset128_get_covers_every_boundary) {
    BitSet128 set;
    for (const i32 bit : {0, 31, 32, 63, 64, 95, 96, 127}) {
        isaec::api::bitset128_set(set, bit, true);
    }
    for (const i32 bit : {0, 31, 32, 63, 64, 95, 96, 127}) {
        CHECK(isaec::api::bitset128_get(set, bit));
    }
    CHECK(!isaec::api::bitset128_get(set, 1));
    CHECK(!isaec::api::bitset128_get(set, 30));
    CHECK(!isaec::api::bitset128_get(set, 33));
    CHECK(!isaec::api::bitset128_get(set, 65));
}

ISAEC_TEST(bitset128_get_and_set_ignore_out_of_range) {
    BitSet128 set;
    CHECK(!isaec::api::bitset128_get(set, -1));
    CHECK(!isaec::api::bitset128_get(set, 128));
    CHECK(!isaec::api::bitset128_get(set, 1000));

    isaec::api::bitset128_set(set, -1, true);
    isaec::api::bitset128_set(set, 128, true);
    isaec::api::bitset128_set(set, 1000, true);
    CHECK((set == BitSet128{}));
    CHECK(!isaec::api::bitset128_get(set, 127));
}

ISAEC_TEST(bitset128_set_clears_and_keeps_other_bits) {
    BitSet128 set;
    isaec::api::bitset128_set(set, 5, true);
    isaec::api::bitset128_set(set, 70, true);
    CHECK(isaec::api::bitset128_get(set, 5));
    CHECK(isaec::api::bitset128_get(set, 70));

    isaec::api::bitset128_set(set, 5, false);
    CHECK(!isaec::api::bitset128_get(set, 5));
    CHECK(isaec::api::bitset128_get(set, 70));
}

ISAEC_TEST(bitset128_bitwise_operations_build_new_values) {
    BitSet128 a;
    BitSet128 b;
    a.words[0] = 0xf0f0f0f0u;
    a.words[3] = 0x0000000fu;
    b.words[0] = 0x0ff00ff0u;
    b.words[2] = 0xffffffffu;

    const BitSet128 anded = isaec::api::bitset128_bit_and(a, b);
    const BitSet128 ored = isaec::api::bitset128_bit_or(a, b);
    const BitSet128 xored = isaec::api::bitset128_bit_xor(a, b);
    const BitSet128 noted = isaec::api::bitset128_bit_not(a);

    CHECK(anded.words[0] == 0x00f000f0u);
    CHECK(ored.words[0] == 0xfff0fff0u);
    CHECK(xored.words[0] == 0xff00ff00u);
    CHECK(noted.words[0] == 0x0f0f0f0fu);
    CHECK(noted.words[3] == 0xfffffff0u);

    // The operands are untouched.
    CHECK(a.words[0] == 0xf0f0f0f0u);
    CHECK(b.words[0] == 0x0ff00ff0u);
}

ISAEC_TEST(bitset128_equality_compares_all_four_words) {
    BitSet128 a;
    BitSet128 b;
    a.words[0] = 1u;
    b.words[0] = 1u;
    CHECK(a == b);
    b.words[3] = 1u;
    CHECK(a != b);
}

ISAEC_TEST(bitset128_tostring_prints_high_half_first) {
    BitSet128 set;
    set.words[0] = 0x11223344u;
    set.words[1] = 0x55667788u;
    set.words[2] = 0x99aabbccu;
    set.words[3] = 0xddeeff00u;
    CHECK(isaec::api::bitset128_to_string(set) ==
          "BitSet128: ddeeff0099aabbcc5566778811223344");
}

// -- the Lua face ------------------------------------------------------------

#if ISAEC_HAS_LUA

struct Fixture {
    lua_State* L = nullptr;
    isaec::api::Binding binding;

    Fixture() {
        L = luaL_newstate();
        luaL_openlibs(L);
        binding.attach(L);

        isaec::api::ApiSpec spec;
        spec.source = "isaac-ng.exe registration code";
        spec.classes = isaec::api::generated::isaac_api_spec();
        isaec::api::apply(spec, binding);

        // The field class provides `l` and `h`; the BitSet128 members go on
        // top of the same metatable and class table.
        isaec::api::install_field_classes(binding);
        isaec::api::install_bitset128_class(binding);
    }

    ~Fixture() { lua_close(L); }
};

ISAEC_TEST(bitset128_constructor_splits_both_integers) {
    Fixture fixture;
    lua_State* L = fixture.L;

    const char* script =
        "local b = BitSet128(0x0011223344556677, 0x0123456789abcdef)\n"
        "assert(b.l == 0x0011223344556677, 'low')\n"
        "assert(b.h == 0x0123456789abcdef, 'high')\n"
        "assert(tostring(b) == 'BitSet128: 0123456789abcdef0011223344556677',\n"
        "       tostring(b))\n";
    CHECK(luaL_dostring(L, script) == 0);
}

ISAEC_TEST(bitset128_lua_get_and_set_match_the_boundaries) {
    Fixture fixture;
    lua_State* L = fixture.L;

    const char* script =
        "local b = BitSet128(0, 0)\n"
        "b:Set(0, true)\n"
        "b:Set(31, true)\n"
        "b:Set(32, true)\n"
        "b:Set(63, true)\n"
        "b:Set(64, true)\n"
        "b:Set(95, true)\n"
        "b:Set(96, true)\n"
        "b:Set(127, true)\n"
        "assert(b:Get(0) and b:Get(31) and b:Get(32) and b:Get(63))\n"
        "assert(b:Get(64) and b:Get(95) and b:Get(96) and b:Get(127))\n"
        "assert(not b:Get(1) and not b:Get(30) and not b:Get(65))\n"
        "assert(not b:Get(-1) and not b:Get(128) and not b:Get(1000))\n"
        "b:Set(-1, true)\n"
        "b:Set(128, true)\n"
        "assert(b:Get(127))\n"
        "b:Set(5, false)\n"
        "assert(not b:Get(5))\n"
        "b:Set(6, true)\n"
        "b:Set(6)\n"
        "assert(not b:Get(6), 'a missing value clears')\n"
        "assert(not pcall(function() return b:Get('x') end), 'index type')\n";
    CHECK(luaL_dostring(L, script) == 0);
}

ISAEC_TEST(bitset128_lua_metamethods_and_operand_errors) {
    Fixture fixture;
    lua_State* L = fixture.L;

    const char* script =
        "local a = BitSet128(0xf0f0f0f0, 0x0000000f)\n"
        "local b = BitSet128(0x0ff00ff0, 0)\n"
        "assert((a & b):Get(4))\n"
        "assert((a | b):Get(28))\n"
        "assert((a ~ b):Get(31))\n"
        "local n = ~a\n"
        "assert(n:Get(0) and not a:Get(0))\n"
        "assert(a ~= n, '__bnot builds a new value')\n"
        "assert(a == BitSet128(0xf0f0f0f0, 0x0000000f), '__eq')\n"
        "assert(a ~= b, '__eq false')\n"
        "-- Only BitSet128 is accepted as the second operand.\n"
        "assert(not pcall(function() return a & 2 end), 'number operand')\n"
        "assert(not pcall(function() return a | 2 end), 'number operand')\n"
        "assert(not pcall(function() return a ~ 2 end), 'number operand')\n";
    CHECK(luaL_dostring(L, script) == 0);
}

#endif  // ISAEC_HAS_LUA

}  // namespace
