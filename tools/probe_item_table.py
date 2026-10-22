#!/usr/bin/env python3
"""Extract the item table from the game's LZW-compressed resources/items.xml.

The archive is mode 0x01 (LZW, see docs/arch000-format.md). LZW diverges past the
first chunk (issue #52), so only the items that survive the partial decode are
extracted here. The result is a JSON list of {id, name, description, gfx, cache}
records, written to --out.

Usage:
    python3 tools/probe_item_table.py <exe-or-archive> --out bindings/item-table.json
    python3 tools/probe_item_table.py --self-test
"""
import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

ARCHIVE = Path("/home/caos/.local/share/Steam/steamapps/common/The Binding of Isaac Rebirth/resources/packed/config.a")


def load_archive_module():
    spec = importlib.util.spec_from_file_location("ia", "tools/isaac-archive.py")
    ia = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ia)
    return ia


def extract_items(archive_path, entry_index=14):
    ia = load_archive_module()
    mode, entries = ia.read_archive(archive_path)
    blob = archive_path.read_bytes()
    e = entries[entry_index]
    nxt = entries[entry_index + 1][2] if entry_index + 1 < len(entries) else len(blob)
    span = nxt - e[2]
    raw = ia.decode_lzw(blob, e[2], span)
    # Keep only the valid XML prefix: truncate at the first non-text byte.
    cut = len(raw)
    for i, b in enumerate(raw):
        if b not in (9, 10, 13) and not (32 <= b < 127):
            cut = i
            break
    text = raw[:cut].decode("latin1", errors="replace")
    items = []
    for m in re.finditer(r'<(\w+)\s+([^>]*)/>', text):
        kind = m.group(1)
        attrs = m.group(2)
        def attr(name):
            mm = re.search(r'\b' + name + r'="([^"]*)"', attrs)
            return mm.group(1) if mm else None
        items.append({
            "kind": kind,
            "id": attr("id"),
            "name": attr("name"),
            "description": attr("description"),
            "gfx": attr("gfx"),
            "cache": attr("cache"),
        })
    return items, cut, len(raw)


def self_test():
    # The first item of items.xml is "The Sad Onion" (id 1).
    archive = ARCHIVE
    if not archive.exists():
        print("SELF-TEST SKIPPED: archive not present")
        return 0
    items, cut, total = extract_items(archive)
    assert len(items) >= 7, f"expected at least 7 items, got {len(items)}"
    assert items[0]["name"] == "The Sad Onion", items[0]
    assert items[0]["id"] == "1"
    print(f"SELF-TEST PASSED: {len(items)} items extracted (valid prefix {cut} of {total} decoded bytes)")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archive", nargs="?", default=str(ARCHIVE))
    ap.add_argument("--out", default="bindings/item-table.json")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    archive = Path(args.archive)
    items, cut, total = extract_items(archive)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"source": str(archive), "decoded_bytes": total,
                              "valid_prefix_bytes": cut, "items": items}, indent=2))
    print(f"wrote {len(items)} items to {out} (valid prefix {cut} of {total} decoded bytes)")


if __name__ == "__main__":
    raise SystemExit(main())
