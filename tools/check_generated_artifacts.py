#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FORBIDDEN_PATTERNS = [
    re.compile(r"(^|/)\.pio/"),
    re.compile(r"(^|/)__pycache__/"),
    re.compile(r"(^|/)(html|xml|latex)/"),
    re.compile(r"\.(o|obj|a|lib|map|elf|bin|hex|gcda|gcno|tar|tgz|zip)$", re.IGNORECASE),
    re.compile(r"\.tar\.gz$", re.IGNORECASE),
]

IGNORED_ROOT_ARCHIVE_GLOBS = ("*.tar.gz", "*.tgz", "*.zip")


def git_lines(args: list[str]) -> tuple[int, list[str], str]:
    result = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return result.returncode, [], result.stderr.strip()
    return result.returncode, result.stdout.splitlines(), ""


def main() -> int:
    tracked_rc, tracked, tracked_err = git_lines(["ls-files"])
    if tracked_rc != 0:
        print(tracked_err)
        return tracked_rc
    untracked_rc, untracked, untracked_err = git_lines(["ls-files", "--others", "--exclude-standard"])
    if untracked_rc != 0:
        print(untracked_err)
        return untracked_rc

    errors: list[str] = []
    for rel in tracked:
        normalized = rel.replace("\\", "/")
        if normalized == "include/MCP45HVX1/Version.h":
            continue
        if any(pattern.search(normalized) for pattern in FORBIDDEN_PATTERNS):
            errors.append(f"tracked generated artifact: {normalized}")
    for rel in untracked:
        normalized = rel.replace("\\", "/")
        if any(pattern.search(normalized) for pattern in FORBIDDEN_PATTERNS):
            errors.append(f"untracked generated artifact: {normalized}")
    for glob in IGNORED_ROOT_ARCHIVE_GLOBS:
        for path in ROOT.glob(glob):
            if path.is_file():
                errors.append(f"ignored generated artifact: {path.name}")

    if errors:
        print("Generated artifact check FAILED:")
        for rel in errors:
            print(f"- {rel}")
        return 1

    print("Generated artifact check PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
