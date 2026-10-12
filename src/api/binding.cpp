// Binding core implementation.
//
// Two halves, deliberately kept in one file because they are one concern:
//
//   1. The interpreter side. When built with Lua, every registration pushes a
//      real table onto the Lua stack and wires the metatable keys Isaac uses.
//      When built without Lua (ISAEC_HAS_LUA=0), this half compiles away
//      entirely and the rest of the engine still links.
//
//   2. The registry side. A plain C++ mirror of the class tree, which is what
//      resolve() walks and what the tests assert against. It is always present,
//      so the inheritance model is testable on a host with no interpreter.

#include "isaec/api/binding.hpp"

#include <algorithm>
#include <unordered_map>

#if ISAEC_HAS_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace isaec::api {
namespace {

// Keeps the registry side O(1) per lookup instead of scanning a vector.
using ClassIndex = std::unordered_map<std::string, usize>;

}  // namespace

struct BindingState {
    ClassIndex index;
};

Binding::Binding() = default;

Binding::~Binding() = default;

Binding::Binding(Binding&&) noexcept = default;

Binding& Binding::operator=(Binding&&) noexcept = default;

void Binding::attach(void* lua_state) { state_ = lua_state; }

Binding::ClassEntry* Binding::find(std::string_view cls) {
    for (auto& entry : classes_) {
        if (entry.name == cls) return &entry;
    }
    return nullptr;
}

const Binding::ClassEntry* Binding::find(std::string_view cls) const {
    for (const auto& entry : classes_) {
        if (entry.name == cls) return &entry;
    }
    return nullptr;
}

bool Binding::register_class(std::string_view name, std::string_view parent) {
    if (name.empty()) return false;
    if (has_class(name)) return false;
    // A parent that does not exist yet is allowed: the spec is generated in
    // arbitrary order, so Entity may be registered after EntityPlayer. The
    // parent name is recorded either way and resolve() copes with a missing
    // ancestor by simply finding nothing there.
    if (!parent.empty() && parent == name) return false;

    ClassEntry entry;
    entry.name.assign(name);
    entry.parent.assign(parent);
    classes_.push_back(std::move(entry));

#if ISAEC_HAS_LUA
    if (state_ != nullptr) {
        auto* L = static_cast<lua_State*>(state_);
        lua_createtable(L, 0, 8);

        lua_pushstring(L, name.data());  // NOLINT(google-runtime-printf)
        lua_setfield(L, -2, key::Class.data());

        if (!parent.empty()) {
            lua_pushstring(L, parent.data());  // NOLINT(google-runtime-printf)
            lua_setfield(L, -2, key::Parent.data());
        }

        // Child tables: __index holds methods, __propget/__propset the
        // property accessors, __const the read-only mirror, __newindex the
        // write dispatch hook. Registering them up front means the metatable
        // shape is identical whether or not anything has been added yet.
        for (const std::string_view k : {key::Index, key::PropGet, key::PropSet,
                                         key::Const, key::NewIndex}) {
            lua_createtable(L, 0, 8);
            lua_setfield(L, -2, k.data());
        }

        lua_pushstring(L, "table");  // NOLINT(google-runtime-printf)
        lua_setfield(L, -2, key::Type.data());

        lua_setfield(L, LUA_REGISTRYINDEX, name.data());  // NOLINT
    }
#endif

    return true;
}

bool Binding::register_constructor(std::string_view cls, NativeFunction fn) {
    if (fn == nullptr) return false;
    auto* entry = find(cls);
    if (entry == nullptr) return false;
    if (entry->constructor != nullptr) return false;
    entry->constructor = fn;

#if ISAEC_HAS_LUA
    if (state_ != nullptr) {
        auto* L = static_cast<lua_State*>(state_);
        // The class name becomes a global function of the same name, which is
        // how the original exposes constructors: EntityPlayer(...) works
        // without any namespace.
        lua_pushcfunction(L, reinterpret_cast<lua_CFunction>(fn));
        lua_setglobal(L, cls.data());
    }
#else
    (void)fn;
#endif
    return true;
}

bool Binding::register_method(std::string_view cls, std::string_view method, NativeFunction fn) {
    if (fn == nullptr || method.empty()) return false;
    auto* entry = find(cls);
    if (entry == nullptr) return false;

    bool replaced = false;
    for (auto& slot : entry->methods) {
        if (slot.name == method) {
            slot.fn = fn;
            replaced = true;
            break;
        }
    }
    if (!replaced) entry->methods.push_back(MethodSlot{std::string(method), fn});
    std::sort(entry->methods.begin(), entry->methods.end(),
              [](const MethodSlot& a, const MethodSlot& b) { return a.name < b.name; });

#if ISAEC_HAS_LUA
    if (state_ != nullptr) {
        auto* L = static_cast<lua_State*>(state_);
        lua_getfield(L, LUA_REGISTRYINDEX, cls.data());  // NOLINT
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key::Index.data());
            if (lua_istable(L, -1)) {
                lua_pushcfunction(L, reinterpret_cast<lua_CFunction>(fn));
                lua_setfield(L, -2, method.data());
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
#else
    (void)fn;
#endif
    return true;
}

bool Binding::register_property(std::string_view cls, std::string_view prop, NativeFunction getter,
                                NativeFunction setter) {
    if (getter == nullptr || prop.empty()) return false;
    auto* entry = find(cls);
    if (entry == nullptr) return false;

    bool replaced = false;
    for (auto& slot : entry->properties) {
        if (slot.name == prop) {
            slot.getter = getter;
            slot.setter = setter;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entry->properties.push_back(PropertySlot{std::string(prop), getter, setter});
    }
    std::sort(entry->properties.begin(), entry->properties.end(),
              [](const PropertySlot& a, const PropertySlot& b) { return a.name < b.name; });

#if ISAEC_HAS_LUA
    if (state_ != nullptr) {
        auto* L = static_cast<lua_State*>(state_);
        lua_getfield(L, LUA_REGISTRYINDEX, cls.data());  // NOLINT
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key::PropGet.data());
            if (lua_istable(L, -1)) {
                lua_pushcfunction(L, reinterpret_cast<lua_CFunction>(getter));
                lua_setfield(L, -2, prop.data());
            }
            lua_pop(L, 1);

            if (setter != nullptr) {
                lua_getfield(L, -1, key::PropSet.data());
                if (lua_istable(L, -1)) {
                    lua_pushcfunction(L, reinterpret_cast<lua_CFunction>(setter));
                    lua_setfield(L, -2, prop.data());
                }
                lua_pop(L, 1);
            } else {
                // No setter means read-only. Mirror the key into __const so it
                // can be enumerated as immutable, which is how the original
                // advertises constants.
                lua_getfield(L, -1, key::Const.data());
                if (lua_istable(L, -1)) {
                    lua_pushboolean(L, 1);
                    lua_setfield(L, -2, prop.data());
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
#else
    (void)setter;
#endif
    return true;
}

NativeFunction Binding::resolve(std::string_view cls, std::string_view method) const {
    // Walk up the __parent chain. Bounded by class count so a cycle introduced
    // by a malformed spec cannot hang the caller.
    std::string_view current = cls;
    for (usize hops = 0; hops <= classes_.size(); ++hops) {
        const auto* entry = find(current);
        if (entry == nullptr) return nullptr;
        for (const auto& slot : entry->methods) {
            if (slot.name == method) return slot.fn;
        }
        if (entry->parent.empty()) return nullptr;
        current = entry->parent;
    }
    return nullptr;
}

PropertyBinding Binding::resolve_property(std::string_view cls, std::string_view prop) const {
    PropertyBinding binding;
    std::string_view current = cls;
    for (usize hops = 0; hops <= classes_.size(); ++hops) {
        const auto* entry = find(current);
        if (entry == nullptr) return binding;
        for (const auto& slot : entry->properties) {
            if (slot.name == prop) {
                binding.getter = slot.getter;
                binding.setter = slot.setter;
                return binding;
            }
        }
        if (entry->parent.empty()) return binding;
        current = entry->parent;
    }
    return binding;
}

std::string_view Binding::parent_of(std::string_view cls) const {
    const auto* entry = find(cls);
    return entry == nullptr ? std::string_view{} : std::string_view{entry->parent};
}

std::vector<std::string> Binding::lineage(std::string_view cls) const {
    std::vector<std::string> chain;
    std::string_view current = cls;
    while (true) {
        const auto* entry = find(current);
        if (entry == nullptr) break;
        if (std::find(chain.begin(), chain.end(), entry->name) != chain.end()) break;
        chain.push_back(entry->name);
        if (entry->parent.empty()) break;
        current = entry->parent;
    }
    return chain;
}

bool Binding::has_class(std::string_view cls) const { return find(cls) != nullptr; }

std::vector<std::string> Binding::class_names() const {
    std::vector<std::string> names;
    names.reserve(classes_.size());
    for (const auto& entry : classes_) names.push_back(entry.name);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> Binding::effective_methods(std::string_view cls) const {
    std::vector<std::string> out;
    for (const auto& name : lineage(cls)) {
        const auto* entry = find(name);
        if (entry == nullptr) continue;
        for (const auto& slot : entry->methods) {
            if (std::find(out.begin(), out.end(), slot.name) == out.end()) {
                out.push_back(slot.name);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace isaec::api
