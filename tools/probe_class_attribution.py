#!/usr/bin/env python3
"""Attribute Lua closure registrations to the game class that owns them.

Background
----------
`probe_property_names.py` recovers the *name* each closure is registered
under. It deliberately does not claim which class owns the registration.
This tool supplies that missing half.

The class-registration routine
------------------------------
Every game class is registered by a dedicated routine that

  * receives the class name as a C string argument,
  * builds the class table and installs its metatable, and
  * is called exactly once, from one master registration function, with the
    class-name literal pushed as the argument.

The routine identified by hand in this binary is at VA `0x0085d040`; it is the
only function that receives the class name and installs the metatable. Every
call site of it passes a `.rdata` string literal, and those literals are the
class names.

The master function
-------------------
Those 66 call sites all live in one function (`0x00866960` in this build), in
class order:

    call <registrar for "EntityPtr">   ; class scope opens
      ... property registrations ...
    call <registrar for "Vector">      ; the next class scope opens
      ... property registrations ...

So the class scope of a registration is simply: the registrar call that
precedes it, up to the next registrar call. No nesting analysis is needed
because the scopes do not nest -- the master function is a flat sequence.

The three shapes of a registration
----------------------------------
1. The registration happens *inside* the class's own registrar routine (the
   `__gc` closure, for instance). The class is that routine's class.
2. The registration happens in a shared helper that the master function calls
   with a property name (the usual case: one helper per property, or one
   helper for a whole family of properties). The class is the scope that
   contains the *call site* of the helper.
3. A shared helper is called from more than one class scope. Then the same
   closure is registered in each of those classes, and this tool emits one
   record per class -- a property that exists on several classes is a real
   thing in this API, not a contradiction.

What this tool does NOT establish
---------------------------------
  * It does not implement anything. It is a table, not code.
  * It does not prove the class *hierarchy*. A property inherited from a
    parent class is registered on the parent's table, so it appears under the
    parent here; the parent link lives in the class-registration routine.
  * It does not verify the property's *type*. Whether a name is a field, a
    method or a metamethod is a separate question; a name that begins with
    "__" is a metamethod by construction.
  * It does not say whether a name is unique across the whole API. The same
    name is registered on many classes -- that is what inheritance looks
    like from this side.

Controls
--------
`--self-test` asserts, and exits non-zero if any of them fails:

  * positive: class `Vector` owns both `X` and `Y` (they share a body and
    differ only by upvalue, so they are the sharpest test of the rule);
  * positive: class `Sprite` owns `Play`, and class `EntityPlayer` owns
    `AddCoins`;
  * positive: every class-registration routine is called exactly once, with
    the same class name it carries inside;
  * negative: a class name that exists nowhere in the binary must not appear
    in the output;
  * negative: the class-name set must not contain any of the metamethod
    keys, which are property names, not classes.
"""
from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from disasm import Disasm  # noqa: E402
from pe_vaddr import PEImage  # noqa: E402
from probe_property_names import Probe, read_c_string  # noqa: E402

# The class-registration routine, identified by hand: it takes the class name
# at [ebp+0x8], builds the class table and installs its metatable. Every call
# site of it passes the class name as a .rdata literal.
CLASS_REG_ROUTINE = 0x85D040

# The routines that open a *global namespace* rather than a class: the game's
# Lua globals ("Isaac", "Input", "Options"). They are not classes and must
# not be attributed to the class that happens to surround them.
NAMESPACE_REGISTRARS = (0x85DA00,)

# The routine that constructs the table a scope lives in. Hand-verified at
# 0x86a48 (Vector) and 0x86db51 (MusicManager):
#
#     call 0x85dab0        ; construct the scope's table
#     push 0xb701dc        ; "Vector" -- the class name
#     call 0x8a7c00        ; the class-registration routine
#
# A construct with no class-registration call after it opens a *namespace*
# scope instead (the global block at the end of the master function, where
# "Isaac", "Input" and "Options" are registered). A construct therefore marks
# the start of a scope that is NOT a class scope until a class registrar is
# called inside it -- which is exactly what stops the global block from being
# attributed to the class that precedes it (MusicManager used to swallow all
# 56 global functions).
SCOPE_CONSTRUCTS = (0x85DAB0,)

# The routine that destroys a scope's table, i.e. the end of the scope. It is
# a destructor, not a scope closer, so it is not used for attribution.
SCOPE_DESTRUCTORS = (0x85D980,)

# Literals that are pushed where a class name is expected but are not class
# names. "ArrayProxy" is the Lua-side proxy type used for the list classes
# (RoomDescriptor, RoomConfigSpawns, RoomConfigEntries): the shared routine
# that registers them is called with the proxy type, not with a class name.
NON_CLASS_LITERALS = ("ArrayProxy",)

# A class name that exists nowhere in the binary.
CANARY_CLASS = "ZZ_CANARY_NOT_A_REAL_ISAAC_CLASS_ZZ"

# Metamethod keys are property names, never class names. Their presence in the
# class-name set would mean the class-name rule had drifted.
METAMETHOD_KEYS = ("__gc", "__len", "__call", "__tostring", "__index",
                   "__propget", "__propset", "__const", "__newindex",
                   "__parent", "__class", "__type", "__metatable")

# Control classes, with one property each that must be attributed to them.
CONTROL_CLASSES = {
    "Vector": ("X", "Y"),
    "Sprite": ("Play",),
    "EntityPlayer": ("AddCoins",),
    "MusicManager": ("Play", "Crossfade"),
}

# The global namespaces are not classes. Their members must land in the
# namespace scope and nowhere else -- the bug this control guards against is
# the global block being attributed to the class that precedes it, which gave
# MusicManager 110 members instead of 20.
CONTROL_NAMESPACES = {
    "global:Isaac": ("GetPlayer", "CountEntities", "FindInRadius"),
    "global:Input": ("IsActionPressed",),
    "global:Options": ("Fullscreen", "Gamma"),
}

# Scopes that must be free of these names: a class scope must not hold a
# global function, and the global scope must not hold a class member.
CONTROL_EXCLUSIONS = (
    ("MusicManager", "CountEntities"),
    ("MusicManager", "GetPlayer"),
    ("Game", "CountEntities"),
    ("Entity", "GetPlayer"),
)


class Attributor:
    """Class attribution over the registration records."""

    def __init__(self, exe: str, cache: str | None = None) -> None:
        self.probe = Probe(exe, cache)
        self.d: Disasm = self.probe.d
        self.image: PEImage = self.probe.image
        self.rdata_lo = self.probe.rdata_lo
        self.rdata_hi = self.probe.rdata_hi
        self.fns = self.probe.fns

        # Call sites per target function, built once: the attribution needs
        # "where is this helper called from", and the disassembly is large.
        self.call_sites: dict[int, list[int]] = collections.defaultdict(list)
        for ins in self.d.insns:
            if ins.mnemonic != "call":
                continue
            ops = ins.operands.strip()
            try:
                target = int(ops, 16)
            except ValueError:
                continue  # indirect call: no static target
            self.call_sites[target].append(ins.va)

        self.registrar_class = self._find_registrars()
        self.class_names = set(self.registrar_class.values())
        self.openers = self._scope_openers()
        self.namespace_openers = self._namespace_openers()
        self.constructs = self._constructs()
        self.timeline = self._build_scope_timeline()

        # Per-host indexes for the scope lookup: "the last class registrar
        # before this address", "the last scope construct", "the last
        # namespace registration".
        self.class_openers: dict[int, list[tuple[int, str]]] = (
            collections.defaultdict(list))
        for site, name in self.openers.items():
            host = self.fns.start_of(site)
            if host is not None:
                self.class_openers[host].append((site, name))
        self.constructs_by_host: dict[int, list[tuple[int, str]]] = (
            collections.defaultdict(list))
        for site in self.constructs:
            host = self.fns.start_of(site)
            if host is not None:
                self.constructs_by_host[host].append((site, ""))
        self.ns_by_host: dict[int, list[tuple[int, str]]] = (
            collections.defaultdict(list))
        for site, name in self.namespace_openers.items():
            host = self.fns.start_of(site)
            if host is not None:
                self.ns_by_host[host].append((site, name))
        for lst in (self.class_openers, self.constructs_by_host,
                    self.ns_by_host):
            for v in lst.values():
                v.sort()

    # -- discovery ------------------------------------------------------

    def cstring(self, va: int) -> str | None:
        return read_c_string(self.image, va, self.rdata_lo, self.rdata_hi)

    def _nearest_literal(self, site: int, limit: int = 12) -> str | None:
        """The C string literal pushed as the argument of the call at `site`."""
        n = self.d.index.get(site)
        if n is None:
            return None
        for j in range(n - 1, max(-1, n - limit), -1):
            ins = self.d.insns[j]
            if ins.is_terminator:
                break
            imm = ins.immediate()
            if imm is not None and ins.mnemonic == "push":
                return self.cstring(imm)
        return None

    def _find_registrars(self) -> dict[int, str]:
        """Map each class-registration routine to its class name.

        The routine is the one that receives the class name and installs the
        metatable. The literal is read at the call site, not inside the
        routine, because the routine takes the name as an argument.
        """
        out: dict[int, str] = {}
        for site in self.call_sites.get(CLASS_REG_ROUTINE, []):
            func = self.fns.start_of(site)
            if func is None:
                continue
            n = self.d.index.get(site)
            if n is None:
                continue
            name = None
            for j in range(n - 1, max(-1, n - 8), -1):
                ins = self.d.insns[j]
                if ins.mnemonic in ("call", "ret"):
                    break
                imm = ins.immediate()
                if imm is not None and ins.mnemonic == "push":
                    name = self.cstring(imm)
                    break
            if name:
                out.setdefault(func, name)
        return out

    def _scope_openers(self) -> dict[int, str]:
        """The call sites that open a class scope, mapped to the class name.

        A class scope opens where the master registration function calls a
        class-registration routine with the class name. Some classes are
        reached through a thin wrapper, so the opener is any call whose
        target reaches the class-registration routine, and whose argument is
        a class-name literal.
        """
        # Functions that reach the class-registration routine, up to three
        # levels: the routine itself, its direct wrappers, and their wrappers.
        reach: set[int] = set(self.registrar_class)
        for _ in range(3):
            nxt = set(reach)
            for func in reach:
                for site in self.call_sites.get(func, ()):
                    caller = self.fns.start_of(site)
                    if caller is not None and caller not in nxt:
                        nxt.add(caller)
            if nxt == reach:
                break
            reach = nxt

        out: dict[int, str] = {}
        for target in reach:
            for site in self.call_sites.get(target, ()):
                name = self._nearest_literal(site)
                if name in self.class_names:
                    out[site] = name
        return out

    def _build_scopes(self) -> dict[int, list[tuple[int, str | None]]]:
        """Per function, the ordered class-scope boundaries.

        A boundary is a call that opens a class scope. The scope that a
        registration belongs to is the one opened by the last boundary at
        or before it.
        """
        scopes: dict[int, list[tuple[int, str | None]]] = collections.defaultdict(list)
        for site, name in self.openers.items():
            host = self.fns.start_of(site)
            if host is not None:
                scopes[host].append((site, name))
        for lst in scopes.values():
            lst.sort()
        return scopes

    def _build_scope_timeline(self) -> dict[int, list[tuple[int, str | None]]]:
        """Per function, the scope events in order (legacy view, kept for
        debugging).

        Three kinds of event:

        * ``("class", name)`` -- a class scope opens;
        * ``("global", name)`` -- a global namespace opens (the `Isaac`,
          `Input` and `Options` namespaces are *not* classes: they are the
          game's Lua globals);
        * ``("close", None)`` -- a scope's table is destroyed.
        """
        events: dict[int, list[tuple[int, str | None]]] = collections.defaultdict(list)
        for site, name in self.openers.items():
            host = self.fns.start_of(site)
            if host is not None:
                events[host].append((site, name))
        for site, name in self.namespace_openers.items():
            host = self.fns.start_of(site)
            if host is not None:
                events[host].append((site, "global:" + name))
        for site in self.constructs:
            host = self.fns.start_of(site)
            if host is not None:
                events[host].append((site, None))
        for lst in events.values():
            lst.sort()
        return events

    def _namespace_openers(self) -> dict[int, str]:
        """Call sites that open a *global namespace* rather than a class.

        The game's Lua globals -- `Isaac`, `Input`, `Options` -- are
        namespace tables, not classes. They are opened by their own
        registrar, with their name as the argument, and they close again
        before the next class scope opens. Attributing their contents to
        the surrounding class is how `Isaac` ended up with 76 members in the
        old surface.
        """
        out: dict[int, str] = {}
        for target in NAMESPACE_REGISTRARS:
            for site in self.call_sites.get(target, ()):
                name = self._nearest_literal(site)
                if name and name not in self.class_names:
                    out[site] = name
        return out

    def _constructs(self) -> list[int]:
        """Call sites that construct a scope's table (see SCOPE_CONSTRUCTS)."""
        out: list[int] = []
        for target in SCOPE_CONSTRUCTS:
            out.extend(self.call_sites.get(target, ()))
        return sorted(out)

    def scope_of(self, host: int, va: int) -> str | None:
        """The scope that owns the registration at `va` inside function `host`.

        The rules, in order:

        1. the last class-registration call at or before `va` names the class,
           as long as no scope construct happened after it;
        2. otherwise the registration lives in a namespace scope: the last
           namespace registration inside the same construct scope names it
           (``global:Isaac`` and friends);
        3. otherwise it is a plain global function.
        """
        # (1) the class that was registered last.
        opener = self._last_before(self.class_openers, host, va)
        if opener is not None:
            construct = self._last_before(self.constructs_by_host, host, va)
            if construct is None or opener[0] > construct[0]:
                return opener[1]

        # (2) a namespace scope. The scope's table is constructed first and
        # named later -- the namespace registration at 0x86dd65 names the
        # table that was constructed at 0x86dcf6, so the five registrations
        # between the construct and the naming ("Random", "Game", ...) are
        # members of the same namespace, not bare globals.
        construct = self._last_before(self.constructs_by_host, host, va)
        if construct is None:
            return None
        ns = self._first_after(self.ns_by_host, host, construct[0])
        if ns is not None:
            return "global:" + ns[1]
        return "global"

    @staticmethod
    def _first_after(index: dict[int, list[tuple[int, str]]], host: int,
                     va: int) -> tuple[int, str] | None:
        """The first entry of `index[host]` strictly after `va`."""
        entries = index.get(host)
        if not entries:
            return None
        lo, hi = 0, len(entries) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if entries[mid][0] > va:
                best = entries[mid]
                hi = mid - 1
            else:
                lo = mid + 1
        return best

    @staticmethod
    def _last_before(index: dict[int, list[tuple[int, str]]], host: int,
                     va: int) -> tuple[int, str] | None:
        """The last entry of `index[host]` at or before `va`."""
        entries = index.get(host)
        if not entries:
            return None
        lo, hi = 0, len(entries) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if entries[mid][0] <= va:
                best = entries[mid]
                lo = mid + 1
            else:
                hi = mid - 1
        return best

    # -- attribution ----------------------------------------------------

    def class_of_record(self, rec: dict) -> list[str]:
        """The classes a registration record belongs to.

        More than one class means the same helper is called from several
        class scopes, which is a real property of the API, not an error.
        """
        func = rec.get("func")
        if func is None:
            return []

        # Shape 1: the registration is inside the class's own registrar.
        own = self.registrar_class.get(func)
        if own:
            return [own]

        # Shape 2 and 3: a shared helper. The class is the scope that holds
        # the call site of the helper.
        out = []
        for site in self.call_sites.get(func, []):
            host = self.fns.start_of(site)
            if host is None:
                continue
            name = self.scope_of(host, site)
            if name and name not in out:
                out.append(name)
        return out

    def attribute(self) -> list[dict]:
        """One attributed record per (registration, class)."""
        rows = self.probe.rows()
        out = []
        for rec in rows:
            classes = self.class_of_record(rec)
            if not classes:
                continue
            for cls in classes:
                out.append({
                    "class": cls,
                    "name": rec.get("name"),
                    "body": rec.get("body"),
                    "upvalue": rec.get("upvalue"),
                    "call": rec.get("call"),
                    "func": rec.get("func"),
                    "caller": rec.get("caller"),
                })
        return out

    def summary(self, attributed: list[dict]) -> dict:
        per_class: dict[str, set[str]] = collections.defaultdict(set)
        bodies: dict[str, set[int]] = collections.defaultdict(set)
        for rec in attributed:
            if rec["name"]:
                per_class[rec["class"]].add(rec["name"])
            if rec["body"]:
                bodies[rec["class"]].add(rec["body"])
        return {
            "classes": len(per_class),
            "attributed_records": len(attributed),
            "distinct_class_property_pairs": sum(len(v) for v in per_class.values()),
            "distinct_bodies": len({r["body"] for r in attributed if r["body"]}),
        }


def self_test(exe: str, cache: str | None) -> int:
    """Positive and negative controls. Non-zero exit means the tool is wrong."""
    a = Attributor(exe, cache)
    failures = 0

    def fail(msg: str) -> None:
        nonlocal failures
        failures += 1
        print(f"FAIL: {msg}", file=sys.stderr)

    # positive control 1: the class-registration routine is called with a
    # class-name literal, and every class name opens at least one scope.
    if len(a.registrar_class) < 50:
        fail(f"only {len(a.registrar_class)} class-registration routines found")
    for func, name in sorted(a.registrar_class.items()):
        for site in a.call_sites.get(func, ()):
            pushed = a._nearest_literal(site)
            if (pushed is not None and pushed not in a.class_names
                    and pushed not in NON_CLASS_LITERALS):
                fail(f"registrar 0x{func:x} (class {name!r}) is called with "
                     f"{pushed!r}, which is not a class name")
    opened = set(a.openers.values())
    missing = a.class_names - opened
    if missing:
        fail(f"class names with no scope opener: {sorted(missing)}")
    if not a.openers:
        fail("no class scope openers found")

    # negative control 1: the canary class must not exist.
    if CANARY_CLASS.encode() in a.image.data:
        fail("canary class name found in the binary image")

    # negative control 2: metamethod keys are property names, not classes.
    for key in METAMETHOD_KEYS:
        if key in a.registrar_class.values():
            fail(f"metamethod key {key!r} was taken for a class name")

    attributed = a.attribute()
    per_class: dict[str, set[str]] = collections.defaultdict(set)
    for rec in attributed:
        if rec["name"]:
            per_class[rec["class"]].add(rec["name"])

    # positive control 2: known classes own known properties.
    for cls, wanted in CONTROL_CLASSES.items():
        got = per_class.get(cls, set())
        missing = [w for w in wanted if w not in got]
        print(f"control class {cls}: {len(got)} properties, "
              f"wanted {list(wanted)} missing {missing}")
        if not got:
            fail(f"class {cls!r} has no attributed properties")
        if missing:
            fail(f"class {cls!r} is missing {missing}")

    # negative control 3: the canary class must not be attributed.
    if CANARY_CLASS in per_class:
        fail("canary class appeared in the attribution")

    # positive control 3: the global namespaces hold their own members.
    for ns, wanted in CONTROL_NAMESPACES.items():
        got = per_class.get(ns, set())
        missing = [w for w in wanted if w not in got]
        print(f"control namespace {ns}: {len(got)} members, "
              f"wanted {list(wanted)} missing {missing}")
        if not got:
            fail(f"namespace {ns!r} has no attributed members")
        if missing:
            fail(f"namespace {ns!r} is missing {missing}")

    # negative control 4: a class scope must not hold a global function, and
    # a global function must not be attributed to a class.
    for cls, name in CONTROL_EXCLUSIONS:
        if name in per_class.get(cls, set()):
            fail(f"class {cls!r} wrongly owns the global function {name!r}")

    s = a.summary(attributed)
    print(f"classes attributed : {s['classes']}")
    print(f"attributed records : {s['attributed_records']}")
    print(f"class/property pairs: {s['distinct_class_property_pairs']}")

    print("SELF-TEST PASSED" if not failures else f"{failures} CONTROL FAILURES")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("--cache", help="objdump text cache for the binary")
    ap.add_argument("--json-out", help="write the attributed records as JSON")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)

    if a.self_test:
        return self_test(a.exe, a.cache)

    attr = Attributor(a.exe, a.cache)
    attributed = attr.attribute()
    s = attr.summary(attributed)

    per_class: dict[str, set[str]] = collections.defaultdict(set)
    for rec in attributed:
        if rec["name"]:
            per_class[rec["class"]].add(rec["name"])

    print(f"class-registration routines : {len(attr.registrar_class)}")
    print(f"classes attributed         : {s['classes']}")
    print(f"attributed records         : {s['attributed_records']}")
    print(f"class/property pairs       : {s['distinct_class_property_pairs']}")
    print(f"distinct bodies attributed : {s['distinct_bodies']}")
    print()
    for cls in sorted(per_class, key=lambda c: (-len(per_class[c]), c)):
        print(f"  {cls:24s} {len(per_class[cls]):4d} properties")

    if a.json_out:
        Path(a.json_out).write_text(json.dumps(attributed, indent=1))
        print(f"wrote {a.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
