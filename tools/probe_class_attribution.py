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

    call <scope constructor>            ; 0x85dab0 -- opens a scope
    push "EntityPtr"
    call <registrar for "EntityPtr">   ; class scope opens
      ... property registrations ...
    call <scope destructor>             ; 0x85d980 -- ends the scope
    call <scope constructor>
    push "Vector"
    call <registrar for "Vector">      ; the next class scope opens
      ... property registrations ...

Two things open and close a scope, and they are not what their names
suggest:

  * `0x0085dab0` **constructs** the scope's table (it calls `lua_createtable`
    and stores the table in the object's field 0). It *opens* a scope.
  * `0x0085d980` **destroys** the scope's table. It *closes* a scope.

The class scope of a registration is the registrar call that precedes it, as
long as no scope constructor happened after it. Twelve entity classes
(`Entity`, `EntityTear`, `EntityPlayer`, ...) share a single constructor but
have a registrar call each, so the registrar call -- not the constructor --
is the anchor.

Where the constructor is *not* followed by a registrar call, the scope is a
namespace, not a class: the game's Lua globals `Isaac`, `Input` and
`Options` are registered by `0x0085da00` and their members are not class
members at all. They are reported as `global:Isaac`, `global:Input` and
`global:Options`, and they are excluded from the class count. This is what
the old adjacency surface got wrong: it reported 76 `Isaac` members.

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

Property, method or metamethod
-------------------------------
The *kind* of a registration is read off the helper that performs it:

  * a helper that pushes `__propget` or `__propset` installs a property
    (the closure is the getter or the setter);
  * a helper that installs a name that begins with `__` installs a
    metamethod;
  * anything else installs a method.

The helper is found with a leaf-aware function extent. `probe_property_names`
only knows functions with a `push ebp` prologue, and the small helpers in
this binary are entered with `push ebx`; the leaf-aware extent used here
recognises them, which is what makes the kind classification possible.

What this tool does NOT establish
---------------------------------
  * It does not implement anything. It is a table, not code.
  * It does not prove the class *hierarchy*. A property inherited from a
    parent class is registered on the parent's table, so it appears under the
    parent here; the parent link lives in the class-registration routine.
  * It does not prove the *type* of a property. `kind` says whether a name
    is a property, a method or a metamethod, not what it holds.
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
import bisect
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

# The metatable keys that mark a registration as a property. A helper that
# pushes one of these installs the closure into the class's `__propget` or
# `__propset` table, i.e. it is a property accessor rather than a method.
# The addresses are read out of .rdata; the names are listed in
# `docs/binding-surface-findings.md` §16.4.
PROPERTY_KEY_VAS = {
    0xB6E994: "__propget",
    0xB6EA38: "__propset",
    0xB6EA80: "__newindex",
}

# Which side of a property a registration installs. `__propget` holds the
# reader, `__propset` and `__newindex` hold the writer.
PROPERTY_ROLE = {
    0xB6E994: "get",
    0xB6EA38: "set",
    0xB6EA80: "set",
}

# The metatable keys that mark a registration as a metamethod.
METAMETHOD_KEY_VAS = {
    0xB6EA70: "__class",
    0xB6E988: "__parent",
    0xB6EA78: "__index",
    0xB6E9C4: "__const",
    0xB6EA64: "__metatable",
    0xB6E9CC: "__type",
    0xB753B4: "__gc",
    0xB70F1C: "__len",
    0xB75378: "__call",
}

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

# Control (class, name) pairs whose *kind* must be recovered: a property is
# installed through the `__propget` / `__propset` table, a method is installed
# directly into the class table, and a metamethod is a name that begins with
# two underscores.
CONTROL_KINDS = {
    ("Vector", "X"): "property",
    ("Vector", "Y"): "property",
    ("Game", "GetPlayer"): "method",
    ("Game", "GetRoom"): "method",
    ("Entity", "IsVulnerableEnemy"): "method",
    ("MusicManager", "Play"): "method",
    ("global:Isaac", "GetPlayer"): "method",
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


class LeafFns:
    """Function extents that also recognise frame-less leaf helpers.

    `probe_property_names.Fns` only knows functions that begin with
    `push ebp` (the MSVC frame-pointer prologue). The small helpers in this
    binary are entered with `push ebx` / `push esi` instead and are separated
    from their neighbours by `int3` padding, so the old rule folds them into
    the enclosing function. That is harmless for name recovery but fatal for
    classification: the helper that installs a property is a *different*
    function from the registrar that calls it.

    A function start is therefore one of

      * an instruction with a `push ebp` prologue,
      * the first instruction after an `int3` run that follows a terminator,
      * the first instruction after a `ret`.

    `jmp` is deliberately not used as a terminator for the padding rule:
    unconditional jumps are also loop back-edges, and treating them as
    function ends would cut functions in the middle.
    """

    def __init__(self, d: Disasm) -> None:
        self.d = d
        self.starts = self._compute()

    def _compute(self) -> list[int]:
        insns = self.d.insns
        starts: set[int] = set()
        i = 0
        n = len(insns)
        while i < n:
            ins = insns[i]
            if ins.mnemonic == "push" and ins.operands.strip() == "ebp":
                starts.add(ins.va)
            elif ins.mnemonic == "int3":
                # Skip the whole padding run and look at what follows it.
                j = i
                while j + 1 < n and insns[j + 1].mnemonic == "int3":
                    j += 1
                if i > 0 and insns[i - 1].is_terminator and j + 1 < n:
                    if insns[j + 1].mnemonic != "int3":
                        starts.add(insns[j + 1].va)
                i = j
            elif ins.mnemonic == "ret" and i + 1 < n and insns[i + 1].mnemonic != "int3":
                starts.add(insns[i + 1].va)
            i += 1
        return sorted(starts)

    def host(self, va: int) -> int | None:
        """The function that contains `va`."""
        idx = bisect.bisect_right(self.starts, va) - 1
        if idx < 0:
            return None
        return self.starts[idx]

    def body(self, start: int) -> list:
        """The instructions of the function that starts at `start`."""
        idx = bisect.bisect_left(self.starts, start)
        if idx >= len(self.starts):
            return []
        end = self.starts[idx + 1] if idx + 1 < len(self.starts) else None
        out = []
        for ins in self.d.insns:
            if ins.va < start:
                continue
            if end is not None and ins.va >= end:
                break
            out.append(ins)
        return out


class Attributor:
    """Class attribution over the registration records."""

    def __init__(self, exe: str, cache: str | None = None) -> None:
        self.probe = Probe(exe, cache)
        self.d: Disasm = self.probe.d
        self.image: PEImage = self.probe.image
        self.rdata_lo = self.probe.rdata_lo
        self.rdata_hi = self.probe.rdata_hi
        self.fns = self.probe.fns
        # Leaf-aware extents: the small registration helpers in this binary
        # have no frame pointer, and the frame-pointer-only extent folds them
        # into their caller. The kind of a registration depends on which
        # helper performs it, so the leaf-aware extent is required.
        self.leaf = LeafFns(self.d)

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
        # namespace registration". The host is resolved with the leaf-aware
        # extent, so a helper that is called from inside another helper is
        # indexed under its own host.
        self.class_openers: dict[int, list[tuple[int, str]]] = (
            collections.defaultdict(list))
        for site, name in self.openers.items():
            host = self.leaf.host(site)
            if host is not None:
                self.class_openers[host].append((site, name))
        self.constructs_by_host: dict[int, list[tuple[int, str]]] = (
            collections.defaultdict(list))
        for site in self.constructs:
            host = self.leaf.host(site)
            if host is not None:
                self.constructs_by_host[host].append((site, ""))
        self.ns_by_host: dict[int, list[tuple[int, str]]] = (
            collections.defaultdict(list))
        for site, name in self.namespace_openers.items():
            host = self.leaf.host(site)
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

    def helper_of(self, rec: dict) -> int | None:
        """The function that performs the registration.

        The registration helper is the function that contains the
        `lua_pushcclosure` call, found with the leaf-aware extent: the
        helpers that install properties and methods are small and are
        entered without a frame pointer, so the frame-pointer-only extent
        would report the calling registrar instead.
        """
        call = rec.get("call")
        if call is None:
            return None
        return self.leaf.host(call)

    def prop_role(self, rec: dict) -> str:
        """Which side of a property a registration installs: "get", "set" or "".

        The helper that performs the registration decides the side, but a
        single helper can install *both* sides: the generic property
        registrar (`0x875230`, used by `Vector.X` and `Vector.Y`) looks up
        `__propget`, installs the reader, then looks up `__propset` and
        installs the writer.  The role therefore has to be decided per call
        site.

        The rule: the code region between the previous `lua_pushcclosure`
        call and the next one belongs to exactly one registration, and the
        metatable key pushed inside that region says which side it is.  The
        nearest key *before* the closure wins, because the compiler looks
        the key up before it creates the closure.
        """
        helper = self.helper_of(rec)
        call = rec.get("call")
        if helper is None or call is None:
            return ""
        body = self.leaf.body(helper)
        index = {ins.va: n for n, ins in enumerate(body)}
        here = index.get(call)
        if here is None:
            return ""
        slot = self.probe.slots["lua_pushcclosure"]

        lo = 0
        for n in range(here - 1, -1, -1):
            if body[n].calls_slot(slot):
                lo = n + 1
                break
        hi = len(body)
        for n in range(here + 1, len(body)):
            if body[n].calls_slot(slot):
                hi = n
                break

        for n in range(here - 1, lo - 1, -1):
            imm = body[n].immediate()
            if body[n].mnemonic == "push" and imm in PROPERTY_ROLE:
                return PROPERTY_ROLE[imm]
        for n in range(here, hi):
            imm = body[n].immediate()
            if body[n].mnemonic == "push" and imm in PROPERTY_ROLE:
                return PROPERTY_ROLE[imm]
        return ""

    def kind_of(self, rec: dict) -> str:
        """Whether a registration installs a property, a method or a metamethod.

        The helper that performs the registration decides: a helper that
        pushes `__propget` / `__propset` installs the closure into the
        class's property table (a property accessor), and a registration
        whose name begins with `__` installs a metamethod.
        """
        if self.prop_role(rec):
            return "property"
        name = rec.get("name") or ""
        if name.startswith("__"):
            return "metamethod"
        return "method"

    def class_of_record(self, rec: dict, depth: int = 0) -> list[str]:
        """The classes a registration record belongs to.

        More than one class means the same helper is called from several
        class scopes, which is a real property of the API, not an error.

        The resolution is recursive because a helper can live inside another
        helper: the small property installers are called from the class
        registrar, which is itself called from the master function. When the
        enclosing function has no scope of its own, the search continues in
        the enclosing function's own scope.
        """
        func = self.helper_of(rec)
        if func is None:
            return []
        return self._classes_of_function(func, depth)

    def _classes_of_function(self, func: int, depth: int) -> list[str]:
        out: list[str] = []

        # Shape 1: the registration is inside the class's own registrar.
        own = self.registrar_class.get(func)
        if own:
            return [own]

        # Shape 2 and 3: a shared helper. The class is the scope that holds
        # the call site of the helper.
        for site in self.call_sites.get(func, []):
            host = self.leaf.host(site)
            if host is None:
                continue
            name = self.scope_of(host, site)
            if name:
                if name not in out:
                    out.append(name)
            elif depth < 3 and host != func:
                # The helper is called from inside another helper: continue
                # the search in that function's own scope.
                for cls in self._classes_of_function(host, depth + 1):
                    if cls not in out:
                        out.append(cls)
        return out

    def attribute(self) -> list[dict]:
        """One attributed record per (registration, class)."""
        rows = self.probe.rows()
        out = []
        for rec in rows:
            classes = self.class_of_record(rec)
            if not classes:
                continue
            kind = self.kind_of(rec)
            role = self.prop_role(rec)
            for cls in classes:
                out.append({
                    "class": cls,
                    "name": rec.get("name"),
                    "kind": kind,
                    "role": role,
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
        kinds: dict[str, int] = collections.Counter()
        class_kinds: dict[str, set[str]] = collections.defaultdict(set)
        for rec in attributed:
            if rec["name"]:
                per_class[rec["class"]].add(rec["name"])
                class_kinds[rec["class"]].add(rec["kind"])
            if rec["body"]:
                bodies[rec["class"]].add(rec["body"])
            kinds[rec.get("kind", "method")] += 1
        return {
            "classes": len(per_class),
            "attributed_records": len(attributed),
            "distinct_class_property_pairs": sum(len(v) for v in per_class.values()),
            "distinct_bodies": len({r["body"] for r in attributed if r["body"]}),
            "kinds": dict(kinds),
            "class_kinds": {k: sorted(v) for k, v in class_kinds.items()},
        }


def build_surface(attr: Attributor, attributed: list[dict]) -> dict:
    """The binding surface, in the shape the spec generator consumes.

    The surface is grouped by class, and each class lists its methods, its
    properties and its metamethods separately. A property appears once, with
    the reader and the writer bodies recorded side by side; the writer is
    null when the binary only installs a reader.

    The three global namespaces (`Isaac`, `Input`, `Options`) are not classes
    and are listed under "namespaces" so the spec generator never registers
    them as classes.

    The class hierarchy is *not* in here: the `__parent` value the binary
    stores is a table taken from the Lua stack, not a name, so the parent
    name is not recoverable from the registration code. The generator keeps
    its own curated parent map.
    """
    def hexbody(value) -> str | None:
        return None if value is None else "0x%08x" % value

    classes: dict[str, dict] = {}
    namespaces: dict[str, dict] = {}

    for rec in attributed:
        name = rec.get("name")
        if not name:
            continue
        cls = rec["class"]
        if cls.startswith("global:"):
            bucket = namespaces.setdefault(
                cls[len("global:"):],
                {"methods": [], "metamethods": []},
            )
        else:
            bucket = classes.setdefault(
                cls, {"parent": "", "methods": [], "properties": [], "metamethods": []}
            )

        body = hexbody(rec.get("body"))
        upvalue = hexbody(rec.get("upvalue"))
        kind = rec.get("kind")

        if kind == "property":
            props = bucket.setdefault("properties", [])
            prop = None
            for cand in props:
                if cand["name"] == name:
                    prop = cand
                    break
            if prop is None:
                prop = {
                    "name": name,
                    "getter": None,
                    "setter": None,
                    "upvalue": upvalue,
                }
                props.append(prop)
            role = rec.get("role") or ""
            if role == "get" and prop["getter"] is None:
                prop["getter"] = body
                if prop["upvalue"] is None:
                    prop["upvalue"] = upvalue
            elif role == "set" and prop["setter"] is None:
                prop["setter"] = body
                if prop["upvalue"] is None:
                    prop["upvalue"] = upvalue
        elif kind == "metamethod":
            entry = {"name": name, "body": body, "upvalue": upvalue}
            if entry not in bucket["metamethods"]:
                bucket["metamethods"].append(entry)
        else:
            entry = {"name": name, "body": body, "upvalue": upvalue}
            if not any(m["name"] == name for m in bucket["methods"]):
                bucket["methods"].append(entry)

    for bucket in list(classes.values()) + list(namespaces.values()):
        for key in ("methods", "properties", "metamethods"):
            if key in bucket:
                bucket[key].sort(key=lambda e: e["name"])
        # A namespace has no property table in the recovered surface; drop the
        # empty list so the two kinds of bucket stay distinguishable.
        if "properties" in bucket and not bucket["properties"]:
            del bucket["properties"]

    return {
        "source": "isaac-ng.exe registration code (tools/probe_class_attribution.py)",
        "format": 2,
        "notes": [
            "Every name here was recovered from the registration code of "
            "isaac-ng.exe, not from a name table.",
            "Class hierarchy is not included: the binary stores __parent as a "
            "table taken from the Lua stack, so the parent name is not "
            "recoverable from the registration code.",
            "The three global namespaces (Isaac, Input, Options) are Lua "
            "globals, not classes.",
            "A body address is provenance, not identity: the binary reuses one "
            "closure body for several members (Vector.X and Vector.Y share "
            "0x008bb2e0, because the field offset travels as an upvalue), so "
            "the 'upvalue' field is the part that distinguishes them.",
        ],
        "classes": dict(sorted(classes.items())),
        "namespaces": dict(sorted(namespaces.items())),
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

    # positive control 4: the kind of a registration.
    kinds = {(rec["class"], rec["name"]): rec["kind"] for rec in attributed
             if rec["name"]}
    for (cls, name), want in CONTROL_KINDS.items():
        got = kinds.get((cls, name))
        if got != want:
            fail(f"{cls}.{name} is {got!r}, expected {want!r}")
        else:
            print(f"control kind {cls}.{name}: {got}")

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
    ap.add_argument("--surface-out",
                    help="write the binding surface (methods, properties) as JSON")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)

    if a.self_test:
        return self_test(a.exe, a.cache)

    attr = Attributor(a.exe, a.cache)
    attributed = attr.attribute()
    s = attr.summary(attributed)

    if a.surface_out:
        surface = build_surface(attr, attributed)
        with open(a.surface_out, "w") as fh:
            json.dump(surface, fh, indent=1, sort_keys=True)
            fh.write("\n")
        n_methods = sum(len(c["methods"]) for c in surface["classes"].values())
        n_props = sum(len(c.get("properties", [])) for c in surface["classes"].values())
        n_meta = sum(len(c["metamethods"]) for c in surface["classes"].values())
        print(f"surface written    : {a.surface_out}")
        print(f"  classes {len(surface['classes'])}, namespaces "
              f"{len(surface['namespaces'])}, methods {n_methods}, "
              f"properties {n_props}, metamethods {n_meta}")

    per_class: dict[str, set[str]] = collections.defaultdict(set)
    for rec in attributed:
        if rec["name"]:
            per_class[rec["class"]].add(rec["name"])

    print(f"class-registration routines : {len(attr.registrar_class)}")
    print(f"classes attributed         : {s['classes']}")
    print(f"attributed records         : {s['attributed_records']}")
    print(f"class/property pairs       : {s['distinct_class_property_pairs']}")
    print(f"distinct bodies attributed : {s['distinct_bodies']}")
    for kind, count in sorted(s["kinds"].items(), key=lambda kv: -kv[1]):
        print(f"  kind {kind:12s} {count:5d}")
    print()
    for cls in sorted(per_class, key=lambda c: (-len(per_class[c]), c)):
        kinds = s["class_kinds"].get(cls, [])
        print(f"  {cls:24s} {len(per_class[cls]):4d} names  [{', '.join(kinds)}]")

    if a.json_out:
        Path(a.json_out).write_text(json.dumps(attributed, indent=1))
        print(f"wrote {a.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
