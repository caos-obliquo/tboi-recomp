// The Color class: the second group of closure bodies that are really
// implemented instead of refused.
//
// What the binary gives us and what it does not is spelled out in
// include/isaec/api/color.hpp and include/isaec/api/color_lua.hpp: the seven
// colour properties and their field offsets are recovered, the four methods
// and the __mul metamethod are calls into the game's own C++ and are not, so
// they keep the loud stub of the generated spec.

#include "isaec/api/binding.hpp"
#include "isaec/api/color.hpp"
#include "isaec/api/color_lua.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"

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

using isaec::api::Color;
using isaec::f32;

constexpr f32 kEpsilon = 1e-6f;

bool near(f32 a, f32 b) { return std::fabs(a - b) <= kEpsilon; }

Color sample_color() {
    Color color{};
    color.r = 0.25f;
    color.g = 0.5f;
    color.b = 0.75f;
    color.a = 1.0f;
    color.ro = 0.1f;
    color.go = 0.2f;
    color.bo = 0.3f;
    return color;
}

// -- the value ---------------------------------------------------------------

ISAEC_TEST(color_fields_sit_at_the_recovered_offsets) {
    const Color color = sample_color();

    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetR), 0.25f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetG), 0.5f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetB), 0.75f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetA), 1.0f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetRO), 0.1f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetGO), 0.2f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetBO), 0.3f));

    // The object ends with the last recovered field.
    CHECK(isaec::api::kColorOffsetBO + sizeof(f32) == isaec::api::kColorObjectBytes);
}

ISAEC_TEST(color_fields_can_be_written) {
    Color color = sample_color();
    isaec::api::color_set_field(color, isaec::api::kColorOffsetG, 0.125f);
    CHECK(near(color.g, 0.125f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetG), 0.125f));
}

// -- the Lua face ------------------------------------------------------------

#if ISAEC_HAS_LUA

ISAEC_TEST(color_properties_are_reachable_from_a_script) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    // The standard libraries are needed for pcall in the scripts.
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    // The implementation replaces the stubs of the members it implements.
    isaec::api::install_color_class(binding);

    isaec::api::push_color(L, sample_color());
    lua_setglobal(L, "c");

    const char* script = R"lua(
        assert(c.R == 0.25, "R")
        assert(c.G == 0.5, "G")
        assert(c.B == 0.75, "B")
        assert(c.A == 1, "A")
        -- the overlay channels are not exactly representable, so compare
        -- them with a tolerance
        assert(math.abs(c.RO - 0.1) < 1e-6, "RO")
        assert(math.abs(c.GO - 0.2) < 1e-6, "GO")
        assert(math.abs(c.BO - 0.3) < 1e-6, "BO")
        c.R = 0.5
        assert(c.R == 0.5, "write R")
        c.BO = 0.9
        assert(math.abs(c.BO - 0.9) < 1e-6, "write BO")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(color_members_that_are_not_implemented_still_refuse) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    isaec::api::install_color_class(binding);

    isaec::api::push_color(L, sample_color());
    lua_setglobal(L, "c");

    // Reset, SetColorize, SetTint, SetOffset and __mul are calls into the
    // game's C++ colour object, which is not in the binary. They have to
    // refuse loudly rather than pretend.
    const char* script = R"lua(
        local ok, err = pcall(function() return c:Reset() end)
        assert(not ok, "Reset must refuse")
        assert(string.find(err, "not implemented") ~= nil, "Reset message: " .. tostring(err))
        ok, err = pcall(function() return c:SetOffset(1, 2, 3) end)
        assert(not ok, "SetOffset must refuse")
        ok, err = pcall(function() return c:SetColorize(1, 2, 3, 4) end)
        assert(not ok, "SetColorize must refuse")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(color_ignores_a_foreign_userdata) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    isaec::api::install_color_class(binding);

    // A userdata that belongs to no class has no metatable, so indexing it
    // is an error: the original refuses a wrong receiver as well.
    isaec::api::push_color(L, sample_color());
    lua_setglobal(L, "c");
    lua_newuserdata(L, 64);
    lua_setglobal(L, "foreign");

    const char* script = R"lua(
        local ok, err = pcall(function() return foreign.R end)
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
