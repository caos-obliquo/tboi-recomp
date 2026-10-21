// The Vector class: the first group of closure bodies that are really
// implemented instead of refused.
//
// The tests come in two halves. The first half is the arithmetic, which has no
// interpreter in it and can therefore be checked on its own. The second half
// drives the class from a script the way the game does, and it only exists
// when the binding layer was built with Lua.
//
// What the binary gives us and what it does not is spelled out in
// include/isaec/api/vector.hpp and include/isaec/api/vector_lua.hpp: the ABI
// is recovered, the arithmetic is the documented Isaac API reimplemented here.

#include "isaec/api/binding.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"
#include "isaec/api/vector.hpp"
#include "isaec/api/vector_lua.hpp"

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

using isaec::api::Vector;
using isaec::f32;

constexpr f32 kEpsilon = 1e-5f;

bool near(f32 a, f32 b) { return std::fabs(a - b) <= kEpsilon; }

}  // namespace

// -- the arithmetic ---------------------------------------------------------

ISAEC_TEST(vector_length_is_the_euclidean_norm) {
    CHECK(near(isaec::api::vector_length(Vector{3.0f, 4.0f}), 5.0f));
    CHECK(near(isaec::api::vector_length_squared(Vector{3.0f, 4.0f}), 25.0f));
    CHECK(near(isaec::api::vector_length(Vector{0.0f, 0.0f}), 0.0f));
}

ISAEC_TEST(vector_products_follow_the_documented_rules) {
    const Vector a{1.0f, 2.0f};
    const Vector b{3.0f, 4.0f};
    CHECK(near(isaec::api::vector_dot(a, b), 11.0f));
    // The z component of the 3D cross product of two 2D vectors.
    CHECK(near(isaec::api::vector_cross(a, b), -2.0f));
    CHECK(near(isaec::api::vector_distance(a, b), std::sqrt(8.0f)));
    CHECK(near(isaec::api::vector_distance_squared(a, b), 8.0f));
}

ISAEC_TEST(vector_lerp_interpolates_linearly) {
    const Vector a{0.0f, 0.0f};
    const Vector b{10.0f, 20.0f};
    CHECK(isaec::api::vector_lerp(a, b, 0.0f) == a);
    CHECK(isaec::api::vector_lerp(a, b, 1.0f) == b);
    const Vector mid = isaec::api::vector_lerp(a, b, 0.5f);
    CHECK(near(mid.x, 5.0f));
    CHECK(near(mid.y, 10.0f));
}

ISAEC_TEST(vector_normalize_and_resize_keep_the_direction) {
    const Vector unit = isaec::api::vector_normalized(Vector{3.0f, 4.0f});
    CHECK(near(isaec::api::vector_length(unit), 1.0f));
    CHECK(near(unit.x, 0.6f));
    CHECK(near(unit.y, 0.8f));

    // A vector without a length normalises to the zero vector rather than to
    // a NaN.
    CHECK((isaec::api::vector_normalized(Vector{0.0f, 0.0f}) == Vector{0.0f, 0.0f}));

    const Vector scaled = isaec::api::vector_resized(Vector{1.0f, 2.0f}, 3.0f);
    CHECK(near(scaled.x, 3.0f));
    CHECK(near(scaled.y, 6.0f));
}

ISAEC_TEST(vector_division_by_zero_yields_the_zero_vector) {
    const Vector result = isaec::api::vector_divide(Vector{1.0f, 2.0f}, 0.0f);
    CHECK((result == Vector{0.0f, 0.0f}));
}

ISAEC_TEST(vector_rotation_is_counter_clockwise_in_degrees) {
    const Vector rotated = isaec::api::vector_rotated(Vector{1.0f, 0.0f}, 90.0f);
    CHECK(near(rotated.x, 0.0f));
    CHECK(near(rotated.y, 1.0f));

    const Vector back = isaec::api::vector_rotated(Vector{0.0f, 1.0f}, 90.0f);
    CHECK(near(back.x, -1.0f));
    CHECK(near(back.y, 0.0f));
}

ISAEC_TEST(vector_angle_is_reported_in_degrees) {
    CHECK(near(isaec::api::vector_angle_degrees(Vector{1.0f, 0.0f}), 0.0f));
    CHECK(near(isaec::api::vector_angle_degrees(Vector{0.0f, 1.0f}), 90.0f));
    CHECK(near(isaec::api::vector_angle_degrees(Vector{-1.0f, 0.0f}), 180.0f));
    // The range is (-180, 180], so a straight down vector is -90 and not 270.
    CHECK(near(isaec::api::vector_angle_degrees(Vector{0.0f, -1.0f}), -90.0f));
}

// -- the Lua face ------------------------------------------------------------

#if ISAEC_HAS_LUA

ISAEC_TEST(vector_class_is_reachable_from_a_script) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    // The standard libraries are needed for pcall and math in the scripts.
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    // The implementation replaces the stubs of the members it implements.
    isaec::api::install_vector_class(binding);

    isaec::api::push_vector(L, Vector{3.0f, 4.0f});
    lua_setglobal(L, "v");
    isaec::api::push_vector(L, Vector{1.0f, 0.0f});
    lua_setglobal(L, "w");

    const char* script = R"lua(
        assert(v.X == 3, "X")
        assert(v.Y == 4, "Y")
        assert(v:Length() == 5, "Length")
        assert(v:LengthSquared() == 25, "LengthSquared")
        assert(v:Dot(w) == 3, "Dot")
        assert(v:Cross(w) == -4, "Cross")
        assert(math.abs(v:Distance(w) - math.sqrt(20)) < 1e-5, "Distance")
        assert(v:GetAngleDegrees() > 53 and v:GetAngleDegrees() < 54, "angle")
        local lerped = v:Lerp(w, 0.5)
        assert(lerped.X == 2 and lerped.Y == 2, "Lerp")
        local resized = v:Resized(2)
        assert(resized.X == 6 and resized.Y == 8, "Resized")
        local normalized = v:Normalized()
        assert(math.abs(normalized:Length() - 1) < 1e-5, "Normalized")
        local sum = v + w
        assert(sum.X == 4 and sum.Y == 4, "__add")
        local difference = v - w
        assert(difference.X == 2 and difference.Y == 4, "__sub")
        local scaled = v * 2
        assert(scaled.X == 6 and scaled.Y == 8, "__mul")
        local negated = -v
        assert(negated.X == -3 and negated.Y == -4, "__unm")
        v.X = 10
        assert(v.X == 10, "write X")
        v.Y = 20
        assert(v.Y == 20, "write Y")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(vector_members_that_are_not_implemented_still_refuse) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    // The standard libraries are needed for pcall and math in the scripts.
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);
    isaec::api::install_vector_class(binding);

    isaec::api::push_vector(L, Vector{3.0f, 4.0f});
    lua_setglobal(L, "v");

    // Vector:Clamp and Vector:Clamped are deliberately not implemented: the
    // original's closure reads four numbers while the public API documents
    // two, so the semantics are unresolved.
    const char* script = R"lua(
        local ok, err = pcall(function() return v:Clamp(1, 2) end)
        assert(not ok, "Clamp must refuse")
        assert(string.find(err, "not implemented") ~= nil, err)
        local ok2, err2 = pcall(function() return v:Clamped(1, 2) end)
        assert(not ok2, "Clamped must refuse")
        assert(string.find(err2, "not implemented") ~= nil, err2)
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(vector_rejects_a_foreign_userdata) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    // The standard libraries are needed for pcall and math in the scripts.
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);
    isaec::api::install_vector_class(binding);

    lua_newuserdata(L, 16);
    lua_setglobal(L, "foreign");

    const char* script = R"lua(
        local ok, err = pcall(function() return foreign:Length() end)
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
