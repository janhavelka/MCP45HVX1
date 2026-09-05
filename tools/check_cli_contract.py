#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

import check_idf_example_contract
from contract_common import (
    IDF_REQUIRED_COMPONENTS, MANDATORY_COMMANDS, GENERAL_CALL_SUBCOMMANDS,
    check_argument_contract, command_help_names, function_body, parse_command_help_specs,
)

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "I2cTransport.h",
    "I2cScanner.h",
    "CliShell.h",
    "CliStyle.h",
    "HealthView.h",
]

COMMAND_ACTIONS = {
    "scan": "i2c_scanner::scan",
    "color": "handleColor",
    "begin": "handleBegin",
    "addr": "handleAddress",
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
    r"\b(TODO|FIXME|placeholder|stub|not implemented|coming soon|TBD|NYI|dummy)\b",
    re.IGNORECASE,
)

ARDUINO_OUTPUT_WARNING_TOKENS = [
    'warnOutputChanging("wiper"',
    'warnOutputChanging("frac"',
    'warnOutputChanging(increment ? "inc" : "dec")',
    'warnOutputChanging("tcon"',
    'warnOutputChanging("term")',
    'warnOutputChanging("TCON software shutdown")',
    'warnOutputChanging("mode")',
    'warnOutputChanging("selftest output")',
    'warnOutputChanging("stress_mix")',
    'warnOutputChanging("defaults")',
    'warnOutputChanging("zero")',
    'warnOutputChanging("mid")',
    'warnOutputChanging("max")',
    'warnDangerous("raw register write")',
    "[DANGER]",
    "It affects every enabled device on the bus",
]

COMMAND_DOCS = [
    "README.md",
    "docs/IDF_PORT.md",
    "docs/MCP45HVX1_API_CONTRACT.md",
    "docs/MCP45HVX1_RELEASE_CHECKLIST.md",
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


def require_token(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"{label} missing token '{token}'")


def help_commands_from_specs(specs: list[str]) -> set[str]:
    commands: set[str] = set()
    for spec in specs:
        for part in re.split(r"\s+(?:/|\|)\s+", spec):
            token = part.strip().split()[0] if part.strip() else ""
            token = token.strip("[]")
            if token:
                commands.add(token)
    return commands


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
    check_argument_contract(text, "Arduino CLI", "command", "parseMode")

    help_specs = parse_command_help_specs(text, "Arduino CLI")
    detailed_help_names = command_help_names(help_specs)

    for cmd in MANDATORY_COMMANDS:
        dispatch_re = re.compile(
            rf'strcmp\s*\(\s*command\s*,\s*"{re.escape(cmd)}"\s*\)\s*==\s*0'
        )
        if dispatch_re.search(text) is None:
            fail(f"mandatory command '{cmd}' missing from handleCommand() dispatch")
        if cmd not in detailed_help_names:
            fail(f"mandatory command '{cmd}' missing from COMMAND_HELP lookup table")

    advertised_specs = [spec["synopsis"] for spec in help_specs]
    dispatch_commands = set(
        re.findall(r'strcmp\s*\(\s*command\s*,\s*"([^"]+)"\s*\)\s*==\s*0', text)
    )
    if detailed_help_names != dispatch_commands:
        fail(
            "Arduino detailed-help/dispatch command mismatch: "
            f"help_only={sorted(detailed_help_names - dispatch_commands)}, "
            f"dispatch_only={sorted(dispatch_commands - detailed_help_names)}"
        )
    for cmd in help_commands_from_specs(advertised_specs):
        if cmd not in dispatch_commands:
            fail(f"help advertises command '{cmd}' without handleCommand() dispatch")

    for spec in help_specs:
        for field in ("synopsis", "description", "section", "safety", "syntax", "examples"):
            if not spec[field].strip():
                fail(f"detailed help for '{spec['canonical']}' has empty {field}")

    for token in (
        "handleHelp(p)",
        "findCommandHelp",
        "helpNameMatches",
        "printCommandHelp",
        "printHelpItem(spec.synopsis, spec.description)",
        "Use `help <command>` for syntax, safety, aliases, and examples.",
        "Safety:",
        "Syntax:",
        "Examples:",
        "No help for",
    ):
        require_token(text, token, "Arduino detailed help")

    hil_runner_text = (ROOT / "tools" / "run_hil_mcp45hvx1.py").read_text(
        encoding="utf-8", errors="replace"
    )
    for command in ("help wiper", "? health", "help gc"):
        require_token(hil_runner_text, f'"{command}"', "HIL detailed-help coverage")
    for token in ("Help: wiper", "Aliases: health", "DANGEROUS / RAW OR BUS-WIDE"):
        require_token(hil_runner_text, token, "HIL detailed-help assertions")

    readme_text = (ROOT / "README.md").read_text(encoding="utf-8", errors="replace")
    for token in ("`help <command>`", "safety classification", "Help lookup never performs"):
        require_token(readme_text, token, "README detailed-help documentation")

    for cmd, token in COMMAND_ACTIONS.items():
        if token not in text:
            fail(f"command '{cmd}' missing expected action token '{token}'")

    for token in ARDUINO_OUTPUT_WARNING_TOKENS:
        require_token(text, token, "Arduino CLI output-changing warning/danger guard")

    general_call_body = function_body(text, "handleGeneralCall", "Arduino CLI")
    for sub in ("arm", "disarm", "wiper", "tcon", "inc", "dec"):
        if f'"{sub}"' not in general_call_body:
            fail(f"General Call subcommand '{sub}' missing handler branch")

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

    for source, label in ((text, "Arduino CLI"), (idf_text, "ESP-IDF CLI")):
        require_token(source, "allowGeneralCall = true", f"{label} General Call core opt-in")
    require_token(text, "General Call core opt-in", "Arduino CLI General Call config output")
    require_token(idf_text, "general_call_allowed=", "ESP-IDF CLI General Call config output")

    for token in (
        "uncertain=",
        "hardwareStateUncertain",
        "hardwareStateUncertainError",
        "cachedWiperKnown",
        "cachedTconKnown",
        "output-changing startup write",
    ):
        require_token(text, token, "bringup CLI uncertainty output")
        require_token(idf_text, token, "ESP-IDF CLI uncertainty output")

    cmake_text = idf_cmake.read_text(encoding="utf-8", errors="replace")
    for component in IDF_REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake_text) is None:
            fail(f"ESP-IDF CMake file missing required component '{component}'")

    idf_result = check_idf_example_contract.main()
    if idf_result != 0:
        return idf_result

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

    for rel in COMMAND_DOCS:
        path = ROOT / rel
        if not path.exists():
            continue
        content = path.read_text(encoding="utf-8", errors="replace")
        if re.search(
            r"(?im)(command|cli|selftest|stress|color|gc|raw|wreg).{0,80}"
            r"(TODO|FIXME|placeholder|stub|not implemented|coming soon|TBD|NYI|dummy)",
            content,
        ):
            fail(f"advertised command placeholder/stub marker in {rel}")

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
            rel = path.relative_to(ROOT).as_posix()
            allowed_raw_ansi = rel == "examples/espidf_basic/main/main.cpp"
            content = path.read_text(encoding="utf-8", errors="replace")
            if ("\\033" in content or "\\x1B" in content) and not allowed_raw_ansi:
                fail(f"raw ANSI escape must live only in Log.h: {path.relative_to(ROOT)}")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
