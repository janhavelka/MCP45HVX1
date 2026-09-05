#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys
from typing import Dict

from contract_common import strip_non_code

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "include")
VALID_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

FORBIDDEN_CALLS = {
    "millis": re.compile(r"\bmillis\s*\("),
    "micros": re.compile(r"\bmicros\s*\("),
    "esp_timer_get_time": re.compile(r"\besp_timer_get_time\s*\("),
    "delay": re.compile(r"\bdelay\s*\("),
    "delayMicroseconds": re.compile(r"\bdelayMicroseconds\s*\("),
    "yield": re.compile(r"\byield\s*\("),
}

INCLUDE_ARDUINO_RE = re.compile(r'^\s*#\s*include\s*[<\"]Arduino\.h[>\"]', re.MULTILINE)
INCLUDE_IDF_RE = re.compile(r'^\s*#\s*include\s*[<\"]esp_timer\.h[>\"]', re.MULTILINE)
INCLUDE_WIRE_RE = re.compile(r'^\s*#\s*include\s*[<\"]Wire\.h[>\"]', re.MULTILINE)
FORBIDDEN_CORE_TOKENS = {
    "TwoWire": re.compile(r"\bTwoWire\b"),
    "Wire": re.compile(r"\bWire\b"),
    "Serial": re.compile(r"\bSerial\b"),
    "Print": re.compile(r"\bPrint\b"),
    "String": re.compile(r"\bString\b"),
    "HardwareSerial": re.compile(r"\bHardwareSerial\b"),
}


def collect_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for dirname in SCAN_DIRS:
        root = ROOT / dirname
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in VALID_SUFFIXES:
                files.append(path)
    return files


def main() -> int:
    observed_calls: Dict[str, Dict[str, int]] = {}
    observed_includes: Dict[str, int] = {}
    observed_wire_includes: Dict[str, int] = {}
    observed_tokens: Dict[str, Dict[str, int]] = {}

    for path in collect_sources():
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = strip_non_code(raw)

        call_counts: Dict[str, int] = {}
        for call_name, pattern in FORBIDDEN_CALLS.items():
            count = len(pattern.findall(code))
            if count > 0:
                call_counts[call_name] = count
        if call_counts:
            observed_calls[rel] = call_counts

        include_count = len(INCLUDE_ARDUINO_RE.findall(raw)) + len(INCLUDE_IDF_RE.findall(raw))
        if include_count > 0:
            observed_includes[rel] = include_count
        wire_include_count = len(INCLUDE_WIRE_RE.findall(raw))
        if wire_include_count > 0:
            observed_wire_includes[rel] = wire_include_count

        token_counts: Dict[str, int] = {}
        for token_name, pattern in FORBIDDEN_CORE_TOKENS.items():
            count = len(pattern.findall(code))
            if count > 0:
                token_counts[token_name] = count
        if token_counts:
            observed_tokens[rel] = token_counts

    errors: list[str] = []

    for rel, counts in observed_calls.items():
        errors.append(f"timing calls forbidden in core file {rel}: {counts}")

    for rel, count in observed_includes.items():
        errors.append(f"framework include forbidden in core file {rel}: observed={count}")

    for rel, count in observed_wire_includes.items():
        if count != 0:
            errors.append(f"Wire include forbidden in core file {rel}: observed={count}")

    for rel, counts in observed_tokens.items():
        errors.append(f"framework tokens forbidden in core file {rel}: {counts}")

    if errors:
        print("Core timing guard FAILED:")
        for err in errors:
            print(f"- {err}")
        return 1

    print("Core timing guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
