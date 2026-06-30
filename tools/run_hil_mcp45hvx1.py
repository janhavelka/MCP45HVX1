#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
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
MAX_COMMAND_OUTPUT_BYTES = 64 * 1024
OUTPUT_TRUNCATED_MARKER = "\n[HIL_OUTPUT_TRUNCATED]\n"
NOT_RUN = "NOT_RUN"
UNKNOWN = "UNKNOWN"
NOT_APPLICABLE = "NOT_APPLICABLE"


SAFE_COMMAND_PLAN = [
    "version",
    "color off",
    "help",
    "scan",
    "addr",
    "probe",
    "cfg",
    "settings",
    "state",
    "drv",
    "health",
    "info",
    "errata",
    "readwiper",
    "readtcon",
    "read",
    "reg 0x00",
    "reg 0x04",
    "last",
    "dump",
    "selftest safe",
    "stress {stress_count}",
    "recover",
    "state",
    "drv",
]

BENCHMARK_COMMANDS = ("readwiper", "readtcon", "dump", "read", "probe")
SOAK_COMMANDS = ("readwiper", "readtcon", "dump", "state", "drv", "probe", "recover", "stress 5")


@dataclass
class CommandResult:
    seq: int
    test_id: str
    feature_area: str
    command: str
    group: str
    output_changing: bool
    required_flags: str
    expected: str
    started_utc: str
    ended_utc: str
    duration_s: float
    output: str
    failed: bool
    classification: str
    notes: str
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


def parse_positive_float(text: str) -> float:
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("value must be finite and greater than zero")
    return value


def parse_nonnegative_float(text: str) -> float:
    value = float(text)
    if not math.isfinite(value) or value < 0:
        raise argparse.ArgumentTypeError("value must be finite and non-negative")
    return value


def parse_nonnegative_int(text: str) -> int:
    value = int(text, 0)
    if value < 0:
        raise argparse.ArgumentTypeError("value must be non-negative")
    return value


def parse_positive_int(text: str) -> int:
    value = int(text, 0)
    if value <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
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
    def __init__(
        self,
        port: str,
        baud: int,
        timeout: float,
        idle_timeout: float,
        serial_dtr: str = "on",
        serial_rts: str = "off",
    ) -> None:
        serial = import_serial()
        self._serial = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=0.1,
            write_timeout=min(2.0, timeout),
            rtscts=False,
            dsrdtr=False,
        )
        self.apply_line_state("dtr", serial_dtr)
        self.apply_line_state("rts", serial_rts)
        self._timeout = timeout
        self._idle_timeout = idle_timeout

    def apply_line_state(self, name: str, value: str) -> None:
        if value == "unchanged":
            return
        setattr(self._serial, name, value == "on")

    def close(self) -> None:
        self._serial.close()

    def read_until_quiet(self, timeout: float | None = None, quiet_s: float = 0.35) -> str:
        deadline = time.monotonic() + (timeout if timeout is not None else self._timeout)
        last_rx = time.monotonic()
        quiet_s = self._idle_timeout if quiet_s <= 0 else quiet_s
        chunks: list[bytes] = []
        total_bytes = 0
        while time.monotonic() < deadline:
            data = self._serial.read(4096)
            if data:
                remaining = MAX_COMMAND_OUTPUT_BYTES - total_bytes
                if remaining <= 0:
                    chunks.append(OUTPUT_TRUNCATED_MARKER.encode("utf-8"))
                    break
                if len(data) > remaining:
                    chunks.append(data[:remaining])
                    chunks.append(OUTPUT_TRUNCATED_MARKER.encode("utf-8"))
                    break
                chunks.append(data)
                total_bytes += len(data)
                last_rx = time.monotonic()
                decoded = b"".join(chunks).decode("utf-8", errors="replace")
                if decoded.endswith("> ") or decoded.endswith("\n> "):
                    break
            elif time.monotonic() - last_rx >= quiet_s:
                break
        return b"".join(chunks).decode("utf-8", errors="replace")

    def command(self, command: str, timeout: float | None = None) -> str:
        try:
            self._serial.write((command + "\n").encode("utf-8"))
            self._serial.flush()
            return self.read_until_quiet(timeout=timeout, quiet_s=self._idle_timeout)
        except Exception as exc:
            pending = ""
            try:
                pending = self.read_until_quiet(timeout=2.0, quiet_s=self._idle_timeout)
            except Exception as read_exc:
                pending = f"[E] serial drain failed: {type(read_exc).__name__}: {read_exc}\n"
            return f"[E] serial command failed: {type(exc).__name__}: {exc}\n{pending}"


def command_failed(command: str, output: str) -> bool:
    text = strip_ansi(output)
    if not text.strip() or text.strip() == ">":
        return True
    if OUTPUT_TRUNCATED_MARKER.strip() in text:
        return True
    if re.search(r"(?m)^\s*\[FAIL\]", text):
        return True
    if re.search(r"(?m)^\s*\[E\]", text):
        return True
    if "Unknown command" in text:
        return True
    if "Usage:" in text and command not in {"help", "?"}:
        return True
    if re.search(r"(?i)restore .*failed", text):
        return True
    if re.search(r"(?mi)^\s*(?:Result|Restore)\s*:\s*FAIL\b", text):
        return True
    if re.search(r"(?m)\bStatus:\s+(?!OK\b)[A-Z_]+", text):
        return True
    command_prefixes = (
        r"probe|read|readwiper|readtcon|dump|stress|selftest|gc|wiper|tcon|"
        r"mode|raw write|cfg|settings|drv|health|state|reg|rreg|addr|addr_alt|"
        r"variant|res|rab|recover|last"
    )
    if re.search(rf"(?m)^(?:{command_prefixes})[^:\n]*:\s+(?!OK\b)[A-Z_]+", text):
        return True
    if command.startswith(("stress", "selftest")):
        if re.search(r"\bfail(?:ures)?\s*[=:]\s*[1-9]\d*", text, re.IGNORECASE):
            return True
        if re.search(r"\bErrors:\s*[1-9]\d*", text):
            return True
    if command in {"help", "?", "version", "ver"}:
        return False
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


def run_parser_self_test() -> bool:
    checks = [
        command_failed("probe", "[FAIL] probe\n"),
        command_failed("probe", "[E] timeout\n"),
        command_failed("settings", "settings: I2C_TIMEOUT\n> "),
        command_failed("stress 100", "stress ok=99 failures=1\n> "),
        not command_failed("help", "Usage: help\n> "),
        not command_failed("settings", "settings: OK\n> "),
        command_failed("reg 0x01", "reg: INVALID_PARAM\n> "),
        command_failed("addr 0x60", "addr: INVALID_PARAM\n> "),
    ]
    return all(checks)


class HilRun:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        self.output_dir = Path(args.output_dir) / f"mcp45hvx1_{timestamp}"
        self.output_dir.mkdir(parents=True, exist_ok=False)
        self.raw_parts: list[str] = []
        self.commands: list[CommandResult] = []
        self.operator_notes: list[str] = []
        self.skipped: list[str] = []
        self.limitations: list[str] = []
        self.runner_errors: list[str] = []
        self.baseline: dict[str, int] = {}
        self.final_state: dict[str, Any] = {}
        self.benchmarks: dict[str, dict[str, Any]] = {}
        self.soak_summary: dict[str, Any] = {}
        self.restore_uncertain = False
        self.operator_review_required = False

    def note(self, text: str) -> None:
        self.operator_notes.append(text)

    def synthetic_log(self, label: str, text: str) -> None:
        self.raw_parts.append(f"{utc_now()} EVENT {label}\n{text.rstrip()}\n")

    def next_test_id(self, group: str) -> str:
        clean_group = re.sub(r"[^A-Za-z0-9]+", "-", group).strip("-").upper() or "STEP"
        return f"{clean_group}-{len(self.commands) + 1:03d}"

    def run_command(self, cli: SerialCli, command: str, group: str, output_changing: bool = False,
                    timeout: float | None = None, required_flags: str = "-",
                    feature_area: str = "CLI", expected: str = "No CLI error tokens",
                    expected_tokens: tuple[str, ...] = (), notes: str = "",
                    expect_cli_failure: bool = False) -> CommandResult:
        seq = len(self.commands) + 1
        started = utc_now()
        t0 = time.monotonic()
        self.raw_parts.append(f"{started} TX {command}\n")
        if getattr(self.args, "command_pacing_s", 0.0) > 0:
            time.sleep(self.args.command_pacing_s)
        output = cli.command(command, timeout=timeout)
        duration = time.monotonic() - t0
        ended = utc_now()
        stored_output = sanitize_output(output, self.args.no_color)
        missing = [token for token in expected_tokens if token not in strip_ansi(stored_output)]
        failed_by_output = command_failed(command, stored_output)
        failed = (not failed_by_output) if expect_cli_failure else failed_by_output
        failed = failed or bool(missing)
        if missing:
            notes = f"{notes}; missing expected token(s): {', '.join(missing)}".strip("; ")
        if expect_cli_failure:
            notes = f"{notes}; expected visible CLI rejection".strip("; ")
        for line in stored_output.splitlines():
            self.raw_parts.append(f"{ended} RX {line}\n")
        self.raw_parts.append(f"{ended} EVENT read_complete seq={seq:03d} duration_s={duration:.3f}\n")
        result = CommandResult(
            seq=seq,
            test_id=self.next_test_id(group),
            feature_area=feature_area,
            command=command,
            group=group,
            output_changing=output_changing,
            required_flags=required_flags,
            expected=expected,
            started_utc=started,
            ended_utc=ended,
            duration_s=round(duration, 3),
            output=stored_output,
            failed=failed,
            classification=FAIL if failed else "PASS",
            notes=notes or "-",
        )
        self.commands.append(result)
        return result

    def record_not_run(self, group: str, command: str, feature_area: str, reason: str,
                       output_changing: bool = False, required_flags: str = "-",
                       classification: str = NOT_RUN, affect_verdict: bool = False) -> None:
        now = utc_now()
        seq = len(self.commands) + 1
        self.commands.append(CommandResult(
            seq=seq,
            test_id=self.next_test_id(group),
            feature_area=feature_area,
            command=command,
            group=group,
            output_changing=output_changing,
            required_flags=required_flags,
            expected="Fixture and operator preconditions available",
            started_utc=now,
            ended_utc=now,
            duration_s=0.0,
            output="",
            failed=False,
            classification=classification,
            notes=reason,
            skipped=True,
        ))
        item = f"{command}: {reason}"
        if affect_verdict:
            self.skipped.append(item)
        else:
            self.limitations.append(item)

    def safe_sequence(self, cli: SerialCli) -> None:
        if getattr(self.args, "boot_settle_s", 0.0) > 0:
            time.sleep(self.args.boot_settle_s)
        boot = cli.read_until_quiet(timeout=max(self.args.timeout, 2.0), quiet_s=0.5)
        if boot:
            for line in sanitize_output(boot, self.args.no_color).splitlines():
                self.raw_parts.append(f"{utc_now()} RX_BOOT {line}\n")
        self.run_command(cli, "version", "safe", feature_area="Connectivity",
                         expected_tokens=("MCP45HVX1",))
        self.run_command(cli, "color off", "safe", feature_area="CLI")
        self.run_command(cli, "help", "safe", timeout=max(self.args.timeout, 4.0),
                         feature_area="CLI", expected_tokens=("scan", "probe", "stress"))
        self.run_command(cli, "scan", "safe", timeout=max(self.args.timeout, 8.0),
                         feature_area="I2C discovery")
        self.run_command(cli, "addr", "safe", feature_area="Configuration")
        if self.args.address is not None:
            self.run_command(cli, address_command(self.args.address), "safe",
                             feature_area="Configuration")
        self.run_command(cli, "probe", "safe", feature_area="Connectivity")
        self.run_command(cli, "cfg", "safe", feature_area="Configuration")
        self.run_command(cli, "settings", "safe", feature_area="Configuration")
        self.run_command(cli, "state", "safe", feature_area="Lifecycle/health")
        self.run_command(cli, "drv", "safe", feature_area="Lifecycle/health")
        self.run_command(cli, "health", "safe", feature_area="Lifecycle/health")
        self.run_command(cli, "info", "safe", feature_area="Identity/configuration")
        self.run_command(cli, "errata", "safe", feature_area="Diagnostics")
        self.run_command(cli, "readwiper", "safe", feature_area="Register read")
        self.run_command(cli, "readtcon", "safe", feature_area="Register read")
        self.run_command(cli, "read", "safe", feature_area="Aggregate read")
        self.run_command(cli, "reg 0x00", "safe", feature_area="Raw register read")
        self.run_command(cli, "reg 0x04", "safe", feature_area="Raw register read")
        self.run_command(cli, "last", "safe", feature_area="Raw register read")
        self.run_command(cli, "dump", "safe", feature_area="Aggregate/raw read")
        self.run_command(cli, "selftest safe", "safe", timeout=max(self.args.timeout, 8.0),
                         feature_area="Self-test")
        stress_count = getattr(self.args, "stress_count", 100)
        stress_timeout = getattr(self.args, "stress_timeout_s", 25.0)
        self.run_command(cli, f"stress {stress_count}", "safe",
                         timeout=max(self.args.timeout, stress_timeout),
                         feature_area="Read-only stress")
        self.run_command(cli, "recover", "safe", feature_area="Recovery")
        self.run_command(cli, "invalid_command", "invalid-input",
                         feature_area="Invalid input", expected="Unknown command must be visible",
                         expected_tokens=("Unknown command",),
                         notes="Expected CLI-visible invalid-command rejection",
                         expect_cli_failure=True)
        self.run_command(cli, "reg 0x01", "invalid-input", feature_area="Invalid input",
                         expected="Reserved register rejected", expect_cli_failure=True)
        self.run_command(cli, "addr 0x60", "invalid-input", feature_area="Invalid input",
                         expected="Invalid address rejected", expect_cli_failure=True)
        self.run_command(cli, "state", "safe-final", feature_area="Lifecycle/health")
        self.run_command(cli, "drv", "safe-final", feature_area="Lifecycle/health")

    def capture_baseline(self, cli: SerialCli) -> bool:
        wiper = self.run_command(cli, "readwiper", "baseline")
        tcon = self.run_command(cli, "readtcon", "baseline")
        state = self.run_command(cli, "state", "baseline")
        wiper_value = parse_hex_field(wiper.output, "wiper")
        tcon_value = parse_hex_field(tcon.output, "tcon")
        resolution = parse_resolution(state.output)
        if wiper.failed or tcon.failed or state.failed:
            self.skipped.append("output-changing group skipped because baseline commands failed")
            return False
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
                else:
                    self.note(f"TCON mode {mode} measurement not collected; operator review required.")

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
        self.run_command(cli, "state", group)
        self.run_command(cli, "drv", group)

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
                "Type ISOLATED_SAFE to confirm only MCP45HVX1 validation devices are on this I2C bus "
                "and a safe load/measurement setup is connected: "
            )
            if answer != "ISOLATED_SAFE":
                self.skipped.append(
                    "General Call skipped: operator did not confirm isolated bus and safe load"
                )
                return
        if not self.baseline and not self.capture_baseline(cli):
            return
        self.run_command(cli, "errata", "general-call")
        self.run_command(cli, "gc arm", "general-call", True,
                         required_flags="--include-general-call --confirm-isolated-bus --operator-prompts")
        self.run_command(cli, "gc inc", "general-call", True,
                         required_flags="--include-general-call --confirm-isolated-bus --operator-prompts")
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

    def dry_run_sequence(self) -> None:
        stress_count = getattr(self.args, "stress_count", 100)
        for command in SAFE_COMMAND_PLAN:
            cmd = command.format(stress_count=stress_count)
            self.record_not_run("dry-run-plan", cmd, "Dry run", "dry-run plan only",
                                classification=UNKNOWN)
        if self.args.address is not None:
            self.record_not_run("dry-run-plan", address_command(self.args.address),
                                "Dry run", "dry-run plan only", classification=UNKNOWN)
        self.record_static_limitations()

    def benchmark_sequence(self, cli: SerialCli) -> None:
        samples = getattr(self.args, "benchmark_samples", 0)
        if samples <= 0:
            return
        for command in BENCHMARK_COMMANDS:
            latencies: list[float] = []
            failures = 0
            started = time.monotonic()
            for _ in range(samples):
                result = self.run_command(cli, command, "benchmark", feature_area="Sampling/timing",
                                          timeout=max(self.args.timeout, 3.0))
                latencies.append(result.duration_s)
                if result.failed:
                    failures += 1
                    if failures >= self.args.max_failure_burst:
                        break
            elapsed = time.monotonic() - started
            success_count = len(latencies) - failures
            mean = sum(latencies) / len(latencies) if latencies else 0.0
            self.benchmarks[command] = {
                "attempts": len(latencies),
                "success": success_count,
                "failures": failures,
                "elapsed_s": round(elapsed, 3),
                "min_latency_s": round(min(latencies), 3) if latencies else None,
                "mean_latency_s": round(mean, 3) if latencies else None,
                "max_latency_s": round(max(latencies), 3) if latencies else None,
                "effective_hz": round(success_count / elapsed, 3) if elapsed > 0 else 0.0,
            }
            if failures >= self.args.max_failure_burst:
                self.runner_errors.append(
                    f"benchmark stopped after {failures} failures for command {command}"
                )
                break

    def soak_sequence(self, cli: SerialCli) -> None:
        duration_s = getattr(self.args, "soak_duration_s", 0.0)
        if duration_s <= 0:
            self.soak_summary = {"requested": False}
            return

        started_utc = utc_now()
        started = time.monotonic()
        deadline = started + duration_s
        max_commands = getattr(self.args, "soak_max_commands", 0)
        command_counts: dict[str, int] = {}
        latencies: list[float] = []
        failures = 0
        consecutive_failures = 0
        worst_latency = 0.0
        loop_index = 0
        stopped_reason = ""

        while time.monotonic() < deadline:
            if max_commands > 0 and sum(command_counts.values()) >= max_commands:
                stopped_reason = "max command limit reached"
                break
            command = SOAK_COMMANDS[loop_index % len(SOAK_COMMANDS)]
            loop_index += 1
            result = self.run_command(cli, command, "soak", feature_area="8-hour soak",
                                      timeout=max(self.args.timeout, self.args.stress_timeout_s))
            command_counts[command] = command_counts.get(command, 0) + 1
            latencies.append(result.duration_s)
            worst_latency = max(worst_latency, result.duration_s)
            if result.failed:
                failures += 1
                consecutive_failures += 1
                if consecutive_failures >= self.args.max_failure_burst:
                    stopped_reason = f"soak stopped after {consecutive_failures} consecutive failures"
                    self.runner_errors.append(stopped_reason)
                    break
            else:
                consecutive_failures = 0

        elapsed = time.monotonic() - started
        attempts = sum(command_counts.values())
        mean = sum(latencies) / len(latencies) if latencies else 0.0
        self.soak_summary = {
            "requested": True,
            "start_utc": started_utc,
            "end_utc": utc_now(),
            "requested_duration_s": duration_s,
            "elapsed_s": round(elapsed, 3),
            "attempts": attempts,
            "success": attempts - failures,
            "failures": failures,
            "command_counts": command_counts,
            "min_latency_s": round(min(latencies), 3) if latencies else None,
            "mean_latency_s": round(mean, 3) if latencies else None,
            "max_latency_s": round(worst_latency, 3) if latencies else None,
            "effective_hz": round((attempts - failures) / elapsed, 3) if elapsed > 0 else 0.0,
            "completed_requested_duration": (
                time.monotonic() >= deadline and failures == 0 and not stopped_reason
            ),
            "max_commands_limit": max_commands,
            "stopped_reason": stopped_reason,
        }
        self.run_command(cli, "state", "soak-final", feature_area="8-hour soak")
        self.run_command(cli, "drv", "soak-final", feature_area="8-hour soak")

    def record_static_limitations(self) -> None:
        self.record_not_run("not-run", "output-changing matrix", "Output-changing",
                            "requires explicit safe load and --include-output-change",
                            output_changing=True,
                            required_flags="--include-output-change --operator-prompts")
        self.record_not_run("not-run", "General Call matrix", "General Call",
                            "requires isolated bus confirmation and operator prompts",
                            output_changing=True,
                            required_flags="--include-general-call --confirm-isolated-bus --operator-prompts")
        self.record_not_run("not-run", "SHDN physical behavior", "SHDN",
                            "requires controlled SHDN wiring and external physical observation",
                            required_flags="--include-shdn --operator-prompts")
        self.record_not_run("not-run", "WLAT physical behavior", "WLAT",
                            "requires controlled WLAT wiring and external physical observation",
                            required_flags="--include-wlat --operator-prompts")
        self.record_not_run("not-run", "I2C fault injection", "Fault injection",
                            "no safe fault-injection fixture was declared")
        self.record_not_run("not-run", "address strap matrix", "Address straps",
                            "requires rewiring A1/A0 strap combinations")
        self.record_not_run("not-run", "POR/BOR rail cycling", "Reset/persistence",
                            "requires controlled rail cycling fixture")
        self.record_not_run("not-run", "poll-job HIL", "Staged jobs",
                            "current CLI does not expose start*/pollJob job API")
        self.record_not_run("not-applicable", "unique silicon identity register",
                            "Identity", "MCP45HVX1 exposes no unique chip ID in this driver",
                            classification=NOT_APPLICABLE)
        self.record_not_run("not-applicable", "ADC conversion modes",
                            "Modes/timing", "digital potentiometer has no ADC conversion modes",
                            classification=NOT_APPLICABLE)
        self.record_not_run("not-applicable", "thresholds/alerts/FIFO/accumulators",
                            "Diagnostics", "not exposed by MCP45HVX1 volatile register model",
                            classification=NOT_APPLICABLE)

    def verdict(self) -> str:
        final_bad = self.final_state_is_bad()
        actual_results = [result for result in self.commands if result.classification in {"PASS", FAIL}]
        if self.restore_uncertain:
            return FAIL_RESTORE_UNCERTAIN
        if final_bad and any(result.output_changing for result in actual_results):
            return FAIL_RESTORE_UNCERTAIN
        if self.runner_errors:
            return FAIL
        unsafe_skip_tokens = (
            "General Call",
            "SHDN",
            "WLAT",
            "output-changing",
            "--include-output-change",
            "--include-general-call",
        )
        if self.skipped and any(any(token in item for token in unsafe_skip_tokens)
                                for item in self.skipped):
            return SKIPPED_UNSAFE
        if not self.commands or not actual_results:
            return UNKNOWN
        if not self.commands:
            return FAIL
        if any(result.failed for result in self.commands):
            return FAIL
        if final_bad:
            return FAIL
        output_changed = any(result.output_changing for result in actual_results)
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
            entry["output_changing"] += (
                1 if result.output_changing and result.classification in {"PASS", FAIL} else 0
            )
        for item in self.skipped:
            entry = groups.setdefault("skipped", {"sent": 0, "failed": 0, "output_changing": 0})
            entry["failed"] += 1 if "runner error" in item else 0
        return groups

    def result_counts(self) -> dict[str, int]:
        counts = {"PASS": 0, FAIL: 0, UNKNOWN: 0, NOT_RUN: 0, NOT_APPLICABLE: 0}
        for result in self.commands:
            counts[result.classification] = counts.get(result.classification, 0) + 1
        return counts

    def write_outputs(self, args: argparse.Namespace) -> None:
        verdict = self.verdict()
        raw_header = (
            f"{utc_now()} EVENT runner=tools/run_hil_mcp45hvx1.py\n"
            f"{utc_now()} EVENT argv={' '.join(sys.argv)}\n"
            f"{utc_now()} EVENT port={args.port or 'NOT_SET'} baud={args.baud} "
            f"address={format_hex(args.address) if args.address is not None else 'default'} "
            f"dtr={getattr(args, 'serial_dtr', 'on')} rts={getattr(args, 'serial_rts', 'off')}\n"
        )
        raw_serial = raw_header + "".join(self.raw_parts).lstrip()
        (self.output_dir / "raw_serial.txt").write_text(raw_serial, encoding="utf-8")
        (self.output_dir / "commands.txt").write_text(self.commands_text(), encoding="utf-8")
        (self.output_dir / "operator_notes.md").write_text(self.operator_notes_text(), encoding="utf-8")
        final_state_text = next((r.output for r in reversed(self.commands) if r.command == "state"), "")
        final_health_text = next((r.output for r in reversed(self.commands) if r.command in {"drv", "health"}), "")
        actual_output_change = any(
            r.output_changing and r.classification in {"PASS", FAIL} for r in self.commands
        )
        summary = {
            "schema_version": "mcp45hvx1-hil-evidence-v1",
            "generated_at_utc": utc_now(),
            "generated_at_local": datetime.now().astimezone().replace(microsecond=0).isoformat(),
            "verdict": verdict,
            "verdict_reasons": self.verdict_reasons(),
            "repo": self.repo_info(),
            "runner": {
                "path": "tools/run_hil_mcp45hvx1.py",
                "argv": sys.argv,
                "python": sys.version.split()[0],
            },
            "serial": {
                "port": args.port or "NOT_SET",
                "baud": args.baud,
                "address": format_hex(args.address) if args.address is not None else None,
                "timeout_s": args.timeout,
                "dtr": getattr(args, "serial_dtr", "on"),
                "rts": getattr(args, "serial_rts", "off"),
            },
            "options": {
                "no_color": bool(args.no_color),
                "dry_run": bool(getattr(args, "dry_run", False)),
                "parser_self_test": bool(getattr(args, "parser_self_test", False)),
                "boot_settle_s": getattr(args, "boot_settle_s", 0.0),
                "idle_timeout_s": getattr(args, "idle_timeout_s", 0.35),
                "command_pacing_s": getattr(args, "command_pacing_s", 0.0),
                "serial_dtr": getattr(args, "serial_dtr", "on"),
                "serial_rts": getattr(args, "serial_rts", "off"),
                "benchmark_samples": getattr(args, "benchmark_samples", 0),
                "soak_duration_s": getattr(args, "soak_duration_s", 0.0),
                "soak_max_commands": getattr(args, "soak_max_commands", 0),
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
                "board": getattr(args, "board", ""),
                "environment": getattr(args, "environment", ""),
                "dut_variant": "",
                "rab_ohms": None,
                "package_marking": "",
                "date_code": "",
                "bus_topology": "",
                "shdn_wiring": "",
                "wlat_wiring": "",
                "instruments": [],
                "rails_and_load": "",
                "fixture_notes": getattr(args, "fixture_note", ""),
                "safety_assumptions": getattr(args, "safety_assumption", ""),
            },
            "started_utc": self.commands[0].started_utc if self.commands else utc_now(),
            "output_dir": str(self.output_dir),
            "port": args.port or "NOT_SET",
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
            "result_counts": self.result_counts(),
            "baseline": self.baseline,
            "final": {
                "wiper": self.final_state.get("wiper"),
                "tcon": self.final_state.get("tcon"),
                "state": final_state_text,
                "health": final_health_text,
            },
            "restore": {
                "required": actual_output_change,
                "verified": None if not actual_output_change else not self.restore_uncertain,
                "reason": "restore uncertain" if self.restore_uncertain else "",
            },
            "measurements": self.operator_notes,
            "benchmarks": self.benchmarks,
            "soak_summary": self.soak_summary,
            "skipped": self.skipped,
            "limitations": self.limitations,
            "operator_notes": self.operator_notes,
            "commands": [
                {
                    "seq": result.seq,
                    "test_id": result.test_id,
                    "feature_area": result.feature_area,
                    "command": result.command,
                    "group": result.group,
                    "output_changing": result.output_changing,
                    "required_flags": result.required_flags,
                    "expected": result.expected,
                    "started_utc": result.started_utc,
                    "ended_utc": result.ended_utc,
                    "duration_s": result.duration_s,
                    "failed": result.failed,
                    "result": result.classification,
                    "notes": result.notes,
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
        report = self.report_text(summary)
        (self.output_dir / "report.md").write_text(report, encoding="utf-8")
        if getattr(args, "report_file", None):
            report_path = Path(args.report_file)
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(report, encoding="utf-8")

    def commands_text(self) -> str:
        lines = [
            "seq\ttest_id\tutc\tgroup\tfeature_area\tclassification\tcommand\trequired_flags\t"
            "expected\tresult\tduration_s\tnotes"
        ]
        for result in self.commands:
            lines.append(
                f"{result.seq:03d}\t{result.test_id}\t{result.started_utc}\t{result.group}\t"
                f"{result.feature_area}\t"
                f"{'output_changing' if result.output_changing else 'read_only'}\t"
                f"{result.command}\t{result.required_flags}\t"
                f"{result.expected}\t{result.classification}\t{result.duration_s:.3f}\t{result.notes}"
            )
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
        def md(value: object) -> str:
            return str(value).replace("|", "\\|").replace("\n", " ").replace("\r", " ")

        counts = summary.get("result_counts", {})
        soak = summary.get("soak_summary", {}) or {"requested": False}
        benchmarks = summary.get("benchmarks", {})
        lines = [
            f"# MCP45HVX1 HIL Validation Report - {summary['port']}",
            "",
            f"Date/time UTC: `{summary['generated_at_utc']}`",
            f"Date/time local: `{summary['generated_at_local']}`",
            f"Verdict: `{summary['verdict']}`",
            f"Repo: `{summary['repo']['branch']}` at `{summary['repo']['commit']}` dirty={summary['repo']['dirty']}",
            f"Runner: `{' '.join(summary['runner']['argv'])}`",
            f"Port: `{summary['port']}`",
            f"Baud: `{summary['baud']}`",
            f"Address: `{summary['address']}`",
            f"Board/environment: `{summary['hardware']['board']}` / `{summary['hardware']['environment']}`",
            f"Output directory: `{summary['output_dir']}`",
            "",
            "## Hardware Setup",
            "",
            f"- Fixture notes: {summary['hardware']['fixture_notes'] or 'not recorded'}",
            f"- Safety assumptions: {summary['hardware']['safety_assumptions'] or 'safe/read-only HIL only unless operator-gated flags were used'}",
            "- Detected identity/address comes from CLI scan/probe/config output; MCP45HVX1 has no unique ID register exposed by this driver.",
            "- Analog, high-voltage, SHDN, WLAT, and General Call safety require external evidence and are not inferred from register readback.",
            "",
            "## Scope",
            "",
            "- Safe/read-only sequence always runs first.",
            f"- Output-changing group requested: `{summary['include_output_change']}`.",
            f"- General Call requested: `{summary['include_general_call']}`.",
            f"- SHDN check requested: `{summary['include_shdn']}`.",
            f"- WLAT check requested: `{summary['include_wlat']}`.",
            f"- Benchmark samples per path: `{summary['options']['benchmark_samples']}`.",
            f"- Soak duration requested: `{summary['options']['soak_duration_s']}` seconds.",
            "",
            "## Evidence Files",
            "",
            "- `raw_serial.txt` - raw command transcript after optional ANSI stripping.",
            "- `commands.txt` - command list with group, timing, and failure flags.",
            "- `summary.json` - machine-readable summary and verdict.",
            "- `operator_notes.md` - measurements, observations, skipped unsafe checks.",
            "- `report.md` - this report.",
            "",
            "## Commands Run",
            "",
            f"- HIL command: `{' '.join(summary['runner']['argv'])}`",
            "- Build, upload, and repository validation commands are recorded in the parent validation notes/final response.",
            "",
            "## Summary",
            "",
            "| Result | Count |",
            "|---|---:|",
            f"| PASS | {counts.get('PASS', 0)} |",
            f"| FAIL | {counts.get(FAIL, 0)} |",
            f"| UNKNOWN | {counts.get(UNKNOWN, 0)} |",
            f"| NOT RUN | {counts.get(NOT_RUN, 0)} |",
            f"| NOT APPLICABLE | {counts.get(NOT_APPLICABLE, 0)} |",
            "",
            "## Group Summary",
            "",
            "| Group | Sent | Failed | Output-changing |",
            "|---|---:|---:|---:|",
        ]
        for group, data in sorted(summary["groups"].items()):
            lines.append(
                f"| {md(group)} | {data['sent']} | {data['failed']} | {data['output_changing']} |"
            )
        lines.extend([
            "",
            "## Baseline",
            "",
            f"- Wiper: `{format_hex(summary['baseline']['wiper']) if 'wiper' in summary['baseline'] else 'unknown'}`",
            f"- TCON: `{format_hex(summary['baseline']['tcon']) if 'tcon' in summary['baseline'] else 'unknown'}`",
            "",
            "## Detailed Steps",
            "",
            "| Test ID | Area | Command/Step | Expected | Observed | Elapsed s | Result | Notes |",
            "|---|---|---|---|---|---:|---|---|",
        ])
        for command in summary["commands"]:
            observed = "see raw_serial.txt"
            if command["result"] in {NOT_RUN, NOT_APPLICABLE, UNKNOWN}:
                observed = command["result"]
            lines.append(
                f"| {md(command['test_id'])} | {md(command['feature_area'])} | "
                f"`{md(command['command'])}` | {md(command['expected'])} | {md(observed)} | "
                f"{command['duration_s']:.3f} | {md(command['result'])} | {md(command['notes'])} |"
            )
        lines.extend([
            "",
            "## Sampling And Timing",
            "",
        ])
        if benchmarks:
            lines.extend([
                "| Path | Attempts | Success | Failures | Min s | Mean s | Max s | Hz |",
                "|---|---:|---:|---:|---:|---:|---:|---:|",
            ])
            for path, data in benchmarks.items():
                lines.append(
                    f"| `{md(path)}` | {data['attempts']} | {data['success']} | "
                    f"{data['failures']} | {data['min_latency_s']} | {data['mean_latency_s']} | "
                    f"{data['max_latency_s']} | {data['effective_hz']} |"
                )
        else:
            lines.append("- No benchmark mode was requested.")
        lines.extend([
            "",
            "## Soak Summary",
            "",
        ])
        if soak.get("requested"):
            lines.extend([
                f"- Start UTC: `{soak.get('start_utc')}`",
                f"- End UTC: `{soak.get('end_utc')}`",
                f"- Requested duration: `{soak.get('requested_duration_s')}` seconds",
                f"- Actual duration: `{soak.get('elapsed_s')}` seconds",
                f"- Attempts/success/failures: `{soak.get('attempts')}` / `{soak.get('success')}` / `{soak.get('failures')}`",
                f"- Worst latency: `{soak.get('max_latency_s')}` seconds",
                f"- Effective rate: `{soak.get('effective_hz')}` Hz",
                f"- Completed requested duration: `{soak.get('completed_requested_duration')}`",
                f"- Stopped reason: `{soak.get('stopped_reason') or '-'}`",
                f"- Command mix: `{json.dumps(soak.get('command_counts', {}), sort_keys=True)}`",
            ])
        else:
            lines.append("- Soak mode was not requested in this run.")
        lines.extend([
            "",
            "## Failures And Anomalies",
            "",
        ])
        if summary["verdict_reasons"]:
            lines.extend(f"- {reason}" for reason in summary["verdict_reasons"])
        else:
            lines.append("- None recorded by runner classification.")
        lines.extend([
            "",
            "## Limitations And Not Run",
            "",
        ])
        if summary.get("limitations"):
            lines.extend(f"- {item}" for item in summary["limitations"])
        else:
            lines.append("- None recorded.")
        if summary["skipped"]:
            lines.extend(["", "Unsafe/requested skips:"])
            lines.extend(f"- {item}" for item in summary["skipped"])
        lines.extend([
            "",
            "## Proposed Fixes And Audit Notes",
            "",
            "- Review any FAIL rows against `raw_serial.txt` before making hardware claims.",
            "- Staged poll-job HIL remains a CLI coverage gap unless a read-only job command is added.",
            "- Physical analog movement, terminal current, high-voltage behavior, SHDN/WLAT overrides, and General Call safety require external measurements.",
            "",
            "## Fixes Implemented During Run",
            "",
            "- Recorded separately in git diff and final response; this runner report captures HIL evidence only.",
            "",
            "## Final Verification",
            "",
            "- Final state and health commands are included in the detailed steps when the serial session reached them.",
            "- Repository build/test commands and results should be recorded in the parent validation notes or final response.",
            "",
            "## Claims Supported",
            "",
            f"- Safe-only HIL evidence: `{summary['verdict'] in {PASS_SAFE_ONLY, PASS_WITH_OUTPUT_CHANGE, OPERATOR_REVIEW_REQUIRED}}`.",
            f"- Output-changing HIL evidence: `{summary['verdict'] == PASS_WITH_OUTPUT_CHANGE}`.",
            f"- General Call isolated-bus command evidence: `{summary['include_general_call'] and summary['confirm_isolated_bus'] and summary['verdict'] == PASS_WITH_OUTPUT_CHANGE}`.",
            "- General Call analog/safety evidence: `false` unless operator notes attach external measurements and release-checklist signoff.",
            "- High-voltage evidence: `false` unless operator notes attach external measurements.",
            "",
        ])
        return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run safe-by-default MCP45HVX1 CLI HIL evidence capture."
    )
    parser.add_argument("--port", help="Serial port, for example COM15 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--address", type=parse_address, help="Expected MCP45HVX1 7-bit address")
    parser.add_argument("--timeout", "--timeout-s", dest="timeout", type=parse_positive_float, default=10.0,
                        help="Base per-command timeout in seconds")
    parser.add_argument("--idle-timeout-s", type=parse_positive_float, default=0.35,
                        help="Quiet period that ends a serial read")
    parser.add_argument("--boot-settle-s", type=parse_nonnegative_float, default=0.0,
                        help="Wait after opening serial before reading boot text")
    parser.add_argument("--command-pacing-s", type=parse_nonnegative_float, default=0.0,
                        help="Delay before each command write")
    parser.add_argument("--serial-dtr", choices=("on", "off", "unchanged"), default="on",
                        help="DTR line state after opening serial; USB CDC boards often require on")
    parser.add_argument("--serial-rts", choices=("on", "off", "unchanged"), default="off",
                        help="RTS line state after opening serial")
    parser.add_argument("--stress-count", type=parse_nonnegative_int, default=100,
                        help="Read-only stress count for the safe sequence")
    parser.add_argument("--stress-timeout-s", type=parse_positive_float, default=25.0,
                        help="Timeout for stress commands")
    parser.add_argument("--benchmark-samples", type=parse_nonnegative_int, default=0,
                        help="Run this many samples per safe benchmark command")
    parser.add_argument("--soak-duration-s", type=parse_nonnegative_float, default=0.0,
                        help="Run bounded safe-only soak for this many seconds")
    parser.add_argument("--soak-max-commands", type=parse_nonnegative_int, default=0,
                        help="Optional max command cap for soak; zero means duration only")
    parser.add_argument("--max-failure-burst", type=parse_positive_int, default=3,
                        help="Stop benchmark/soak after this many consecutive failures")
    parser.add_argument("--dry-run", action="store_true", help="Write a planned report without opening serial")
    parser.add_argument("--parser-self-test", action="store_true", help="Run host parser/classifier self-test")
    parser.add_argument("--output-dir", default="hil_logs", help="Base output directory")
    parser.add_argument("--report-file", help="Optional Markdown report output path")
    parser.add_argument("--board", default="", help="Board description for report metadata")
    parser.add_argument("--environment", default="", help="Firmware/build environment for report metadata")
    parser.add_argument("--fixture-note", default="", help="Fixture details for report metadata")
    parser.add_argument("--safety-assumption", default="", help="Electrical safety assumptions for report metadata")
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
    if not args.port and not args.dry_run and not args.parser_self_test:
        raise argparse.ArgumentTypeError("--port is required unless --dry-run or --parser-self-test is used")
    if args.include_output_change and not args.operator_prompts:
        warnings.append(
            "--include-output-change requires --operator-prompts for safe-load "
            "confirmation and will be recorded as skipped"
        )
        args.include_output_change = False
    if args.include_general_call and not args.operator_prompts:
        warnings.append(
            "--include-general-call requires --operator-prompts for safe-load "
            "and isolated-bus confirmation and will be recorded as skipped"
        )
        args.include_general_call = False
    if args.include_wiper_ramp and not args.include_output_change:
        warnings.append("--include-wiper-ramp ignored because --include-output-change was not provided")
        args.include_wiper_ramp = False
    if args.include_tcon_toggle and not args.include_output_change:
        warnings.append("--include-tcon-toggle ignored because --include-output-change was not provided")
        args.include_tcon_toggle = False
    if (args.include_shdn or args.include_wlat) and not args.operator_prompts:
        warnings.append("SHDN/WLAT checks require --operator-prompts and will be recorded as skipped")
    return warnings


def default_report_path(port: str | None) -> str | None:
    if not port:
        return None
    safe_port = re.sub(r"[^A-Za-z0-9_-]+", "-", port).strip("-") or "serial"
    date = datetime.now().strftime("%Y%m%d")
    return str(ROOT / "docs" / "reports" / f"hil-validation-{safe_port}-{date}.md")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        warnings = validate_args(args)
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))
    if args.report_file is None:
        args.report_file = default_report_path(args.port)
    if args.parser_self_test:
        ok = run_parser_self_test()
        print(f"Parser self-test: {'PASS' if ok else 'FAIL'}")
        if not ok:
            return 1
        if not args.dry_run and not args.port:
            return 0
    run = HilRun(args)
    for warning in warnings:
        run.skipped.append(warning)
    if args.dry_run:
        run.synthetic_log("DRY RUN", "No serial port was opened.")
        run.dry_run_sequence()
        run.write_outputs(args)
        print(f"HIL dry-run evidence written to {run.output_dir}")
        print(f"Verdict: {run.verdict()}")
        return 0
    cli: SerialCli | None = None
    try:
        cli = SerialCli(
            args.port,
            args.baud,
            args.timeout,
            args.idle_timeout_s,
            args.serial_dtr,
            args.serial_rts,
        )
        run.safe_sequence(cli)
        if args.include_output_change:
            run.output_change_sequence(cli)
        if args.include_general_call:
            run.general_call_sequence(cli)
        if args.include_shdn or args.include_wlat:
            run.shdn_wlat_sequence(cli)
        run.benchmark_sequence(cli)
        run.soak_sequence(cli)
        run.record_static_limitations()
    except Exception as exc:  # Evidence should still be written after runtime failures.
        run.synthetic_log("RUNNER ERROR", f"{type(exc).__name__}: {exc}")
        run.runner_errors.append(f"runner error: {type(exc).__name__}: {exc}")
        run.record_static_limitations()
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
