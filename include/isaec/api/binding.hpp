// The Lua binding core.
//
// Isaac's engine does not register Lua functions with lua_register. It builds a
// custom metatable model instead, and the keys below were recovered from the
// shipped executable. Any reimplementation has to reproduce this shape or
// mods written against the original will not resolve.
//
// This header deliberately does NOT include <lua.h>. The interpreter handle is
// an opaque void*, and NativeFunction is our own signature so that callers can
// build bindings without Lua headers on their include path. Only binding.cpp
// includes Lua.

#ifndef ISAEC_API_BINDING_HPP
#define ISAEC_API_BINDING_HPP

#include <string>
#include <string_view>
#include <vector>

#include "isaec/platform/types.hpp"

namespace isaec::api {

// Lua's C function signature, restated so this header stays Lua-free.
// The first parameter is the interpreter state, passed as an opaque pointer.
using NativeFunction = i32 (*)(void* state);

// Metatable keys. These exact strings appear in the shipped game binary.
namespace key {
inline constexpr std::string_view Class = "__class";
inline constexpr std::string_view Parent = "__parent";
inline constexpr std::string_view Index = "__index";
inline constexpr std::string_view PropGet = "__propget";
inline constexpr std::string_view PropSet = "__propset";
inline constexpr std::string_view NewIndex = "__newindex";
inline constexpr std::string_view Const = "__const";
inline constexpr std::string_view Metatable = "__metatable";
inline constexpr std::string_view Type = "__type";
}  // namespace key

// How a property behaves when read and written.
enum class PropertyKind : u8 {
    ReadWrite,  // getter in __propget, setter in __propset
    Constant,   // getter in __propget, value mirrored into __const
};

// A registered property: who reads it, who writes it.
struct PropertyBinding {
    NativeFunction getter = nullptr;
    NativeFunction setter = nullptr;

    [[nodiscard]] PropertyKind kind() const noexcept {
        return setter == nullptr ? PropertyKind::Constant : PropertyKind::ReadWrite;
    }
};

// The binding core.
//
// One Binding owns the class registry for a single interpreter state. It holds
// a per-class table keyed by class name; each entry is a table of tables (the
// metatable described above). Class lookup walks the __parent chain so that a
// subclass inherits its parent's methods without copying them.
//
// When built without Lua (ISAEC_HAS_LUA=0) every operation is a validated
// no-op and the class registry still works, so the core is testable and
// linkable on hosts with no interpreter installed.
class Binding {
public:
    Binding();
    ~Binding();

    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;
    Binding(Binding&&) noexcept;
    Binding& operator=(Binding&&) noexcept;

    // Attach an interpreter state. Passing nullptr detaches.
    void attach(void* lua_state);
    [[nodiscard]] void* state() const noexcept { return state_; }
    [[nodiscard]] bool attached() const noexcept { return state_ != nullptr; }

    // Create a class. `parent` may be empty for a root class. The class name is
    // stored under __class; the parent name, if any, under __parent. Returns
    // false if the class already exists.
    bool register_class(std::string_view name, std::string_view parent = {});

    // Make `cls` callable as a global constructor function. A class has
    // exactly one, so a second registration is rejected rather than silently
    // replacing the first: that would mask a duplicated entry in a spec.
    // Returns false if the class is unknown or already constructible.
    bool register_constructor(std::string_view cls, NativeFunction fn);

    // Add a method to a class's __index table.
    bool register_method(std::string_view cls, std::string_view method, NativeFunction fn);

    // Add a property. A null setter makes it a constant, which also mirrors the
    // value into the class's __const table so it can be enumerated.
    bool register_property(std::string_view cls, std::string_view prop, NativeFunction getter,
                           NativeFunction setter = nullptr);

    // Resolve `method` starting at `cls`, walking __parent until found.
    // Returns nullptr when neither the class nor any ancestor defines it.
    [[nodiscard]] NativeFunction resolve(std::string_view cls, std::string_view method) const;

    // Resolve a property the same way.
    [[nodiscard]] PropertyBinding resolve_property(std::string_view cls, std::string_view prop) const;

    // Immediate base class of `cls`, or empty for a root or unknown class.
    [[nodiscard]] std::string_view parent_of(std::string_view cls) const;

    // Ancestor chain from `cls` up to its root, including `cls` itself.
    // Terminates on a repeated class, so a malformed spec cannot loop.
    [[nodiscard]] std::vector<std::string> lineage(std::string_view cls) const;

    [[nodiscard]] bool has_class(std::string_view cls) const;
    [[nodiscard]] std::size_t class_count() const noexcept { return classes_.size(); }

    // Every class name registered so far.
    [[nodiscard]] std::vector<std::string> class_names() const;

    // Every method a class can see, including inherited ones, sorted by name.
    [[nodiscard]] std::vector<std::string> effective_methods(std::string_view cls) const;

private:
    // Method and property function pointers are kept alongside their names so
    // resolve() hands back the real address. A registry that stored names only
    // could not answer a lookup without re-reading the Lua stack, and returning
    // anything derived from the entry's own address would be a dangling jump.
    struct MethodSlot {
        std::string name;
        NativeFunction fn = nullptr;
    };

    struct PropertySlot {
        std::string name;
        NativeFunction getter = nullptr;
        NativeFunction setter = nullptr;
    };

    struct ClassEntry {
        std::string name;
        std::string parent;
        NativeFunction constructor = nullptr;
        std::vector<MethodSlot> methods;
        std::vector<PropertySlot> properties;
    };

    ClassEntry* find(std::string_view cls);
    const ClassEntry* find(std::string_view cls) const;

    void* state_ = nullptr;
    std::vector<ClassEntry> classes_;
};

}  // namespace isaec::api

#endif  // ISAEC_API_BINDING_HPP
