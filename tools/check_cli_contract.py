#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "I2cTransport.h",
    "I2cScanner.h",
    "CommandHandler.h",
    "TransportAdapter.h",
    "BusDiag.h",
    "CliShell.h",
    "CliStyle.h",
    "HealthView.h",
    "HealthDiag.h",
]

MANDATORY_COMMANDS = [
    "help",
    "?",
    "version",
    "ver",
    "scan",
    "begin",
    "addr",
    "res",
    "rab",
    "probe",
    "recover",
    "iface_reset",
    "defaults",
    "drv",
    "cfg",
    "settings",
    "info",
    "read",
    "rregs",
    "dump",
    "last",
    "rreg",
    "wreg",
    "wregs",
    "wiper",
    "frac",
    "pos",
    "zero",
    "mid",
    "max",
    "inc",
    "dec",
    "tcon",
    "term",
    "shutdown",
    "mode",
    "gc",
    "selftest",
    "verbose",
    "stress",
    "stress_mix",
]


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    for cmd in MANDATORY_COMMANDS:
        dispatch_re = re.compile(
            rf'strcmp\s*\(\s*command\s*,\s*"{re.escape(cmd)}"\s*\)\s*==\s*0'
        )
        if dispatch_re.search(text) is None:
            fail(f"mandatory command '{cmd}' missing from handleCommand() dispatch")
        if cmd != "?":
            help_re = re.compile(rf'printHelpItem\s*\(\s*"[^"]*\b{re.escape(cmd)}\b')
            if help_re.search(text) is None:
                fail(f"mandatory command '{cmd}' missing from help text")

    if re.search(r"\bcfg\b", text) is None and re.search(r"\bsettings\b", text) is None:
        fail("either 'cfg' or 'settings' command must be present")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
