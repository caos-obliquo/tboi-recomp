// The Color class: the properties, the methods recovered from the shipped
// binary and the __mul metamethod.
//
// What the binary gives us is spelled out in include/isaec/api/color.hpp: the
// seven property offsets, the eleven floats, and the native routines for
// Reset, SetTint, SetColorize, SetOffset, Lerp and __mul. The oracle checked
// everything except Color:Lerp, which the installed (older) build predates.

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

Color color_with_colorize(f32 r, f32 g, f32 b, f32 a) {
    Color color = sample_color();
    color.colorize[0] = r;
    color.colorize[1] = g;
    color.colorize[2] = b;
    color.colorize[3] = a;
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

    // The colorize quadruple is the block SetColorize writes, at 16..31.
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetColorize), 0.0f));

    // The object ends with the last recovered field.
    CHECK(isaec::api::kColorOffsetBO + sizeof(f32) == isaec::api::kColorObjectBytes);
}

ISAEC_TEST(color_fields_can_be_written) {
    Color color = sample_color();
    isaec::api::color_set_field(color, isaec::api::kColorOffsetG, 0.125f);
    CHECK(near(color.g, 0.125f));
    CHECK(near(isaec::api::color_field(color, isaec::api::kColorOffsetG), 0.125f));
}

// -- the native methods ------------------------------------------------------

ISAEC_TEST(color_reset_restores_opaque_white) {
    Color color = color_with_colorize(0.1f, 0.2f, 0.3f, 0.4f);
    isaec::api::color_reset(color);
    CHECK(color.r == 1.0f && color.g == 1.0f && color.b == 1.0f && color.a == 1.0f);
    CHECK(color.colorize[0] == 0.0f && color.colorize[1] == 0.0f);
    CHECK(color.colorize[2] == 0.0f && color.colorize[3] == 0.0f);
    CHECK(color.ro == 0.0f && color.go == 0.0f && color.bo == 0.0f);
}

ISAEC_TEST(color_setters_write_the_recovered_blocks) {
    Color color{};
    isaec::api::color_set_tint(color, 1.0f, 0.5f, 0.25f, 0.0f);
    CHECK(color.r == 1.0f && color.g == 0.5f && color.b == 0.25f && color.a == 0.0f);

    isaec::api::color_set_colorize(color, 0.1f, 0.2f, 0.3f, 0.4f);
    CHECK(color.colorize[0] == 0.1f && color.colorize[1] == 0.2f);
    CHECK(color.colorize[2] == 0.3f && color.colorize[3] == 0.4f);
    // Colorize is not the tint.
    CHECK(color.r == 1.0f);

    isaec::api::color_set_offset(color, 0.55f, 0.66f, 0.77f);
    CHECK(near(color.ro, 0.55f) && near(color.go, 0.66f) && near(color.bo, 0.77f));
}

ISAEC_TEST(color_lerp_returns_a_new_mix) {
    const Color from = sample_color();
    Color to = sample_color();
    to.r = 1.0f;
    to.g = 0.0f;
    to.b = 0.0f;
    to.a = 0.5f;
    to.ro = 0.5f;
    to.go = 0.6f;
    to.bo = 0.7f;

    const Color at_zero = isaec::api::color_lerp(from, to, 0.0f);
    CHECK(at_zero.r == from.r && at_zero.ro == from.ro);

    const Color at_one = isaec::api::color_lerp(from, to, 1.0f);
    CHECK(at_one.r == to.r && at_one.ro == to.ro);

    const Color middle = isaec::api::color_lerp(from, to, 0.5f);
    CHECK(near(middle.r, 0.625f));
    CHECK(near(middle.ro, 0.3f));
    // The source is untouched.
    CHECK(from.r == 0.25f && from.ro == 0.1f);
}

ISAEC_TEST(color_multiply_mutates_rgba_and_adds_offsets) {
    Color color = sample_color();
    Color other{};
    other.r = 0.5f;
    other.g = 0.5f;
    other.b = 0.5f;
    other.a = 0.5f;
    other.ro = 0.4f;
    other.go = 0.5f;
    other.bo = 0.6f;

    isaec::api::color_multiply(color, other);
    CHECK(near(color.r, 0.125f) && near(color.g, 0.25f));
    CHECK(near(color.b, 0.375f) && near(color.a, 0.5f));
    CHECK(near(color.ro, 0.5f) && near(color.go, 0.7f) && near(color.bo, 0.9f));
    // The other colour is not touched.
    CHECK(other.r == 0.5f && other.ro == 0.4f);
}

ISAEC_TEST(color_multiply_copies_colorize_when_the_receiver_has_none) {
    Color color = sample_color();  // colorize sum is zero
    const Color other = color_with_colorize(0.2f, 0.4f, 0.6f, 0.8f);

    isaec::api::color_multiply(color, other);
    CHECK(color.colorize[0] == 0.2f && color.colorize[1] == 0.4f);
    CHECK(color.colorize[2] == 0.6f && color.colorize[3] == 0.8f);
}

ISAEC_TEST(color_multiply_averages_colorize_when_both_sides_have_one) {
    Color color = color_with_colorize(0.2f, 0.4f, 0.6f, 0.8f);
    const Color other = color_with_colorize(0.4f, 0.6f, 0.8f, 1.0f);

    isaec::api::color_multiply(color, other);
    CHECK(near(color.colorize[0], 0.3f) && near(color.colorize[1], 0.5f));
    CHECK(near(color.colorize[2], 0.7f) && near(color.colorize[3], 0.9f));
}

ISAEC_TEST(color_multiply_leaves_colorize_alone_when_the_other_has_none) {
    Color color = color_with_colorize(0.2f, 0.4f, 0.6f, 0.8f);
    const Color other = sample_color();  // colorize sum is zero

    isaec::api::color_multiply(color, other);
    CHECK(color.colorize[0] == 0.2f && color.colorize[1] == 0.4f);
    CHECK(color.colorize[2] == 0.6f && color.colorize[3] == 0.8f);
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

ISAEC_TEST(color_methods_are_reachable_from_a_script) {
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
    isaec::api::push_color(L, sample_color());
    lua_setglobal(L, "g");

    const char* script = R"lua(
        assert(c:Reset() == nil, "Reset must return nothing")
        assert(c.R == 1 and c.G == 1 and c.B == 1 and c.A == 1, "Reset")
        assert(c.RO == 0 and c.GO == 0 and c.BO == 0, "Reset offsets")

        assert(c:SetTint(1, 0.5, 0.25, 0) == nil, "SetTint")
        assert(c.R == 1 and c.G == 0.5 and c.B == 0.25 and c.A == 0, "SetTint values")

        assert(c:SetOffset(0.55, 0.66, 0.77) == nil, "SetOffset")
        assert(math.abs(c.RO - 0.55) < 1e-6, "SetOffset RO")
        assert(math.abs(c.GO - 0.66) < 1e-6, "SetOffset GO")
        assert(math.abs(c.BO - 0.77) < 1e-6, "SetOffset BO")

        assert(c:SetColorize(0.1, 0.2, 0.3, 0.4) == nil, "SetColorize")
        assert(c.R == 1, "SetColorize must not change the tint")

        -- Color:Lerp is not in the public API: it is a v1.9.7.17 addition.
        local goal = g
        goal:SetTint(0, 0, 0, 0)
        local mixed = c:Lerp(goal, 0.5)
        assert(mixed ~= c, "Lerp returns a new colour")
        assert(math.abs(mixed.R - 0.5) < 1e-6, "Lerp R")
        assert(c.R == 1, "Lerp does not mutate")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(color_multiply_metamethod_mutates_and_returns_the_left_operand) {
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

    isaec::api::Color left = sample_color();
    left.r = 0.5f;
    left.g = 0.5f;
    left.b = 0.5f;
    left.a = 1.0f;
    isaec::api::push_color(L, left);
    lua_setglobal(L, "c1");

    isaec::api::Color right{};
    right.r = 0.2f;
    right.g = 0.4f;
    right.b = 0.8f;
    right.a = 0.5f;
    right.ro = 0.4f;
    right.go = 0.5f;
    right.bo = 0.6f;
    isaec::api::push_color(L, right);
    lua_setglobal(L, "c2");

    const char* script = R"lua(
        local result = c1 * c2
        assert(math.abs(c1.R - 0.1) < 1e-6, "R")
        assert(math.abs(c1.G - 0.2) < 1e-6, "G")
        assert(math.abs(c1.B - 0.4) < 1e-6, "B")
        assert(math.abs(c1.A - 0.5) < 1e-6, "A")
        assert(math.abs(c1.RO - 0.5) < 1e-6, "RO")
        assert(math.abs(c1.GO - 0.7) < 1e-6, "GO")
        assert(math.abs(c1.BO - 0.9) < 1e-6, "BO")
        assert(result == c1, "__mul returns the left operand")
        c1.R = 99
        assert(result.R == 99, "__mul result aliases the left operand")
        assert(math.abs(c2.R - 0.2) < 1e-6, "the right operand is untouched")
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
