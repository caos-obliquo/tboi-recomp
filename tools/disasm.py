#!/usr/bin/env python3
"""Aligned instruction stream for the game binary, sourced from objdump.

Why this exists: every probe that reasons about instruction order used to scan
.text as raw bytes. That is unsound. A 0x68 byte occurs constantly as part of
some other instruction's encoding, so a byte-pattern search for `push imm32` or
`ff 15` reports matches that no instruction boundary supports. `probe_closures`
turned a match inside a neighbouring function into a "confirmed" binding for 92
names before disassembly proved the claim false.

So: read the real instruction stream. objdump already knows the boundaries and
it runs in 1.5s over 7.4MB, so there is no reason to guess.

Note on section arithmetic: .text is 0x401000..0xB17134. Do not hand-compute
this; `0x401000 + 0x716134` is 0xB17134, not 0x717134, and getting it backwards
puts every address past 0x717134 in a gap that no section maps.
"""
from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

# objdump -M intel lines: "  87c608:\tmov    DWORD PTR [eax],0x930390"
LINE_RE = re.compile(r"^\s*([0-9a-f]+):\t(.*)$")

# Instructions after which straight-line argument setup cannot continue.
TERMINATORS = frozenset({
    "ret", "retf", "retn", "iret", "iretd", "iretq",
    "int3", "ud2", "jmp", "ljmp",
})


@dataclass(frozen=True)
class Insn:
    va: int
    mnemonic: str
    operands: str

    @property
    def is_terminator(self) -> bool:
        return self.mnemonic in TERMINATORS

    def immediate(self) -> int | None:
        """The immediate this instruction supplies, if it supplies exactly one.

        Covers the three forms observed at real closure-registration sites:
            push   0x930390
            mov    eax,0x930390
            mov    DWORD PTR [eax],0x930390
        A load form such as `mov eax,DWORD PTR [0xb183fc]` has no trailing
        immediate and returns None, which is the correct answer: it reads a
        value, it does not stage one.
        """
        ops = self.operands.strip()
        if self.mnemonic == "push":
            src = ops
        elif self.mnemonic == "mov":
            if "," not in ops:
                return None
            src = ops.split(",", 1)[1].strip()
        else:
            return None
        if not src.startswith("0x"):
            return None
        try:
            return int(src, 16)
        except ValueError:
            return None

    def calls_slot(self, slot_va: int) -> bool:
        """True for `call DWORD PTR ds:<slot_va>` -- an IAT-mediated import call."""
        if self.mnemonic != "call":
            return False
        marker = f"ds:0x{slot_va:x}"
        return marker in self.operands


class Disasm:
    """Instruction list for .text, in address order, with a boundary index."""

    def __init__(self, exe: str, cache: str | None = None,
                 lo: int | None = None, hi: int | None = None) -> None:
        # lo/hi scope the stream to one section. objdump disassembles every
        # CODE section, so .bind is interleaved into the same address-ordered
        # list unless the caller bounds it; a caller reasoning about "the
        # instruction after this one" must not silently reach across sections.
        text = self._load(exe, cache)
        insns: list[Insn] = []
        for line in text.splitlines():
            m = LINE_RE.match(line)
            if not m:
                continue
            rest = m.group(2).strip()
            if not rest or rest.startswith("("):
                continue
            va = int(m.group(1), 16)
            if lo is not None and va < lo:
                continue
            if hi is not None and va > hi:
                continue
            head = rest.split(None, 1)
            insns.append(Insn(va, head[0],
                              head[1] if len(head) > 1 else ""))
        insns.sort(key=lambda i: i.va)
        self.insns = insns
        self.index = {i.va: n for n, i in enumerate(insns)}

    @staticmethod
    def _load(exe: str, cache: str | None) -> str:
        if cache and Path(cache).is_file():
            return Path(cache).read_text(errors="replace")
        proc = subprocess.run(
            ["objdump", "-d", "-M", "intel", "--no-show-raw-insn", exe],
            capture_output=True, text=True, check=True)
        text = proc.stdout
        if cache:
            Path(cache).parent.mkdir(parents=True, exist_ok=True)
            Path(cache).write_text(text)
        return text

    def at(self, va: int) -> Insn | None:
        """The instruction starting exactly at `va`, else None.

        None is load-bearing: it means `va` is not an instruction boundary, so
        no claim about instruction order may rest on it.
        """
        n = self.index.get(va)
        return self.insns[n] if n is not None else None

    def forward(self, va: int) -> list[Insn]:
        """Instructions starting at `va` in address order, to the end of .text."""
        n = self.index.get(va)
        return [] if n is None else self.insns[n:]

    def walk_back(self, start_va: int, max_insns: int = 16) -> list[Insn]:
        """Instructions before `start_va`, nearest first.

        `start_va` is expected to be an instruction address. The walk stops at a
        terminator so a backward search cannot tunnel through the previous
        function, for the same reason `walk` refuses to cross forward.
        """
        n = self.index.get(start_va)
        if n is None:
            return []
        out: list[Insn] = []
        for i in range(n - 1, max(-1, n - max_insns), -1):
            insn = self.insns[i]
            out.append(insn)
            if insn.is_terminator:
                break
        return out

    def walk(self, start_va: int, max_insns: int = 96) -> tuple[list[Insn], str]:
        """Straight-line walk from `start_va`, stopping at a terminator.

        Returns (instructions_including_the_stopper, reason). Reason is one of
        "terminator", "max_insns", "end_of_text", or "not_a_boundary".

        Stopping at the terminator is the whole point. Without it the walk runs
        past `ret` and the int3 padding into the next function and attributes
        its `lua_pushcclosure` to this one.
        """
        n = self.index.get(start_va)
        if n is None:
            return [], "not_a_boundary"
        out: list[Insn] = []
        for insn in self.insns[n:n + max_insns]:
            out.append(insn)
            if insn.is_terminator:
                return out, "terminator"
        if n + max_insns < len(self.insns):
            return out, "max_insns"
        return out, "end_of_text"


def self_test(exe: str, cache: str | None = None) -> int:
    """Positive control: the site the retraction rests on must parse as claimed.

    If this fails, the parser is wrong and every count derived from it is
    worthless, so it exits non-zero rather than reporting numbers.
    """
    d = Disasm(exe, cache)
    print(f"instructions parsed: {len(d.insns)}")
    if not d.insns:
        print("FAIL: parsed zero instructions", file=sys.stderr)
        return 1
    lo, hi = d.insns[0].va, d.insns[-1].va
    print(f"range: 0x{lo:x} .. 0x{hi:x}")

    # The exact sequence from the retraction: mov DWORD PTR [eax],imm32 staging
    # a value, then the IAT call. The old probe never saw the mov form.
    site = d.at(0x87C608)
    if site is None:
        print("FAIL: 0x87c608 is not an instruction boundary", file=sys.stderr)
        return 1
    got = site.immediate()
    print(f"0x87c608 = {site.mnemonic} {site.operands} -> immediate {got}")
    if got != 0x930390:
        print("FAIL: expected immediate 0x930390", file=sys.stderr)
        return 1

    nxt = d.at(0x87C60E)
    if nxt is None or nxt.mnemonic != "call":
        print(f"FAIL: 0x87c60e is not a call ({nxt})", file=sys.stderr)
        return 1
    print(f"0x87c60e = {nxt.mnemonic} {nxt.operands}")

    # The boundary that the old probe ignored: the previous function ends here.
    walked, reason = d.walk(0x87C5BD)
    print(f"walk from 0x87c5bd: {len(walked)} insns, stopped on {reason} "
          f"({walked[-1].mnemonic} {walked[-1].operands}) at 0x{walked[-1].va:x}")
    if walked[-1].va == 0x87C60E:
        print("FAIL: walk still reaches the neighbouring call", file=sys.stderr)
        return 1

    # Negative control: a load must not report a staged immediate.
    loaded = d.at(0x87C617)
    if loaded is not None and loaded.immediate() == loaded.immediate() \
            and "[" in loaded.operands and "," in loaded.operands:
        src = loaded.operands.split(",", 1)[1].strip()
        if not src.startswith("0x") and loaded.immediate() is not None:
            print(f"FAIL: load form 0x{loaded.va:x} reported an immediate",
                  file=sys.stderr)
            return 1

    print("SELF-TEST PASSED")
    return 0


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        argv = [a for a in sys.argv[1:] if not a.startswith("--")]
        cache = next((sys.argv[i + 1] for i, a in enumerate(sys.argv)
                      if a == "--cache"), None)
        sys.exit(self_test(argv[0], cache))
    print(__doc__)
    sys.exit(2)
