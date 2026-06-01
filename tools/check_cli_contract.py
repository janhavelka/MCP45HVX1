#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import runpy
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
    "color",
    "begin",
    "addr",
    "addr_alt",
    "res",
    "variant",
    "rab",
    "probe",
    "recover",
    "iface_reset",
    "defaults",
    "drv",
    "cfg",
    "settings",
    "detail",
    "health",
    "state",
    "info",
    "errata",
    "read",
    "rregs",
    "readwiper",
    "readtcon",
    "dump",
    "raw",
    "last",
    "reg",
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
    "terminal",
    "shutdown",
    "software-shutdown",
    "mode",
    "gc",
    "selftest",
    "verbose",
    "stress",
    "stress_mix",
]
GENERAL_CALL_SUBCOMMANDS = ["arm", "disarm", "wiper", "tcon", "inc", "dec"]
GENERAL_CALL_IDF_TOKENS = [
    "addr == 0x00U",
    "transmitWithManualAddress",
    "I2C_DEVICE_ADDRESS_NOT_USED",
    "addressByte = static_cast<uint8_t>(addr << 1)",
    "i2c_master_execute_defined_operations",
]

IDF_REQUIRED_COMPONENTS = [
    "MCP45HVX1",
    "esp_driver_i2c",
    "esp_driver_gpio",
    "esp_timer",
    "freertos",
    "vfs",
]

COMMAND_ACTIONS = {
    "scan": "bus_diag::scan",
    "color": "handleColor",
    "begin": "handleBegin",
    "addr": "handleAddress",
    "addr_alt": "handleAlternateAddress",
    "variant": "handleResolution",
    "rab": "handleResistance",
    "probe": "gDev.probe",
    "recover": "gDev.recover",
    "defaults": "gDev.restorePowerOnDefaults",
    "read": "readRegisters",
    "readwiper": "handleWiper",
    "readtcon": "handleTcon",
    "raw": "handleRaw",
    "reg": "handleReadRegister",
    "wreg": "handleWriteRegister",
    "wiper": "handleWiper",
    "tcon": "handleTcon",
    "terminal": "handleTerm",
    "software-shutdown": "handleShutdown",
    "cfg": "printConfigSnapshot",
    "health": "printDriverHealth",
    "state": "printStateLine",
    "selftest": "handleSelfTest",
    "stress": "runStress",
    "stress_mix": "runStress",
    "verbose": "handleVerbose",
    "gc": "handleGeneralCall",
}

FORBIDDEN_PLACEHOLDER_RE = re.compile(
    r"\b(TODO|FIXME|placeholder|stub|not implemented)\b", re.IGNORECASE
)


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def require_token(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"{label} missing token '{token}'")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    idf_main = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    idf_cmake = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")
    ensure_exists(idf_main, "ESP-IDF bringup entry point")
    ensure_exists(idf_cmake, "ESP-IDF bringup CMake file")

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

    for cmd, token in COMMAND_ACTIONS.items():
        if token not in text:
            fail(f"command '{cmd}' missing expected action token '{token}'")

    general_call_body = text[text.find("void handleGeneralCall") :]
    for sub in ("arm", "disarm", "wiper", "tcon", "inc", "dec"):
        if f'"{sub}"' not in general_call_body:
            fail(f"General Call subcommand '{sub}' missing handler branch")

    if re.search(r"\bcfg\b", text) is None and re.search(r"\bsettings\b", text) is None:
        fail("either 'cfg' or 'settings' command must be present")

    for subcommand in GENERAL_CALL_SUBCOMMANDS:
        help_re = re.compile(rf"\bgc\s+{re.escape(subcommand)}\b")
        dispatch_re = re.compile(
            rf'strcmp\s*\(\s*sub\s*,\s*"{re.escape(subcommand)}"\s*\)\s*==\s*0'
        )
        if help_re.search(text) is None:
            fail(f"General Call subcommand 'gc {subcommand}' missing from help/usage text")
        if dispatch_re.search(text) is None:
            fail(f"General Call subcommand '{subcommand}' missing from dispatch")

    idf_text = idf_main.read_text(encoding="utf-8", errors="replace")
    if 'extern "C" void app_main(void)' not in idf_text:
        fail("ESP-IDF entry point must define app_main()")

    cmake_text = idf_cmake.read_text(encoding="utf-8", errors="replace")
    for component in IDF_REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake_text) is None:
            fail(f"ESP-IDF CMake file missing required component '{component}'")

    idf_contract = runpy.run_path(str(ROOT / "tools" / "check_idf_example_contract.py"))
    idf_contract["main"]()

    for rel in (
        "examples/01_basic_bringup_cli/main.cpp",
        "examples/common/Log.h",
        "examples/common/CliStyle.h",
        "examples/common/CliShell.h",
    ):
        path = ROOT / rel
        content = path.read_text(encoding="utf-8", errors="replace")
        if FORBIDDEN_PLACEHOLDER_RE.search(content):
            fail(f"placeholder/stub marker found in {rel}")

    log_text = (common_dir / "Log.h").read_text(encoding="utf-8", errors="replace")
    build_text = (common_dir / "BuildConfig.h").read_text(encoding="utf-8", errors="replace")
    for token in (
        "MCP45HVX1_CLI_ENABLE_COLOR",
        "MCP45HVX1_CLI_COLOR_DEFAULT",
        "log_color::setEnabled",
    ):
        if token not in log_text and token not in build_text and token not in text:
            fail(f"color disable support missing token '{token}'")

    for path in (ROOT / "examples").rglob("*"):
        if path.is_file() and path.suffix in {".h", ".cpp"} and path.name != "Log.h":
            if "\\033" in path.read_text(encoding="utf-8", errors="replace"):
                fail(f"raw ANSI escape must live only in Log.h: {path.relative_to(ROOT)}")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
