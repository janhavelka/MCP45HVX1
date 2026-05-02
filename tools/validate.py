#!/usr/bin/env python3
from __future__ import annotations

import shutil
import subprocess
import sys
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str], required: bool = True) -> bool:
    print("+ " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0 and required:
        raise SystemExit(result.returncode)
    return result.returncode == 0


def main() -> int:
    python = sys.executable
    run([python, "tools/check_cli_contract.py"])
    run([python, "tools/check_core_timing_guard.py"])
    run([python, "scripts/generate_version.py", "check"])
    obj = os.devnull

    if shutil.which("g++"):
        run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror=return-type",
            "-Iinclude", "-Itest/stubs", "-c", "src/MCP45HVX1.cpp", "-o", obj
        ])
        run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror=return-type",
            "-I.", "-Iinclude", "-Iexamples", "-Itest/stubs", "-c",
            "examples/01_basic_bringup_cli/main.cpp", "-o", obj
        ])
    else:
        print("SKIP: g++ not found")

    if shutil.which("pio"):
        run(["pio", "test", "-e", "native"])
        run(["pio", "run", "-e", "esp32s3dev"])
        run(["pio", "run", "-e", "esp32s2dev"])
    else:
        print("SKIP: pio not found")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
