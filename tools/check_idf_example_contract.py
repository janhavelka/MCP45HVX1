#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FORBIDDEN_TOKENS = [
    "ArduinoCompat",
    "IdfArduinoCompat",
    "String",
    "Serial",
    "TwoWire",
    "examples/01_basic_bringup_cli/main.cpp",
]

FORBIDDEN_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](Arduino\.h|Wire\.h)[>"]',
    re.MULTILINE,
)

FORBIDDEN_PLACEHOLDER_RE = re.compile(
    r"\b(TODO|FIXME|placeholder|stub|not implemented|coming soon|TBD|NYI|dummy)\b",
    re.IGNORECASE,
)

IDF_FORBIDDEN_RE = re.compile(
    r'#\s*include\s*[<"](?:Arduino|Wire)\.h[>"]|'
    r"\b(?:ArduinoCompat|IdfArduinoCompat|String|Serial|TwoWire|Print)\b|"
    r"(?:\.\./\.\./common|examples[/\\]common|01_basic_bringup_cli)"
)

RISKY_NARROW_CAST_RE = re.compile(
    r"static_cast\s*<\s*uint8_t\s*>\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\)"
)

REQUIRED_NATIVE_TOKENS = [
    'extern "C" void app_main(void)',
    "driver/i2c_master.h",
    "esp_timer_get_time",
    "vTaskDelay",
    "fgets",
    "i2c_new_master_bus",
]

IDF_COMMAND_ACTIONS = {
    "scan": "scanBus",
    "color": "styleSetEnabled",
    "begin": "beginDriver",
    "addr": "parsePrimaryAddressArg",
    "addr_alt": "parseAlternateAddressArg",
    "variant": "parseResolutionText",
    "rab": "parseResistance",
    "probe": "gDev.probe",
    "recover": "gDev.recover",
    "iface_reset": "gDev.resetI2cState",
    "defaults": "gDev.restorePowerOnDefaults",
    "drv": "printDriverHealth",
    "cfg": "printConfigSnapshot",
    "settings": "printConfigSnapshot",
    "detail": "printDriverHealth",
    "health": "printDriverHealth",
    "state": "printStateLine",
    "info": "printInfo",
    "errata": "printErrata",
    "read": "readRegisters(false)",
    "rregs": "readRegisters(false)",
    "readwiper": "readWiperCommand",
    "readtcon": "readTconCommand",
    "dump": "readRegisters(true)",
    "last": "gDev.readLastAddress",
    "reg": "gDev.readRegister",
    "rreg": "gDev.readRegister",
    "raw": "gDev.writeRegister",
    "wreg": "gDev.writeRegister",
    "wregs": "gDev.writeRegister",
    "wiper": "gDev.writeWiper",
    "frac": "gDev.writeWiperFraction",
    "pos": "gDev.writeWiperFraction",
    "zero": "gDev.writeWiper",
    "mid": "gDev.writeWiper",
    "max": "gDev.writeWiper",
    "inc": "gDev.incrementWiper",
    "dec": "gDev.decrementWiper",
    "tcon": "gDev.writeTcon",
    "term": "gDev.setTerminalEnabled",
    "terminal": "gDev.setTerminalEnabled",
    "shutdown": "gDev.setSoftwareShutdown",
    "software-shutdown": "gDev.setSoftwareShutdown",
    "mode": "gDev.setTerminalMode",
    "gc": "handleGc",
    "selftest": "runOutputSelftest",
    "verbose": "gVerbose",
    "stress": "runStress",
    "stress_mix": "runStress",
}


def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def strip_comments_and_strings(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text


def require_token(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"{label} missing token '{token}'")


def help_commands_from_idf_print_help(text: str) -> set[str]:
    match = re.search(r"void\s+printHelp\s*\(\s*\)\s*\{(?P<body>.*?)\n\}", text, re.DOTALL)
    if match is None:
        fail("ESP-IDF printHelp() body missing")
    commands: set[str] = set()
    for spec in re.findall(r'puts\s*\(\s*"  ([^"]+)"\s*\)', match.group("body")):
        for part in re.split(r"\s+(?:/|\|)\s+", spec):
            token = part.strip().split()[0] if part.strip() else ""
            token = token.strip("[]")
            if token:
                commands.add(token)
    return commands


def main() -> int:
    ns = runpy.run_path(str(ROOT / "tools" / "check_cli_contract.py"))
    commands = ns.get("MANDATORY_COMMANDS", [])
    components = ns.get("IDF_REQUIRED_COMPONENTS", [])
    main_path = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    cmake_path = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"
    text = main_path.read_text(encoding="utf-8", errors="replace")
    cmake = cmake_path.read_text(encoding="utf-8", errors="replace")

    for path in (ROOT / "examples" / "espidf_basic").rglob("*"):
        if path.is_file() and (
            path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp", ".txt"}
            or path.name == "CMakeLists.txt"
        ):
            content = path.read_text(encoding="utf-8", errors="replace")
            if IDF_FORBIDDEN_RE.search(content):
                fail(
                    "Arduino/helper dependency leaked into native IDF example: "
                    f"{path.relative_to(ROOT)}"
                )

    if FORBIDDEN_INCLUDE_RE.search(text):
        fail("forbidden Arduino/Wire include in IDF example")
    code_only = strip_comments_and_strings(text)
    cmake_code_only = strip_comments_and_strings(cmake)
    for token in FORBIDDEN_TOKENS:
        if re.search(rf"\b{re.escape(token)}\b", code_only):
            fail(f"forbidden Arduino compatibility token in IDF example: {token}")
        if re.search(rf"\b{re.escape(token)}\b", cmake_code_only):
            fail(f"forbidden Arduino compatibility token in IDF CMake: {token}")
    if FORBIDDEN_PLACEHOLDER_RE.search(text):
        fail("placeholder/stub marker found in IDF example")
    for token in REQUIRED_NATIVE_TOKENS:
        if token not in text:
            fail(f"native ESP-IDF token missing: {token}")
    dispatch = set(
        re.findall(r'strcmp\s*\(\s*cmd\s*,\s*"([^"]+)"\s*\)\s*==\s*0', text)
    )
    for cmd in commands:
        if cmd == "?":
            if "?" not in dispatch:
                fail("mandatory command '?' missing from IDF example")
        elif cmd not in dispatch:
            fail(f"mandatory command '{cmd}' missing from ESP-IDF dispatch")
    for cmd, token in IDF_COMMAND_ACTIONS.items():
        if cmd in dispatch:
            require_token(text, token, f"ESP-IDF command '{cmd}' action")
    for cmd in help_commands_from_idf_print_help(text):
        if cmd not in dispatch:
            fail(f"ESP-IDF help advertises command '{cmd}' without dispatch")
    for component in components:
        if re.search(rf"\b{re.escape(component)}\b", cmake) is None:
            fail(f"ESP-IDF CMake file missing component '{component}'")

    for token in (
        "parseU32Bounded",
        "parseU8Bounded",
        "parseFloatRangeArg",
        "parseAnySupportedAddressArg",
        "parsePrimaryAddressArg",
        "parseAlternateAddressArg",
        "parseWiperCodeArg",
        "parseTconArg",
        "parseRegisterArg",
        "parseRawRegisterValueArg",
        "parseStepArg",
        "parseStressCountArg",
        "splitWhitespace",
        "lowerAscii",
        "requireNoArgs",
        "errno == ERANGE",
        "*text == '-'",
        "*text == '+'",
    ):
        require_token(text, token, "bounded IDF parsing")
    handler = code_only[code_only.index("void handleCommand") :]
    if re.search(r"\bparseU32\s*\(", handler):
        fail("IDF command handler must not use unbounded parseU32()")
    if RISKY_NARROW_CAST_RE.search(handler):
        fail("IDF command handler contains risky static_cast<uint8_t> from parsed variable")
    for pat in (
        r"is(?:Primary|Alternate)Address\s*\(\s*static_cast<uint8_t>",
        r"write(?:Wiper|Tcon|Register)\s*\(\s*static_cast<uint8_t>",
        r"(?:incrementWiper|decrementWiper)\s*\(\s*static_cast<uint8_t>",
        r"\(\s*uint8_t\s*\)\s*[A-Za-z_][A-Za-z0-9_]*",
        r"uint8_t\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\)",
        r"parseFloatArg\s*\([^;\n]*&&[^;\n]*(?:>=|<=)",
    ):
        if re.search(pat, handler):
            fail(f"ESP-IDF command handler bypasses bounded parse helper: {pat}")

    for token in (
        'printWarning("defaults',
        'printWarning("wiper',
        'printWarning("fraction write',
        'printWarning("wiper preset',
        'printWarning("wiper step',
        'printWarning("tcon',
        'printWarning("terminal command',
        'printWarning("software shutdown',
        'printWarning("mode command',
        'printWarning("selftest output',
        'printWarning("stress_mix',
        'printDanger("raw write',
        'printDanger("raw register write',
    ):
        require_token(text, token, "ESP-IDF output-changing warning/danger guard")

    if "color [on|off]" in text:
        for token in (
            "MCP45HVX1_CLI_ENABLE_COLOR",
            "MCP45HVX1_CLI_COLOR_DEFAULT",
            "styleSetEnabled(",
            "styleIsEnabled()",
            "styleCode(",
            "\\x1B[32m",
            "passFailColor",
            "printWarning",
            "printDanger",
        ):
            require_token(text, token, "functional IDF color command")
        if "parseBoolArg(args, &gColor)" in text or "gColor ? 1 : 0" in handler:
            fail("ESP-IDF color command must use styleSetEnabled/styleIsEnabled")
    else:
        fail("mandatory IDF color command missing from help")

    if "selftest output" in text:
        for token in (
            "runSelftestSafe",
            "runOutputSelftest",
            "restoreSnapshot",
            "runOutputSelftest();",
            "readSnapshot(baseline",
            "writeWiper",
            "writeTcon",
            "restore baseline",
            "selftest (safe)",
            "selftest (output-changing)",
            "Selftest result: pass=",
        ):
            require_token(text, token, "IDF selftest behavior")
        if "native checks remain state-restoring" in text:
            fail("selftest output is still documented as read-only/state-restoring only")
    else:
        fail("selftest output missing from IDF help")

    for token in (
        "gBus.bus == nullptr",
        "I2C bus not initialized",
        "allowGeneralCall = true",
        "general_call_allowed=",
        "gOutputStateUncertain",
        "cache_known wiper=",
        "last_status=",
        "last_uncertain=",
    ):
        require_token(text, token, "IDF state/bus safety output")

    for token in (
        "addr == 0x00U",
        "transmitGeneralCall",
        "I2C_DEVICE_ADDRESS_NOT_USED",
        "addressByte = 0x00U",
        "i2c_master_execute_defined_operations",
        'printDanger("General Call armed for one broadcast command.',
        "It affects every enabled device on the bus",
        "DS80000649B: affected silicon has General Call decode anomalies",
        "Output-changing General Call commands require isolated-bus evidence",
        "General Call is broadcast; run 'gc arm' first.",
        "Usage: gc arm",
        "Usage: gc disarm",
        "Usage: gc inc",
        "Usage: gc dec",
    ):
        require_token(text, token, "ESP-IDF General Call implementation")

    docs_text = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in [
            ROOT / "README.md",
            ROOT / "docs" / "IDF_PORT.md",
        ]
        if path.exists()
    )
    if re.search(
        r"skipped:\s*no ESP-IDF example|No native ESP-IDF .* exists|current native Serial stub",
        docs_text,
    ):
        fail("stale ESP-IDF/serial-stub docs in command contract docs")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
