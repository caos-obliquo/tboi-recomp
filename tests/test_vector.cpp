// The Vector class: the arithmetic translated from the shipped binary and the
// Lua face that the in-game closures expose.
//
// The first half has no interpreter in it and can therefore be checked on its
// own. The second half drives the class from a script the way the game does,
// and it only exists when the binding layer was built with Lua.
//
// The binary routines and the oracle run that settled their Lua-visible
// behaviour are recorded in include/isaec/api/vector.hpp and
// docs/binding-surface-findings.md.

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

ISAEC_TEST(vector_products_follow_the_native_routines) {
    const Vector a{1.0f, 2.0f};
    const Vector b{3.0f, 4.0f};
    CHECK(near(isaec::api::vector_dot(a, b), 11.0f));
    // The z component of the 3D cross product of two 2D vectors.
    CHECK(near(isaec::api::vector_cross(a, b), -2.0f));
    CHECK(near(isaec::api::vector_distance(a, b), std::sqrt(8.0f)));
    CHECK(near(isaec::api::vector_distance_squared(a, b), 8.0f));
}

ISAEC_TEST(vector_products_are_component_wise) {
    const Vector a{1.0f, 2.0f};
    const Vector b{2.0f, 0.5f};
    CHECK((isaec::api::vector_multiply(a, b) == Vector{2.0f, 1.0f}));
    CHECK((isaec::api::vector_divide(a, b) == Vector{0.5f, 4.0f}));
}

ISAEC_TEST(vector_lerp_mutates_the_receiver_and_honours_the_clamp_flag) {
    Vector v{0.0f, 0.0f};
    isaec::api::vector_lerp(v, Vector{10.0f, 20.0f}, 0.5f, false);
    CHECK(near(v.x, 5.0f));
    CHECK(near(v.y, 10.0f));

    // The fourth argument is the clamp flag the public API does not document.
    Vector unclamped{0.0f, 0.0f};
    isaec::api::vector_lerp(unclamped, Vector{10.0f, 20.0f}, -1.0f, false);
    CHECK(near(unclamped.x, -10.0f));
    CHECK(near(unclamped.y, -20.0f));

    Vector low{0.0f, 0.0f};
    isaec::api::vector_lerp(low, Vector{10.0f, 20.0f}, -1.0f, true);
    CHECK((low == Vector{0.0f, 0.0f}));

    Vector high{0.0f, 0.0f};
    isaec::api::vector_lerp(high, Vector{10.0f, 20.0f}, 2.0f, true);
    CHECK((high == Vector{10.0f, 20.0f}));
}

ISAEC_TEST(vector_normalize_and_resize_keep_the_direction) {
    const Vector unit = isaec::api::vector_normalized(Vector{3.0f, 4.0f});
    CHECK(near(isaec::api::vector_length(unit), 1.0f));
    CHECK(near(unit.x, 0.6f));
    CHECK(near(unit.y, 0.8f));

    // A vector without a length is copied through rather than turned into
    // NaNs; the native guard is `length > 0`.
    CHECK((isaec::api::vector_normalized(Vector{0.0f, 0.0f}) == Vector{0.0f, 0.0f}));
    const Vector nan_vector{std::nanf(""), 1.0f};
    const Vector nan_result = isaec::api::vector_normalized(nan_vector);
    CHECK(std::isnan(nan_result.x));
    CHECK(nan_result.y == 1.0f);

    // Resize sets the length; it does not multiply the coordinates.
    const Vector resized = isaec::api::vector_resized(Vector{3.0f, 4.0f}, 10.0f);
    CHECK(near(resized.x, 6.0f));
    CHECK(near(resized.y, 8.0f));
    CHECK(near(isaec::api::vector_length(resized), 10.0f));
    CHECK((isaec::api::vector_resized(Vector{0.0f, 0.0f}, 5.0f) == Vector{0.0f, 0.0f}));
}

ISAEC_TEST(vector_division_by_zero_yields_the_zero_vector) {
    // This is the one arithmetic rule that is not from the binary: the
    // `Vector / number` path was not disassembled, so the earlier documented
    // rule is kept and stated here.
    const Vector result = isaec::api::vector_scale_divide(Vector{1.0f, 2.0f}, 0.0f);
    CHECK((result == Vector{0.0f, 0.0f}));
}

ISAEC_TEST(vector_rotation_matches_the_measured_float_results) {
    const Vector rotated = isaec::api::vector_rotated(Vector{1.0f, 0.0f}, 90.0f);
    // The oracle gave -4.3711388286737929e-08 for the x component, not zero:
    // the trigonometric result is rounded to float before it is multiplied.
    CHECK(near(rotated.x, -4.3711388286737929e-08f));
    CHECK(near(rotated.y, 1.0f));

    const Vector back = isaec::api::vector_rotated(Vector{1.0f, 0.0f}, 180.0f);
    CHECK(near(back.x, -1.0f));
    CHECK(near(back.y, -8.7422776573475858e-08f));
}

ISAEC_TEST(vector_from_angle_is_the_unit_direction) {
    const Vector right = isaec::api::vector_from_angle(0.0f);
    CHECK(near(right.x, 1.0f));
    CHECK(near(right.y, 0.0f));

    const Vector up = isaec::api::vector_from_angle(90.0f);
    CHECK(near(up.x, -4.3711388286737929e-08f));
    CHECK(near(up.y, 1.0f));
}

ISAEC_TEST(vector_angle_is_reported_in_degrees_without_a_remap) {
    CHECK(near(isaec::api::vector_angle_degrees(Vector{1.0f, 0.0f}), 0.0f));
    CHECK(near(isaec::api::vector_angle_degrees(Vector{0.0f, 1.0f}), 90.0f));
    CHECK(near(isaec::api::vector_angle_degrees(Vector{-1.0f, 0.0f}), 180.0f));
    // The range is (-180, 180], so a straight down vector is -90 and not 270.
    CHECK(near(isaec::api::vector_angle_degrees(Vector{0.0f, -1.0f}), -90.0f));
    // The oracle gave -126.86989593505859; the old `-180 -> 180` remap would
    // have produced 233.13 here.
    CHECK(near(isaec::api::vector_angle_degrees(Vector{-3.0f, -4.0f}), -126.86989593505859f));
}

ISAEC_TEST(vector_clamp_uses_min_then_max_per_component) {
    Vector v{50.0f, 5.0f};
    isaec::api::vector_clamp(v, 0.0f, 10.0f, 20.0f, 30.0f);
    CHECK((v == Vector{20.0f, 10.0f}));

    const Vector clamped = isaec::api::vector_clamped(Vector{50.0f, 5.0f}, 0.0f, 10.0f, 20.0f, 30.0f);
    CHECK((clamped == Vector{20.0f, 10.0f}));
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
    isaec::api::push_vector(L, Vector{2.0f, 2.0f});
    lua_setglobal(L, "u");

    const char* script = R"lua(
        assert(v.X == 3, "X")
        assert(v.Y == 4, "Y")
        assert(v:Length() == 5, "Length")
        assert(v:LengthSquared() == 25, "LengthSquared")
        assert(v:Dot(w) == 3, "Dot")
        assert(v:Cross(w) == -4, "Cross")
        assert(math.abs(v:Distance(w) - math.sqrt(20)) < 1e-5, "Distance")
        assert(v:GetAngleDegrees() > 53 and v:GetAngleDegrees() < 54, "angle")

        local sum = v + w
        assert(sum.X == 4 and sum.Y == 4, "__add")
        local difference = v - w
        assert(difference.X == 2 and difference.Y == 4, "__sub")
        local scaled = v * 2
        assert(scaled.X == 6 and scaled.Y == 8, "__mul scalar")
        local componentwise = v * u
        assert(componentwise.X == 6 and componentwise.Y == 8, "__mul vector")
        local divided = v / u
        assert(divided.X == 1.5 and divided.Y == 2, "__div vector")
        local negated = -v
        assert(negated.X == -3 and negated.Y == -4, "__unm")

        -- Lerp mutates the receiver and returns the same userdata.
        local receiver = v
        local lerped = receiver:Lerp(w, 0.5)
        assert(receiver.X == 2 and receiver.Y == 2, "Lerp mutation")
        assert(lerped == receiver, "Lerp alias")
        assert(lerped.X == 2, "Lerp result")
        receiver.X = 99
        assert(lerped.X == 99, "Lerp aliases the receiver")

        -- Clamp mutates; Clamped returns a fresh vector.
        local c = v
        c.X = 50
        c.Y = 5
        assert(c:Clamp(0, 10, 20, 30) == nil, "Clamp return")
        assert(c.X == 20 and c.Y == 10, "Clamp")
        local clamped = v:Clamped(0, 10, 20, 30)
        assert(clamped.X == 20 and clamped.Y == 10, "Clamped")
        assert(clamped ~= v, "Clamped must be fresh")

        local resized = v:Resized(10)
        assert(math.abs(resized:Length() - 10) < 1e-5, "Resized")
        local normalized = v:Normalized()
        assert(math.abs(normalized:Length() - 1) < 1e-5, "Normalized")

        local from_angle = Vector.FromAngle(0)
        assert(from_angle.X == 1 and from_angle.Y == 0, "FromAngle")
        assert(v.FromAngle == nil, "FromAngle is not an instance method")

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

ISAEC_TEST(vector_clamp_and_clamped_are_implemented) {
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    isaec::api::Binding binding;
    binding.attach(L);

    isaec::api::ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);
    isaec::api::install_vector_class(binding);

    isaec::api::push_vector(L, Vector{50.0f, 5.0f});
    lua_setglobal(L, "v");

    const char* script = R"lua(
        v:Clamp(0, 10, 20, 30)
        assert(v.X == 20 and v.Y == 10, "Clamp")
        local clamped = v:Clamped(10, 10, 15, 15)
        assert(clamped.X == 15 and clamped.Y == 10, "Clamped")
        assert(v.X == 20 and v.Y == 10, "Clamped must not mutate")
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
