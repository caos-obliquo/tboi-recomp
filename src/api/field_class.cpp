// The support for the classes whose members are plain fields.
//
// See include/isaec/api/field_class.hpp for what this is and where the
// numbers come from.

#include "isaec/api/field_class.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "isaec/api/binding.hpp"
#include "isaec/api/lua_value.hpp"

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

// The measured layout of one class.
struct FieldClassInfo {
    const char* name;
    usize object_size;
    const FieldSpec* fields;
    usize field_count;
};

// The registered field classes.
//
// The getter looks the class up by name, so the table has to outlive every
// instance; it is allocated once and never freed.
std::vector<FieldClassInfo*>& registry() {
    static std::vector<FieldClassInfo*>* classes = new std::vector<FieldClassInfo*>();
    return *classes;
}

const FieldClassInfo* find_class(const char* name) {
    for (const FieldClassInfo* info : registry()) {
        if (std::strcmp(info->name, name) == 0) {
            return info;
        }
    }
    return nullptr;
}

const FieldSpec* find_field(const FieldClassInfo* info, const char* name) {
    for (usize i = 0; i < info->field_count; ++i) {
        if (std::strcmp(info->fields[i].name, name) == 0) {
            return &info->fields[i];
        }
    }
    return nullptr;
}

#if ISAEC_HAS_LUA

// The payload of the fields is read and written byte by byte: the layout is
// the game's, and the port does not reinterpret it as a C++ struct.
i32 load_i32(const u8* bytes) noexcept {
    i32 value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

f32 load_f32(const u8* bytes) noexcept {
    f32 value = 0.0f;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

u8 load_u8(const u8* bytes) noexcept { return *bytes; }

i64 load_i64(const u8* bytes) noexcept {
    // The game stores a 64 bit integer in two halves, low first.
    u64 low = 0;
    u64 high = 0;
    for (usize i = 0; i < 4; ++i) {
        low |= static_cast<u64>(bytes[i]) << (8 * i);
        high |= static_cast<u64>(bytes[4 + i]) << (8 * i);
    }
    return static_cast<i64>((high << 32) | low);
}

i16 load_i16(const u8* bytes) noexcept {
    u16 value = 0;
    for (usize i = 0; i < 2; ++i) {
        value |= static_cast<u16>(bytes[i]) << (8 * i);
    }
    return static_cast<i16>(value);
}

void store_i32(u8* bytes, i32 value) noexcept {
    std::memcpy(bytes, &value, sizeof(value));
}

void store_f32(u8* bytes, f32 value) noexcept {
    std::memcpy(bytes, &value, sizeof(value));
}

void store_u8(u8* bytes, u8 value) noexcept { *bytes = value; }

void store_i64(u8* bytes, i64 value) noexcept {
    const u64 bits = static_cast<u64>(value);
    for (usize i = 0; i < 4; ++i) {
        bytes[i] = static_cast<u8>((bits >> (8 * i)) & 0xff);
        bytes[4 + i] = static_cast<u8>((bits >> (8 * (4 + i))) & 0xff);
    }
}

void store_i16(u8* bytes, i16 value) noexcept {
    const u16 bits = static_cast<u16>(value);
    for (usize i = 0; i < 2; ++i) {
        bytes[i] = static_cast<u8>((bits >> (8 * i)) & 0xff);
    }
}

// The class name of the instance at `index`, taken from its metatable.
std::string instance_class_name(lua_State* L, int index) {
    if (lua_getmetatable(L, index) == 0) {
        return {};
    }
    lua_getfield(L, -1, "__name");
    std::string name;
    if (lua_type(L, -1) == LUA_TSTRING) {
        name = lua_tostring(L, -1);  // NOLINT(google-runtime-printf)
    }
    lua_pop(L, 2);
    return name;
}

// The property getter, shared by every field of every class.
//
// The dispatch hands over the instance (argument 1) and the name of the
// property (argument 2); the name is what tells the getter which field to
// read. The original has one closure per property, with the offset in its
// upvalue.
int field_get(lua_State* L) {
    const std::string class_name = instance_class_name(L, 1);
    if (class_name.empty()) {
        return luaL_error(L, "isaec: the value has no class");
    }
    const FieldClassInfo* info = find_class(class_name.c_str());
    if (info == nullptr) {
        return luaL_error(L, "isaec: %s is not a field class", class_name.c_str());
    }
    const char* name = luaL_checkstring(L, 2);
    const FieldSpec* field = find_field(info, name);
    if (field == nullptr) {
        return luaL_error(L, "isaec: %s:%s is not a field", class_name.c_str(), name);
    }
    const auto* self = static_cast<const u8*>(ValueClass::check(L, 1, info->name));
    switch (field->type) {
        case FieldType::Int:
            lua_pushinteger(L, load_i32(self + field->offset));
            break;
        case FieldType::Int64:
            lua_pushinteger(L, static_cast<lua_Integer>(load_i64(self + field->offset)));
            break;
        case FieldType::Int16:
            lua_pushinteger(L, static_cast<lua_Integer>(load_i16(self + field->offset)));
            break;
        case FieldType::UInt8:
            lua_pushinteger(L, static_cast<lua_Integer>(load_u8(self + field->offset)));
            break;
        case FieldType::Float:
            lua_pushnumber(L, static_cast<lua_Number>(load_f32(self + field->offset)));
            break;
        case FieldType::Bool:
            lua_pushboolean(L, load_u8(self + field->offset) != 0);
            break;
        case FieldType::String: {
            // The field is a std::string in the game's layout: a pointer, a
            // length and a capacity. The port stores the bytes it owns.
            const char* data = nullptr;
            u64 size = 0;
            std::memcpy(&data, self + field->offset, sizeof(data));
            std::memcpy(&size, self + field->offset + 8, sizeof(size));
            if (data == nullptr || size == 0) {
                lua_pushliteral(L, "");
            } else {
                lua_pushlstring(L, data, static_cast<size_t>(size));
            }
            break;
        }
    }
    return 1;
}

// The property setter, shared by every field of every class.
//
// The dispatch hands over the instance (argument 1), the new value
// (argument 2) and the name of the property (argument 3).
int field_set(lua_State* L) {
    const std::string class_name = instance_class_name(L, 1);
    if (class_name.empty()) {
        return luaL_error(L, "isaec: the value has no class");
    }
    const FieldClassInfo* info = find_class(class_name.c_str());
    if (info == nullptr) {
        return luaL_error(L, "isaec: %s is not a field class", class_name.c_str());
    }
    const char* name = luaL_checkstring(L, 3);
    const FieldSpec* field = find_field(info, name);
    if (field == nullptr) {
        return luaL_error(L, "isaec: %s:%s is not a field", class_name.c_str(), name);
    }
    auto* self = static_cast<u8*>(ValueClass::check(L, 1, info->name));
    switch (field->type) {
        case FieldType::Int:
            store_i32(self + field->offset, static_cast<i32>(luaL_checknumber(L, 2)));
            break;
        case FieldType::Int64:
            store_i64(self + field->offset, static_cast<i64>(luaL_checkinteger(L, 2)));
            break;
        case FieldType::Int16:
            store_i16(self + field->offset, static_cast<i16>(luaL_checkinteger(L, 2)));
            break;
        case FieldType::UInt8:
            store_u8(self + field->offset, static_cast<u8>(luaL_checkinteger(L, 2)));
            break;
        case FieldType::Float:
            store_f32(self + field->offset, static_cast<f32>(luaL_checknumber(L, 2)));
            break;
        case FieldType::Bool:
            store_u8(self + field->offset, lua_toboolean(L, 2) ? 1 : 0);
            break;
        case FieldType::String: {
            // The port owns the bytes it stores, so the old buffer is
            // released here and the rest is released by the collector.
            size_t length = 0;
            const char* value = luaL_checklstring(L, 2, &length);
            char* buffer = new char[length + 1];
            std::memcpy(buffer, value, length);
            buffer[length] = '\0';
            char* old = nullptr;
            std::memcpy(&old, self + field->offset, sizeof(old));
            delete[] old;
            const u64 size = static_cast<u64>(length);
            std::memcpy(self + field->offset, &buffer, sizeof(buffer));
            std::memcpy(self + field->offset + 8, &size, sizeof(size));
            std::memcpy(self + field->offset + 16, &size, sizeof(size));
            break;
        }
    }
    return 0;
}

// The collector of a field class.
//
// The port owns the bytes of the string fields, so they are released when the
// instance is collected. The other fields are plain numbers.
int field_gc(lua_State* L) {
    const std::string class_name = instance_class_name(L, 1);
    const FieldClassInfo* info =
        class_name.empty() ? nullptr : find_class(class_name.c_str());
    if (info != nullptr) {
        auto* self = static_cast<u8*>(lua_touserdata(L, 1));
        if (self != nullptr) {
            for (usize i = 0; i < info->field_count; ++i) {
                if (info->fields[i].type != FieldType::String) {
                    continue;
                }
                char* data = nullptr;
                std::memcpy(&data, self + info->fields[i].offset, sizeof(data));
                delete[] data;
                data = nullptr;
                std::memcpy(self + info->fields[i].offset, &data, sizeof(data));
            }
        }
    }
    return 0;
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void FieldClass::install(Binding& binding, const char* class_name, usize object_size,
                         const FieldSpec* fields, usize field_count) {
    // Remember the layout: the getter looks the class up by name, so the
    // table has to be registered before the first instance exists.
    if (find_class(class_name) == nullptr) {
        registry().push_back(new FieldClassInfo{class_name, object_size, fields, field_count});
    }
#if ISAEC_HAS_LUA
    if (!binding.attached()) {
        return;
    }
    auto* L = static_cast<lua_State*>(binding.state());

    // The metatable with the read and the write dispatch is shared with the
    // other value classes.
    ValueClass::install(L, class_name);

    // The collector of the class releases the string fields the port owns.
    ValueClass::push_metatable(L, class_name);
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, field_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);

    // One property per field. The getter and the setter are the same
    // function for every field: the name tells them which field to work on.
    for (usize i = 0; i < field_count; ++i) {
        binding.register_property(class_name, fields[i].name,
                                  reinterpret_cast<NativeFunction>(field_get),
                                  reinterpret_cast<NativeFunction>(field_set));
    }
#else
    (void)binding;
    (void)object_size;
    (void)fields;
    (void)field_count;
#endif
}

void* FieldClass::create(void* state, const char* class_name, usize object_size) {
#if ISAEC_HAS_LUA
    return ValueClass::push(state, class_name, object_size);
#else
    (void)state;
    (void)class_name;
    (void)object_size;
    return nullptr;
#endif
}

void* FieldClass::payload(void* state, int index, const char* class_name) {
#if ISAEC_HAS_LUA
    return ValueClass::check(state, index, class_name);
#else
    (void)state;
    (void)index;
    (void)class_name;
    return nullptr;
#endif
}

}  // namespace isaec::api
