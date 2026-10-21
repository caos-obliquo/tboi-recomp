// The Lua support shared by the value classes. See include/isaec/api/lua_value.hpp.

#include "isaec/api/lua_value.hpp"

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

// The registry key of the table that maps a class name to its metatable.
constexpr const char* kMetatablesKey = "__isaec_value_metatables";

#if ISAEC_HAS_LUA

lua_State* as_state(void* state) { return static_cast<lua_State*>(state); }

// The table that maps a class name to its metatable, created on first use.
void push_metatables_table(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kMetatablesKey);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kMetatablesKey);
}

// The metatable of `class_name`, created on first use. The metatable carries
// the type name, a no-op collector hook and the read and write dispatch.
void push_class_metatable(lua_State* L, const char* class_name) {
    push_metatables_table(L);
    lua_getfield(L, -1, class_name);
    if (lua_istable(L, -1)) {
        lua_remove(L, -2);  // the metatable table
        return;
    }
    lua_pop(L, 1);

    lua_createtable(L, 0, 6);
    lua_pushstring(L, class_name);  // NOLINT(google-runtime-printf)
    lua_setfield(L, -2, "__name");
    lua_pushcfunction(L, [](lua_State* /*state*/) -> int { return 0; });
    lua_setfield(L, -2, "__gc");

    // Remember it for the next instance: the metatables table is below the
    // metatable and the copy on the top of the stack.
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, class_name);
    lua_remove(L, -2);  // the metatable table
}

// The class name of the instance at `index`, taken from its metatable. The
// name stays on the stack while the caller uses it.
void push_class_name(lua_State* L, int index) {
    if (lua_getmetatable(L, index) == 0) {
        lua_pushnil(L);
        return;
    }
    lua_getfield(L, -1, "__name");
    lua_remove(L, -2);  // the metatable
}

// __index(instance, name).
//
// A property wins over a method: a name in the class table's __propget table
// is a property and is read here, a name in its __index table is a method and
// is handed back so that Lua calls it with the instance as the first
// argument.
int value_index_dispatch(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }

    push_class_name(L, 1);
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }
    const char* class_name = lua_tostring(L, -1);  // NOLINT(google-runtime-printf)

    lua_getfield(L, LUA_REGISTRYINDEX, class_name);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        lua_pushnil(L);
        return 1;
    }

    // A property getter: call it with the instance and return its result.
    lua_getfield(L, -1, "__propget");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, lua_tostring(L, 2));  // NOLINT(google-runtime-printf)
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, 1);
            lua_call(L, 1, 1);
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // A method: hand the function back so Lua calls it with the instance.
    lua_getfield(L, -1, "__index");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, lua_tostring(L, 2));  // NOLINT(google-runtime-printf)
        if (lua_isfunction(L, -1)) {
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_pop(L, 2);  // the class table and the class name
    lua_pushnil(L);
    return 1;
}

// __newindex(instance, name, value).
//
// Writes go through the class table's __propset table. A name without a
// setter is an error, which is what the original does for a read-only
// property.
int value_newindex_dispatch(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        return luaL_error(L, "isaec: property name must be a string");
    }

    push_class_name(L, 1);
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        return luaL_error(L, "isaec: cannot assign to a value without a class");
    }
    const char* class_name = lua_tostring(L, -1);  // NOLINT(google-runtime-printf)

    lua_getfield(L, LUA_REGISTRYINDEX, class_name);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "__propset");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, lua_tostring(L, 2));  // NOLINT(google-runtime-printf)
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, 1);  // the instance
                lua_pushvalue(L, 3);  // the value
                lua_call(L, 2, 0);
                return 0;
            }
        }
    }
    return luaL_error(L, "isaec: %s:%s cannot be assigned", class_name,
                       lua_tostring(L, 2));  // NOLINT(google-runtime-printf)
}

#endif  // ISAEC_HAS_LUA

}  // namespace

void ValueClass::install(void* state, const char* class_name) {
#if ISAEC_HAS_LUA
    auto* L = as_state(state);

    push_class_metatable(L, class_name);
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, value_index_dispatch);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, value_newindex_dispatch);
        lua_setfield(L, -2, "__newindex");
    }
    lua_pop(L, 1);
#else
    (void)state;
    (void)class_name;
#endif
}

void ValueClass::push_metatable(void* state, const char* class_name) {
#if ISAEC_HAS_LUA
    auto* L = as_state(state);
    push_class_metatable(L, class_name);
#else
    (void)state;
    (void)class_name;
#endif
}

void* ValueClass::check(void* state, int index, const char* class_name) {
#if ISAEC_HAS_LUA
    auto* L = as_state(state);
    void* payload = lua_touserdata(L, index);
    if (payload == nullptr) {
        luaL_error(L, "isaec: %s expected at argument %d", class_name, index);
        return nullptr;
    }
    if (lua_getmetatable(L, index) == 0) {
        luaL_error(L, "isaec: %s expected at argument %d", class_name, index);
        return nullptr;
    }
    // The metatable has to be the one registered for this class, so that a
    // value of another class is refused even if it carries the same name.
    push_class_metatable(L, class_name);
    const bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    if (!same) {
        luaL_error(L, "isaec: %s expected at argument %d", class_name, index);
        return nullptr;
    }
    return payload;
#else
    (void)state;
    (void)index;
    (void)class_name;
    return nullptr;
#endif
}

void* ValueClass::push(void* state, const char* class_name, std::size_t bytes) {
#if ISAEC_HAS_LUA
    auto* L = as_state(state);
    void* payload = lua_newuserdata(L, bytes);
    // The userdata goes on the stack first and the metatable second, so that
    // lua_setmetatable(L, -2) sets the metatable of the instance.
    push_class_metatable(L, class_name);
    lua_setmetatable(L, -2);
    return payload;
#else
    (void)state;
    (void)class_name;
    (void)bytes;
    return nullptr;
#endif
}

}  // namespace isaec::api
