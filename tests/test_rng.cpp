// The RNG class: the 16-byte state, the shift table, and the five methods
// recovered from the shipped binary.
//
// The expected integer and float streams below were computed from the same
// algorithm, but the inputs and the default state are the measured ones: the
// constructor's seed 0xaa17414f and shift row 35 = {5, 9, 7}. The live oracle
// is the second check; see docs/binding-surface-findings.md.

#include "isaec/api/binding.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"
#include "isaec/api/rng.hpp"
#include "isaec/api/rng_lua.hpp"

#include "test_harness.hpp"

#include <cmath>
#include <cstdio>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace {

using isaec::f32;
using isaec::api::Rng;

constexpr f32 kEpsilon = 1e-7f;

bool near(f32 a, f32 b) { return std::fabs(a - b) <= kEpsilon; }

// -- the state ---------------------------------------------------------------

ISAEC_TEST(rng_default_is_the_constructors_state) {
    const Rng rng = isaec::api::rng_default();

    // The native constructor stores these three constants directly rather
    // than going through SetSeed.
    CHECK(rng.seed == 0xaa17414fu);
    CHECK(rng.shifts[0] == 5u);
    CHECK(rng.shifts[1] == 9u);
    CHECK(rng.shifts[2] == 7u);
}

ISAEC_TEST(rng_set_seed_selects_a_shift_row_and_clamps) {
    Rng rng{};

    isaec::api::rng_set_seed(rng, 123u, 35u);
    CHECK(rng.seed == 123u);
    CHECK(rng.shifts[0] == 5u && rng.shifts[1] == 9u && rng.shifts[2] == 7u);

    isaec::api::rng_set_seed(rng, 456u, 0u);
    CHECK(rng.seed == 456u);
    CHECK(rng.shifts[0] == 1u && rng.shifts[1] == 3u && rng.shifts[2] == 10u);

    // The native min() clamps to row 80, whose row is {17, 15, 26}.
    isaec::api::rng_set_seed(rng, 789u, 0x51u);
    CHECK(rng.shifts[0] == 17u && rng.shifts[1] == 15u && rng.shifts[2] == 26u);
}

ISAEC_TEST(rng_next_advances_the_seed) {
    Rng rng{};
    isaec::api::rng_set_seed(rng, 1u, 0u);
    CHECK(isaec::api::rng_next(rng) == 9u);
    CHECK(isaec::api::rng_get_seed(rng) == 9u);

    isaec::api::rng_set_seed(rng, 12345u, 35u);
    CHECK(isaec::api::rng_next(rng) == 6522683u);
    CHECK(isaec::api::rng_get_seed(rng) == 6522683u);
}

ISAEC_TEST(rng_random_int_matches_the_measured_stream) {
    Rng rng{};
    isaec::api::rng_set_seed(rng, 12345u, 35u);

    // unsigned new_seed % 100 after each advance.
    CHECK(isaec::api::rng_random_int(rng, 100u) == 83u);
    CHECK(isaec::api::rng_random_int(rng, 100u) == 64u);
    CHECK(isaec::api::rng_random_int(rng, 100u) == 50u);
    CHECK(isaec::api::rng_random_int(rng, 100u) == 93u);
    CHECK(isaec::api::rng_random_int(rng, 100u) == 48u);
    CHECK(isaec::api::rng_get_seed(rng) == 38037648u);
}

ISAEC_TEST(rng_random_int_zero_still_advances) {
    Rng rng{};
    isaec::api::rng_set_seed(rng, 12345u, 35u);

    CHECK(isaec::api::rng_random_int(rng, 0u) == 0u);
    CHECK(isaec::api::rng_get_seed(rng) == 6522683u);
}

ISAEC_TEST(rng_random_float_matches_the_measured_stream) {
    Rng rng{};
    isaec::api::rng_set_seed(rng, 12345u, 35u);

    CHECK(near(isaec::api::rng_random_float(rng), 0.0015186802484095097f));
    CHECK(near(isaec::api::rng_random_float(rng), 0.7532377243041992f));
    CHECK(near(isaec::api::rng_random_float(rng), 0.3874836564064026f));
    CHECK(isaec::api::rng_get_seed(rng) == 1664229950u);
}

ISAEC_TEST(rng_zero_seed_is_left_alone_instead_of_aborting) {
    // The native prints "RNG Seed is zero!" and executes INT3. That is a
    // deliberate difference: a reimplementation must not kill the host.
    Rng rng{};
    isaec::api::rng_set_seed(rng, 0u, 35u);

    CHECK(isaec::api::rng_next(rng) == 0u);
    CHECK(isaec::api::rng_random_int(rng, 10u) == 0u);
    CHECK(isaec::api::rng_random_float(rng) == 0.0f);
    CHECK(isaec::api::rng_get_seed(rng) == 0u);
}

// -- the Lua face ------------------------------------------------------------

#if ISAEC_HAS_LUA

ISAEC_TEST(rng_class_is_callable_from_a_script) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    isaec::api::install_rng_class(binding);

    const char* script = R"lua(
        assert(type(RNG) == "table", "the class table is the global")

        local rng = RNG()
        assert(rng:GetSeed() == 0xaa17414f, "constructor seed")
        assert(type(rng:GetSeed()) == "number", "GetSeed type")

        -- The native constructor ignores all arguments.
        local ignored = RNG(12345, 7)
        assert(ignored:GetSeed() == 0xaa17414f, "constructor ignores arguments")

        rng:SetSeed(12345, 35)
        assert(rng:GetSeed() == 12345, "SetSeed")

        -- Next advances and returns the new seed.
        local next_seed = rng:Next()
        assert(type(next_seed) == "number", "Next type")
        assert(next_seed == 6522683, "Next value")
        assert(rng:GetSeed() == 6522683, "Next updates the seed")

        rng:SetSeed(12345, 35)
        assert(rng:RandomInt(100) == 83, "RandomInt 1")
        assert(rng:RandomInt(100) == 64, "RandomInt 2")
        assert(rng:RandomInt(100) == 50, "RandomInt 3")
        assert(rng:RandomInt(100) == 93, "RandomInt 4")
        assert(rng:RandomInt(100) == 48, "RandomInt 5")

        -- RandomInt(0) is defined to return 0 while still advancing.
        rng:SetSeed(12345, 35)
        assert(rng:RandomInt(0) == 0, "RandomInt(0)")
        assert(rng:GetSeed() == 6522683, "RandomInt(0) advances")

        rng:SetSeed(12345, 35)
        local f = rng:RandomFloat()
        assert(type(f) == "number", "RandomFloat type")
        assert(math.abs(f - 0.0015186802484095097) < 1e-7, "RandomFloat 1")
        local g = rng:RandomFloat()
        assert(math.abs(g - 0.7532377243041992) < 1e-7, "RandomFloat 2")

        -- The clamp is observable through the shift row: row 80 differs.
        rng:SetSeed(1, 0)
        assert(rng:Next() == 9, "row 0")
        rng:SetSeed(1, 0x51)
        assert(rng:Next() == 32769, "row 80 clamped")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(rng_ignores_a_foreign_userdata) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    isaec::api::install_rng_class(binding);

    isaec::api::push_rng(L, isaec::api::rng_default());
    lua_setglobal(L, "rng");
    lua_newuserdata(L, 64);
    lua_setglobal(L, "foreign");

    const char* script = R"lua(
        local ok, err = pcall(function() return foreign:GetSeed() end)
        assert(not ok, "a foreign userdata must be refused")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

#endif  // ISAEC_HAS_LUA

}  // namespace
