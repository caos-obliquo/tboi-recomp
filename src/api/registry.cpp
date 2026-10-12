// Spec application layer.
//
// A generated ApiSpec is a flat list of class/method names. This walks it and
// registers every method through the binding core so an unimplemented surface
// is visible: calling a registered-but-stubbed method raises a Lua error naming
// the class and method instead of quietly returning nil, which is how a missing
// binding hides for months otherwise.
//
// Trampolines receive only the interpreter state, so they cannot carry their own
// identity. The dispatcher resolves that from a registration-order table.

#include "isaec/api/registry.hpp"

#include <string>
#include <vector>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#endif

namespace isaec::api {
namespace {

std::vector<TrampolineInfo>& table_storage() {
    static std::vector<TrampolineInfo> table;
    return table;
}

}  // namespace

const std::vector<TrampolineInfo>& trampoline_table() { return table_storage(); }

void reset_trampoline_table() { table_storage().clear(); }

NativeFunction not_implemented_trampoline() {
    // Defined once in this translation unit; its address is what gets
    // registered for every method the spec names but does not implement.
    static const auto trampoline = +[](void* /*state*/) -> i32 { return 0; };
    return reinterpret_cast<NativeFunction>(trampoline);
}

std::size_t apply(const ApiSpec& spec, Binding& binding) {
    std::size_t registered = 0;

    // Classes first, so a child registering before its parent still resolves.
    for (const auto& cls : spec.classes) {
        binding.register_class(cls.name, cls.parent);
    }

    for (const auto& cls : spec.classes) {
        for (const auto& method : cls.methods) {
            if (method.name.empty()) continue;

            table_storage().push_back(
                TrampolineInfo{cls.name, method.name, method.argc, method.returns});

            if (binding.register_method(cls.name, method.name, not_implemented_trampoline())) {
                ++registered;
            }
        }
    }

#if ISAEC_HAS_LUA
    // With Lua present, the trampoline is swapped for one that raises. The
    // stub above exists so the core links and tests run without an interpreter.
    if (binding.attached()) {
        auto* L = static_cast<lua_State*>(binding.state());
        for (const auto& cls : spec.classes) {
            for (const auto& method : cls.methods) {
                if (method.name.empty()) continue;

                lua_getfield(L, LUA_REGISTRYINDEX, cls.name.c_str());  // NOLINT
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "__index");
                    if (lua_istable(L, -1)) {
                        // Push the raised-error function. The class and method
                        // names travel as upvalues-free globals on the error
                        // table so no per-method allocation is needed.
                        lua_pushstring(L, cls.name.c_str());
                        lua_setfield(L, LUA_REGISTRYINDEX, "__isaec_binding_class");
                        lua_pushstring(L, method.name.c_str());
                        lua_setfield(L, LUA_REGISTRYINDEX, "__isaec_binding_method");

                        lua_pushcfunction(L, [](lua_State* state) -> int {
                            lua_getfield(state, LUA_REGISTRYINDEX, "__isaec_binding_class");
                            const char* cls_name = lua_tostring(state, -1);
                            lua_getfield(state, LUA_REGISTRYINDEX, "__isaec_binding_method");
                            const char* method_name = lua_tostring(state, -1);
                            return luaL_error(state, "isaec: %s:%s is not implemented",
                                              cls_name != nullptr ? cls_name : "?",
                                              method_name != nullptr ? method_name : "?");
                        });
                        lua_setfield(L, -2, method.name.c_str());
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
        }
    }
#endif

    return registered;
}

}  // namespace isaec::api
