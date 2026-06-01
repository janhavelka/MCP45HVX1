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
]


def main() -> int:
    result = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        print(result.stderr.strip())
        return result.returncode

    errors: list[str] = []
    for rel in result.stdout.splitlines():
        normalized = rel.replace("\\", "/")
        if normalized == "include/MCP45HVX1/Version.h":
            continue
        if any(pattern.search(normalized) for pattern in FORBIDDEN_PATTERNS):
            errors.append(normalized)

    if errors:
        print("Generated artifact check FAILED:")
        for rel in errors:
            print(f"- {rel}")
        return 1

    print("Generated artifact check PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
