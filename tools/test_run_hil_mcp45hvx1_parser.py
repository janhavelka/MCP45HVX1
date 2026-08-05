#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from argparse import Namespace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "tools" / "run_hil_mcp45hvx1.py"

spec = importlib.util.spec_from_file_location("run_hil_mcp45hvx1_under_test", RUNNER_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot import {RUNNER_PATH}")
hil = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = hil
spec.loader.exec_module(hil)


def make_args(output_dir: str, **overrides: object) -> Namespace:
    values: dict[str, object] = {
        "port": "TEST",
        "baud": 115200,
        "address": None,
        "timeout": 1.0,
        "idle_timeout_s": 0.35,
        "boot_settle_s": 0.0,
        "command_pacing_s": 0.0,
        "serial_dtr": "on",
        "serial_rts": "off",
        "stress_count": 100,
        "stress_timeout_s": 25.0,
        "benchmark_samples": 0,
        "soak_duration_s": 0.0,
        "soak_max_commands": 0,
        "max_failure_burst": 3,
        "dry_run": False,
        "parser_self_test": False,
        "report_file": None,
        "board": "",
        "environment": "",
        "fixture_note": "",
        "safety_assumption": "",
        "output_dir": output_dir,
        "no_color": True,
        "include_output_change": False,
        "include_wiper_ramp": False,
        "include_tcon_toggle": False,
        "include_shdn": False,
        "include_wlat": False,
        "include_general_call": False,
        "confirm_isolated_bus": False,
        "operator_prompts": False,
    }
    values.update(overrides)
    return Namespace(**values)


class FakeCli:
    def __init__(self, outputs: dict[str, str | list[str]] | None = None) -> None:
        self.outputs = outputs or {}
        self.commands: list[str] = []

    def read_until_quiet(self, timeout: float | None = None, quiet_s: float = 0.35) -> str:
        del timeout, quiet_s
        return "boot ok\n> "

    def command(self, command: str, timeout: float | None = None) -> str:
        del timeout
        self.commands.append(command)
        if command in self.outputs:
            output = self.outputs[command]
            if isinstance(output, list):
                if len(output) > 1:
                    return output.pop(0)
                return output[0]
            return output
        if command == "state":
            return "state=READY initialized=1 online=1 uncertain=0 dirty=0\n> "
        if command in {"drv", "health"}:
            return "health state=READY initialized=1 online=1 uncertain=0 dirty=0\n> "
        if command == "version":
            return "MCP45HVX1 CLI version test\n> "
        if command == "help":
            return "scan\nprobe\nstress\n> "
        if command == "help wiper":
            return (
                "=== Help: wiper ===\nSafety: READ-ONLY QUERY; OUTPUT-CHANGING WHEN SET\n"
                "Syntax:\nwiper percent <0..100>\nExamples:\nwiper 0x7f\n> "
            )
        if command == "? health":
            return "=== Help: drv ===\nAliases: health\nSafety: SAFE / READ-ONLY\n> "
        if command == "help gc":
            return "=== Help: gc ===\nSafety: DANGEROUS / RAW OR BUS-WIDE\ngc arm\n> "
        if command in {"cfg", "settings"}:
            return f"{command}: OK\naddr=0x3C resolution=8\n> "
        if command in {"info", "errata", "read", "reg 0x00", "reg 0x04", "last", "recover"}:
            return f"{command}: OK\n> "
        if command == "invalid_command":
            return "Unknown command: invalid_command\n> "
        if command == "reg 0x01":
            return "reg: INVALID_PARAM\n> "
        if command == "addr 0x60":
            return "addr: INVALID_PARAM\n> "
        if command == "readwiper":
            return "readwiper: OK\nwiper=0x7F\n> "
        if command == "readtcon":
            return "readtcon: OK\ntcon=0xFF\n> "
        if command.startswith("selftest"):
            return "Selftest result: pass=8 fail=0 skip=0\n> "
        if command.startswith("stress"):
            return "stress ok=100 failures=0 Errors: 0\n> "
        return f"{command}: OK\n> "


class EmptyCli(FakeCli):
    def read_until_quiet(self, timeout: float | None = None, quiet_s: float = 0.35) -> str:
        del timeout, quiet_s
        return ""

    def command(self, command: str, timeout: float | None = None) -> str:
        del timeout
        self.commands.append(command)
        return ""


class RaisingSerial:
    def __init__(self) -> None:
        self.reads = [
            b"Guru Meditation Error: Core  0 panic'ed (LoadProhibited).\n",
            b"Backtrace: 0x40081234:0x3ffc0000\n> ",
        ]

    def write(self, data: bytes) -> int:
        del data
        raise TimeoutError("write stalled")

    def flush(self) -> None:
        return None

    def read(self, size: int) -> bytes:
        del size
        if self.reads:
            return self.reads.pop(0)
        return b""


class HilParserTests(unittest.TestCase):
    def test_command_failed_detects_common_failure_tokens(self) -> None:
        self.assertTrue(hil.command_failed("probe", "[FAIL] probe\n"))
        self.assertTrue(hil.command_failed("probe", "[E] timeout\n"))
        self.assertTrue(hil.command_failed("probe", "Status: I2C_TIMEOUT\n"))
        self.assertTrue(hil.command_failed("probe", ""))
        self.assertTrue(hil.command_failed("help", "Unknown command\n"))
        self.assertTrue(hil.command_failed("version", "[E] timeout\n"))
        self.assertFalse(hil.command_failed("help", "Usage: help\n> "))
        self.assertTrue(hil.command_failed("wiper 0x00", "Result: FAIL\n"))
        self.assertTrue(hil.command_failed("wiper 0x00", "Restore: FAIL\n"))
        self.assertTrue(hil.command_failed("probe", hil.OUTPUT_TRUNCATED_MARKER))
        self.assertTrue(hil.command_failed("settings", "settings: I2C_BUS\n"))
        self.assertTrue(hil.command_failed("health", "health: BUSY\n"))
        self.assertTrue(hil.command_failed("reg 0x01", "reg: INVALID_PARAM\n"))
        self.assertTrue(hil.command_failed("addr 0x60", "addr: INVALID_PARAM\n"))
        self.assertTrue(hil.command_failed("stress 100", "stress ok=99 failures=1\n"))
        self.assertFalse(hil.command_failed("settings", "settings: OK\n"))
        self.assertFalse(hil.command_failed("health", "health: OK\n"))

    def test_parser_self_test_passes(self) -> None:
        self.assertTrue(hil.run_parser_self_test())

    def test_safe_sequence_covers_common_minimum_commands(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp)
            run = hil.HilRun(args)
            cli = FakeCli()

            run.safe_sequence(cli)

        for command in (
            "version", "help wiper", "? health", "help gc", "scan", "probe",
            "settings", "health",
        ):
            self.assertIn(command, cli.commands)
        self.assertFalse(any(result.failed for result in run.commands))

    def test_empty_cli_output_does_not_pass_safe_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp)
            run = hil.HilRun(args)
            cli = EmptyCli()

            run.safe_sequence(cli)

        self.assertTrue(any(result.failed for result in run.commands))
        self.assertEqual(hil.FAIL, run.verdict())

    def test_serial_write_exception_is_reported_as_command_output(self) -> None:
        cli = object.__new__(hil.SerialCli)
        cli._serial = RaisingSerial()
        cli._timeout = 1.0
        cli._idle_timeout = 0.35

        output = cli.command("version")

        self.assertIn("[E] serial command failed", output)
        self.assertIn("TimeoutError", output)
        self.assertIn("Guru Meditation Error", output)
        self.assertIn("Backtrace:", output)

    def test_report_generation_does_not_false_pass_failed_settings(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp)
            run = hil.HilRun(args)
            cli = FakeCli({"settings": "settings: I2C_TIMEOUT\n> "})

            run.safe_sequence(cli)
            run.write_outputs(args)

            summary = json.loads((run.output_dir / "summary.json").read_text(encoding="utf-8"))
            report = (run.output_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(hil.FAIL, summary["verdict"])
        self.assertIn("command failed or produced error output: settings", summary["verdict_reasons"])
        self.assertIn("Verdict: `FAIL`", report)

    def test_dry_run_writes_unknown_plan_and_limitations(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp, port=None, dry_run=True)
            run = hil.HilRun(args)

            run.dry_run_sequence()
            run.write_outputs(args)

            summary = json.loads((run.output_dir / "summary.json").read_text(encoding="utf-8"))
            report = (run.output_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(hil.UNKNOWN, summary["verdict"])
        self.assertGreater(summary["result_counts"][hil.UNKNOWN], 0)
        self.assertGreater(summary["result_counts"][hil.NOT_RUN], 0)
        self.assertIn("Detailed Steps", report)

    def test_partial_soak_summary_records_failure_burst(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp, soak_duration_s=60.0, max_failure_burst=2)
            run = hil.HilRun(args)
            cli = EmptyCli()

            run.soak_sequence(cli)
            run.write_outputs(args)

            report = (run.output_dir / "report.md").read_text(encoding="utf-8")

        self.assertTrue(run.soak_summary["requested"])
        self.assertEqual(2, run.soak_summary["failures"])
        self.assertIn("2 consecutive failures", run.soak_summary["stopped_reason"])
        self.assertTrue(
            all(
                result.feature_area == "Safe-only soak"
                for result in run.commands
                if result.group in {"soak", "soak-final"}
            )
        )
        self.assertIn("Omitted `2` repeated benchmark/soak rows", report)
        self.assertNotIn("8-hour soak", report)
        self.assertEqual(hil.FAIL, run.verdict())

    def test_report_copy_is_opt_in(self) -> None:
        parser = hil.build_parser()
        args = parser.parse_args(["--dry-run"])

        self.assertIsNone(args.report_file)

    def test_output_change_requires_operator_prompts_for_confirmation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(
                tmp,
                include_output_change=True,
                include_wiper_ramp=True,
                include_tcon_toggle=True,
                operator_prompts=False,
            )

            warnings = hil.validate_args(args)

        self.assertFalse(args.include_output_change)
        self.assertFalse(args.include_wiper_ramp)
        self.assertFalse(args.include_tcon_toggle)
        self.assertTrue(any("--include-output-change requires --operator-prompts" in w for w in warnings))

    def test_timeout_argument_must_be_finite_and_positive(self) -> None:
        parser = hil.build_parser()
        for value in ("0", "-1", "nan", "inf"):
            with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
                parser.parse_args(["--port", "TEST", "--timeout", value])

    def test_serial_line_defaults_and_choices_are_bounded(self) -> None:
        parser = hil.build_parser()
        args = parser.parse_args(["--port", "TEST"])
        self.assertEqual("on", args.serial_dtr)
        self.assertEqual("off", args.serial_rts)
        args = parser.parse_args(["--port", "TEST", "--serial-dtr", "unchanged", "--serial-rts", "on"])
        self.assertEqual("unchanged", args.serial_dtr)
        self.assertEqual("on", args.serial_rts)
        with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
            parser.parse_args(["--port", "TEST", "--serial-dtr", "bad"])

    def test_general_call_requires_operator_prompts_for_confirmation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(
                tmp,
                include_general_call=True,
                confirm_isolated_bus=True,
                operator_prompts=False,
            )

            warnings = hil.validate_args(args)

        self.assertFalse(args.include_general_call)
        self.assertTrue(any("--include-general-call requires --operator-prompts" in w for w in warnings))

    def test_operator_can_decline_general_call_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(
                tmp,
                include_general_call=True,
                confirm_isolated_bus=True,
                operator_prompts=True,
            )
            run = hil.HilRun(args)
            cli = FakeCli()
            prompts: list[str] = []
            original_prompt_line = hil.prompt_line
            hil.prompt_line = lambda prompt: prompts.append(prompt) or "ISOLATED"
            try:
                run.general_call_sequence(cli)
            finally:
                hil.prompt_line = original_prompt_line

        self.assertFalse(cli.commands)
        self.assertTrue(any("safe load" in prompt for prompt in prompts))
        self.assertIn("General Call skipped: operator did not confirm isolated bus and safe load", run.skipped)
        self.assertEqual(hil.SKIPPED_UNSAFE, run.verdict())

    def test_operator_can_decline_output_changing_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp, include_output_change=True, operator_prompts=True)
            run = hil.HilRun(args)
            cli = FakeCli()
            prompts: list[str] = []
            original_prompt_line = hil.prompt_line
            hil.prompt_line = lambda prompt: prompts.append(prompt) or "NO"
            try:
                run.output_change_sequence(cli)
            finally:
                hil.prompt_line = original_prompt_line

        self.assertFalse(cli.commands)
        self.assertTrue(any("safe load" in prompt for prompt in prompts))
        self.assertIn("output-changing group skipped by operator", run.skipped)
        self.assertEqual(hil.SKIPPED_UNSAFE, run.verdict())

    def test_failed_baseline_command_blocks_output_change(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp, include_output_change=True, operator_prompts=True)
            run = hil.HilRun(args)
            cli = FakeCli({"readwiper": "readwiper: I2C_TIMEOUT\nwiper=0x7F\n> "})
            original_prompt_line = hil.prompt_line
            hil.prompt_line = lambda prompt: "YES"
            try:
                run.output_change_sequence(cli)
            finally:
                hil.prompt_line = original_prompt_line

        self.assertNotIn("wiper 0x00", cli.commands)
        self.assertTrue(any("baseline commands failed" in item for item in run.skipped))
        self.assertEqual(hil.SKIPPED_UNSAFE, run.verdict())

    def test_restore_records_final_state_after_output_change(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            args = make_args(tmp, include_output_change=True, operator_prompts=True)
            run = hil.HilRun(args)
            cli = FakeCli({
                "state": [
                    "state=READY initialized=1 online=1 uncertain=0 dirty=0 resolution=8\n> ",
                    "state=READY initialized=1 online=1 uncertain=1 dirty=0 resolution=8\n> ",
                ],
            })
            original_prompt_line = hil.prompt_line
            hil.prompt_line = lambda prompt: "YES"
            try:
                run.output_change_sequence(cli)
            finally:
                hil.prompt_line = original_prompt_line

        self.assertEqual("state", run.commands[-2].command)
        self.assertEqual("drv", run.commands[-1].command)
        self.assertEqual(hil.FAIL_RESTORE_UNCERTAIN, run.verdict())


if __name__ == "__main__":
    unittest.main()
