#!/usr/bin/env python3
"""Shared data and source parsing for the example contract checks."""
from __future__ import annotations

import re

# Match each comment/literal in one pass so delimiters inside a string, or
# quotes inside a comment, cannot consume real code later in the file.
NON_CODE_RE = re.compile(
    r'//[^\n]*|/\*.*?\*/|'
    r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"|'
    r'"(?:\\.|[^"\\])*"|(?<![\w\'])(?:u8|u|U|L)?\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)


def strip_non_code(text: str) -> str:
    """Blank C++ comments and literals, preserving token boundaries and lines."""
    return NON_CODE_RE.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), text)


def function_definition(text: str, name: str, label: str) -> str:
    """Return one function definition; comments/literals cannot introduce braces."""
    code = strip_non_code(text)
    match = re.search(rf"^[ \t]*[\w:*& \t]+\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{",
                      code, re.MULTILINE)
    if match is None:
        fail(f"{label} missing {name}() definition")
    depth = 1
    for pos in range(match.end(), len(code)):
        if code[pos] == "{":
            depth += 1
        elif code[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():pos + 1]
    fail(f"{label} has unterminated {name}() definition")


def function_body(text: str, name: str, label: str) -> str:
    definition = function_definition(text, name, label)
    return definition[strip_non_code(definition).index("{") + 1:-1]


NO_ARGUMENT_COMMANDS = {
    "version", "ver", "scan", "probe", "recover", "iface_reset", "defaults",
    "cfg", "settings", "detail", "drv", "health", "state", "info", "errata",
    "read", "rregs", "readwiper", "readtcon", "dump", "last", "zero", "mid", "max",
}
MODE_ALIASES = {
    "pot", "potentiometer", "bw", "b-w", "rheostat-bw", "rheostat_bw",
    "aw", "a-w", "rheostat-aw", "rheostat_aw", "float", "wiper-floating",
    "floating", "shutdown",
}


def check_argument_contract(text: str, label: str, command_var: str, mode_parser: str) -> None:
    handler = function_body(text, "handleCommand", label)
    # Both examples use an if/else chain. Bound each branch by balanced braces,
    # checking the guard before its first action rather than searching the file.
    code = strip_non_code(handler)
    guarded: set[str] = set()
    branch_re = re.compile(r"\bif\s*\((?P<condition>.*?)\)\s*\{", re.DOTALL)
    for match in branch_re.finditer(handler):
        if code[match.start():match.start() + 2] != "if":
            continue
        names = set(re.findall(
            rf'strcmp\s*\(\s*{command_var}\s*,\s*"([^"]+)"\s*\)\s*==\s*0',
            match.group("condition"),
        )) & NO_ARGUMENT_COMMANDS
        if not names:
            continue
        if re.match(r"\s*if\s*\(\s*!?requireNoArgs\s*\(", code[match.end():]):
            guarded.update(names)
    missing = NO_ARGUMENT_COMMANDS - guarded
    if missing:
        fail(f"{label} commands missing leading requireNoArgs() guard: {sorted(missing)}")
    mode = function_body(text, mode_parser, label)
    aliases = set(re.findall(r'strcmp\s*\(\s*\w+\s*,\s*"([^"]+)"\s*\)\s*==\s*0', mode))
    if aliases != MODE_ALIASES:
        fail(f"{label} terminal-mode aliases differ: missing={sorted(MODE_ALIASES - aliases)}, "
             f"extra={sorted(aliases - MODE_ALIASES)}")


MANDATORY_COMMANDS = [
    "help",
    "?",
    "version",
    "ver",
    "scan",
    "color",
    "begin",
    "addr",
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
IDF_REQUIRED_COMPONENTS = [
    "MCP45HVX1",
    "esp_driver_i2c",
    "esp_driver_gpio",
    "esp_driver_uart",
    "esp_driver_usb_serial_jtag",
    "esp_usb_cdc_rom_console",
    "esp_timer",
    "freertos",
    "vfs",
]

COMMAND_HELP_SPEC_RE = re.compile(
    r'\{\s*"(?P<canonical>[^"]+)",\s*"(?P<aliases>[^"]*)",\s*'
    r'"(?P<synopsis>[^"]+)",\s*"(?P<description>[^"]+)",\s*'
    r'HelpSection::(?P<section>[A-Za-z]+),\s*'
    r'HelpSafety::(?P<safety>[A-Za-z]+),\s*'
    r'"(?P<syntax>[^"]+)",\s*"(?P<examples>[^"]+)"\s*\}',
    re.DOTALL,
)


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def parse_command_help_specs(text: str, label: str) -> list[dict[str, str]]:
    match = re.search(
        r"\bCOMMAND_HELP\s*\[\s*\]\s*=\s*\{(?P<body>.*?)\n\s*\};",
        text,
        re.DOTALL,
    )
    if match is None:
        fail(f"{label} missing static COMMAND_HELP table")
    specs = [item.groupdict() for item in COMMAND_HELP_SPEC_RE.finditer(match.group("body"))]
    if not specs:
        fail(f"{label} COMMAND_HELP table is empty or unparsable")
    return specs


def command_help_names(specs: list[dict[str, str]]) -> set[str]:
    names: set[str] = set()
    for spec in specs:
        for name in (spec["canonical"], *spec["aliases"].split()):
            if not name:
                continue
            if name in names:
                fail(f"duplicate detailed-help command/alias '{name}'")
            names.add(name)
    return names
