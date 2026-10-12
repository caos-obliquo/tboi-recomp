// Registration descriptors.
//
// registry.hpp is the thin, declarative layer over binding.hpp: instead of
// calling register_method directly for each of the game's several hundred API
// functions, a class is described as a table of names and handed to Registry,
// which walks it and issues the calls.
//
// This is the layer the generated spec from tools/gen-api-spec.py feeds into.

#ifndef ISAEC_API_REGISTRY_HPP
#define ISAEC_API_REGISTRY_HPP

#include <string>
#include <string_view>
#include <vector>

#include "isaec/api/binding.hpp"

namespace isaec::api {

// One entry of a generated API table: a method name and, once recovered from
// disassembly, its prototype. `argc` and `returns` are placeholders until the
// disassembly pass fills them in; `documented` stays false because the method
// semantics have not been written from scratch yet.
struct MethodSpec {
    std::string name;
    int argc = 0;
    int returns = 0;
    bool documented = false;
};

// A generated API table for one class.
struct ClassSpec {
    std::string name;
    std::string parent;
    std::vector<MethodSpec> methods;
};

// A whole API surface, as produced by the generator.
struct ApiSpec {
    std::string source;
    std::vector<ClassSpec> classes;
};

// Apply a spec to a binding core. Every method in `spec` is registered as a
// trampoline that pushes a "not yet implemented" Lua error naming the class
// and method, so an unimplemented surface fails loudly at the call site rather
// than silently returning nil.
//
// Returns the number of methods registered.
std::size_t apply(const ApiSpec& spec, Binding& binding);

// The shared trampoline: raises a Lua error when a binding is missing.
// Safe to register for every method regardless of implementation status.
NativeFunction not_implemented_trampoline();

// Trampolines cannot carry per-method context (they receive only the Lua state),
// so each registration gets a small dispatcher keyed by index. This is that
// table: name and argument-count metadata for every registered method, in
// registration order.
struct TrampolineInfo {
    std::string class_name;
    std::string method_name;
    int argc = 0;
    int returns = 0;
};

const std::vector<TrampolineInfo>& trampoline_table();
void reset_trampoline_table();

}  // namespace isaec::api

#endif  // ISAEC_API_REGISTRY_HPP
