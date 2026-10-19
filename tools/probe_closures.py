#!/usr/bin/env python3
"""Recover Lua method-name to closure-body bindings, anchored on the call site.

Two earlier versions of this probe anchored on the *name* and looked forward for
a `lua_pushcclosure` in a fixed window. Both produced bindings that
disassembly refuted.

The failure is structural. A single function can contain hundreds of registration
records -- one giant data-table initializer pushes thousands of name literals --
so "the next call within 96 instructions" is not a record boundary. It is not
even a function boundary, which is why the terminator check alone did not catch
it: the whole initializer lives inside one function with no `ret` in sight.

So this version anchors on the call instead, which is unambiguous:

    push   0x1                  ; nupvalues
    push   0x8bb960             ; fn          <- the body
    mov    DWORD PTR [eax],0x0  ; upvalue setup, may be absent
    push   DWORD PTR [esi]      ; L
    call   DWORD PTR ds:0xb183fc ; lua_pushcclosure

Walking backward from a real call site, the body is the nearest `push imm32`
carrying a .text address, and the upvalue count is the `push imm32` before it.
The name is the literal fed to the nearest preceding `lua_pushstring`. Every step
stops at a terminator.

Evidence classes:

  bound       one name, one body, across every call site that named it
  conflicting the same name was registered against different bodies
  unnamed     a closure with no `lua_pushstring` in reach (lambda, destructor)
  no_body     the call was reached but no .text immediate identifies a body
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from disasm import Disasm, Insn  # noqa: E402
from pe_vaddr import PEImage  # noqa: E402
from probe_iat import parse_imports  # noqa: E402

BACK = 24

# The site the retraction rests on: a `lua_pushcclosure` at 0x87c60e that the
# name-anchored probe attributed to AddTrinketEffect in the previous function.
# It has no name literal in reach, so it must land in `unnamed`. If it ever turns
# up as `bound` to AddTrinketEffect, the backward walk has regressed.
CONTROL_CALL = 0x87C60E
CONTROL_NAME = "AddTrinketEffect"


def read_c_string(image: PEImage, va: int, lo: int, hi: int) -> str | None:
    if not (lo <= va < hi):
        return None
    # Despite the name, `va_to_offset` takes an RVA: it feeds its argument to
    # `Section.contains_va`, which compares against the RVA-space `vma`. Every
    # existing caller in tools/ passes an RVA, so convert rather than rename.
    off = image.va_to_offset(image.va_to_rva(va))
    end = image.data.find(b"\x00", off, off + 512)
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
    head = text[0]
    if not (head.isalpha() or head == "_"):
        return None
    if not all(c.isalnum() or c == "_" for c in text):
        return None
    return text


def analyse_call(d: Disasm, call: Insn, slot_cc: int, slot_ps: int,
                 text_lo: int, text_hi: int,
                 rdata_lo: int, rdata_hi: int) -> tuple[int | None, str | None,
                                                          int | None]:
    """(body_va, name, nupvalues) for one lua_pushcclosure call site."""
    back = d.walk_back(call.va, max_insns=BACK)
    body = None
    nup = None
    name = None
    for insn in back:
        if name is None and insn.calls_slot(slot_ps):
            prev = d.walk_back(insn.va, max_insns=6)
            for p in prev:
                if p.mnemonic == "push" and (imm := p.immediate()) is not None:
                    if rdata_lo <= imm < rdata_hi:
                        name = imm
                    break
            break
        imm = insn.immediate()
        if imm is None:
            continue
        if body is None:
            if text_lo <= imm <= text_hi and insn.mnemonic == "push":
                body = imm
                continue
        elif nup is None and insn.mnemonic == "push" and 0 <= imm <= 8:
            nup = imm
    return body, name, nup


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("spec")
    ap.add_argument("--back", type=int, default=BACK)
    ap.add_argument("--cache")
    ap.add_argument("--json-out")
    args = ap.parse_args()

    image = PEImage(args.exe)
    rdata, text = image.section(".rdata"), image.section(".text")

    # `Section.vma` is an RVA; its absolute VA is vma + image_base, the idiom
    # pe_vaddr's own self-test asserts.
    text_lo = text.vma + image.image_base
    text_hi = text.vma_end + image.image_base - 1
    rdata_lo = rdata.vma + image.image_base
    rdata_hi = rdata.vma_end + image.image_base - 1

    imports = {n: va for _d, n, va in parse_imports(image)}
    for need in ("lua_pushcclosure", "lua_pushstring"):
        if need not in imports:
            print(f"{need} not imported", file=sys.stderr)
            return 2
    slot_cc, slot_ps = imports["lua_pushcclosure"], imports["lua_pushstring"]

    claimers: dict[str, set[str]] = {}
    spec_path = Path(args.spec)
    if spec_path.is_file():
        for cls, val in json.loads(spec_path.read_text()).items():
            if cls.startswith("_") or not isinstance(val, dict):
                continue
            for m in val.get("api", []) or []:
                if isinstance(m, str):
                    claimers.setdefault(m, set()).add(cls)

    d = Disasm(args.exe, args.cache, lo=text_lo, hi=text_hi)
    calls = [i for i in d.insns if i.calls_slot(slot_cc)]
    print(f"lua_pushcclosure call sites : {len(calls)}")

    rows, unnamed, no_body = [], 0, 0
    control_body = None
    for call in calls:
        body, name_va, nup = analyse_call(
            d, call, slot_cc, slot_ps, text_lo, text_hi, rdata_lo, rdata_hi)
        if call.va == CONTROL_CALL:
            control_body = body
        if body is None:
            no_body += 1
            if name_va is not None:
                unnamed += 1
            continue
        nm = read_c_string(image, name_va, rdata_lo, rdata_hi) \
            if name_va is not None else None
        if nm is None:
            unnamed += 1
            continue
        rows.append({"name": nm, "body": body, "call": call.va, "nup": nup})

    by_name: dict[str, dict] = {}
    for r in rows:
        e = by_name.setdefault(r["name"], {"bodies": set(), "calls": []})
        e["bodies"].add(r["body"])
        e["calls"].append(r["call"])

    bound, conflicting, keys = [], [], []
    for nm in sorted(by_name):
        e = by_name[nm]
        rec = {"name": nm,
               "classes": sorted(claimers.get(nm, ())),
               "calls": sorted(e["calls"])}
        if len(e["bodies"]) == 1:
            body = next(iter(e["bodies"]))
            rec["body"] = body
            rec["body_rva"] = image.va_to_rva(body)
            bound.append(rec)
        elif nm.startswith("__"):
            # A metatable key legitimately has many bodies -- one closure per
            # property. That is the shape of the data, not a contradiction, so it
            # is reported apart from a genuine name/body disagreement.
            rec["bodies"] = sorted(e["bodies"])
            keys.append(rec)
        else:
            rec["bodies"] = sorted(e["bodies"])
            conflicting.append(rec)

    print(f"named registrations         : {len(rows)}")
    print(f"distinct names bound 1:1    : {len(bound)}")
    print(f"metatable keys, many bodies : {len(keys)}")
    print(f"names bound to >1 body      : {len(conflicting)}")
    print(f"closures with no name       : {unnamed}")
    print(f"call sites with no body     : {no_body}\n")

    if keys:
        print("metatable key -> body count (one closure per property):")
        for k in sorted(keys, key=lambda r: -len(r["bodies"])):
            print(f"  {k['name']:<12} {len(k['bodies']):>4} bodies "
                  f"from {len(k['calls'])} registrations")
        print()

    if control_body is None:
        print(f"CONTROL FAILED: no call site found at 0x{CONTROL_CALL:x}",
              file=sys.stderr)
        return 1
    hit = [r for r in bound + conflicting + keys
           if r["name"] == CONTROL_NAME
           and CONTROL_CALL in r["calls"]]
    if hit:
        print(f"CONTROL FAILED: {CONTROL_NAME} bound via 0x{CONTROL_CALL:x}",
              file=sys.stderr)
        return 1
    print(f"control ok: 0x{CONTROL_CALL:x} reaches body "
          f"0x{control_body:x} but carries no {CONTROL_NAME} literal")

    if bound:
        by_class: dict[str, int] = {}
        for b in bound:
            for cls in b["classes"]:
                by_class[cls] = by_class.get(cls, 0) + 1
        if by_class:
            print("\nbound per class (a name may be claimed by several):")
            for cls, n in sorted(by_class.items(), key=lambda kv: -kv[1])[:15]:
                print(f"  {cls:<18} {n:>4}")
        print("\nsample bound (name -> body va, upvalues):")
        for b in bound[:20]:
            print(f"  {b['name']:<28} 0x{b['body']:08x}  "
                  f"calls={len(b['calls'])}")

    bodies = {b["body"] for b in bound}
    print(f"\ndistinct bodies among bound: {len(bodies)} "
          f"(from {len(bound)} names)")

    # A `__`-leading name is a metatable key, not a method. The key closures are
    # the bulk of the named registrations, so reporting them merged with method
    # names would overstate the recovered method surface.
    keys = [b for b in bound if b["name"].startswith("__")]
    methods = [b for b in bound if not b["name"].startswith("__")]
    print(f"  of which metatable keys : {len(keys)} "
          f"({', '.join(sorted({b['name'] for b in keys}))})")
    print(f"  of which method names   : {len(methods)}")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(
            {"source": args.exe, "call_sites": len(calls), "bound": bound,
             "metatable_keys": keys, "conflicting": conflicting,
             "unnamed": unnamed,
             "no_body": no_body},
            indent=2, sort_keys=True))
        print(f"wrote {args.json_out}")
    return 0 if bound else 1


if __name__ == "__main__":
    sys.exit(main())
