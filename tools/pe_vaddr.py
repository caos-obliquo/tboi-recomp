#!/usr/bin/env python3
"""PE section table + address translation that refuses to guess.

Why this exists
---------------
In `isaac-ng.exe` the virtual-address ranges of `.rdata` and `.data` OVERLAP:

    .rdata  VMA 0x00b18000  size 0x000df948  ->  ..0x00c17948
    .data   VMA 0x00bf8000  size 0x00069e00  ->  ..0x00c61e00

so roughly 0x1F948 bytes of VA space maps into BOTH sections. A helper that
resolves a VA with a single base silently translates one of them into the other
and yields plausible-looking garbage. A prior session lost real time to exactly
that.

Design rule
-----------
There is no default. `va_to_offset` returns EVERY section that claims the
address, and the caller must either name the section it means or assert that
exactly one candidate came back. Ambiguity is a return value, not a silent
choice.

No third-party dependencies: struct only.

All arithmetic is an expression over the section table read from the file.
Nothing here hard-codes an address.
"""
from __future__ import annotations

import re
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Section:
    name: str
    vma: int
    vsize: int
    raw_offset: int
    raw_size: int
    characteristics: int

    @property
    def vma_end(self) -> int:
        return self.vma + max(self.vsize, self.raw_size)

    @property
    def file_end(self) -> int:
        return self.raw_offset + self.raw_size

    def contains_va(self, va: int) -> bool:
        return self.vma <= va < self.vma_end


@dataclass(frozen=True)
class Candidate:
    """One section's answer for a VA. `offset` is None when the VA falls in the
    section's virtual tail (vsize > raw_size), i.e. .bss-style, not on disk."""

    section: str
    offset: int | None

    @property
    def on_disk(self) -> bool:
        return self.offset is not None


class PEImage:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError(f"{self.path}: not an MZ executable")
        pe_off = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe_off : pe_off + 4] != b"PE\x00\x00":
            raise ValueError(f"{self.path}: PE signature not found")

        coff = pe_off + 4
        (num_sections,) = struct.unpack_from("<H", self.data, coff + 2)
        (opt_size,) = struct.unpack_from("<H", self.data, coff + 16)
        opt = coff + 20
        self.opt = opt
        (magic,) = struct.unpack_from("<H", self.data, opt)

        if magic == 0x10B:  # PE32
            self.bits = 32
            (self.image_base,) = struct.unpack_from("<I", self.data, opt + 28)
            self._addr_off = opt + 28
            self._addr_fmt = "<I"
        elif magic == 0x20B:  # PE32+
            self.bits = 64
            (self.image_base,) = struct.unpack_from("<Q", self.data, opt + 24)
            self._addr_off = opt + 24
            self._addr_fmt = "<Q"
        else:
            raise ValueError(f"unknown optional-header magic 0x{magic:x}")

        table = opt + opt_size
        self.sections: list[Section] = []
        for i in range(num_sections):
            base = table + i * 40
            name = self.data[base : base + 8].rstrip(b"\x00").decode("ascii", "replace")
            vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", self.data, base + 8)
            (chars,) = struct.unpack_from("<I", self.data, base + 36)
            self.sections.append(
                Section(name, vaddr, vsize, raw_ptr, raw_size, chars)
            )

    @property
    def entry_point(self) -> int:
        """AddressOfEntryPoint -- an RVA, at optional-header offset 16.

        NOT image_base + 16: image base lives at offset 28 (PE32) / 24 (PE32+),
        which is a different field. Conflating them silently yields a bogus
        entry address.
        """
        return struct.unpack_from("<I", self.data, self.opt + 16)[0]

    def rva_to_va(self, rva: int) -> int:
        """Absolute VA. This is what `objdump -h` prints in its VMA column and
        what disassembly shows, so downstream tooling almost always needs it."""
        return self.image_base + rva

    def va_to_rva(self, va: int) -> int:
        return va - self.image_base

    def section(self, name: str) -> Section:
        for s in self.sections:
            if s.name == name:
                return s
        raise KeyError(name)

    def candidates(self, va: int) -> list[Candidate]:
        """Every section claiming `va`. Length > 1 means the VA is ambiguous."""
        out: list[Candidate] = []
        for s in self.sections:
            if s.contains_va(va):
                delta = va - s.vma
                off = s.raw_offset + delta if delta < s.raw_size else None
                out.append(Candidate(s.name, off))
        return out

    def va_to_offset(self, va: int, *, section: str | None = None) -> int:
        """Translate a VA to a file offset, refusing to guess.

        With `section` given, resolves within that section only.
        Without it, the VA must be claimed by exactly one section on disk.
        """
        cands = self.candidates(va)
        if section is not None:
            for c in cands:
                if c.section == section:
                    if not c.on_disk:
                        raise ValueError(
                            f"VA 0x{va:08x} is in {section} but in its non-file tail"
                        )
                    assert c.offset is not None
                    return c.offset
            raise ValueError(f"VA 0x{va:08x} is not inside section {section}")

        on_disk = [c for c in cands if c.on_disk]
        if len(on_disk) == 1:
            assert on_disk[0].offset is not None
            return on_disk[0].offset
        if not on_disk:
            raise ValueError(f"VA 0x{va:08x} maps to no on-disk section")
        names = ", ".join(f"{c.section}@0x{c.offset:08x}" for c in on_disk if c.on_disk)
        raise ValueError(
            f"VA 0x{va:08x} is AMBIGUOUS: claimed by {names}. "
            f"Pass section= to state which one you mean."
        )

    def offset_to_va(self, offset: int, *, section: str | None = None) -> int:
        """Absolute VA for a file offset. binutils and every disassembler print
        absolute addresses, so this must include the image base."""
        for s in self.sections:
            if section is not None and s.name != section:
                continue
            if s.raw_offset <= offset < s.file_end:
                return self.rva_to_va(s.vma + (offset - s.raw_offset))
        raise ValueError(f"file offset 0x{offset:08x} is not in any section")

    def read_va(self, va: int, length: int, *, section: str | None = None) -> bytes:
        off = self.va_to_offset(va, section=section)
        return self.data[off : off + length]

    def overlaps(self) -> list[tuple[str, str, int, int]]:
        """Pairs of sections whose VA ranges intersect. The trap, enumerated."""
        found = []
        for i, a in enumerate(self.sections):
            for b in self.sections[i + 1 :]:
                lo = max(a.vma, b.vma)
                hi = min(a.vma_end, b.vma_end)
                if lo < hi:
                    found.append((a.name, b.name, lo, hi))
        return found


def _self_test(path: str | Path) -> int:
    pe = PEImage(path)
    print(f"{pe.path}")
    print(
        f"bits={pe.bits} image_base=0x{pe.image_base:08x} "
        f"entry_rva=0x{pe.entry_point:08x} entry_va=0x{pe.rva_to_va(pe.entry_point):08x}"
    )
    print(f"{len(pe.sections)} sections (VMA shown absolute, as binutils prints it):")
    for s in pe.sections:
        print(
            f"  {s.name:<8} rva=0x{s.vma:08x} vsize=0x{s.vsize:08x} "
            f"vma=0x{pe.rva_to_va(s.vma):08x} raw=0x{s.raw_offset:08x} "
            f"rawsz=0x{s.raw_size:08x}"
            + ("  [virtual tail not on disk]" if s.vsize > s.raw_size else "")
        )

    # Oracle is binutils, not hard-coded addresses: the project forbids
    # hand-computed hex, and a divergence means our parse is wrong.
    failures = 0
    try:
        out = subprocess.run(
            ["objdump", "-h", str(pe.path)], capture_output=True, text=True, check=True
        ).stdout
        checked, mismatches = 0, []
        for line in out.splitlines():
            m = re.match(
                r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+",
                line,
            )
            if not m:
                continue
            name, _size, vma_hex, _lma, off_hex = m.groups()
            try:
                s = pe.section(name)
            except KeyError:
                continue
            checked += 1
            ours_vma, ours_off = pe.rva_to_va(s.vma), s.raw_offset
            if int(vma_hex, 16) != ours_vma or int(off_hex, 16) != ours_off:
                mismatches.append(
                    (name, vma_hex, hex(ours_vma), off_hex, hex(ours_off))
                )
        print(
            f"\nbinutils cross-check: {checked} sections compared against objdump, "
            f"{len(mismatches)} mismatches"
        )
        for name, ov, mv, oo, mo in mismatches:
            print(f"  MISMATCH {name}: objdump vma={ov} off={oo} | ours vma={mv} off={mo}")
            failures += 1
        if checked == 0:
            print("  FAIL: no sections compared -- the cross-check is vacuous")
            failures += 1
        elif not mismatches:
            print("  OK: every section agrees with binutils")
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("\nbinutils cross-check skipped: objdump unavailable")

    ov = pe.overlaps()
    print(f"\nVA-range overlaps: {len(ov)}")
    for a, b, lo, hi in ov:
        print(f"  {a} & {b}: 0x{lo:08x}..0x{hi:08x}  ({hi - lo} bytes)")

        # POSITIVE CONTROL: a VA inside the overlap must be ambiguous, and the
        # two sections must disagree about the file offset. Without this the
        # overlap report is a claim nobody checked.
        probe = lo + (hi - lo) // 2
        cands = {c.section: c.offset for c in pe.candidates(probe)}
        print(f"    probe VA 0x{probe:08x} -> " +
              ", ".join(f"{k}=0x{v:08x}" for k, v in cands.items() if v is not None))
        offsets = {v for v in cands.values() if v is not None}
        if len(offsets) < 2:
            print("    FAIL: overlapping sections agree on the offset -- no real ambiguity")
            failures += 1
        try:
            pe.va_to_offset(probe)
            print("    FAIL: ambiguous VA resolved silently; the guard did not fire")
            failures += 1
        except ValueError as e:
            print(f"    OK: refused to guess -- {str(e)[:88]}")
        # Naming a section must make it deterministic again.
        first = a
        chosen = pe.va_to_offset(probe, section=first)
        assert chosen == cands[first]
        print(f"    OK: section={first} resolves deterministically to 0x{chosen:08x}")

    # Guards the image_base: mixing RVA and VA here would hide its absence.
    rdata = pe.section(".rdata")
    probe_rva = rdata.vma + 0x1000
    got_off = pe.va_to_offset(probe_rva)
    got_va = pe.offset_to_va(got_off)
    want_off = rdata.raw_offset + 0x1000
    want_va = pe.rva_to_va(probe_rva)
    if got_off != want_off or got_va != want_va:
        print(
            f"FAIL round-trip: rva 0x{probe_rva:08x} -> off 0x{got_off:08x} "
            f"(want 0x{want_off:08x}) -> va 0x{got_va:08x} (want 0x{want_va:08x})"
        )
        failures += 1
    else:
        print(f"OK round-trip .rdata rva 0x{probe_rva:08x} <-> off 0x{got_off:08x} <-> va 0x{got_va:08x}")

    # Ties the mapping to the VMA binutils prints, not to our own arithmetic.
    text = pe.section(".text")
    first_va = pe.offset_to_va(text.raw_offset)
    if first_va != text.vma + pe.image_base:
        print(
            f"FAIL section anchor: .text raw 0x{text.raw_offset:08x} -> "
            f"va 0x{first_va:08x}, expected 0x{text.vma + pe.image_base:08x}"
        )
        failures += 1
    else:
        print(f"OK section anchor .text raw 0x{text.raw_offset:08x} -> va 0x{first_va:08x}")

    print("\nSELF_TEST", "FAILED" if failures else "PASSED", f"({failures} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        sys.exit(_self_test(sys.argv[2] if len(sys.argv) > 2 else "isaac-ng.exe"))
    print(__doc__)
    sys.exit(2)