#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str], required: bool = True) -> bool:
    print("+ " + " ".join(cmd))
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0 and required:
        raise SystemExit(result.returncode)
    return result.returncode == 0


def platformio_command() -> list[str] | None:
    if os.name == "nt":
        wrapper = ROOT / "scripts" / "pio.cmd"
        if not wrapper.is_file():
            raise SystemExit(f"PlatformIO wrapper not found: {wrapper}")
        return [str(wrapper)]
    executable = shutil.which("pio")
    return [executable] if executable else None


def main() -> int:
    python = sys.executable
    run([python, "tools/check_cli_contract.py"])
    run([python, "tools/check_idf_example_contract.py"])
    run([python, "tools/check_core_timing_guard.py"])
    run([python, "tools/check_generated_artifacts.py"])
    run([python, "tools/test_run_hil_mcp45hvx1_parser.py"])
    run([python, "tools/test_contract_tools.py"])
    run([python, "tools/run_hil_mcp45hvx1.py", "--parser-self-test"])
    run([python, "scripts/generate_version.py", "check"])
    obj = os.devnull

    if shutil.which("g++"):
        run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror=return-type",
            "-Iinclude", "-c", "src/MCP45HVX1.cpp", "-o", obj
        ])
        run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror=return-type",
            "-I.", "-Iinclude", "-Iexamples", "-Itest/stubs", "-c",
            "examples/01_basic_bringup_cli/main.cpp", "-o", obj
        ])
        run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror=return-type",
            "-DMCP45HVX1_CLI_ENABLE_COLOR=0", "-DMCP45HVX1_CLI_COLOR_DEFAULT=0",
            "-I.", "-Iinclude", "-Iexamples", "-Itest/stubs", "-c",
            "examples/01_basic_bringup_cli/main.cpp", "-o", obj
        ])
        with tempfile.TemporaryDirectory(prefix="mcp45hvx1-cli-") as tmp:
            cli_test = str(Path(tmp) / ("cli-test.exe" if os.name == "nt" else "cli-test"))
            run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror=return-type",
                "-I.", "-Iinclude", "-Iexamples", "-Itest/stubs",
                "tests/test_arduino_cli.cpp", "src/MCP45HVX1.cpp", "-o", cli_test,
            ])
            run([cli_test])
    else:
        print("SKIP: g++ not found")

    pio = platformio_command()
    if pio:
        run([*pio, "test", "-e", "native"])
        run([*pio, "run", "-e", "esp32s3dev"])
        run([*pio, "run", "-e", "esp32s2dev"])
    else:
        print("SKIP: pio not found")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
