#include "isaec/api/binding.hpp"

#include "test_harness.hpp"

#include <algorithm>
#include <string>

namespace {

using isaec::api::Binding;
using isaec::api::NativeFunction;
using isaec::api::PropertyKind;

isaec::i32 fn_a(void*) { return 1; }
isaec::i32 fn_b(void*) { return 2; }
isaec::i32 fn_c(void*) { return 3; }
isaec::i32 fn_get(void*) { return 10; }
isaec::i32 fn_set(void*) { return 0; }

}  // namespace

ISAEC_TEST(binding_starts_empty_and_detached) {
    Binding binding;
    CHECK(!binding.attached());
    CHECK(binding.state() == nullptr);
    CHECK(binding.class_count() == 0);
}

ISAEC_TEST(binding_registers_classes_and_rejects_duplicates) {
    Binding binding;
    CHECK(binding.register_class("Entity"));
    CHECK(binding.has_class("Entity"));
    CHECK(binding.class_count() == 1);

    // A second registration under the same name must not silently duplicate.
    CHECK(!binding.register_class("Entity"));
    CHECK(binding.class_count() == 1);
}

ISAEC_TEST(binding_tracks_parent_links) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_class("EntityPlayer", "Entity"));

    CHECK(binding.parent_of("EntityPlayer") == "Entity");
    CHECK(binding.parent_of("Entity").empty());
    CHECK(binding.parent_of("NoSuchClass").empty());

    const std::vector<std::string> chain = binding.lineage("EntityPlayer");
    REQUIRE(chain.size() == 2);
    CHECK(chain[0] == "EntityPlayer");
    CHECK(chain[1] == "Entity");
}

ISAEC_TEST(binding_resolves_inherited_methods) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_class("EntityPlayer", "Entity"));
    REQUIRE(binding.register_method("Entity", "TakeDamage", fn_a));
    REQUIRE(binding.register_method("EntityPlayer", "AddCoins", fn_b));

    // Defined on the class itself.
    CHECK(binding.resolve("EntityPlayer", "AddCoins") == fn_b);

    // Defined only on the parent: must still resolve through __parent.
    CHECK(binding.resolve("EntityPlayer", "TakeDamage") == fn_a);

    // Must not leak downwards.
    CHECK(binding.resolve("Entity", "AddCoins") == nullptr);
    CHECK(binding.resolve("EntityPlayer", "NoSuchMethod") == nullptr);
    CHECK(binding.resolve("NoSuchClass", "TakeDamage") == nullptr);
}

ISAEC_TEST(binding_child_override_wins) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_class("EntityPlayer", "Entity"));
    REQUIRE(binding.register_method("Entity", "TakeDamage", fn_a));
    REQUIRE(binding.register_method("EntityPlayer", "TakeDamage", fn_c));

    CHECK(binding.resolve("EntityPlayer", "TakeDamage") == fn_c);
    CHECK(binding.resolve("Entity", "TakeDamage") == fn_a);
}

ISAEC_TEST(binding_lineage_survives_a_cycle) {
    Binding binding;
    REQUIRE(binding.register_class("A", "B"));
    REQUIRE(binding.register_class("B", "A"));

    // A malformed spec must not hang the walk.
    const std::vector<std::string> chain = binding.lineage("A");
    CHECK(chain.size() == 2);
    CHECK(std::find(chain.begin(), chain.end(), "A") != chain.end());
    CHECK(std::find(chain.begin(), chain.end(), "B") != chain.end());
}

ISAEC_TEST(binding_properties_distinguish_constant_from_readwrite) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_property("Entity", "Health", fn_get, fn_set));
    REQUIRE(binding.register_property("Entity", "Speed", fn_get));

    const isaec::api::PropertyBinding readwrite = binding.resolve_property("Entity", "Health");
    CHECK(readwrite.getter == fn_get);
    CHECK(readwrite.setter == fn_set);
    CHECK(readwrite.kind() == PropertyKind::ReadWrite);

    // A null setter is what makes it constant, and it is what mirrors the key
    // into __const during Lua table construction.
    const isaec::api::PropertyBinding constant = binding.resolve_property("Entity", "Speed");
    CHECK(constant.getter == fn_get);
    CHECK(constant.setter == nullptr);
    CHECK(constant.kind() == PropertyKind::Constant);

    const isaec::api::PropertyBinding missing = binding.resolve_property("Entity", "Nope");
    CHECK(missing.getter == nullptr);
    CHECK(missing.setter == nullptr);
}

ISAEC_TEST(binding_properties_resolve_through_the_parent_chain) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_class("EntityPlayer", "Entity"));
    REQUIRE(binding.register_property("Entity", "Position", fn_get, fn_set));
    REQUIRE(binding.register_property("EntityPlayer", "ControlledBy", fn_get, fn_set));

    CHECK(binding.resolve_property("EntityPlayer", "Position").getter == fn_get);
    CHECK(binding.resolve_property("EntityPlayer", "ControlledBy").getter == fn_get);
    CHECK(binding.resolve_property("Entity", "ControlledBy").getter == nullptr);
}

ISAEC_TEST(binding_effective_methods_are_the_sorted_union) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_class("EntityPlayer", "Entity"));
    REQUIRE(binding.register_method("Entity", "Zebra", fn_a));
    REQUIRE(binding.register_method("Entity", "Alpha", fn_b));
    REQUIRE(binding.register_method("EntityPlayer", "Middle", fn_c));

    const std::vector<std::string> methods = binding.effective_methods("EntityPlayer");
    REQUIRE(methods.size() == 3);
    CHECK(methods[0] == "Alpha");
    CHECK(methods[1] == "Middle");
    CHECK(methods[2] == "Zebra");

    // Parent must not see the child's addition.
    CHECK(binding.effective_methods("Entity").size() == 2);
}

ISAEC_TEST(binding_effective_methods_deduplicate_overrides) {
    Binding binding;
    REQUIRE(binding.register_class("Entity"));
    REQUIRE(binding.register_class("EntityPlayer", "Entity"));
    REQUIRE(binding.register_method("Entity", "TakeDamage", fn_a));
    REQUIRE(binding.register_method("EntityPlayer", "TakeDamage", fn_c));

    const std::vector<std::string> methods = binding.effective_methods("EntityPlayer");
    CHECK(methods.size() == 1);
    CHECK(methods[0] == "TakeDamage");
}

ISAEC_TEST(binding_rejects_registration_into_unknown_classes) {
    Binding binding;
    CHECK(!binding.register_method("Ghost", "TakeDamage", fn_a));
    CHECK(!binding.register_constructor("Ghost", fn_a));
    CHECK(!binding.register_property("Ghost", "Health", fn_get));
    CHECK(binding.class_count() == 0);
}

ISAEC_TEST(binding_registers_constructors) {
    Binding binding;
    REQUIRE(binding.register_class("Sprite"));
    REQUIRE(binding.register_constructor("Sprite", fn_a));

    // A second constructor for the same class is a spec bug, not an append.
    CHECK(!binding.register_constructor("Sprite", fn_b));
}
