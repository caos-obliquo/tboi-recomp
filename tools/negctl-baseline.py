#!/usr/bin/env python3
"""Negative control for the isaec test suite.

Proves the suite can actually go red before anyone trusts a green run.

A green baseline is worthless if it cannot detect a real behavioural break. This
tool applies exactly one minimal, surgical behavioural change to a *throwaway
copy* of the tree, rebuilds, runs the suite, and asserts that it fails.

It never modifies the repository. It works on a copy under a scratch directory.

Current break: the bogocrypt1 word-rotation mask, ``key & 15`` -> ``key & 7``.
Affects only the `partial_word` fixture, so a correct suite reports exactly two
failures and nothing else.

Usage:
    tools/negctl-baseline.py [--repo DIR] [--scratch DIR] [--break/--no-break]

Exit codes:
    0  negative control behaved as expected (suite went red with the expected failures)
    1  the suite stayed green under the break -- THE HARNESS IS VACUOUS
    2  setup error (repo missing, copy failed)
    3  the break did not compile -- invalid control, a syntax break proves nothing
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# The break, and the tests that own it. Keep the expectation exact: an
# under-sensitive suite and an over-broad one are both failures.
BREAK_OLD = "switch (key & 15) {"
BREAK_NEW = "switch (key & 7) {"
BREAK_FILE = Path("src/data/codec.cpp")
EXPECTED_FAILURES = {
    "codec_xor_matches_reference_fixtures",
    "archive_decode_of_a_xor_entry_matches_the_reference_decoder",
}


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def parse_results(stdout: str) -> tuple[list[str], list[str], int | None]:
    """Return (passed, failed, reported_total) from the suite's line output."""
    passed: list[str] = []
    failed: list[str] = []
    total = None
    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("ok "):
            passed.append(line[3:].split()[0])
        elif line.startswith("FAIL "):
            failed.append(line[5:].split()[0])
        elif "cases," in line and "failed" in line:
            try:
                total = int(line.split()[0])
            except ValueError:
                total = None
    return passed, failed, total


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--scratch", default="/tmp/isaec-negctl")
    ap.add_argument("--break", dest="do_break", action="store_true", default=True)
    ap.add_argument("--no-break", dest="do_break", action="store_false")
    ap.add_argument("--jobs", default="")
    args = ap.parse_args()

    repo = Path(args.repo).resolve()
    if not (repo / "CMakeLists.txt").is_file():
        print(f"FATAL: no CMakeLists.txt under {repo}", file=sys.stderr)
        return 2

    scratch = Path(args.scratch)
    tree = scratch / "tree"
    build = scratch / "build"
    if scratch.exists():
        shutil.rmtree(scratch)
    tree.mkdir(parents=True)

    # Copy the tree, excluding VCS metadata and any existing build output.
    src = run(
        ["tar", "cf", "-", "--exclude=./.git", "--exclude=./build", "."], cwd=repo
    )
    if src.returncode != 0:
        print(f"FATAL: tar failed: {src.stderr}", file=sys.stderr)
        return 2
    dst = subprocess.run(["tar", "xf", "-"], cwd=tree, input=src.stdout, capture_output=True)
    if dst.returncode != 0:
        print(f"FATAL: untar failed: {dst.stderr}", file=sys.stderr)
        return 2

    if args.do_break:
        target = tree / BREAK_FILE
        text = target.read_text()
        count = text.count(BREAK_OLD)
        if count != 1:
            print(
                f"FATAL: expected exactly 1 occurrence of {BREAK_OLD!r} in {BREAK_FILE}, "
                f"found {count}. The source moved; this control is stale.",
                file=sys.stderr,
            )
            return 2
        target.write_text(text.replace(BREAK_OLD, BREAK_NEW))
        # shutil/tar preserve mtime, which makes make skip the recompile and
        # report a phantom pass. Force the timestamp forward.
        target.touch()
        print(f"applied break: {BREAK_OLD} -> {BREAK_NEW}")

    cfg = run(["cmake", "-S", str(tree), "-B", str(build), "-DCMAKE_BUILD_TYPE=RelWithDebInfo"])
    if cfg.returncode != 0:
        print(f"FATAL: configure failed:\n{cfg.stdout}\n{cfg.stderr}", file=sys.stderr)
        return 2

    jobs = ["-j", args.jobs] if args.jobs else []
    bld = run(["cmake", "--build", str(build), *jobs])
    if bld.returncode != 0:
        print("FATAL: the break did not compile. A syntax break is not a valid control.", file=sys.stderr)
        print(bld.stdout[-4000:], file=sys.stderr)
        return 3

    res = run([str(build / "tests" / "isaec_tests")])
    passed, failed, total = parse_results(res.stdout)
    print(f"total reported: {total}")
    print(f"passed: {len(passed)}  failed: {len(failed)}")
    for name in failed:
        print(f"  FAIL {name}")

    if not args.do_break:
        # Control mode: a green baseline is expected.
        if failed:
            print(f"UNEXPECTED: clean tree has {len(failed)} failures: {failed}", file=sys.stderr)
            return 1
        print("OK: clean tree is green")
        return 0

    if not failed:
        print("VACUOUS: the suite stayed green under a known behavioural break.", file=sys.stderr)
        return 1

    unexpected = set(failed) - EXPECTED_FAILURES
    missing = EXPECTED_FAILURES - set(failed)
    if unexpected:
        print(f"UNEXPECTED failures beyond the known ones: {sorted(unexpected)}", file=sys.stderr)
        return 1
    if missing:
        print(f"EXPECTED failures that did not fire: {sorted(missing)}", file=sys.stderr)
        return 1

    print("OK: negative control behaved exactly as designed")
    return 0


if __name__ == "__main__":
    sys.exit(main())