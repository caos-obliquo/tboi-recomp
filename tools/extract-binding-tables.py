#!/usr/bin/env python3
"""Survey the string regions that surround each Lua binding class anchor.

The shipped binary stores class method names as dense runs of NUL-terminated
identifiers. Measured inter-token gaps inside a real table are 0-3 bytes and a
gap of 4 or more ends a run, so this tool splits on that boundary instead of
guessing a fixed byte span.

WHAT THIS OUTPUT IS NOT
Per-class membership. Adjacency only proves a name lives in the same string
region as the class anchor. Measured evidence that this matters: Font has 8
methods but its region yields 531 candidate names, because the neighbouring
tables are equally dense. Every class reports roughly 500 candidates here for
the same reason.

Recovering true per-class membership requires reading the code that builds the
metatable, not the string pool. Until that disassembly pass exists, treat these
counts as region sizes, not API sizes. See issue #4.
"""

import json
import re
import sys

MAX_GAP = 3  # measured: separators are 0-3 bytes, run boundaries are >= 4
MIN_NAME = 3

DEFAULT_EXE = (
    "/home/caos/.local/share/Steam/steamapps/common/"
    "The Binding of Isaac Rebirth/isaac-ng.exe"
)

# File offset of the NUL-terminated class-name anchor for each class.
CLASS_ANCHORS = {
    "Entity": 0x76ECB8,
    "EntityPlayer": 0x76FB48,
    "EntityTear": 0x76FAB0,
    "EntityPickup": 0x770CDC,
    "EntityFamiliar": 0x770EC4,
    "EntityProjectile": 0x7710BC,
    "EntityBomb": 0x77130C,
    "EntityLaser": 0x7712B0,
    "EntityKnife": 0x771584,
    "Input": 0x76B7D4,
    "Vector": 0x76E7DC,
    "KColor": 0x76E8E8,
    "Font": 0x76EA24,
    "Sprite": 0x76EAAC,
    "Mod": 0x763705,
    "Slot": 0x76C104,
    "Level": 0x772D53,
    "MusicManager": 0x77327C,
    "Room": 0x77BA59,
    "Color": 0x77BC9F,
    "Game": 0x71B5F9,
    "HUD": 0x769938,
    "Isaac": 0x76AD80,
}

METATABLE_KEYS = {
    "__class", "__parent", "__index", "__propget", "__propset",
    "__newindex", "__const", "__metatable", "__type",
}

LUA_MODULES = {
    "coroutine", "debug", "io", "math", "os", "package",
    "string", "table", "tostring", "utf8",
}

TOKEN = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{%d,63}\x00" % (MIN_NAME - 1))

# Sprite and HUD asset filenames that sit inside the same string region.
ASSET = re.compile(
    r"^(Icon\w+|Text\w+|BarOverlay\d*|.*Arrow|IdleOn|IdleOff|Loading"
    r"|.*HeartFull|.*HeartEmpty)$"
)


def anchor_name(data, offset):
    end = data.index(b"\x00", offset)
    return data[offset:end].decode("ascii", "replace")


def dense_runs(data, start, limit):
    """Yield maximal runs of identifiers whose inter-token gap is <= MAX_GAP."""
    stop = min(len(data), start + limit)
    run = []
    prev_end = None
    for match in TOKEN.finditer(data, start, stop):
        if prev_end is not None and match.start() - prev_end > MAX_GAP:
            yield run
            run = []
        run.append((match.start(), match.group()[:-1].decode("ascii")))
        prev_end = match.end()
    if run:
        yield run


def classify(name):
    if name in METATABLE_KEYS:
        return "metatable"
    if name in LUA_MODULES:
        return "stdlib"
    if ASSET.match(name):
        return "asset"
    return None


def extract(data, cls, offset, window):
    names = []
    dropped = {"metatable": 0, "stdlib": 0, "asset": 0}
    runs = 0
    for run in dense_runs(data, data.index(b"\x00", offset) + 1, window):
        runs += 1
        for _, name in run:
            bucket = classify(name)
            if bucket:
                dropped[bucket] += 1
            else:
                names.append(name)
    return {
        "anchor": hex(offset),
        "anchor_verified": anchor_name(data, offset) == cls,
        "runs": runs,
        "candidates": sorted(set(names)),
        "dropped": dropped,
    }


def main(argv):
    exe = argv[1] if len(argv) > 1 else DEFAULT_EXE
    window = int(argv[2], 0) if len(argv) > 2 else 8192
    with open(exe, "rb") as handle:
        data = handle.read()

    result = {}
    for cls, offset in sorted(CLASS_ANCHORS.items(), key=lambda kv: kv[1]):
        if anchor_name(data, offset) != cls:
            raise SystemExit(
                "anchor mismatch for %s at %s: found %r"
                % (cls, hex(offset), anchor_name(data, offset))
            )
        result[cls] = extract(data, cls, offset, window)

    unverified = [c for c, v in result.items() if not v["anchor_verified"]]
    if unverified:
        raise SystemExit("unverified anchors: %s" % unverified)

    unique = set()
    for value in result.values():
        unique.update(value["candidates"])

    json.dump(
        {"source": exe, "max_gap": MAX_GAP, "window": window, "classes": result},
        sys.stdout,
        indent=1,
        sort_keys=True,
    )
    sys.stderr.write(
        "\n%d classes, %d unique candidates\n" % (len(result), len(unique))
    )


if __name__ == "__main__":
    main(sys.argv)
