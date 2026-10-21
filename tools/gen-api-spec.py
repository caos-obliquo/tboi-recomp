#!/usr/bin/env python3
"""Generate the Lua binding surface from the code-derived class surface.

Input is bindings/code-derived-surface.json: the per-class surface recovered
from isaac-ng.exe by reading the game's own registration code (see
tools/probe_class_attribution.py and docs/binding-surface-findings.md §16).
Every class name, method name, property name and metatable key in that file
came out of the shipped binary, and each entry carries the address of the
closure body that implements it, so the file doubles as the provenance record
for the surface.

The older bindings/recovered-binding-surface.json is kept as the record of the
first, adjacency-derived attempt: its class buckets were inferred from where
names sat in .rdata, which put the game's global namespace ("Isaac") and the
Game class in the same bucket and mixed unrelated names together. It is not
read by this generator any more; see docs/HANDOFF.md.

Two artifacts come out of here:

  bindings/isaac-api-spec.json   canonical, human-readable, for tooling
  include/isaec/api/generated/isaac_api_spec.hpp   compiled into isaec_lua

The header form exists so the build never needs a JSON parser at runtime.
Signatures are not recovered yet, so argc and returns are 0 and documented is
false throughout; the body addresses travel as strings because they are
provenance, not something the runtime can call yet. Filling in the signatures
is a separate pass over the disassembly (see issue #3).
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SOURCE_JSON = os.path.join(ROOT, "bindings", "code-derived-surface.json")
SPEC_JSON = os.path.join(ROOT, "bindings", "isaac-api-spec.json")
SPEC_HEADER = os.path.join(
    ROOT, "include", "isaec", "api", "generated", "isaac_api_spec.hpp"
)

# Inheritance. The binary does not store the parent *name*: each class
# registrar stores the parent as a Lua table fetched from the stack
# (lua_absindex(-6) / (-4)), and no parent name literal exists in the
# registration function, so the hierarchy is not recoverable from the binary
# (docs/binding-surface-findings.md §16.4). The map below is therefore
# curated, not recovered: it lists the eight classes the game documents as
# deriving from Entity. Classes absent from the map are registered without a
# parent.
PARENTS = {
    "EntityPlayer": "Entity",
    "EntityPickup": "Entity",
    "EntityFamiliar": "Entity",
    "EntityProjectile": "Entity",
    "EntityBomb": "Entity",
    "EntityLaser": "Entity",
    "EntityKnife": "Entity",
    "EntityTear": "Entity",
}


def cxx_escape(name):
    out = []
    for ch in name:
        if ch.isalnum() or ch == "_":
            out.append(ch)
        else:
            out.append("\\x%02x" % ord(ch))
    return "".join(out)


def body_of(entry):
    """The closure body address of a surface entry, as a string."""
    if not isinstance(entry, dict):
        return ""
    return entry.get("body") or ""


def collect(raw):
    """Turn the surface file into the class list the generator emits."""
    classes = []
    namespaces = []

    def bucket(name, data, is_namespace):
        methods = []
        for m in data.get("methods", []):
            methods.append({"name": m["name"], "body": body_of(m)})
        properties = []
        for p in data.get("properties", []):
            properties.append(
                {
                    "name": p["name"],
                    "getter": p.get("getter") or "",
                    "setter": p.get("setter") or "",
                }
            )
        metamethods = []
        for mm in data.get("metamethods", []):
            metamethods.append({"name": mm["name"], "body": body_of(mm)})
        entry = {
            "name": name,
            "parent": PARENTS.get(name, ""),
            "methods": methods,
            "properties": properties,
            "metamethods": metamethods,
        }
        if is_namespace:
            entry["kind"] = "namespace"
            namespaces.append(entry)
        else:
            entry["kind"] = "class"
            classes.append(entry)

    for name, data in sorted(raw.get("classes", {}).items()):
        bucket(name, data, False)
    for name, data in sorted(raw.get("namespaces", {}).items()):
        bucket(name, data, True)

    return classes, namespaces


def emit_json(source, classes, namespaces):
    def cls_json(c):
        return {
            "name": c["name"],
            "kind": c["kind"],
            "parent": c["parent"],
            "methods": [
                {"name": m["name"], "argc": 0, "returns": 0, "documented": False}
                for m in c["methods"]
            ],
            "properties": [
                {"name": p["name"], "getter": p["getter"], "setter": p["setter"]}
                for p in c["properties"]
            ],
            "metamethods": c["metamethods"],
        }

    spec = {
        "source": source,
        "count": sum(len(c["methods"]) for c in classes + namespaces),
        "classes": [cls_json(c) for c in classes + namespaces],
        "namespaces": [c["name"] for c in namespaces],
    }
    os.makedirs(os.path.dirname(SPEC_JSON), exist_ok=True)
    with open(SPEC_JSON, "w") as fh:
        json.dump(spec, fh, indent=1, sort_keys=True)
        fh.write("\n")


def emit_header(source, classes, namespaces):
    lines = [
        "// Generated by tools/gen-api-spec.py. Do not edit.",
        "//",
        "// Source: " + source,
        "//",
        "// Every name below was recovered from isaac-ng.exe's own registration",
        "// code. Signatures are not recovered yet, so argc and returns are 0 and",
        "// documented is false. Property bodies are carried as address strings:",
        "// they are provenance, not callable code yet.",
        "",
        "#ifndef ISAEC_API_GENERATED_ISAAC_API_SPEC_HPP",
        "#define ISAEC_API_GENERATED_ISAAC_API_SPEC_HPP",
        "",
        "#include <cstddef>",
        "#include <vector>",
        "",
        '#include "isaec/api/registry.hpp"',
        "",
        "namespace isaec::api::generated {",
        "",
        "inline std::vector<ClassSpec> isaac_api_spec() {",
        "    return {",
    ]
    for cls in classes + namespaces:
        lines.append("        ClassSpec{")
        lines.append('            "%s",' % cxx_escape(cls["name"]))
        lines.append('            "%s",' % cxx_escape(cls["parent"]))
        if cls["methods"]:
            lines.append("            {")
            for m in cls["methods"]:
                lines.append('                MethodSpec{"%s"},' % cxx_escape(m["name"]))
            lines.append("            },")
        else:
            lines.append("            {},")
        if cls["properties"]:
            lines.append("            {")
            for p in cls["properties"]:
                lines.append(
                    '                PropertySpec{"%s", "%s", "%s"},'
                    % (
                        cxx_escape(p["name"]),
                        cxx_escape(p["getter"]),
                        cxx_escape(p["setter"]),
                    )
                )
            lines.append("            },")
        else:
            lines.append("            {},")
        lines.append("        },")
    lines += [
        "    };",
        "}",
        "",
        "}  // namespace isaec::api::generated",
        "",
        "#endif  // ISAEC_API_GENERATED_ISAAC_API_SPEC_HPP",
        "",
    ]
    os.makedirs(os.path.dirname(SPEC_HEADER), exist_ok=True)
    with open(SPEC_HEADER, "w") as fh:
        fh.write("\n".join(lines))


def main():
    if not os.path.exists(SOURCE_JSON):
        sys.stderr.write("missing recovered binding data: %s\n" % SOURCE_JSON)
        return 1
    with open(SOURCE_JSON) as fh:
        raw = json.load(fh)

    classes, namespaces = collect(raw)
    source = "isaac-ng.exe registration code (0x00866960 master function)"
    emit_json(source, classes, namespaces)
    emit_header(source, classes, namespaces)

    total = sum(len(c["methods"]) for c in classes + namespaces)
    props = sum(len(c["properties"]) for c in classes + namespaces)
    print(
        "generated %d classes / %d namespaces / %d methods / %d properties"
        % (len(classes), len(namespaces), total, props)
    )
    print("  " + SPEC_JSON)
    print("  " + SPEC_HEADER)
    return 0


if __name__ == "__main__":
    sys.exit(main())
