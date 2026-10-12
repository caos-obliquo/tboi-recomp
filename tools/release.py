#!/usr/bin/env python3
"""Version and release-notes plumbing for tboi-recomp.

The version lives in exactly one place, the `project(... VERSION ...)` line of
CMakeLists.txt, because CMake already has to agree with whatever ships. This
script is the only thing that reads or writes it, so there is no second copy to
drift.

    release.py version            print the current version
    release.py body <version>     render the GitHub release body for it
    release.py check              exit nonzero if CHANGELOG has no section for it
    release.py bump patch         bump and rewrite CMakeLists.txt in place

`body` prefers the curated CHANGELOG section, because that is where the Why and
Effect lines live, and falls back to the commit log only when there is no
section. A release must never publish a commit list in place of the explanation
someone bothered to write.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE_LISTS = ROOT / "CMakeLists.txt"
CHANGELOG = ROOT / "CHANGELOG.md"
REPO_URL = "https://github.com/caos-obliquo/tboi-recomp"

PROJECT_LINE = re.compile(
    r"^\s*project\(\s*\S+\s+VERSION\s+(?P<version>\d+\.\d+\.\d+)\s",
    re.MULTILINE,
)
HEADER = re.compile(
    r"^## \[(?P<tag>[^\]]+)\](?: - (?P<date>[\d-]+))?[^\S\n]*$", re.MULTILINE
)


def read_version() -> str:
    text = CMAKE_LISTS.read_text(encoding="utf-8")
    match = PROJECT_LINE.search(text)
    if match is None:
        raise SystemExit(
            f"no `project(... VERSION x.y.z ...)` line in {CMAKE_LISTS.name}; "
            "this script refuses to guess the version"
        )
    return match.group("version")


def write_version(new: str) -> None:
    text = CMAKE_LISTS.read_text(encoding="utf-8")
    updated = PROJECT_LINE.sub(
        lambda m: m.group(0).replace(m.group("version"), new), text, count=1
    )
    if updated == text:
        raise SystemExit(f"rewriting the version in {CMAKE_LISTS.name} changed nothing")
    CMAKE_LISTS.write_text(updated, encoding="utf-8")


def section_for(version: str) -> tuple[str, str] | None:
    if not CHANGELOG.exists():
        return None
    text = CHANGELOG.read_text(encoding="utf-8")
    headers = list(HEADER.finditer(text))
    wanted = version.lstrip("v")

    for index, header in enumerate(headers):
        if header.group("tag").lstrip("v") != wanted:
            continue
        end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
        body = text[header.end() : end].strip()
        if not header.group("date"):
            return None
        return header.group("date"), strip_link_refs(body)

    return None


def strip_link_refs(body: str) -> str:
    """Drop the trailing `[v0.1.0]: https://...` definitions.

    They belong to the changelog's own footnote block, not in release notes,
    where the equivalent link is the Full Changelog line already appended.
    """
    kept = [
        line
        for line in body.splitlines()
        if not re.match(r"^\[[^\]]+\]:\s*\S", line)
    ]
    return "\n".join(kept).strip()


def previous_tag(version: str) -> str:
    """The highest tag strictly below `version`, for the compare link."""
    completed = subprocess.run(
        ["git", "tag", "--list", "v*", "--sort=-v:refname"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    wanted = version.lstrip("v")
    for line in completed.stdout.split():
        candidate = line.lstrip("v")
        if candidate == wanted:
            break
        return line
    return ""


def compare_url(version: str) -> str:
    """The compare link, which degrades to the tag itself on a first release."""
    tag = f"v{version.lstrip('v')}"
    previous = previous_tag(version)
    return f"{REPO_URL}/compare/{previous}...{tag}" if previous else f"{REPO_URL}/releases/tag/{tag}"


def commit_list(since: str) -> list[str]:
    args = ["git", "log", "--no-merges", "--pretty=- %s"]
    args += [f"{since}..HEAD"] if since else ["HEAD"]
    out = subprocess.run(
        args, cwd=ROOT, capture_output=True, text=True, check=False
    )
    return [line for line in out.stdout.splitlines() if line.startswith("- ")]


def render_body(version: str) -> str:
    tag = f"v{version.lstrip('v')}"
    section = section_for(version)
    parts = [f"## {tag}", ""]

    if section is not None:
        date, body = section
        parts += [
            f"Released {date}.", "",
            "### Changes", "",
            body, "",
            f"**Full Changelog**: {compare_url(version)}",
        ]
    else:
        parts += [
            "### Changes", "",
            "No CHANGELOG section was written for this version, so this is a bare "
            "commit list and not an explanation.", "",
        ]
        parts += commit_list(previous_tag(version))
        parts += ["", f"**Full Changelog**: {compare_url(version)}"]

    return "\n".join(parts).rstrip() + "\n"


def cmd_bump(part: str) -> int:
    old = read_version()
    major, minor, patch = (int(p) for p in old.split("."))
    new = {
        "major": f"{major + 1}.0.0",
        "minor": f"{major}.{minor + 1}.0",
        "patch": f"{major}.{minor}.{patch + 1}",
    }[part]
    write_version(new)
    today = dt.date.today().isoformat()
    print(f"{old} -> {new}   (today is {today})")
    print(
        f"next: add a `## [{new}] - {today}` section to {CHANGELOG.name}, "
        f"commit, push, then tag {new}"
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("version")
    body = sub.add_parser("body")
    body.add_argument("version")
    sub.add_parser("check")
    bump = sub.add_parser("bump")
    bump.add_argument("part", choices=["major", "minor", "patch"])

    args = parser.parse_args(argv)

    if args.command == "version":
        print(read_version())
        return 0
    if args.command == "body":
        print(render_body(args.version), end="")
        return 0
    if args.command == "bump":
        return cmd_bump(args.part)
    if args.command == "check":
        version = read_version()
        if section_for(version) is None:
            print(
                f"::error::CMakeLists.txt is at {version} but {CHANGELOG.name} has no "
                f"`## [v{version}]` section, so the release notes would fall back to a "
                "bare commit list",
                file=sys.stderr,
            )
            return 1
        print(f"changelog ok for {version}")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))