#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


# Captures major, minor, patch, optional prerelease, and optional build metadata from v-prefixed semver tags.
SEMVER_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$")
PR_RE = re.compile(r"\(#(\d+)\)|#(\d+)")
FIX_RE = re.compile(r"\b(fix|fixed|fixes|bug|bugs|regression|error|issue|crash)\b", re.IGNORECASE)
BREAKING_RE = re.compile(r"\b(breaking|migration|migrate|config|maslow\.yaml|update required)\b", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate release notes for a Maslow firmware release.")
    parser.add_argument("--tag", required=True, help="Release tag, e.g. v1.21")
    parser.add_argument("--owner", required=True, help="GitHub owner")
    parser.add_argument("--repo", required=True, help="GitHub repository name")
    parser.add_argument("--assets-manifest", required=True, help="Path to release-assets.json")
    parser.add_argument("--output", required=True, help="Path to write markdown notes")
    return parser.parse_args()


def run_git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True, stderr=subprocess.DEVNULL).strip()


def semver_key(tag: str) -> tuple:
    match = SEMVER_RE.match(tag)
    if not match:
        return (-1, -1, -1, tag)
    major, minor, patch, prerelease, build = match.groups()
    return (int(major), int(minor), int(patch), prerelease or "", build or "")


def find_previous_tag(current_tag: str) -> str | None:
    try:
        tags = run_git("tag", "--list", "v*.*.*").splitlines()
    except subprocess.CalledProcessError:
        return None
    semver_tags = sorted((tag for tag in tags if SEMVER_RE.match(tag)), key=semver_key, reverse=True)
    for index, tag in enumerate(semver_tags):
        if tag == current_tag and index + 1 < len(semver_tags):
            return semver_tags[index + 1]
    older = [tag for tag in semver_tags if semver_key(tag) < semver_key(current_tag)]
    return older[0] if older else None


def load_commits(current_tag: str, previous_tag: str | None) -> list[dict]:
    commit_range = f"{previous_tag}..{current_tag}" if previous_tag else current_tag
    try:
        output = subprocess.check_output(
            [
                "git",
                "log",
                commit_range,
                "--pretty=format:%H%x1f%s%x1f%b%x1e",
                "--name-only",
            ],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        return []

    commits = []
    for block in output.split("\x1e"):
        block = block.strip()
        if not block:
            continue
        header, *file_lines = block.splitlines()
        parts = header.split("\x1f")
        if len(parts) < 3:
            continue
        sha, subject, body = parts[0], parts[1].strip(), parts[2].strip()
        files = [line.strip() for line in file_lines if line.strip()]
        commits.append({"sha": sha, "subject": subject, "body": body, "files": files})
    return commits


def extract_pr_number(commit: dict) -> str | None:
    haystack = f"{commit['subject']}\n{commit['body']}"
    match = PR_RE.search(haystack)
    if not match:
        return None
    return match.group(1) or match.group(2)


def commit_line(commit: dict, owner: str, repo: str) -> str:
    pr_number = extract_pr_number(commit)
    sha_short = commit["sha"][:7]
    if pr_number:
        return f"- {commit['subject']} ([#{pr_number}](https://github.com/{owner}/{repo}/pull/{pr_number}), `{sha_short}`)"
    return f"- {commit['subject']} (`{sha_short}`)"


def is_ui_commit(commit: dict) -> bool:
    return any(path.startswith("ESP3D-WEBUI/") or path.startswith("firmware/embedded/") for path in commit["files"])


def is_firmware_commit(commit: dict) -> bool:
    return any(path.startswith("firmware/") or path.startswith(".github/workflows/") or path.startswith("scripts/") for path in commit["files"])


def is_docs_only(commit: dict) -> bool:
    return bool(commit["files"]) and all(path.startswith("docs/") or path.endswith(".md") for path in commit["files"])


def section_lines(commits: list[dict], owner: str, repo: str, limit: int | None = None) -> list[str]:
    lines = [commit_line(commit, owner, repo) for commit in commits]
    return lines[:limit] if limit else lines


def make_highlights(commits: list[dict], owner: str, repo: str) -> list[str]:
    preferred = [commit for commit in commits if not is_docs_only(commit)]
    if not preferred:
        preferred = commits
    ordered = []
    seen = set()
    for commit in preferred:
        key = commit["sha"]
        if key not in seen:
            ordered.append(commit)
            seen.add(key)
    return section_lines(ordered, owner, repo, limit=5)


def human_size(num_bytes: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    size = float(num_bytes)
    for unit in units:
        if size < 1024 or unit == units[-1]:
            return f"{size:.1f} {unit}" if unit != "B" else f"{int(size)} B"
        size /= 1024


def artifacts_table(path: Path) -> list[str]:
    with path.open("r", encoding="utf-8") as handle:
        assets = json.load(handle)
    lines = [
        "| Filename | Target | Size | SHA256 |",
        "| --- | --- | ---: | --- |",
    ]
    for asset in sorted(assets, key=lambda item: item["name"]):
        lines.append(
            f"| `{asset['name']}` | `{asset['target']}` | {human_size(asset['size'])} | `{asset['sha256']}` |"
        )
    return lines


def add_section(lines: list[str], title: str, entries: list[str], fallback: str) -> None:
    lines.append(f"## {title}")
    if entries:
        lines.extend(entries)
    else:
        lines.append(fallback)
    lines.append("")


def main() -> int:
    args = parse_args()
    output_path = Path(args.output)
    previous_tag = find_previous_tag(args.tag)
    commits = load_commits(args.tag, previous_tag)

    highlights = make_highlights(commits, args.owner, args.repo)
    firmware_commits = [commit for commit in commits if is_firmware_commit(commit)]
    ui_commits = [commit for commit in commits if is_ui_commit(commit)]
    fix_commits = [commit for commit in commits if FIX_RE.search(f"{commit['subject']}\n{commit['body']}")]
    breaking_commits = [commit for commit in commits if BREAKING_RE.search(f"{commit['subject']}\n{commit['body']}")]

    compare_link = (
        f"https://github.com/{args.owner}/{args.repo}/compare/{previous_tag}...{args.tag}"
        if previous_tag
        else f"https://github.com/{args.owner}/{args.repo}/commits/{args.tag}"
    )

    lines = [f"# {args.tag} Firmware Release", ""]
    if previous_tag:
        lines.append(f"Automated release for `{args.tag}` covering changes since `{previous_tag}`.")
    else:
        lines.append(f"Automated release for `{args.tag}`.")
    lines.append("")

    add_section(lines, "Highlights", highlights, "- Automated release generated successfully; see the changelog sections below for details.")
    add_section(lines, "Firmware changes", section_lines(firmware_commits, args.owner, args.repo), "- No firmware-specific commits were detected for this release range.")
    add_section(lines, "UI/Web changes", section_lines(ui_commits, args.owner, args.repo), "- No UI or web changes were detected for this release range.")
    add_section(lines, "Fixes", section_lines(fix_commits, args.owner, args.repo), "- No explicit fix-oriented commits were detected; review the full changelog for details.")
    add_section(lines, "Breaking changes / migration notes", section_lines(breaking_commits, args.owner, args.repo), "- No breaking changes or migration notes were detected automatically.")

    lines.append("## Full changelog")
    lines.append(f"- [Compare `{previous_tag or 'initial history'}` to `{args.tag}`]({compare_link})")
    lines.append("")
    lines.append("## Artifacts")
    lines.extend(artifacts_table(Path(args.assets_manifest)))
    lines.append("")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
