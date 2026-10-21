// The field classes: the game objects whose Lua members are plain fields.
//
// The tables under test come from tools/probe_field_layout.py, which reads
// the offset out of the property's upvalue and the type out of the getter
// body, so the numbers here are measurements of the shipped binary. The
// object valued members (a pointer into the game's heap) keep the loud stub
// of the generated spec, and the test checks that they still refuse.

#include "isaec/api/binding.hpp"
#include "isaec/api/field_class.hpp"
#include "isaec/api/field_classes.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"
#include "isaec/platform/types.hpp"

#include "test_harness.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace {

using isaec::api::FieldClass;
using isaec::api::FieldType;
using isaec::f32;
using isaec::i32;
using isaec::u8;
using isaec::usize;

#if ISAEC_HAS_LUA

// A Lua state with the generated spec applied and the field classes
// installed. The caller closes it.
struct Fixture {
    lua_State* L = nullptr;
    isaec::api::Binding binding;

    Fixture() {
        L = luaL_newstate();
        // The standard libraries are needed for pcall in the scripts.
        luaL_openlibs(L);
        binding.attach(L);

        isaec::api::ApiSpec spec;
        spec.source = "isaac-ng.exe registration code";
        spec.classes = isaec::api::generated::isaac_api_spec();
        isaec::api::apply(spec, binding);

        // The implementation replaces the stubs of the members it implements.
        isaec::api::install_field_classes(binding);
    }

    ~Fixture() { lua_close(L); }
};

void store_i32(void* payload, usize offset, i32 value) {
    std::memcpy(static_cast<u8*>(payload) + offset, &value, sizeof(value));
}

void store_f32(void* payload, usize offset, f32 value) {
    std::memcpy(static_cast<u8*>(payload) + offset, &value, sizeof(value));
}

void store_u8(void* payload, usize offset, u8 value) {
    static_cast<u8*>(payload)[offset] = value;
}

i32 load_i32(const void* payload, usize offset) {
    i32 value = 0;
    std::memcpy(&value, static_cast<const u8*>(payload) + offset, sizeof(value));
    return value;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

// -- the tables --------------------------------------------------------------

ISAEC_TEST(field_tables_sit_at_the_recovered_offsets) {
    // The upvalues of the ActiveItemDesc properties are the offsets of the
    // fields: the item id at 0, the charge at 8, the partial charge at 24
    // and the var data at 28. The Repentogon layout agrees.
    CHECK(isaec::api::kActiveItemDescFields[0].name != nullptr);
    CHECK(std::strcmp(isaec::api::kActiveItemDescFields[0].name, "Item") == 0);
    CHECK(isaec::api::kActiveItemDescFields[0].offset == 0);
    CHECK(isaec::api::kActiveItemDescFields[0].type == FieldType::Int);
    CHECK(std::strcmp(isaec::api::kActiveItemDescFields[1].name, "Charge") == 0);
    CHECK(isaec::api::kActiveItemDescFields[1].offset == 8);
    CHECK(std::strcmp(isaec::api::kActiveItemDescFields[5].name, "PartialCharge") == 0);
    CHECK(isaec::api::kActiveItemDescFields[5].offset == 24);
    CHECK(isaec::api::kActiveItemDescFields[5].type == FieldType::Float);
    CHECK(std::strcmp(isaec::api::kActiveItemDescFields[6].name, "VarData") == 0);
    CHECK(isaec::api::kActiveItemDescFields[6].offset == 28);
    CHECK(isaec::api::kActiveItemDescSize == 32);

    // KColor is four floats, the same shape as the Color class.
    CHECK(isaec::api::kKColorFields[0].offset == 0);
    CHECK(isaec::api::kKColorFields[3].offset == 12);
    CHECK(isaec::api::kKColorFields[3].type == FieldType::Float);
    CHECK(isaec::api::kKColorSize == 16);

    // The "initialized" flag of a grid entity description is a byte, the
    // rest are integers.
    CHECK(std::strcmp(isaec::api::kGridEntityDescFields[6].name, "Initialized") == 0);
    CHECK(isaec::api::kGridEntityDescFields[6].offset == 24);
    CHECK(isaec::api::kGridEntityDescFields[6].type == FieldType::Bool);
}

#if ISAEC_HAS_LUA

// -- the Lua face ------------------------------------------------------------

ISAEC_TEST(field_properties_are_reachable_from_a_script) {
    Fixture fixture;
    lua_State* L = fixture.L;

    // The port owns the bytes of the object: the game side that would fill
    // them is still missing, so the test writes them by hand.
    void* payload = FieldClass::create(L, "ActiveItemDesc", isaec::api::kActiveItemDescSize);
    REQUIRE(payload != nullptr);
    store_i32(payload, 0, 5);       // the item id
    store_i32(payload, 8, 3);       // the charge
    store_f32(payload, 24, 0.5f);   // the partial charge
    store_i32(payload, 28, 77);     // the var data
    lua_setglobal(L, "d");

    const char* script = R"lua(
        assert(d.Item == 5, "Item")
        assert(d.Charge == 3, "Charge")
        assert(d.PartialCharge == 0.5, "PartialCharge")
        assert(d.VarData == 77, "VarData")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);
}

ISAEC_TEST(field_properties_can_be_written) {
    Fixture fixture;
    lua_State* L = fixture.L;

    void* payload = FieldClass::create(L, "ActiveItemDesc", isaec::api::kActiveItemDescSize);
    REQUIRE(payload != nullptr);
    store_i32(payload, 8, 3);
    lua_setglobal(L, "d");

    const char* script = R"lua(
        d.Charge = 7
        assert(d.Charge == 7, "Charge")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);

    // The write landed in the payload of the object.
    CHECK(load_i32(payload, 8) == 7);
}

ISAEC_TEST(field_bool_members_read_and_write) {
    Fixture fixture;
    lua_State* L = fixture.L;

    void* payload =
        FieldClass::create(L, "GridEntityDesc", isaec::api::kGridEntityDescSize);
    REQUIRE(payload != nullptr);
    store_i32(payload, 0, 2);   // the type
    store_u8(payload, 24, 1);   // the initialized flag
    lua_setglobal(L, "g");

    const char* script = R"lua(
        assert(g.Type == 2, "Type")
        assert(g.Initialized == true, "Initialized")
        g.Initialized = false
        assert(g.Initialized == false, "Initialized after the write")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);
}

ISAEC_TEST(kcolor_fields_round_trip) {
    Fixture fixture;
    lua_State* L = fixture.L;

    void* payload = FieldClass::create(L, "KColor", isaec::api::kKColorSize);
    REQUIRE(payload != nullptr);
    store_f32(payload, 0, 0.25f);
    store_f32(payload, 4, 0.5f);
    store_f32(payload, 8, 0.75f);
    store_f32(payload, 12, 1.0f);
    lua_setglobal(L, "c");

    const char* script = R"lua(
        assert(c.Red == 0.25, "Red")
        assert(c.Green == 0.5, "Green")
        assert(c.Blue == 0.75, "Blue")
        assert(c.Alpha == 1.0, "Alpha")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);
}

ISAEC_TEST(object_members_keep_the_loud_stub) {
    Fixture fixture;
    lua_State* L = fixture.L;

    // The "Item" member of a temporary effect is a pointer into the game's
    // heap: the port cannot reproduce it, so it keeps the stub of the
    // generated spec and refuses loudly.
    void* payload = FieldClass::create(L, "TemporaryEffect", isaec::api::kTemporaryEffectSize);
    REQUIRE(payload != nullptr);
    store_i32(payload, 4, 2);  // the count
    lua_setglobal(L, "e");

    const char* script = R"lua(
        assert(e.Count == 2, "Count")
        local ok, err = pcall(function() return e.Item end)
        assert(not ok, "an object member must refuse")
        assert(string.find(err, "not implemented") ~= nil, "the refusal says so")
    )lua";

    const int status = luaL_dostring(L, script);
    if (status != 0) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
    }
    CHECK(status == 0);
}

#endif  // ISAEC_HAS_LUA
