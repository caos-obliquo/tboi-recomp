#!/usr/bin/env python3
"""Recover the property names behind Lua property closures.

Background
----------
`probe_closures.py` anchors on the `lua_pushcclosure` call and walks
*backwards* to find the body and the name. That works when the name literal
precedes the closure, but it does not work for property registration, where
the name follows the closure. 539 of the 1064 call sites had no name literal
in reach and were reported as `unnamed`.

Why the name follows the closure
--------------------------------
The compiler emits a registration helper that creates the closure first and
names it afterwards (VA 0x875130, the "Ref" property):

    0x875182: push   0x4
    0x875184: push   DWORD PTR [ebx]
    0x875186: call   lua_newuserdata
    0x87518c: push   0x1
    0x87518e: push   0x8a0600                  ; the closure body
    0x875193: mov    DWORD PTR [eax],0x40c3a0   ; the upvalue: the accessor
    0x875199: push   DWORD PTR [ebx]
    0x87519b: call   lua_pushcclosure
    ...
    0x8751b9: push   0xb701d8                  ; "Ref"  <-- the name, AFTER
    0x8751be: push   edi
    0x8751c1: call   lua_pushstring
    0x8751c7: push   0x1
    0x8751c9: push   0xfffffffe
    0x8751cb: push   edi
    0x8751cc: call   lua_rotate                 ; rotate(L, -2, 1)
    0x8751d2: mov    ebx,DWORD PTR ds:0xb18408   ; lua_rawset
    0x8751d8: push   esi
    0x8751d9: push   edi
    0x8751da: call   ebx                        ; rawset(L, table, "Ref")

The rotate is the reason the name ends up *below* the value: Lua 5.1's
`lua_rawset` takes the key from the second-from-top slot and the value from
the top, so the compiler swaps the two. The idiom "pushstring(name);
rotate(L, -2, 1); rawset(L, table)" is therefore a *table assignment*
`t[name] = value`, and the value that was just pushed is the closure.

Two rules keep the forward scan sound
-------------------------------------
1. The naming pushstring must be followed by `lua_rotate` and then
   `lua_rawset`. Without the rotate, a literal is only being used as a lookup
   key (`lua_rawget`), not as a table entry.
2. No other `lua_pushcclosure` may appear between the closure and the naming
   pushstring. Otherwise the literal belongs to the *next* registration.

Without rule 2 the probe steals the next closure's name; without rule 1 it
steals a lookup key.

A third rule picks the right *argument* of the pushstring
----------------------------------------------------------
`lua_pushstring(L, s)` is a cdecl call, so its arguments are pushed right to
left: the string first, the Lua state last. The push immediately before the
call is therefore the state, and the name is the push before that:

    push   0xb701d8     ; the name
    push   edi          ; L
    call   lua_pushstring

The window is bounded by the previous call: a push that belongs to an earlier
call is not an argument of this one.

Three name shapes
-----------------
(a) The name is a literal inside the helper (0x875130 pushes 0xb701d8 = "Ref").
(b) The name is a *stack parameter* of the helper: `push DWORD PTR [ebp+0x8]`.
    The literal then lives in the helper's *caller*. Example, VA 0x875230 is
    "register a property with this name and this upvalue":

        0x866c7d: push   0x1
        0x866c7f: push   0x0
        0x866c81: push   0xb6a748     ; "X"
        0x866c86: call   0x875230
        ...
        0x866c8b: push   0x1
        0x866c8d: push   0x4
        0x866c8f: push   0xb6d134     ; "Y"
        0x866c96: call   0x875230

    One helper, two callers, two different properties. The caller is part of
    the identity of a property, so this tool emits one record per caller.
(c) The name is passed in a *register argument*. Some registrars take the
    name in `edx` and the upvalue on the stack:

        0x89f1b9: mov    edi,ecx            ; edi = L
        0x89f1bb: mov    ebx,edx            ; ebx = the property name
        ...
        0x89f1f1: push   ebx                ; the name
        0x89f1f2: push   edi                ; L
        0x89f1f5: call   lua_pushstring

    and the caller stages the literal:

        0x86747f: mov    edx,0xb70608       ; the property name
        0x867484: push   0x40a9d0           ; the upvalue
        0x867489: call   0x89f1b0

    The tool follows the register back to its definition to decide which of
    the three shapes it is looking at.

The upvalue
-----------
`lua_pushcclosure(L, fn, 1)` pops one upvalue off the stack. The value is
pushed by `mov DWORD PTR [eax], <src>` immediately before the call, where `eax`
holds the slot returned by `lua_newuserdata`. The upvalue is the actual
implementation of the property:

  * a code pointer  -> a field accessor, e.g. 0x40c3a0 (`mov eax,[ecx]; ret`)
  * an integer      -> a field offset inside the userdata (Vector "X" -> 0,
                       Vector "Y" -> 4)

The same generic body serves every property of a class, so (name, upvalue) is
the discriminating key, not the body alone. Vector "X" and "Y" share a body and
differ only in name and upvalue.

What this tool does NOT establish
---------------------------------
  * It does not implement any Lua body. It names registrations.
  * It does not attribute a registration to a game class. The table a
    registration lands in is recovered as an index, not as a class identity.
  * It does not decide whether a name denotes a method or a property; it only
    reports how the name was recovered. Names that begin with "__" are
    metamethods by construction (`__gc`, `__len`, `__call`, ...), but the tool
    does not assert which table received them.
  * It does not replace `probe_closures.py`. It reports the *key* under which
    a closure is stored. Where the two disagree, the disagreement is data,
    not an error to be hidden: see `docs/binding-surface-findings.md`.
  * It does not prove that a name is *unique* to one class. The same name is
    registered by more than one helper, and the same body serves more than
    one name; only the (name, upvalue) pair is unique.

Controls
--------
`--self-test` asserts:
  * positive: the helper at 0x875130 registers body 0x8a0600 under "Ref" with
    upvalue 0x40c3a0;
  * positive: the helper at 0x875230 is called twice, with names "X" and "Y",
    and the two records are distinct;
  * positive: three hand-verified sites resolve to the names read off the
    disassembly by hand (0x8a4d28 -> "__gc", 0x87a18a -> "__len",
    0x87519b -> "Ref");
  * negative: a canary string that exists nowhere in the binary must never
    appear in the output;
  * negative: the same three hand-verified sites must NOT come back with the
    name that the backwards walk of probe_closures.py reports for them
    ("Entity", "Size"). That is the off-by-one bug this tool exists to fix, so
    it is pinned by a control rather than left to a prose claim.

Exit status is non-zero if any control fails, so a broken probe cannot report
numbers that look fine.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from bisect import bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from disasm import Disasm, Insn  # noqa: E402
from pe_vaddr import PEImage  # noqa: E402
from probe_iat import parse_imports  # noqa: E402

# How far back to look for the body / upvalue. The compiler emits the setup
# within a handful of instructions of the call; 24 is the same window
# probe_closures.py uses and is long enough for the `lua_newuserdata` form.
BACK = 24

# How far forward to look for the naming pushstring. The pattern is tight
# (pushstring, rotate, rawset), so 64 instructions is a generous ceiling that
# still cannot reach a different helper.
FWD = 64

# Instructions between the `lua_pushstring` and the `lua_rotate` that closes
# the idiom. In the binary the gap is 3 (push 1, push -2, push edi).
ROTATE_GAP = 12

# Instructions between the `lua_rotate` and the `lua_rawset` that closes it.
RAWSET_GAP = 16

# A name that exists nowhere in the binary. If it ever shows up in the output,
# the string reader is reading the wrong bytes.
CANARY = "ZZ_CANARY_NOT_A_REAL_ISAAC_NAME_ZZ"

# The reference helpers, verified by hand in the disassembly (see module
# docstring). These are the positive and negative controls.
CONTROL_REF_HELPER = 0x875130
CONTROL_REF_NAME = "Ref"
CONTROL_REF_BODY = 0x8A0600
CONTROL_REF_UPVALUE = 0x40C3A0
CONTROL_XY_HELPER = 0x875230
CONTROL_XY_NAMES = ("X", "Y")

# Hand-verified single sites (call VA -> true name, and the *wrong* name that
# the backward walk of probe_closures.py reports for the same site).
#
# The second element of each pair is the class-name literal that stands
# between the previous registration and this one. probe_closures.py walks
# backwards and lands on it; the forward scan must not.
#
#   0x8a4d28: "Entity" (0xb706b8) is pushed at 0x8a4cdb and consumed by
#              lua_rawget (a class-table lookup). The closure body 0x8c2fb0 is
#              stored under "__gc" (0xb753b4) by pushstring@0x8a4d44 ->
#              rotate@0x8a4d4f -> rawset@0x8a4d60.
#   0x87a18a: "Size" (0xb6d1ac) is the class name. The closure body 0x8c3630
#              is stored under "__len" (0xb70f1c) by pushstring@0x87a1a9 ->
#              rotate@0x87a1b4 -> rawset@0x87a1bf.
CONTROL_SITES = (
    (0x8A4D28, "__gc", "Entity"),
    (0x87A18A, "__len", "Size"),
    (0x87519B, "Ref", None),
)

# `push DWORD PTR [ebp+0x8]` and friends: the stack parameter of a helper.
PARAM_RE = re.compile(r"DWORD PTR \[ebp\+0x([0-9a-f]+)\]")

# The destination and source of the upvalue store, `mov DWORD PTR [eax],<src>`.
# The patterns match `Insn.operands`, which excludes the mnemonic.
STORE_RE = re.compile(r"^DWORD PTR \[eax\],(.*)$")

# `mov <reg>,DWORD PTR [ebp+0xN]`: a parameter loaded into a register.
LOAD_RE = re.compile(r"^([a-z]+),DWORD PTR \[ebp\+0x([0-9a-f]+)\]$")

# A bare register operand, e.g. `push edi`.
REG_RE = re.compile(r"^[a-z]+$")

# The two register arguments MSVC passes for a __thiscall/fastcall helper:
# ecx is the first argument, edx the second. Some property registrars take
# the property name in edx and the upvalue on the stack.
FASTCALL_REGS = ("ecx", "edx")

# `mov <reg>,DWORD PTR ds:0x<slot>`: an import loaded into a register.
SLOT_LOAD_RE = re.compile(r"^([a-z]+),DWORD PTR ds:0x([0-9a-f]+)$")


class Fns:
    """Function extents over the instruction stream.

    MSVC emits functions back to back, separated by `int3` padding, and a
    function that owns a stack frame starts with `push ebp`. A `push ebp`
    whose predecessor is a terminator therefore marks a function start. The
    same rule marks the *end* of a function: the next function's start.
    """

    def __init__(self, d: Disasm) -> None:
        starts: list[int] = []
        for i, ins in enumerate(d.insns):
            if ins.mnemonic == "push" and ins.operands.strip() == "ebp" \
                    and i > 0 and d.insns[i - 1].is_terminator:
                starts.append(ins.va)
        self.starts = starts
        self.by_va = {s: i for i, s in enumerate(starts)}

    def start_of(self, va: int) -> int | None:
        i = bisect_right(self.starts, va) - 1
        return self.starts[i] if i >= 0 else None

    def end_of(self, start: int) -> int:
        i = self.by_va.get(start)
        if i is None or i + 1 >= len(self.starts):
            return 1 << 62
        return self.starts[i + 1]

    def callers_of(self, d: Disasm, target: int) -> list[int]:
        """Direct `call <target>` sites, in address order."""
        out = []
        for ins in d.insns:
            if ins.mnemonic != "call":
                continue
            try:
                if int(ins.operands.strip(), 16) == target:
                    out.append(ins.va)
            except ValueError:
                continue
        return out


def hex_or_none(value: int | None) -> str:
    """Format an address for a report, tolerating a missing value.

    The self-test must report a *failed control* as a failed control, not as
    a `TypeError` from formatting `None` -- a crash would hide the failure
    behind a traceback.
    """
    if value is None:
        return "None"
    return f"0x{value:x}"


def read_c_string(image: PEImage, va: int, lo: int, hi: int) -> str | None:
    """A printable ASCII C string at `va`, or None.

    `va_to_offset` takes an RVA even though its parameter is named `va`; every
    caller in tools/ converts, and this one converts too.
    """
    if not (lo <= va < hi):
        return None
    try:
        off = image.va_to_offset(image.va_to_rva(va))
    except ValueError:
        return None
    if off is None:
        return None
    end = image.data.find(b"\x00", off, off + 256)
    if end < 0:
        return None
    raw = image.data[off:end]
    if not raw or len(raw) > 120:
        return None
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    if not text.isprintable():
        return None
    if not all(c.isalnum() or c == "_" for c in text):
        return None
    return text


def stack_args(d: Disasm, call_va: int) -> list[Insn]:
    """The stack arguments pushed for a `call`, nearest push first.

    cdecl pushes arguments right to left, so the push immediately before the
    call is argument 0 (`[ebp+0x8]`), the one before it argument 1, and so on.
    The scan stops at the previous call or return: those boundary the argument
    setup, and crossing them would pick up unrelated pushes.
    """
    n = d.index.get(call_va)
    if n is None:
        return []
    out: list[Insn] = []
    for i in range(n - 1, max(-1, n - 40), -1):
        ins = d.insns[i]
        if ins.mnemonic in ("call", "ret", "retf", "retn"):
            break
        if ins.mnemonic == "push":
            out.append(ins)
    return out


class Probe:
    """The probe, with the import table resolved once."""

    def __init__(self, exe: str, cache: str | None = None) -> None:
        self.image = PEImage(exe)
        rdata, text = self.image.section(".rdata"), self.image.section(".text")
        self.text_lo = text.vma + self.image.image_base
        self.text_hi = text.vma_end + self.image.image_base - 1
        self.rdata_lo = rdata.vma + self.image.image_base
        self.rdata_hi = rdata.vma_end + self.image.image_base - 1
        imports = {n: va for _d, n, va in parse_imports(self.image)}
        for need in ("lua_pushcclosure", "lua_pushstring", "lua_rotate",
                     "lua_rawset", "lua_rawget"):
            if need not in imports:
                raise SystemExit(f"{need} not imported")
        self.slots = {k: imports[k] for k in
                      ("lua_pushcclosure", "lua_pushstring", "lua_rotate",
                       "lua_rawset", "lua_rawget")}
        self.slot_name = {va: n for n, va in imports.items()}
        self.d = Disasm(exe, cache, lo=self.text_lo, hi=self.text_hi)
        self.fns = Fns(self.d)
        self.calls = [i for i in self.d.insns
                      if i.calls_slot(self.slots["lua_pushcclosure"])]

    # -- helpers ---------------------------------------------------------

    def called_name(self, ins: Insn) -> str | None:
        """The import a `call` targets, following a register-held import.

        `call ebx` where `ebx` was loaded with `mov ebx,DWORD PTR ds:0x...`
        is an indirect import call, and the compiler uses that form for
        `lua_rawset`. Resolving it is what makes the "pushstring, rotate,
        rawset" idiom detectable.
        """
        if ins.mnemonic != "call":
            return None
        ops = ins.operands.strip()
        if ops.startswith("DWORD PTR ds:0x"):
            slot = int(ops.split("ds:0x")[1].rstrip("]"), 16)
            return self.slot_name.get(slot)
        m = SLOT_LOAD_RE.match(ops)
        reg = m.group(1) if m else ops
        n = self.d.index.get(ins.va)
        if n is None:
            return None
        for i in range(n - 1, max(-1, n - 40), -1):
            prev = self.d.insns[i]
            if prev.is_terminator:
                break
            if prev.mnemonic == "mov":
                ms = SLOT_LOAD_RE.match(prev.operands.strip())
                if ms and ms.group(1) == reg:
                    return self.slot_name.get(int(ms.group(2), 16))
        return None

    def cstring(self, va: int | None) -> str | None:
        if va is None:
            return None
        return read_c_string(self.image, va, self.rdata_lo, self.rdata_hi)

    # -- the analysis ----------------------------------------------------

    def reg_origin(self, d: Disasm, fn_start_idx: int, use_idx: int,
                   reg: str) -> tuple[str, int] | None:
        """Where the value in `reg` came from, as ("fastcall", reg) or
        ("param", index).

        Some registrars do not push the property name from the stack at all;
        they push a register that was loaded from the caller's second
        register argument:

            mov    edi,edx            ; edi = the name
            ...
            push   edi                ; pushstring(edi)

        Following the register back to its definition recovers the shape, and
        the shape says where the name has to be looked up.

        `fn_start_idx` and `use_idx` are indices into `d.insns`.
        """
        for j in range(use_idx - 1, max(fn_start_idx - 1, use_idx - 60), -1):
            ins = d.insns[j]
            if ins.mnemonic in ("ret", "retf", "retn"):
                break
            if ins.mnemonic != "mov":
                continue
            dst, _, src = ins.operands.partition(",")
            if dst.strip() != reg:
                continue
            src = src.strip()
            if src in FASTCALL_REGS:
                return ("fastcall", src)
            mp = PARAM_RE.search(src)
            if mp:
                return ("param", int(mp.group(1), 16))
            imm = ins.immediate()
            if imm is not None:
                return ("literal", imm)
            return None
        return None

    def register_value(self, d: Disasm, call_va: int, reg: str) -> int | None:
        """The immediate a caller staged in `reg` before calling.

        The scan stops at the previous call or return: those boundary the
        argument setup, and crossing them would pick up an unrelated value.
        """
        n = d.index.get(call_va)
        if n is None:
            return None
        for j in range(n - 1, max(-1, n - 40), -1):
            ins = d.insns[j]
            if ins.mnemonic in ("call", "ret", "retf", "retn"):
                break
            if ins.mnemonic != "mov":
                continue
            dst, _, _src = ins.operands.partition(",")
            if dst.strip() != reg:
                continue
            return ins.immediate()
        return None

    def site_records(self, call: Insn) -> list[dict]:
        d, fns = self.d, self.fns
        func = fns.start_of(call.va)
        if func is None:
            return []
        end = fns.end_of(func)
        n = d.index[call.va]
        fn_end_idx = d.index.get(end, len(d.insns))
        func_idx = d.index.get(func, 0)
        if n is None or n >= fn_end_idx:
            return []

        # --- the body ----------------------------------------------------
        body = None
        for i in range(n - 1, max(-1, n - BACK), -1):
            ins = d.insns[i]
            if ins.is_terminator:
                break
            imm = ins.immediate()
            if imm is not None and self.text_lo <= imm <= self.text_hi \
                    and ins.mnemonic == "push":
                body = imm
                break

        # --- the upvalue -------------------------------------------------
        upvalue = None
        upvalue_src = None
        upvalue_param = None
        for i in range(n - 1, max(-1, n - BACK), -1):
            ins = d.insns[i]
            if ins.is_terminator:
                break
            m = STORE_RE.match(ins.operands.strip())
            if not m:
                continue
            src = m.group(1).strip()
            imm = ins.immediate()
            if imm is not None:
                upvalue = imm
                upvalue_src = "literal"
                break
            for j in range(i - 1, max(-1, i - 12), -1):
                ml = LOAD_RE.match(d.insns[j].operands.strip())
                if ml and ml.group(1) == src:
                    upvalue_param = int(ml.group(2), 16)
                    upvalue_src = "param"
                    break
                if d.insns[j].is_terminator:
                    break
            break

        # --- the preceding dunder literal --------------------------------
        # Informational only: the "__"-prefixed literal pushed before the
        # closure. In the metamethod helpers it *is* the key of the previous
        # registration, so it is not evidence about this closure.
        dunder = None
        for i in range(n - 1, max(-1, n - 80), -1):
            ins = d.insns[i]
            if ins.is_terminator:
                break
            imm = ins.immediate()
            if imm is None or ins.mnemonic != "push":
                continue
            s = self.cstring(imm)
            if s and s.startswith("__"):
                dunder = s
                break

        # --- the naming pushstring, AFTER the call ------------------------
        name = None
        name_src = None
        name_param = None
        name_reg = None
        name_va = None
        rotate_va = None
        rawset_va = None
        stolen = False
        for i in range(n + 1, min(fn_end_idx, n + FWD)):
            ins = d.insns[i]
            if ins.is_terminator:
                break
            if ins.calls_slot(self.slots["lua_pushcclosure"]):
                # Another registration intervenes: the next literal belongs
                # to that one, not to this closure.
                stolen = True
                break
            if self.called_name(ins) != "lua_pushstring":
                continue
            # the rotate that proves this literal is a table key
            rot = None
            for j in range(i + 1, min(fn_end_idx, i + 1 + ROTATE_GAP)):
                if self.called_name(d.insns[j]) == "lua_rotate":
                    rot = d.insns[j]
                    break
            if rot is None:
                break
            raws = None
            for j in range(i + 1, min(fn_end_idx, i + 1 + RAWSET_GAP)):
                if self.called_name(d.insns[j]) == "lua_rawset":
                    raws = d.insns[j]
                    break
            if raws is None:
                break
            rotate_va = rot.va
            rawset_va = raws.va
            name_va = ins.va
            # The address of the string literal itself, when the name is a
            # literal. This is the address of the bytes in .rdata, not the
            # address of the `lua_pushstring` call site: the call site is
            # reported separately as `name_call_va`.
            name_literal = None
            # The argument of the pushstring.
            #
            # `lua_pushstring(L, s)` is a cdecl call, so the arguments are
            # pushed right to left: the string first, the Lua state last.
            # The push immediately before the call is therefore the state,
            # and the string is the push before that.
            #
            # The window is bounded by the previous call: a push that
            # belongs to an earlier call is not an argument of this one.
            pushes = []
            for j in range(i - 1, max(-1, i - 12), -1):
                if d.insns[j].mnemonic in ("call", "ret", "retf", "retn"):
                    break
                if d.insns[j].mnemonic == "push":
                    pushes.append(d.insns[j])
            if pushes:
                # pushes[0] is the last push before the call (the state).
                cand = pushes[1] if len(pushes) > 1 else pushes[0]
                imm = cand.immediate()
                if imm is not None:
                    name = self.cstring(imm)
                    name_src = "literal" if name else None
                    name_literal = imm if name else None
                else:
                    mp = PARAM_RE.search(cand.operands)
                    if mp:
                        name_param = int(mp.group(1), 16)
                        name_src = "param"
                    else:
                        ops = cand.operands.strip()
                        if REG_RE.match(ops):
                            # The name is in a register. Follow it back to
                            # its definition: a stack parameter, a register
                            # argument, or a literal moved into place.
                            origin = self.reg_origin(d, func_idx,
                                                    d.index[cand.va], ops)
                            if origin is not None:
                                kind, val = origin
                                if kind == "fastcall":
                                    name_reg = val
                                    name_src = "register"
                                elif kind == "param":
                                    name_param = val
                                    name_src = "param"
                                else:
                                    name = self.cstring(val)
                                    name_src = "literal" if name else None
                                    name_literal = val if name else None
            break

        common = {
            "call": call.va,
            "func": func,
            "body": body,
            "preceding_dunder": dunder,
            "name": None,
            "name_source": None,
            "name_literal_va": None,
            "rotate_va": rotate_va,
            "rawset_va": rawset_va,
            "upvalue": upvalue,
            "upvalue_source": upvalue_src,
            "caller": None,
            "blocked_by_next_closure": stolen,
        }

        if name_src in ("param", "register"):
            # The name lives in the helper's caller. One helper, many
            # callers, many properties: one record per resolving caller.
            out = []
            for cs in fns.callers_of(d, func):
                args = stack_args(d, cs)
                if name_src == "register":
                    # The name was staged in a register argument (ecx/edx)
                    # by the caller, so the literal is in the caller's own
                    # `mov <reg>,<literal>`.
                    imm = self.register_value(d, cs, name_reg)
                    if imm is None:
                        continue
                    idx = None
                else:
                    idx = (name_param - 8) // 4
                    if idx < 0 or idx >= len(args):
                        continue
                    imm = args[idx].immediate()
                if imm is None:
                    continue
                s = self.cstring(imm)
                if not s:
                    continue
                rec = dict(common)
                rec["name"] = s
                rec["name_source"] = "caller"
                rec["caller"] = cs
                # The literal is the string the *caller* pushed, so its
                # address is the caller's literal address.
                rec["name_literal_va"] = imm
                if upvalue_param is not None:
                    uidx = (upvalue_param - 8) // 4
                    if 0 <= uidx < len(args):
                        uimm = args[uidx].immediate()
                        if uimm is not None:
                            rec["upvalue"] = uimm
                            rec["upvalue_source"] = "caller"
                out.append(rec)
            if out:
                return out
            rec = dict(common)
            rec["name_source"] = "caller-unresolved"
            return [rec]

        rec = dict(common)
        rec["name"] = name
        rec["name_source"] = name_src
        rec["name_literal_va"] = name_literal
        rec["name_call_va"] = name_va
        return [rec]

    def rows(self) -> list[dict]:
        out: list[dict] = []
        for call in self.calls:
            out.extend(self.site_records(call))
        return out


def summarize(rows: list[dict]) -> dict:
    named = [r for r in rows if r["name"]]
    props = [r for r in named if not r["name"].startswith("__")]
    metas = [r for r in named if r["name"].startswith("__")]
    return {
        "records": len(rows),
        "call_sites": len({r["call"] for r in rows}),
        "named": len(named),
        "named_call_sites": len({r["call"] for r in named}),
        "property_names": len(props),
        "metamethod_names": len(metas),
        "distinct_names": len({r["name"] for r in named}),
        "distinct_property_names": len({r["name"] for r in props}),
        "distinct_bodies": len({r["body"] for r in named if r["body"]}),
        "with_upvalue": len([r for r in rows if r["upvalue"] is not None]),
        "distinct_upvalues": len({r["upvalue"] for r in rows
                                  if r["upvalue"] is not None}),
        "blocked_by_next_closure": len([r for r in rows
                                        if r["blocked_by_next_closure"]]),
    }


def build_parser() -> argparse.ArgumentParser:
    """One parser for the tool, so the flags cannot drift apart."""
    ap = argparse.ArgumentParser(
        description="Recover Lua property names and their implementations.")
    ap.add_argument("exe", nargs="?")
    ap.add_argument("--cache")
    ap.add_argument("--json-out")
    ap.add_argument("--compare", help="probe_closures.json to compare against")
    ap.add_argument("--self-test", action="store_true",
                    help="run the positive and negative controls and exit")
    return ap


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.self_test:
        return self_test(args.exe, args.cache)
    if not args.exe:
        build_parser().print_help()
        return 2

    p = Probe(args.exe, args.cache)
    rows = p.rows()
    s = summarize(rows)

    print(f"lua_pushcclosure call sites : {s['call_sites']}")
    print(f"registration records        : {s['records']}")
    print(f"records with a name         : {s['named']}"
          f"  ({s['named_call_sites']} call sites)")
    print(f"  property-style names      : {s['property_names']}"
          f"  ({s['distinct_property_names']} distinct)")
    print(f"  metamethod names          : {s['metamethod_names']}")
    print(f"distinct names               : {s['distinct_names']}")
    print(f"distinct bodies named       : {s['distinct_bodies']}")
    print(f"records with an upvalue      : {s['with_upvalue']}"
          f"  ({s['distinct_upvalues']} distinct)")
    print(f"blocked by a later closure   : {s['blocked_by_next_closure']}")

    if args.compare:
        compare(args.compare, rows)

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(rows, indent=1))
        print(f"wrote {args.json_out}")
    return 0


def compare(closures_path: str, rows: list[dict]) -> None:
    """Compare the forward-derived names with probe_closures' backward ones.

    A disagreement is not an error in either tool: it means the two probes
    anchored on different sides of the closure. The table is printed so the
    disagreement is auditable rather than hidden.
    """
    c = json.loads(Path(closures_path).read_text())
    backward: dict[int, str] = {}
    for grp in ("bound", "conflicting"):
        for e in c.get(grp, []):
            for call in e.get("calls", []):
                backward[call] = e["name"]
    for e in c.get("metatable_keys", []):
        for call in e.get("calls", []):
            backward.setdefault(call, e["name"])
    forward: dict[int, set[str]] = {}
    for r in rows:
        if r["name"]:
            forward.setdefault(r["call"], set()).add(r["name"])

    agree = []
    differ = []
    for call, nm in backward.items():
        fwd = forward.get(call)
        if not fwd:
            continue
        (agree if nm in fwd else differ).append((call, nm, sorted(fwd)))
    print()
    print(f"probe_closures named sites compared : {len(backward)}")
    print(f"  same name on both sides            : {len(agree)}")
    print(f"  different names                    : {len(differ)}")
    for call, nm, fwd in differ[:20]:
        print(f"    0x{call:x}: backward={nm!r} forward={fwd}")


def self_test(exe: str, cache: str | None) -> int:
    """Positive and negative controls. Non-zero exit means the probe is wrong."""
    p = Probe(exe, cache)
    rows = p.rows()
    failures = 0

    def fail(msg: str) -> None:
        nonlocal failures
        failures += 1
        print(f"FAIL: {msg}", file=sys.stderr)

    # positive control 1: the "Ref" helper.
    ref = [r for r in rows if r["func"] == CONTROL_REF_HELPER]
    if not ref:
        fail(f"no record for helper 0x{CONTROL_REF_HELPER:x}")
    else:
        r = ref[0]
        print(f"control Ref: name={r['name']!r} body={hex_or_none(r['body'])} "
              f"upvalue={hex_or_none(r['upvalue'])} "
              f"dunder={r['preceding_dunder']!r}")
        if r["name"] != CONTROL_REF_NAME:
            fail(f"helper 0x{CONTROL_REF_HELPER:x} name {r['name']!r} "
                 f"!= {CONTROL_REF_NAME!r}")
        if r["body"] != CONTROL_REF_BODY:
            fail(f"helper 0x{CONTROL_REF_HELPER:x} body "
                 f"{hex_or_none(r['body'])} != 0x{CONTROL_REF_BODY:x}")
        if r["upvalue"] != CONTROL_REF_UPVALUE:
            fail(f"helper 0x{CONTROL_REF_HELPER:x} upvalue "
                 f"{hex_or_none(r['upvalue'])} != 0x{CONTROL_REF_UPVALUE:x}")

    # positive control 2: the "X"/"Y" helper, two callers, two names.
    xy = [r for r in rows if r["func"] == CONTROL_XY_HELPER]
    xy_names = sorted({r["name"] for r in xy if r["name"]})
    print(f"control XY: names={xy_names} records={len(xy)}")
    if xy_names != sorted(CONTROL_XY_NAMES):
        fail(f"helper 0x{CONTROL_XY_HELPER:x} names {xy_names} "
             f"!= {sorted(CONTROL_XY_NAMES)}")
    if len({r["caller"] for r in xy}) != len(CONTROL_XY_NAMES):
        fail("X/Y helper did not resolve two distinct callers")

    # negative control 1: the canary must not exist in the binary at all.
    if CANARY.encode() in p.image.data:
        fail("canary string found in the binary image")
    if any(r["name"] == CANARY for r in rows):
        fail("canary string appeared in a recovered name")

    # positive control 3 and negative control 2: hand-verified sites.
    #
    # The positive half pins the true name of a site whose disassembly was
    # read by hand. The negative half pins the *wrong* name that the
    # backwards walk of probe_closures.py reports for the same site: if the
    # forward scan ever returns the class-name literal, the probe has
    # regressed into the off-by-one bug this tool exists to fix.
    for site, want, forbidden in CONTROL_SITES:
        recs = [r for r in rows if r["call"] == site]
        if not recs:
            fail(f"no record for the control site 0x{site:x}")
            continue
        names = sorted({r["name"] for r in recs if r["name"]})
        print(f"control site 0x{site:x}: names={names} want={want!r}")
        if want not in names:
            fail(f"site 0x{site:x} names {names} do not include {want!r}")
        if forbidden is not None and forbidden in names:
            fail(f"site 0x{site:x} returned the backwards-walk name "
                 f"{forbidden!r}; the forward scan stole a lookup key")

    print("SELF-TEST PASSED" if not failures else f"{failures} CONTROL FAILURES")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
