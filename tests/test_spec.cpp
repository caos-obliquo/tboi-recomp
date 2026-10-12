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

constexpr std::size_t kExpectedClasses = 9;
constexpr std::size_t kExpectedMethods = 655;

}  // namespace

ISAEC_TEST(spec_recovers_the_expected_surface) {
    const auto classes = isaec::api::generated::isaac_api_spec();
    CHECK(classes.size() == kExpectedClasses);

    std::size_t methods = 0;
    for (const auto& cls : classes) {
        methods += cls.methods.size();
    }
    CHECK(methods == kExpectedMethods);
}

ISAEC_TEST(spec_applies_to_the_binding_core) {
    Binding binding;
    ApiSpec spec;
    spec.source = "isaac-ng.exe binding tables";
    spec.classes = isaec::api::generated::isaac_api_spec();

    isaec::api::reset_trampoline_table();
    const std::size_t registered = isaec::api::apply(spec, binding);

    CHECK(registered == kExpectedMethods);
    CHECK(binding.class_count() == kExpectedClasses);
    CHECK(binding.has_class("Entity"));
    CHECK(binding.has_class("EntityPlayer"));
}

ISAEC_TEST(spec_preserves_recovered_inheritance) {
    Binding binding;
    ApiSpec spec;
    spec.source = "isaac-ng.exe binding tables";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    CHECK(binding.parent_of("EntityPlayer") == std::string("Entity"));
    CHECK(binding.parent_of("Entity") == std::string());

    const auto inherited = binding.effective_methods("EntityPlayer");
    CHECK(inherited.size() > 77);
    CHECK(inherited.front() < inherited.back());
}

ISAEC_TEST(spec_registers_every_method_as_resolvable) {
    Binding binding;
    ApiSpec spec;
    spec.source = "isaac-ng.exe binding tables";
    spec.classes = isaec::api::generated::isaac_api_spec();
    isaec::api::apply(spec, binding);

    std::set<std::string> seen;
    for (const auto& cls : spec.classes) {
        for (const auto& method : cls.methods) {
            const std::string full = cls.name + ":" + method.name;
            CHECK(seen.insert(full).second);
        }
    }
    CHECK(seen.size() == kExpectedMethods);

    isaec::api::reset_trampoline_table();
    isaec::api::apply(spec, binding);
    const auto& trampolines = isaec::api::trampoline_table();
    CHECK(trampolines.size() == kExpectedMethods);
    CHECK(trampolines.front().class_name == "Entity");
    CHECK(trampolines.front().method_name == "Acceleration");
}