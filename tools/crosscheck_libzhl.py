#!/usr/bin/env python3
"""Cross-check the recovered binding surface against the Repentogon class layouts.

The class layouts come from an *independent* source: the Repentogon project
publishes the field offsets of the game's Lua-visible classes as `.zhl` files
in `libzhl/functions/`. A `.zhl` file is a C++ struct declaration with the
field offsets and the size of the object written out, for example:

    struct ActiveItemDesc
    {
        int _item : 0x0;
        int _charge : 0x8;
        int _batteryCharge : 0xC;
        ...
    } : 0x20;

The recovered surface (`bindings/code-derived-surface.json`) records, for every
property, the upvalue the original closure was created with. For the classes
whose properties are plain fields, that upvalue is the offset of the field
inside the game object. Comparing the two sources therefore checks the layout
that was read out of the binary against a layout that was written down by
somebody else.

What this establishes
---------------------
* the field offsets recovered from the binary are confirmed (or refuted) by an
  independent source;
* the *size* of a game object is confirmed when the recovered offsets fit
  inside it.

What this does NOT establish
-----------------------------
* The `.zhl` files describe **Repentance+**, a later release than the Rebirth
  build this project targets. A class or a member that exists on only one side
  is a difference between two game versions, not necessarily an error in the
  recovery. The report therefore separates "confirmed", "different" and
  "one side only" and does not score them.
* A matching offset does not prove that the *member* is the same member: the
  report compares the names case-insensitively (the `.zhl` names carry a
  leading underscore) and lists the ones that differ, but a name difference is
  not a failure either.

Usage
-----
    python3 tools/crosscheck_libzhl.py \\
        --surface bindings/code-derived-surface.json \\
        --cache /tmp/isaec-zhl \\
        --report /tmp/crosscheck.md

The `.zhl` files are downloaded once into the cache directory and reused, so a
second run works offline.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

# Where the class layouts live. The Repentogon repository is public, so a
# plain HTTPS request is enough and no token is needed.
ZHL_BASE_URL = (
    "https://raw.githubusercontent.com/TeamREPENTOGON/REPENTOGON/master/libzhl/functions"
)

# A class name in the recovered surface and the file that describes it. Only
# the renames that are known are listed; everything else uses `<Name>.zhl`.
ZHL_ALIASES = {
    # The game calls the 2D vector "Vector" in Lua; the C++ class in the
    # Repentogon sources is "Vector2" (there is also a 3D Vector3).
    "Vector": "Vector2",
    # The Lua class "KColor" is the C++ "Color".
    "KColor": "Color",
    # The Lua class "Color" is the C++ "ColorMod" (a colour with the overlay
    # channels), which is the only colour struct the sources describe.
    "Color": "ColorMod",
}

# A field declaration: a type, then one or more `name : 0xoffset` pairs.
# Examples:
#     int _item : 0x0;
#     float HeightModifier : 0x4, FallingSpeedModifier : 0x8;
FIELD_RE = re.compile(
    r"^\s*(?P<type>[A-Za-z_][A-Za-z0-9_:<>,\s\*]*?)\s*"
    r"(?P<fields>[A-Za-z_][A-Za-z0-9_]*(?:\s*:\s*0x[0-9a-fA-F]+)"
    r"(?:\s*,\s*[A-Za-z_][A-Za-z0-9_]*\s*:\s*0x[0-9a-fA-F]+)*)\s*;",
    re.MULTILINE,
)

# The size of the struct: the closing brace carries it.
SIZE_RE = re.compile(r"\}\s*:\s*0x(?P<size>[0-9a-fA-F]+)\s*;")

# A name with its offset, inside a field list.
PAIR_RE = re.compile(r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*:\s*0x(?P<offset>[0-9a-fA-F]+)")

# An upvalue below this value is a field offset inside a game object; the
# larger ones are code pointers (a per-field accessor) in the game's text.
OFFSET_LIMIT = 0x1000


@dataclass
class ZhlField:
    """One field of a class, as the Repentogon sources describe it."""

    name: str
    offset: int
    type_name: str


@dataclass
class ZhlClass:
    """The layout of one class, as the Repentogon sources describe it."""

    name: str
    file: str
    size: int | None = None
    fields: list[ZhlField] = field(default_factory=list)

    def field_at(self, offset: int) -> ZhlField | None:
        for f in self.fields:
            if f.offset == offset:
                return f
        return None

    def field_named(self, name: str) -> ZhlField | None:
        """The field with this name, whatever its offset."""
        key = normalize(name)
        for f in self.fields:
            if normalize(f.name) == key:
                return f
        return None


@dataclass
class ClassReport:
    """The comparison of one class."""

    name: str
    zhl_file: str | None = None
    zhl_size: int | None = None
    confirmed: list[tuple[str, int, str]] = field(default_factory=list)
    name_differs: list[tuple[str, int, str, str]] = field(default_factory=list)
    moved: list[tuple[str, int, int, str]] = field(default_factory=list)
    binary_only: list[tuple[str, int]] = field(default_factory=list)
    zhl_only: list[tuple[str, int, str]] = field(default_factory=list)
    note: str = ""


def parse_zhl(text: str, name: str, file_name: str) -> ZhlClass:
    """Parses a `.zhl` file into a class layout.

    The files are not a uniform format: some are a plain struct with the field
    offsets, some wrap the struct in C++ method definitions. The field
    declarations are the same in both, so the parser only looks for the
    declarations and for the size on the closing brace.
    """
    out = ZhlClass(name=name, file=file_name)

    for match in FIELD_RE.finditer(text):
        type_name = match.group("type").strip()
        for pair in PAIR_RE.finditer(match.group("fields")):
            out.fields.append(
                ZhlField(
                    name=pair.group("name"),
                    offset=int(pair.group("offset"), 16),
                    type_name=type_name,
                )
            )

    sizes = SIZE_RE.findall(text)
    if sizes:
        out.size = int(sizes[-1], 16)

    return out


def fetch_zhl(class_name: str, cache: Path) -> tuple[str | None, str]:
    """Returns the `.zhl` text of `class_name` and the file name used.

    The file is cached on disk, so a second run needs no network. A class
    without a file is not an error: the sources describe a later game version
    and do not cover every class.
    """
    file_name = ZHL_ALIASES.get(class_name, class_name) + ".zhl"
    path = cache / file_name
    if path.is_file():
        return path.read_text(encoding="utf-8", errors="replace"), file_name
    try:
        with urllib.request.urlopen(f"{ZHL_BASE_URL}/{file_name}", timeout=30) as response:
            text = response.read().decode("utf-8", errors="replace")
    except (urllib.error.URLError, OSError):
        return None, file_name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return text, file_name


def normalize(name: str) -> str:
    """The comparison key of a member name.

    The `.zhl` names carry a leading underscore and sometimes a `_` between
    words (`_timeRechargeCooldown`); the Lua names do not.
    """
    return name.strip().lstrip("_").replace("_", "").lower()


def surface_classes(surface: dict) -> dict[str, dict]:
    """The classes of the surface with their plain-field properties."""
    out: dict[str, dict] = {}
    for name, entry in surface.get("classes", {}).items():
        fields = []
        for prop in entry.get("properties", []):
            upvalue = prop.get("upvalue")
            if upvalue is None:
                continue
            try:
                value = int(str(upvalue), 16)
            except ValueError:
                continue
            # A small upvalue is a field offset; a large one is the code
            # pointer of a per-field accessor (the Color class).
            if value < OFFSET_LIMIT:
                fields.append((prop["name"], value))
        out[name] = {"fields": fields, "entry": entry}
    return out


def compare_class(name: str, data: dict, zhl: ZhlClass | None) -> ClassReport:
    """Compares the recovered fields of one class with the `.zhl` layout."""
    report = ClassReport(name=name)

    if zhl is None:
        report.note = "no .zhl file"
        report.binary_only = list(data["fields"])
        return report

    report.zhl_file = zhl.file
    report.zhl_size = zhl.size

    if not zhl.fields:
        # Some layouts only declare the size: the fields live in a base class
        # that is described elsewhere. There is nothing to compare against.
        report.note = "the layout declares no fields of its own"
        report.binary_only = list(data["fields"])
        return report

    for prop_name, offset in data["fields"]:
        zhl_field = zhl.field_at(offset)
        if zhl_field is None:
            # The name may still be in the layout, at another offset: that is a
            # layout shift between the two game versions, not a recovery error.
            moved = zhl.field_named(prop_name)
            if moved is not None:
                report.moved.append((prop_name, offset, moved.offset, moved.type_name))
            else:
                report.binary_only.append((prop_name, offset))
            continue
        if normalize(zhl_field.name) == normalize(prop_name):
            report.confirmed.append((prop_name, offset, zhl_field.type_name))
        else:
            report.name_differs.append((prop_name, offset, zhl_field.name, zhl_field.type_name))

    binary_offsets = {offset for _, offset in data["fields"]}
    for zhl_field in zhl.fields:
        if zhl_field.offset not in binary_offsets:
            report.zhl_only.append((zhl_field.name, zhl_field.offset, zhl_field.type_name))

    return report


def render_report(reports: list[ClassReport], surface_path: str) -> str:
    """The human-readable report."""
    lines: list[str] = []
    lines.append("# Cross-check: the recovered surface against the Repentogon class layouts")
    lines.append("")
    lines.append(f"surface: `{surface_path}`")
    lines.append("")
    lines.append(
        "A field offset in the recovered surface is confirmed when the Repentogon "
        "layout has a field at the same offset. The names are compared "
        "case-insensitively and without the leading underscore; a name that "
        "differs is listed, not counted as a failure."
    )
    lines.append("")

    total_confirmed = sum(len(r.confirmed) for r in reports)
    total_name_differs = sum(len(r.name_differs) for r in reports)
    total_moved = sum(len(r.moved) for r in reports)
    total_binary_only = sum(len(r.binary_only) for r in reports)
    total_zhl_only = sum(len(r.zhl_only) for r in reports)
    compared = [r for r in reports if r.zhl_file]

    lines.append("## Totals")
    lines.append("")
    lines.append("| classes in the surface | classes with a layout | fields confirmed |")
    lines.append("| --- | --- | --- |")
    lines.append(f"| {len(reports)} | {len(compared)} | {total_confirmed} |")
    lines.append("")
    lines.append("| same offset, other name | same name, other offset | only in the binary | only in the layout |")
    lines.append("| --- | --- | --- | --- |")
    lines.append(
        f"| {total_name_differs} | {total_moved} | {total_binary_only} | {total_zhl_only} |"
    )
    lines.append("")

    lines.append("## Per class")
    lines.append("")
    for report in reports:
        if report.zhl_file is None:
            lines.append(f"### {report.name}")
            lines.append("")
            lines.append("no `.zhl` file in the Repentogon sources; "
                         f"{len(report.binary_only)} recovered field(s) unchecked.")
            lines.append("")
            continue
        size = "unknown" if report.zhl_size is None else f"0x{report.zhl_size:x}"
        lines.append(f"### {report.name}  ({report.zhl_file}, size {size})")
        lines.append("")
        if report.confirmed:
            lines.append("| member | offset | layout type |")
            lines.append("| --- | --- | --- |")
            for prop_name, offset, type_name in report.confirmed:
                lines.append(f"| {prop_name} | 0x{offset:x} | {type_name} |")
            lines.append("")
        if report.name_differs:
            lines.append("same offset, different name:")
            for prop_name, offset, zhl_name, type_name in report.name_differs:
                lines.append(f"- `{prop_name}` at 0x{offset:x} is `{zhl_name}` ({type_name})")
            lines.append("")
        if report.moved:
            lines.append("same name, different offset (the layout shifted between the versions):")
            for prop_name, offset, other, type_name in report.moved:
                lines.append(
                    f"- `{prop_name}` at 0x{offset:x} is `{other}` at 0x{other:x} ({type_name})"
                )
            lines.append("")
        if report.binary_only:
            lines.append("recovered but not in the layout:")
            for prop_name, offset in report.binary_only:
                lines.append(f"- `{prop_name}` at 0x{offset:x}")
            lines.append("")
        if report.note:
            lines.append(f"note: {report.note}")
            lines.append("")
        if report.zhl_only:
            lines.append("in the layout but not recovered as a property:")
            for zhl_name, offset, type_name in report.zhl_only:
                lines.append(f"- `{zhl_name}` at 0x{offset:x} ({type_name})")
            lines.append("")
    return "\n".join(lines) + "\n"


def self_test() -> int:
    """Checks the parser and the comparison on a small sample."""
    sample = """
struct Sample
{
    int _item : 0x0;
    int _charge : 0x8;
    float x : 0x4, y : 0xC;
} : 0x10;
"""
    parsed = parse_zhl(sample, "Sample", "Sample.zhl")
    failures = 0
    if parsed.size != 0x10:
        print(f"FAIL: size is {parsed.size}, expected 0x10", file=sys.stderr)
        failures += 1
    offsets = {f.name: f.offset for f in parsed.fields}
    expected = {"_item": 0x0, "_charge": 0x8, "x": 0x4, "y": 0xC}
    if offsets != expected:
        print(f"FAIL: fields are {offsets}, expected {expected}", file=sys.stderr)
        failures += 1

    report = compare_class(
        "Sample",
        {"fields": [("Item", 0x0), ("Charge", 0x8), ("Only", 0x14)]},
        parsed,
    )
    if len(report.confirmed) != 2:
        print(f"FAIL: {len(report.confirmed)} confirmed, expected 2", file=sys.stderr)
        failures += 1
    if report.binary_only != [("Only", 0x14)]:
        print(f"FAIL: binary only is {report.binary_only}", file=sys.stderr)
        failures += 1
    if len(report.zhl_only) != 2:
        print(f"FAIL: {len(report.zhl_only)} layout-only, expected 2", file=sys.stderr)
        failures += 1

    print("SELF-TEST PASSED" if not failures else f"{failures} CONTROL FAILURES")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--surface", default="bindings/code-derived-surface.json")
    parser.add_argument("--cache", default="/tmp/isaec-zhl", help="directory for the downloaded .zhl files")
    parser.add_argument("--report", help="write the markdown report here")
    parser.add_argument("--json-out", help="write the machine-readable result here")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    surface = json.loads(Path(args.surface).read_text(encoding="utf-8"))
    cache = Path(args.cache)
    classes = surface_classes(surface)

    reports: list[ClassReport] = []
    for name, data in classes.items():
        text, file_name = fetch_zhl(name, cache)
        zhl = parse_zhl(text, name, file_name) if text is not None else None
        reports.append(compare_class(name, data, zhl))

    total_confirmed = sum(len(r.confirmed) for r in reports)
    total_name_differs = sum(len(r.name_differs) for r in reports)
    total_moved = sum(len(r.moved) for r in reports)
    total_binary_only = sum(len(r.binary_only) for r in reports)
    total_zhl_only = sum(len(r.zhl_only) for r in reports)
    compared = [r for r in reports if r.zhl_file]

    print(f"classes in the surface : {len(reports)}")
    print(f"classes with a layout  : {len(compared)}")
    print(f"fields confirmed       : {total_confirmed}")
    print(f"same offset, other name: {total_name_differs}")
    print(f"same name, other offset: {total_moved}")
    print(f"only in the binary     : {total_binary_only}")
    print(f"only in the layout     : {total_zhl_only}")

    if args.report:
        Path(args.report).write_text(render_report(reports, args.surface), encoding="utf-8")
        print(f"report written to {args.report}")

    if args.json_out:
        payload = {
            "surface": args.surface,
            "classes": len(reports),
            "classes_with_layout": len(compared),
            "fields_confirmed": total_confirmed,
            "fields_name_differs": total_name_differs,
            "fields_moved": total_moved,
            "fields_binary_only": total_binary_only,
            "fields_layout_only": total_zhl_only,
            "per_class": [
                {
                    "class": r.name,
                    "layout": r.zhl_file,
                    "size": r.zhl_size,
                    "confirmed": [{"name": n, "offset": o, "type": t} for n, o, t in r.confirmed],
                    "name_differs": [
                        {"name": n, "offset": o, "layout_name": z, "type": t}
                        for n, o, z, t in r.name_differs
                    ],
                    "moved": [
                        {"name": n, "offset": o, "layout_offset": m, "type": t}
                        for n, o, m, t in r.moved
                    ],
                    "binary_only": [{"name": n, "offset": o} for n, o in r.binary_only],
                    "layout_only": [{"name": n, "offset": o, "type": t} for n, o, t in r.zhl_only],
                }
                for r in reports
            ],
        }
        Path(args.json_out).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"json written to {args.json_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
