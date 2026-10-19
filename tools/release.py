#!/usr/bin/env python3
"""Version and release-notes plumbing for tboi-recomp.

The version lives in exactly one place, the `project(... VERSION ...)` line of
CMakeLists.txt, because CMake already has to agree with whatever ships. This
script is the only thing that reads or writes it, so there is no second copy to
drift.

    release.py version            print the current version
    release.py body <version>     render the GitHub release body for it
    release.py check              exit nonzero if CHANGELOG has no section for it
    release.py documented         print the newest version that has a section
    release.py bump patch         bump and rewrite CMakeLists.txt in place
    release.py bump-notes --pr N  bump patch and write the CHANGELOG section
                              from pull request N: title, body, linked
                              issues, changed files and docs touched

`body` prefers the curated CHANGELOG section, because that is where the Why and
Effect lines live, and falls back to the commit log only when there is no
section. A release must never publish a commit list in place of the explanation
someone bothered to write.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
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


def latest_documented() -> str | None:
    """The newest version that has a dated CHANGELOG section.

    This is not the same as the declared version. The Release workflow bumps
    the patch after publishing, so main normally sits one version ahead of the
    changelog until the next section is written.
    """
    if not CHANGELOG.exists():
        return None
    found = [
        match.group("tag").lstrip("v")
        for match in HEADER.finditer(CHANGELOG.read_text(encoding="utf-8"))
        if match.group("date") and re.fullmatch(r"v?[\d.]+", match.group("tag"))
    ]
    return found[0] if found else None


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


def gh_pr(number: str) -> dict:
    """Pull request metadata, from the GitHub CLI that runners already have."""
    out = subprocess.run(
        ["gh", "pr", "view", str(number),
         "--json", "title,body,number,mergedAt,files,url,commits,"
                  "comments,reviews,author"],
        capture_output=True, text=True, check=True,
    )
    return json.loads(out.stdout)


def gh_issue(number: int) -> dict:
    out = subprocess.run(
        ["gh", "issue", "view", str(number), "--json", "title,state,url"],
        capture_output=True, text=True, check=True,
    )
    return json.loads(out.stdout)


def short(oid: str) -> str:
    return oid[:8]


def compose_version_section(pr: dict, version: str, date: str, carried: str) -> str:
    """Build the `## [x.y.z]` section for one merged pull request.

    Deliberately large. A changelog is the durable record of what shipped and why,
    and a terse one-line summary is what forces the next session to redig the pull
    request in order to understand it. Every field here is pulled from GitHub, so
    the section cannot drift from the change it describes.

    `carried` is whatever was sitting under `## [Unreleased]`, which this version
    is taking out of flight.
    """
    lines = [f"## [{version}] - {date}", ""]
    lines.append(
        f"### Merged: [#{pr['number']}]({pr.get('url', '')}) — {pr['title']}"
    )
    lines.append("")

    files = pr.get("files") or []
    adds = sum(f.get("additions", 0) for f in files)
    dels = sum(f.get("deletions", 0) for f in files)
    author = (pr.get("author") or {}).get("login", "unknown")
    merged = (pr.get("mergedAt") or "")[:10]
    lines += [
        f"**Author:** @{author}  **Merged:** {merged or date}  "
        f"**Files:** {len(files)} changed, +{adds}/-{dels}.",
        "",
    ]

    body = (pr.get("body") or "").strip()
    if body:
        lines += [
            "<details>",
            "<summary>Pull request body</summary>",
            "",
            body,
            "",
            "</details>",
            "",
        ]

    linked = sorted({int(n) for n in re.findall(r"#(\d+)", body or "")})
    if linked:
        lines.append("#### Closes")
        lines.append("")
        for n in linked:
            info = gh_issue(n)
            if not info:
                continue
            state = info.get("state", "?").lower()
            marker = "x" if state == "closed" else " "
            lines.append(
                f"- [`{marker}`] [#{n}]({info.get('url', '')}) "
                f"{info.get('title', '')} ({state})"
            )
        lines.append("")

    commits = pr.get("commits") or []
    if commits:
        lines.append("#### Commits")
        lines.append("")
        for commit in commits:
            # The GitHub CLI returns these as messageHeadline/messageBody,
            # not as a single `message`. Reading the wrong key produced a
            # changelog of 26 identical "(no subject)" lines.
            subject = (commit.get("messageHeadline") or "").strip() or "(no subject)"
            detail = (commit.get("messageBody") or "").strip()
            lines.append(f"- `{short(commit.get('oid', ''))}` {subject}")
            if detail:
                lines += [f"  {line}" for line in detail.splitlines() if line.strip()]
        lines.append("")

    if files:
        lines.append("#### Files changed")
        lines.append("")
        lines.append("| file | + | - |")
        lines.append("|---|---:|---:|")
        for f in sorted(files, key=lambda x: str(x.get("path", ""))):
            lines.append(
                f"| `{f.get('path', '')}` | {f.get('additions', 0)} "
                f"| {f.get('deletions', 0)} |"
            )
        lines.append("")

    docs = sorted(f.get("path", "") for f in files if str(f.get("path", "")).endswith(".md"))
    if docs:
        lines.append("#### Documentation")
        lines.append("")
        lines += [f"- `{d}`" for d in docs]
        lines.append("")

    comments = pr.get("comments") or []
    if comments:
        lines.append("#### Discussion")
        lines.append("")
        for comment in comments:
            who = (comment.get("author") or {}).get("login", "unknown")
            when = (comment.get("createdAt") or "")[:10]
            text = (comment.get("body") or "").strip()
            lines.append(f"**@{who}** ({when}):")
            lines.append("")
            for paragraph in text.split("\n\n"):
                lines += [f"> {line}" for line in paragraph.splitlines()]
                lines.append(">")
        lines.append("")

    reviews = pr.get("reviews") or []
    if reviews:
        lines.append("#### Review")
        lines.append("")
        for review in reviews:
            who = (review.get("author") or {}).get("login", "unknown")
            state = review.get("state", "?")
            text = (review.get("body") or "").strip()
            lines.append(f"- **@{who}** — {state}" + (f": {text}" if text else ""))
        lines.append("")

    if carried.strip():
        lines += ["#### Carried from Unreleased", "", carried.strip(), ""]

    return "\n".join(lines).rstrip() + "\n"


def cmd_bump_notes(pr_number: str) -> int:
    """Bump the patch and record the pull request that caused it.

    The section is written *before* the bump is reported, so a failure here
    leaves CMakeLists.txt untouched: a version with no notes must never exist,
    because `release.py check` would then pass a release built on a commit list.
    """
    old = read_version()
    major, minor, patch = (int(part) for part in old.split("."))
    new = f"{major}.{minor}.{patch + 1}"

    pr = gh_pr(pr_number)

    text = CHANGELOG.read_text(encoding="utf-8")
    headers = list(HEADER.finditer(text))
    unreleased = next(
        (h for h in headers if h.group("tag").lstrip("v").lower() == "unreleased"),
        None,
    )
    if unreleased is None:
        raise SystemExit(f"{CHANGELOG.name} has no `## [Unreleased]` section")

    index = headers.index(unreleased)
    carried_end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
    carried = text[unreleased.end():carried_end].strip()
    section = compose_version_section(pr, new, dt.date.today().isoformat(), carried)

    # Replace everything from `## [Unreleased]` up to the next version header
    # with the new section followed by a fresh, empty Unreleased.
    text = (
        text[:unreleased.start()]
        + section
        + "\n\n## [Unreleased]\n\n"
        + text[carried_end:]
    )

    write_version(new)
    CHANGELOG.write_text(text, encoding="utf-8")
    print(f"{old} -> {new}   (PR #{pr_number})")
    return 0


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
    sub.add_parser("documented")
    body = sub.add_parser("body")
    body.add_argument("version")
    check = sub.add_parser("check")
    check.add_argument(
        "--expect",
        metavar="X.Y.Z",
        help="check this version instead of the one declared in CMakeLists.txt",
    )
    bump = sub.add_parser("bump")
    bump.add_argument("part", choices=["major", "minor", "patch"])
    bump_notes = sub.add_parser(
        "bump-notes",
        help="bump patch and write the changelog section from a merged PR",
    )
    bump_notes.add_argument("--pr", required=True, metavar="N",
                            help="merged pull request number")

    args = parser.parse_args(argv)

    if args.command == "version":
        print(read_version())
        return 0
    if args.command == "body":
        print(render_body(args.version), end="")
        return 0
    if args.command == "documented":
        latest = latest_documented()
        if latest is None:
            print(f"::error::{CHANGELOG.name} has no dated version section", file=sys.stderr)
            return 1
        print(latest)
        return 0
    if args.command == "bump":
        return cmd_bump(args.part)
    if args.command == "bump-notes":
        return cmd_bump_notes(args.pr)
    if args.command == "check":
        version = args.expect or read_version()
        if args.expect is None and section_for(version) is None:
            print(
                f"::error::CMakeLists.txt is at {version} but {CHANGELOG.name} has no "
                f"`## [v{version}]` section, so the release notes would fall back to a "
                "bare commit list",
                file=sys.stderr,
            )
            return 1
        if section_for(version) is None:
            print(
                f"::error::{CHANGELOG.name} has no `## [v{version}]` section",
                file=sys.stderr,
            )
            return 1
        print(f"changelog ok for {version}")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))