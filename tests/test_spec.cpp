#include "isaec/api/binding.hpp"
#include "isaec/api/generated/isaac_api_spec.hpp"
#include "isaec/api/registry.hpp"

#include "test_harness.hpp"

#include <cstddef>
#include <set>
#include <string>

namespace {

using isaec::api::ApiSpec;
using isaec::api::Binding;

// The surface is generated from the code-derived class table, so these counts
// are the generated surface, not a hand-maintained number: tools/gen-api-spec.py
// rewrites bindings/isaac-api-spec.json and the generated header together.
constexpr std::size_t kExpectedClasses = 67;        // 64 classes + 3 namespaces
constexpr std::size_t kExpectedMethods = 1003;      // 938 class + 65 namespace
constexpr std::size_t kExpectedProperties = 461;    // 434 class + 27 namespace

}  // namespace

ISAEC_TEST(spec_recovers_the_expected_surface) {
    const auto classes = isaec::api::generated::isaac_api_spec();
    CHECK(classes.size() == kExpectedClasses);

    std::size_t methods = 0;
    std::size_t properties = 0;
    for (const auto& cls : classes) {
        methods += cls.methods.size();
        properties += cls.properties.size();
    }
    CHECK(methods == kExpectedMethods);
    CHECK(properties == kExpectedProperties);
}

ISAEC_TEST(spec_applies_to_the_binding_core) {
    Binding binding;
    ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();

    isaec::api::reset_trampoline_table();
    const std::size_t registered = isaec::api::apply(spec, binding);

    // Methods and properties are both registered, each behind the
    // not-implemented trampoline; the table records one entry per registration.
    CHECK(registered == kExpectedMethods + kExpectedProperties);
    CHECK(binding.class_count() == kExpectedClasses);
    CHECK(binding.has_class("Entity"));
    CHECK(binding.has_class("EntityPlayer"));
    CHECK(binding.has_class("Vector"));
}

ISAEC_TEST(spec_preserves_recovered_inheritance) {
    Binding binding;
    ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    CHECK(binding.parent_of("EntityPlayer") == std::string("Entity"));
    CHECK(binding.parent_of("Entity") == std::string());

    // EntityPlayer carries 249 of its own methods and inherits Entity's 63.
    const auto inherited = binding.effective_methods("EntityPlayer");
    CHECK(inherited.size() == 312);
    CHECK(inherited.front() < inherited.back());
}

ISAEC_TEST(spec_registers_every_method_and_property_as_resolvable) {
    Binding binding;
    ApiSpec spec;
    spec.source = "isaac-ng.exe registration code";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    std::set<std::string> seen;
    for (const auto& cls : spec.classes) {
        for (const auto& method : cls.methods) {
            const std::string full = cls.name + ":" + method.name;
            CHECK(seen.insert(full).second);
        }
        for (const auto& prop : cls.properties) {
            const std::string full = cls.name + ":" + prop.name;
            CHECK(seen.insert(full).second);
        }
    }
    CHECK(seen.size() == kExpectedMethods + kExpectedProperties);

    // A property resolves through resolve_property, never through resolve():
    // Vector.X has a getter and a setter, Vector.Length is a method.
    CHECK(binding.resolve("Vector", "Length") != nullptr);
    CHECK(binding.resolve_property("Vector", "X").getter != nullptr);
    CHECK(binding.resolve_property("Vector", "X").kind()
          == isaec::api::PropertyKind::ReadWrite);
    CHECK(binding.resolve("Vector", "X") == nullptr);
    CHECK(binding.resolve_property("Vector", "Length").getter == nullptr);

    isaec::api::reset_trampoline_table();
    isaec::api::apply(spec, binding);
    const auto& trampolines = isaec::api::trampoline_table();
    CHECK(trampolines.size() == kExpectedMethods + kExpectedProperties);
    // Trampolines are recorded methods-first, then properties, so the first
    // entry is the first class that has a method: BitSet128.Get.
    CHECK(trampolines.front().class_name == "BitSet128");
    CHECK(trampolines.front().method_name == "Get");
}
