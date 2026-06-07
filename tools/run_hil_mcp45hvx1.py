#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

PASS_SAFE_ONLY = "PASS_SAFE_ONLY"
PASS_WITH_OUTPUT_CHANGE = "PASS_WITH_OUTPUT_CHANGE"
FAIL = "FAIL"
FAIL_RESTORE_UNCERTAIN = "FAIL_RESTORE_UNCERTAIN"
OPERATOR_REVIEW_REQUIRED = "OPERATOR_REVIEW_REQUIRED"
SKIPPED_UNSAFE = "SKIPPED_UNSAFE"


@dataclass
class CommandResult:
    seq: int
    command: str
    group: str
    output_changing: bool
    required_flags: str
    started_utc: str
    ended_utc: str
    duration_s: float
    output: str
    failed: bool
    skipped: bool = False


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def parse_address(text: str) -> int:
    value = int(text, 0)
    if value < 0 or value > 0x7F:
        raise argparse.ArgumentTypeError("address must be a 7-bit value")
    return value


def format_hex(value: int) -> str:
    return f"0x{value:02X}"


def address_command(address: int) -> str:
    if 0x3C <= address <= 0x3F:
        return f"addr {format_hex(address)}"
    if 0x5C <= address <= 0x5F:
        return f"addr_alt {format_hex(address)}"
    raise ValueError("address must be 0x3C..0x3F, or 0x5C..0x5F with alternate opt-in")


def import_serial() -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required to run HIL. Install it with `python -m pip install pyserial`."
        ) from exc
    return serial


class SerialCli:
    def __init__(self, port: str, baud: int, timeout: float) -> None:
        serial = import_serial()
        self._serial = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=0.1,
            write_timeout=min(2.0, timeout),
            rtscts=False,
            dsrdtr=False,
        )
        self._serial.dtr = False
        self._serial.rts = False
        self._timeout = timeout

    def close(self) -> None:
        self._serial.close()

    def read_until_quiet(self, timeout: float | None = None, quiet_s: float = 0.35) -> str:
        deadline = time.monotonic() + (timeout if timeout is not None else self._timeout)
        last_rx = time.monotonic()
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            data = self._serial.read(4096)
            if data:
                chunks.append(data)
                last_rx = time.monotonic()
                decoded = b"".join(chunks).decode("utf-8", errors="replace")
                if decoded.endswith("> ") or decoded.endswith("\n> "):
                    break
            elif time.monotonic() - last_rx >= quiet_s:
                break
        return b"".join(chunks).decode("utf-8", errors="replace")

    def command(self, command: str, timeout: float | None = None) -> str:
        self._serial.write((command + "\n").encode("utf-8"))
        self._serial.flush()
        return self.read_until_quiet(timeout=timeout)


def command_failed(command: str, output: str) -> bool:
    text = strip_ansi(output)
    if command in {"help", "?", "version", "ver"}:
        return False
    if re.search(r"(?m)^\s*\[FAIL\]", text):
        return True
    if re.search(r"(?m)^\s*\[E\]", text):
        return True
    if "Unknown command" in text or "Usage:" in text:
        return True
    if re.search(r"(?i)restore .*failed", text):
        return True
    if re.search(r"(?m)\bStatus:\s+(?!OK\b)[A-Z_]+", text):
        return True
    if re.search(r"(?m)^(?:probe|read|readwiper|readtcon|dump|stress|selftest|gc|wiper|tcon|mode|raw write)[^:\n]*:\s+(?!OK\b)[A-Z_]+", text):
        return True
    if command.startswith(("stress", "selftest")):
        if re.search(r"\bfail(?:ures)?\s*[=:]\s*[1-9]\d*", text, re.IGNORECASE):
            return True
        if re.search(r"\bErrors:\s*[1-9]\d*", text):
            return True
    return False


def parse_hex_field(text: str, name: str) -> int | None:
    patterns = [
        rf"\b{name}\s*=\s*0x([0-9a-fA-F]{{1,2}})\b",
        rf"\b{name.capitalize()}\s*:\s*0x([0-9a-fA-F]{{1,2}})\b",
        rf"\b{name.upper()}\s*:\s*0x([0-9a-fA-F]{{1,2}})\b",
    ]
    for pattern in patterns:
        matches = re.findall(pattern, strip_ansi(text))
        if matches:
            return int(matches[-1], 16)
    return None


def parse_resolution(text: str) -> int | None:
    matches = re.findall(r"\bresolution\s*=\s*(7|8)(?:-bit)?\b", strip_ansi(text), re.IGNORECASE)
    if matches:
        return int(matches[-1])
    return None


def parse_state_value(text: str, name: str) -> str | None:
    matches = re.findall(rf"\b{name}=([^\s]+)", strip_ansi(text))
    return matches[-1] if matches else None


def sanitize_output(output: str, no_color: bool) -> str:
    return strip_ansi(output) if no_color else output


def prompt_line(prompt: str) -> str:
    return input(prompt).strip()


class HilRun:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.output_dir = Path(args.output_dir) / f"mcp45hvx1_{timestamp}"
        self.output_dir.mkdir(parents=True, exist_ok=False)
        self.raw_parts: list[str] = []
        self.commands: list[CommandResult] = []
        self.operator_notes: list[str] = []
        self.skipped: list[str] = []
        self.runner_errors: list[str] = []
        self.baseline: dict[str, int] = {}
        self.final_state: dict[str, Any] = {}
        self.restore_uncertain = False
        self.operator_review_required = False

    def note(self, text: str) -> None:
        self.operator_notes.append(text)

    def synthetic_log(self, label: str, text: str) -> None:
        self.raw_parts.append(f"{utc_now()} EVENT {label}\n{text.rstrip()}\n")

    def run_command(self, cli: SerialCli, command: str, group: str, output_changing: bool = False,
                    timeout: float | None = None, required_flags: str = "-") -> CommandResult:
        seq = len(self.commands) + 1
        started = utc_now()
        t0 = time.monotonic()
        self.raw_parts.append(f"{started} TX {command}\n")
        output = cli.command(command, timeout=timeout)
        duration = time.monotonic() - t0
        ended = utc_now()
        stored_output = sanitize_output(output, self.args.no_color)
        failed = command_failed(command, stored_output)
        for line in stored_output.splitlines():
            self.raw_parts.append(f"{ended} RX {line}\n")
        self.raw_parts.append(f"{ended} EVENT read_complete seq={seq:03d} duration_s={duration:.3f}\n")
        result = CommandResult(
            seq=seq,
            command=command,
            group=group,
            output_changing=output_changing,
            required_flags=required_flags,
            started_utc=started,
            ended_utc=ended,
            duration_s=round(duration, 3),
            output=stored_output,
            failed=failed,
        )
        self.commands.append(result)
        return result

    def safe_sequence(self, cli: SerialCli) -> None:
        boot = cli.read_until_quiet(timeout=max(self.args.timeout, 2.0), quiet_s=0.5)
        if boot:
            for line in sanitize_output(boot, self.args.no_color).splitlines():
                self.raw_parts.append(f"{utc_now()} RX_BOOT {line}\n")
        self.run_command(cli, "version", "safe")
        self.run_command(cli, "color off", "safe")
        self.run_command(cli, "help", "safe", timeout=max(self.args.timeout, 4.0))
        self.run_command(cli, "scan", "safe", timeout=max(self.args.timeout, 8.0))
        self.run_command(cli, "addr", "safe")
        if self.args.address is not None:
            self.run_command(cli, address_command(self.args.address), "safe")
        self.run_command(cli, "probe", "safe")
        self.run_command(cli, "cfg", "safe")
        self.run_command(cli, "state", "safe")
        self.run_command(cli, "drv", "safe")
        self.run_command(cli, "readwiper", "safe")
        self.run_command(cli, "readtcon", "safe")
        self.run_command(cli, "dump", "safe")
        self.run_command(cli, "selftest safe", "safe", timeout=max(self.args.timeout, 8.0))
        self.run_command(cli, "stress 100", "safe", timeout=max(self.args.timeout, 25.0))
        self.run_command(cli, "state", "safe-final")
        self.run_command(cli, "drv", "safe-final")

    def capture_baseline(self, cli: SerialCli) -> bool:
        wiper = self.run_command(cli, "readwiper", "baseline")
        tcon = self.run_command(cli, "readtcon", "baseline")
        state = self.run_command(cli, "state", "baseline")
        wiper_value = parse_hex_field(wiper.output, "wiper")
        tcon_value = parse_hex_field(tcon.output, "tcon")
        resolution = parse_resolution(state.output)
        if wiper_value is None or tcon_value is None:
            self.skipped.append("output-changing group skipped because Wiper/TCON baseline could not be parsed")
            return False
        self.baseline = {"wiper": wiper_value, "tcon": tcon_value}
        if resolution is not None:
            self.baseline["resolution"] = resolution
        return True

    def active_max_code(self) -> int:
        if self.baseline.get("resolution") == 7:
            return 0x7F
        return 0xFF

    def output_change_sequence(self, cli: SerialCli) -> None:
        if self.args.operator_prompts:
            answer = prompt_line(
                "Output-changing HIL requires a safe load and measurement setup. Type YES to continue: "
            )
            if answer != "YES":
                self.skipped.append("output-changing group skipped by operator")
                return
        if not self.capture_baseline(cli):
            return

        max_code = self.active_max_code()
        if self.args.include_wiper_ramp:
            values = sorted(set([0, max_code // 4, max_code // 2, (3 * max_code) // 4, max_code]))
        else:
            values = sorted(set([0, max_code]))
        for value in values:
            self.run_command(cli, f"wiper {format_hex(value)}", "output-change", True)
            self.run_command(cli, "readwiper", "output-change-verify")
            if self.args.operator_prompts:
                measured = prompt_line(
                    f"Record measured voltage/resistance for Wiper {format_hex(value)}: "
                )
                self.note(f"Wiper {format_hex(value)} measurement: {measured}")

        if self.args.include_tcon_toggle:
            for mode in ("bw", "pot"):
                self.run_command(cli, f"mode {mode}", "output-change", True)
                self.run_command(cli, "readtcon", "output-change-verify")
                if self.args.operator_prompts:
                    measured = prompt_line(f"Record terminal measurement for mode {mode}: ")
                self.note(f"TCON mode {mode} measurement: {measured}")

        self.restore_baseline(cli, "output-change-restore")
        if not self.args.operator_prompts:
            self.operator_review_required = True
            self.note("Output-changing commands ran without interactive measurement prompts.")

    def restore_baseline(self, cli: SerialCli, group: str) -> None:
        if "wiper" not in self.baseline or "tcon" not in self.baseline:
            self.restore_uncertain = True
            self.skipped.append("restore could not run because baseline was incomplete")
            return
        self.run_command(cli, f"wiper {format_hex(self.baseline['wiper'])}", group, True)
        self.run_command(cli, f"tcon {format_hex(self.baseline['tcon'])}", group, True)
        wiper = self.run_command(cli, "readwiper", group)
        tcon = self.run_command(cli, "readtcon", group)
        final_wiper = parse_hex_field(wiper.output, "wiper")
        final_tcon = parse_hex_field(tcon.output, "tcon")
        self.final_state.update({"wiper": final_wiper, "tcon": final_tcon})
        if final_wiper != self.baseline["wiper"] or final_tcon != self.baseline["tcon"]:
            self.restore_uncertain = True

    def general_call_sequence(self, cli: SerialCli) -> None:
        warning = (
            "General Call is broadcast and output-changing. DS80000649B errata review, "
            "isolated-bus evidence, or documented shared-bus risk acceptance is required."
        )
        self.synthetic_log("GENERAL CALL ERRATA WARNING", warning)
        self.note(warning)
        if not self.args.confirm_isolated_bus:
            self.skipped.append("General Call skipped: --confirm-isolated-bus was not provided")
            return
        if self.args.operator_prompts:
            answer = prompt_line(
                "Type ISOLATED to confirm only MCP45HVX1 validation devices are on this I2C bus: "
            )
            if answer != "ISOLATED":
                self.skipped.append("General Call skipped: operator did not confirm isolated bus")
                return
        if not self.baseline and not self.capture_baseline(cli):
            return
        self.run_command(cli, "errata", "general-call")
        self.run_command(cli, "gc arm", "general-call", True, required_flags="--include-general-call --confirm-isolated-bus")
        self.run_command(cli, "gc inc", "general-call", True, required_flags="--include-general-call --confirm-isolated-bus")
        self.run_command(cli, "readwiper", "general-call-verify")
        if self.args.operator_prompts:
            measured = prompt_line("Record analog measurement after General Call increment: ")
            self.note(f"General Call measurement: {measured}")
        self.restore_baseline(cli, "general-call-restore")

    def shdn_wlat_sequence(self, cli: SerialCli) -> None:
        for pin in ("SHDN" if self.args.include_shdn else "", "WLAT" if self.args.include_wlat else ""):
            if not pin:
                continue
            if not self.args.operator_prompts:
                self.skipped.append(f"{pin} check skipped: --operator-prompts is required")
                continue
            wired = prompt_line(f"Record {pin} wiring/control path: ")
            self.note(f"{pin} wiring/control: {wired}")
            prompt_line(f"Set {pin} to the asserted/test state, then press Enter.")
            self.run_command(cli, "readwiper", f"{pin.lower()}-readback")
            self.run_command(cli, "readtcon", f"{pin.lower()}-readback")
            physical = prompt_line(
                f"Record physical output observation with {pin} asserted; readback alone is not proof: "
            )
            self.note(f"{pin} asserted physical observation: {physical}")
            prompt_line(f"Release {pin} to normal state, then press Enter.")
            self.run_command(cli, "readwiper", f"{pin.lower()}-release")
            self.run_command(cli, "readtcon", f"{pin.lower()}-release")
            released = prompt_line(f"Record physical output after {pin} release: ")
            self.note(f"{pin} released physical observation: {released}")
            self.operator_review_required = True

    def verdict(self) -> str:
        final_bad = self.final_state_is_bad()
        if self.restore_uncertain:
            return FAIL_RESTORE_UNCERTAIN
        if final_bad and any(result.output_changing for result in self.commands):
            return FAIL_RESTORE_UNCERTAIN
        if self.runner_errors:
            return FAIL
        if not self.commands:
            return FAIL
        if any(result.failed for result in self.commands):
            return FAIL
        if final_bad:
            return FAIL
        if self.skipped and any("General Call" in item or "SHDN" in item or "WLAT" in item for item in self.skipped):
            return SKIPPED_UNSAFE
        output_changed = any(result.output_changing for result in self.commands)
        if self.operator_review_required:
            return OPERATOR_REVIEW_REQUIRED
        if output_changed:
            return PASS_WITH_OUTPUT_CHANGE
        return PASS_SAFE_ONLY

    def final_state_is_bad(self) -> bool:
        state_output = next((r.output for r in reversed(self.commands) if r.command == "state"), "")
        if not state_output:
            return False
        text = strip_ansi(state_output)
        online = parse_state_value(text, "online")
        uncertain = parse_state_value(text, "uncertain")
        dirty = parse_state_value(text, "dirty")
        if online in {"0", "no", "false"}:
            return True
        if uncertain in {"1", "yes", "true"}:
            return True
        if dirty in {"1", "yes", "true"}:
            return True
        return False

    def verdict_reasons(self) -> list[str]:
        reasons: list[str] = []
        if self.restore_uncertain:
            reasons.append("baseline restore was not verified after output-changing commands")
        if self.final_state_is_bad():
            reasons.append("final state is offline, uncertain, or dirty")
        if self.runner_errors:
            reasons.extend(self.runner_errors)
        if not self.commands:
            reasons.append("no CLI commands were captured")
        for result in self.commands:
            if result.failed:
                reasons.append(f"command failed or produced error output: {result.command}")
        reasons.extend(self.skipped)
        if self.operator_review_required:
            reasons.append("operator review is required for measurements or physical pin observations")
        if not reasons:
            reasons.append("safe sequence completed without detected command failures")
        return reasons

    def repo_info(self) -> dict[str, Any]:
        def git(args: list[str]) -> str:
            result = subprocess.run(
                ["git", *args],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            return result.stdout.strip() if result.returncode == 0 else ""

        return {
            "branch": git(["branch", "--show-current"]),
            "commit": git(["rev-parse", "--short", "HEAD"]),
            "dirty": bool(git(["status", "--short"])),
        }

    def group_summary(self) -> dict[str, dict[str, int]]:
        groups: dict[str, dict[str, int]] = {}
        for result in self.commands:
            entry = groups.setdefault(result.group, {"sent": 0, "failed": 0, "output_changing": 0})
            entry["sent"] += 1
            entry["failed"] += 1 if result.failed else 0
            entry["output_changing"] += 1 if result.output_changing else 0
        for item in self.skipped:
            entry = groups.setdefault("skipped", {"sent": 0, "failed": 0, "output_changing": 0})
            entry["failed"] += 1 if "runner error" in item else 0
        return groups

    def write_outputs(self, args: argparse.Namespace) -> None:
        verdict = self.verdict()
        raw_header = (
            f"{utc_now()} EVENT runner=tools/run_hil_mcp45hvx1.py\n"
            f"{utc_now()} EVENT argv={' '.join(sys.argv)}\n"
            f"{utc_now()} EVENT port={args.port} baud={args.baud} "
            f"address={format_hex(args.address) if args.address is not None else 'default'}\n"
        )
        raw_serial = raw_header + "".join(self.raw_parts).lstrip()
        (self.output_dir / "raw_serial.txt").write_text(raw_serial, encoding="utf-8")
        (self.output_dir / "commands.txt").write_text(self.commands_text(), encoding="utf-8")
        (self.output_dir / "operator_notes.md").write_text(self.operator_notes_text(), encoding="utf-8")
        final_state_text = next((r.output for r in reversed(self.commands) if r.command == "state"), "")
        final_health_text = next((r.output for r in reversed(self.commands) if r.command in {"drv", "health"}), "")
        summary = {
            "schema_version": "mcp45hvx1-hil-evidence-v1",
            "generated_at_utc": utc_now(),
            "verdict": verdict,
            "verdict_reasons": self.verdict_reasons(),
            "repo": self.repo_info(),
            "runner": {
                "path": "tools/run_hil_mcp45hvx1.py",
                "argv": sys.argv,
                "python": sys.version.split()[0],
            },
            "serial": {
                "port": args.port,
                "baud": args.baud,
                "address": format_hex(args.address) if args.address is not None else None,
                "timeout_s": args.timeout,
            },
            "options": {
                "no_color": bool(args.no_color),
                "include_output_change": bool(args.include_output_change),
                "include_wiper_ramp": bool(args.include_wiper_ramp),
                "include_tcon_toggle": bool(args.include_tcon_toggle),
                "include_shdn": bool(args.include_shdn),
                "include_wlat": bool(args.include_wlat),
                "include_general_call": bool(args.include_general_call),
                "confirm_isolated_bus": bool(args.confirm_isolated_bus),
                "operator_prompts": bool(args.operator_prompts),
            },
            "hardware": {
                "operator": "",
                "board": "",
                "dut_variant": "",
                "rab_ohms": None,
                "package_marking": "",
                "date_code": "",
                "bus_topology": "",
                "shdn_wiring": "",
                "wlat_wiring": "",
                "instruments": [],
                "rails_and_load": "",
            },
            "started_utc": self.commands[0].started_utc if self.commands else utc_now(),
            "output_dir": str(self.output_dir),
            "port": args.port,
            "baud": args.baud,
            "address": format_hex(args.address) if args.address is not None else None,
            "safe_default": True,
            "include_output_change": bool(args.include_output_change),
            "include_wiper_ramp": bool(args.include_wiper_ramp),
            "include_tcon_toggle": bool(args.include_tcon_toggle),
            "include_shdn": bool(args.include_shdn),
            "include_wlat": bool(args.include_wlat),
            "include_general_call": bool(args.include_general_call),
            "confirm_isolated_bus": bool(args.confirm_isolated_bus),
            "operator_prompts": bool(args.operator_prompts),
            "groups": self.group_summary(),
            "baseline": self.baseline,
            "final": {
                "wiper": self.final_state.get("wiper"),
                "tcon": self.final_state.get("tcon"),
                "state": final_state_text,
                "health": final_health_text,
            },
            "restore": {
                "required": any(r.output_changing for r in self.commands),
                "verified": None if not any(r.output_changing for r in self.commands) else not self.restore_uncertain,
                "reason": "restore uncertain" if self.restore_uncertain else "",
            },
            "measurements": self.operator_notes,
            "skipped": self.skipped,
            "operator_notes": self.operator_notes,
            "commands": [
                {
                    "seq": result.seq,
                    "command": result.command,
                    "group": result.group,
                    "output_changing": result.output_changing,
                    "required_flags": result.required_flags,
                    "started_utc": result.started_utc,
                    "ended_utc": result.ended_utc,
                    "duration_s": result.duration_s,
                    "failed": result.failed,
                    "result": "FAIL" if result.failed else "PASS",
                }
                for result in self.commands
            ],
            "artifacts": {
                "raw_serial": "raw_serial.txt",
                "commands": "commands.txt",
                "summary": "summary.json",
                "report": "report.md",
                "operator_notes": "operator_notes.md",
            },
        }
        (self.output_dir / "summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (self.output_dir / "report.md").write_text(self.report_text(summary), encoding="utf-8")

    def commands_text(self) -> str:
        lines = [
            "seq\tutc\tgroup\tclassification\tcommand\trequired_flags\tresult\tduration_s\tnotes"
        ]
        for result in self.commands:
            lines.append(
                f"{result.seq:03d}\t{result.started_utc}\t{result.group}\t"
                f"{'output_changing' if result.output_changing else 'read_only'}\t"
                f"{result.command}\t{result.required_flags}\t"
                f"{'FAIL' if result.failed else 'PASS'}\t{result.duration_s:.3f}\t-"
            )
        for index, item in enumerate(self.skipped, start=len(self.commands) + 1):
            lines.append(f"{index:03d}\t{utc_now()}\tskipped\tunsafe\t-\t-\tSKIP_UNSAFE\t0.000\t{item}")
        return "\n".join(lines)

    def operator_notes_text(self) -> str:
        lines = ["# MCP45HVX1 HIL Operator Notes", ""]
        if not self.operator_notes:
            lines.append("No operator notes were recorded.")
        else:
            for note in self.operator_notes:
                lines.append(f"- {note}")
        if self.skipped:
            lines.extend(["", "## Skipped"])
            for item in self.skipped:
                lines.append(f"- {item}")
        lines.append("")
        return "\n".join(lines)

    def report_text(self, summary: dict[str, Any]) -> str:
        lines = [
            "# MCP45HVX1 HIL Evidence Report",
            "",
            f"Verdict: `{summary['verdict']}`",
            f"Repo: `{summary['repo']['branch']}` at `{summary['repo']['commit']}` dirty={summary['repo']['dirty']}",
            f"Runner: `{' '.join(summary['runner']['argv'])}`",
            f"Port: `{summary['port']}`",
            f"Baud: `{summary['baud']}`",
            f"Address: `{summary['address']}`",
            f"Output directory: `{summary['output_dir']}`",
            "",
            "## Scope",
            "",
            "- Safe/read-only sequence always runs first.",
            f"- Output-changing group requested: `{summary['include_output_change']}`.",
            f"- General Call requested: `{summary['include_general_call']}`.",
            f"- SHDN check requested: `{summary['include_shdn']}`.",
            f"- WLAT check requested: `{summary['include_wlat']}`.",
            "",
            "## Evidence Files",
            "",
            "- `raw_serial.txt` - raw command transcript after optional ANSI stripping.",
            "- `commands.txt` - command list with group, timing, and failure flags.",
            "- `summary.json` - machine-readable summary and verdict.",
            "- `operator_notes.md` - measurements, observations, skipped unsafe checks.",
            "- `report.md` - this report.",
            "",
            "## Baseline",
            "",
            f"- Wiper: `{format_hex(summary['baseline']['wiper']) if 'wiper' in summary['baseline'] else 'unknown'}`",
            f"- TCON: `{format_hex(summary['baseline']['tcon']) if 'tcon' in summary['baseline'] else 'unknown'}`",
            "",
            "## Skipped Or Unsafe",
            "",
        ]
        if summary["skipped"]:
            lines.extend(f"- {item}" for item in summary["skipped"])
        else:
            lines.append("- None recorded.")
        lines.extend(["", "## Commands", ""])
        for command in summary["commands"]:
            marker = "FAIL" if command["failed"] else "OK"
            lines.append(
                f"- `{command['command']}` - {command['group']}, "
                f"output_changing={command['output_changing']}, {marker}"
            )
        lines.extend([
            "",
            "## Claims Supported",
            "",
            f"- Safe-only HIL evidence: `{summary['verdict'] in {PASS_SAFE_ONLY, PASS_WITH_OUTPUT_CHANGE, OPERATOR_REVIEW_REQUIRED}}`.",
            f"- Output-changing HIL evidence: `{summary['verdict'] == PASS_WITH_OUTPUT_CHANGE}`.",
            f"- General Call evidence: `{summary['include_general_call'] and summary['confirm_isolated_bus'] and summary['verdict'] == PASS_WITH_OUTPUT_CHANGE}`.",
            "- High-voltage evidence: `false` unless operator notes attach external measurements.",
            "",
        ])
        return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run safe-by-default MCP45HVX1 CLI HIL evidence capture."
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM15 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--address", type=parse_address, help="Expected MCP45HVX1 7-bit address")
    parser.add_argument("--timeout", type=float, default=10.0, help="Base per-command timeout in seconds")
    parser.add_argument("--output-dir", default="hil_logs", help="Base output directory")
    parser.add_argument("--no-color", action="store_true", help="Strip ANSI color from captured outputs")
    parser.add_argument("--include-output-change", action="store_true", help="Run local Wiper output-changing checks")
    parser.add_argument("--include-wiper-ramp", action="store_true", help="Use a multi-point Wiper ramp")
    parser.add_argument("--include-tcon-toggle", action="store_true", help="Toggle TCON terminal modes")
    parser.add_argument("--include-shdn", action="store_true", help="Record operator-guided SHDN checks")
    parser.add_argument("--include-wlat", action="store_true", help="Record operator-guided WLAT checks")
    parser.add_argument("--include-general-call", action="store_true", help="Run isolated-bus General Call check")
    parser.add_argument("--confirm-isolated-bus", action="store_true", help="Confirm General Call bus isolation")
    parser.add_argument("--operator-prompts", action="store_true", help="Prompt for measurements and manual pin actions")
    return parser


def validate_args(args: argparse.Namespace) -> list[str]:
    warnings: list[str] = []
    if args.include_wiper_ramp and not args.include_output_change:
        warnings.append("--include-wiper-ramp ignored because --include-output-change was not provided")
        args.include_wiper_ramp = False
    if args.include_tcon_toggle and not args.include_output_change:
        warnings.append("--include-tcon-toggle ignored because --include-output-change was not provided")
        args.include_tcon_toggle = False
    if (args.include_shdn or args.include_wlat) and not args.operator_prompts:
        warnings.append("SHDN/WLAT checks require --operator-prompts and will be recorded as skipped")
    return warnings


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    warnings = validate_args(args)
    run = HilRun(args)
    for warning in warnings:
        run.skipped.append(warning)
    cli: SerialCli | None = None
    try:
        cli = SerialCli(args.port, args.baud, args.timeout)
        run.safe_sequence(cli)
        if args.include_output_change:
            run.output_change_sequence(cli)
        if args.include_general_call:
            run.general_call_sequence(cli)
        if args.include_shdn or args.include_wlat:
            run.shdn_wlat_sequence(cli)
    except Exception as exc:  # Evidence should still be written after runtime failures.
        run.synthetic_log("RUNNER ERROR", f"{type(exc).__name__}: {exc}")
        run.runner_errors.append(f"runner error: {type(exc).__name__}: {exc}")
    finally:
        if cli is not None:
            cli.close()
        run.write_outputs(args)

    verdict = run.verdict()
    print(f"HIL evidence written to {run.output_dir}")
    print(f"Verdict: {verdict}")
    if verdict in {FAIL, FAIL_RESTORE_UNCERTAIN}:
        return 1
    if verdict == SKIPPED_UNSAFE:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
