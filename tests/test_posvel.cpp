// The PosVel class: the third group of closure bodies that are really
// implemented instead of refused.
//
// What the binary gives us and what it does not is spelled out in
// include/isaec/api/posvel.hpp and include/isaec/api/posvel_lua.hpp: the two
// properties, their field offsets and the shape of the copy are recovered;
// nothing else is involved, because this class only moves two vectors in and
// out of a struct.

#include "isaec/api/binding.hpp"
#include "isaec/api/posvel.hpp"
#include "isaec/api/posvel_lua.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"
#include "isaec/api/vector.hpp"
#include "isaec/api/vector_lua.hpp"

#include "test_harness.hpp"

#include <cstddef>
#include <cstdio>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace {

using isaec::api::PosVel;
using isaec::api::Vector;

PosVel sample_posvel() {
    PosVel value{};
    value.position = Vector{1.0f, 2.0f};
    value.velocity = Vector{3.0f, 4.0f};
    return value;
}

// -- the value ---------------------------------------------------------------

ISAEC_TEST(posvel_fields_sit_at_the_recovered_offsets) {
    // The upvalues of the two properties are 0 and 8, and the closure bodies
    // copy eight bytes (two floats) to and from those offsets.
    CHECK(offsetof(PosVel, position) == isaec::api::kPosVelPositionOffset);
    CHECK(offsetof(PosVel, velocity) == isaec::api::kPosVelVelocityOffset);
    CHECK(isaec::api::kPosVelUserdataBytes == 2 * sizeof(Vector));
    CHECK(isaec::api::kPosVelPositionOffset + sizeof(Vector) ==
          isaec::api::kPosVelVelocityOffset);
}

// -- the Lua face ------------------------------------------------------------

#if ISAEC_HAS_LUA

ISAEC_TEST(posvel_properties_are_reachable_from_a_script) {
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
    isaec::api::install_vector_class(binding);
    isaec::api::install_posvel_class(binding);

    isaec::api::push_posvel(L, sample_posvel());
    lua_setglobal(L, "p");

    const char* script = R"lua(
        -- the getter hands out a Vector, and the Vector class is alive
        assert(p.Position.X == 1, "Position.X")
        assert(p.Position.Y == 2, "Position.Y")
        assert(p.Velocity.X == 3, "Velocity.X")
        assert(p.Velocity.Y == 4, "Velocity.Y")
        assert(p.Position:Length() > 2, "the returned value is a Vector")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(posvel_properties_can_be_written) {
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
    isaec::api::install_posvel_class(binding);

    isaec::api::push_posvel(L, sample_posvel());
    lua_setglobal(L, "p");

    // The game creates the vectors, so there is no Vector constructor in the
    // binding: the test hands one over from the C++ side.
    isaec::api::push_vector(L, Vector{5.0f, 6.0f});
    lua_setglobal(L, "v");

    const char* script = R"lua(
        p.Position = v
        assert(p.Position.X == 5, "write Position.X")
        assert(p.Position.Y == 6, "write Position.Y")
        -- the velocity is untouched
        assert(p.Velocity.X == 3, "Velocity.X")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(posvel_getter_hands_out_a_copy) {
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
    isaec::api::install_posvel_class(binding);

    isaec::api::push_posvel(L, sample_posvel());
    lua_setglobal(L, "p");

    // The original copies the eight bytes into a fresh userdata, so writing
    // to the returned vector cannot change the PosVel.
    const char* script = R"lua(
        local copy = p.Position
        copy.X = 99
        assert(p.Position.X == 1, "the getter must hand out a copy")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    lua_close(L);
}

ISAEC_TEST(posvel_refuses_a_value_that_is_not_a_vector) {
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
    isaec::api::install_posvel_class(binding);

    isaec::api::push_posvel(L, sample_posvel());
    lua_setglobal(L, "p");

    const char* script = R"lua(
        local ok, err = pcall(function() p.Position = 42 end)
        assert(not ok, "a number is not a Vector")
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
