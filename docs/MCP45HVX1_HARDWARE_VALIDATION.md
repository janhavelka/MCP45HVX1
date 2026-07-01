# MCP45HVX1 Hardware Validation

This document defines the hardware evidence required before claiming production
readiness, high-voltage validation, analog accuracy, General Call safety, or
field deployment suitability.

Software tests are not a substitute for these checks. Record raw logs and
measurements for every claimed hardware result.

## Evidence Header

Complete this header for each validation run:

| Field | Value |
|---|---|
| Date/time | Pending |
| Operator | Pending |
| Repository commit | Pending |
| Firmware image / build ID | Pending |
| Board name and revision | Pending |
| DUT variant | MCP45HV31 / MCP45HV51 |
| RAB option | 5 k / 10 k / 50 k / 100 k |
| Package marking and date code | Pending |
| Datasheet revision | Pending |
| Errata revision reviewed | Pending |
| I2C bus topology | isolated / shared |
| SHDN wiring | tied / GPIO / external circuit |
| WLAT wiring | tied / GPIO / external circuit |
| Instruments | DMM, scope, logic analyzer, supply, load |
| Safe load used | Pending |
| HIL runner command line | Pending |
| HIL runner script | `tools/run_hil_mcp45hvx1.py` |
| Serial port and baud | Pending |
| HIL output directory | Pending |
| HIL `summary.json` verdict | Pending |
| HIL `report.md` path | Pending |
| Operator prompts enabled | yes / no |
| HIL groups enabled | safe-only / output-change / SHDN / WLAT / General Call |
| General Call isolation confirmation | Pending / not applicable |

## Recorded Safe-Only Evidence

The repository includes safe-only HIL evidence for the current v1.0.0
pre-production software package:

| Run | Evidence | Result |
|---|---|---|
| 8-hour ESP32-S2 COM8 soak | [`docs/reports/hil-validation-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-validation-COM8-20260629.md) | `PASS_SAFE_ONLY`; `183221 / 183221 / 0` soak commands; worst latency `0.188 s` |
| 1-hour ESP32-S2 COM8 panic repro | [`docs/reports/hil-panic-repro-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-panic-repro-COM8-20260629.md) | `PASS_SAFE_ONLY`; `23056 / 23056 / 0` soak commands |

These runs exercised safe/read-only CLI paths only. They do not complete the
output-changing, analog measurement, high-voltage, SHDN/WLAT, address strap,
rail-cycle, fault-injection, or General Call gates below.

## Safety Prerequisites

- Validate at low voltage before any high-voltage rail or final-load test.
- Use a current-limited supply and a safe load for output-changing tests.
- Verify P0A/P0W/P0B voltage and current limits externally; the driver cannot
  enforce analog rail or terminal-current safety.
- Capture the starting Wiper/TCON state before any output-changing test.
- Restore baseline after output-changing tests or record hardware uncertainty
  and the reason restoration could not be proven.
- Confirm VL, V+, V-, reset, SHDN, and WLAT are stable before calling
  `begin()`.
- Account for `TBORD` = 10 us typical / 20 us maximum after reset exit with
  `VL > VBOR`. The core does not provide rail sequencing or startup delays.

## Safe Read-Only Checks

These checks must not intentionally change analog output. The table is a
reusable per-fixture checklist; for the v1.0.0 COM8 evidence, see
[`docs/reports/hil-validation-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-validation-COM8-20260629.md).

| Check | Expected evidence | Result |
|---|---|---|
| HIL safe runner | `python tools/run_hil_mcp45hvx1.py --port <port> --baud 115200 --address <addr>` | Pending |
| `version`, `color off`, `help` | Firmware/library version and color-disabled transcript | Pending |
| `scan`, `addr`, optional address selection | Bus scan and active address recorded | Pending |
| `probe` | Raw Wiper read result and status; no health/cache mutation claim | Pending |
| `cfg`, `settings`, `state`, `drv`, `health` | Address, variant, cache-known flags, uncertainty, and last status visible | Pending |
| `readwiper`, `readtcon`, `dump` | Volatile Wiper/TCON and raw pointer readback captured | Pending |
| `selftest safe` | Read-only or state-restoring behavior documented by log | Pending |
| `stress 100` | Read-only stress summary with no output-changing frames | Pending |
| Final `state`, final `drv` | Final online/uncertainty/dirty state recorded | Pending |

## Address Strap Matrix

Record each populated A1:A0 combination:

| A1 | A0 | Expected standard | Alternate candidate | Observed 7-bit address | Pass/fail |
|---:|---:|---:|---:|---|---|
| 0 | 0 | `0x3C` | `0x5C` | Pending | Pending |
| 0 | 1 | `0x3D` | `0x5D` | Pending | Pending |
| 1 | 0 | `0x3E` | `0x5E` | Pending | Pending |
| 1 | 1 | `0x3F` | `0x5F` | Pending | Pending |

The driver defaults to `0x3C..0x3F`. Use `0x5C..0x5F` only with explicit
alternate-range opt-in and recorded hardware evidence.

## POR/BOR Rail Cycling

| Check | Expected evidence | Result |
|---|---|---|
| Power-on reset, MCP45HV31 | Wiper reads `0x3F`; TCON reads `0xFF` | Pending |
| Power-on reset, MCP45HV51 | Wiper reads `0x7F`; TCON reads `0xFF` | Pending |
| Brown-out / rail cycling | Readback returns documented volatile defaults after rails recover | Pending |
| Reset exit timing | Measurement or design note shows `TBORD` margin before `begin()` | Pending |
| SHDN/WLAT stable during startup | Pin states logged before first device transaction | Pending |

## I2C Fault And Recoverability Checks

Inject faults using safe loads and record raw logs:

| Fault | Expected behavior | Result |
|---|---|---|
| Address NACK during `begin()`/`probe()` | `DEVICE_NOT_FOUND`, detail preserved | Pending |
| Data NACK after write attempt | Original status preserved; affected cache unknown if mutation possible | Pending |
| Timeout after write attempt | Original status preserved; hardware uncertainty set | Pending |
| Bus error after write attempt | Original status preserved; hardware uncertainty set | Pending |
| Device unplug/replug | OFFLINE/DEGRADED behavior and `recover()` path logged | Pending |
| Bus reset callback OK | Does not claim READY until a tracked read or `recover()` succeeds | Pending |

## Output-Changing Checks

Run only on a safe load, then repeat on the final circuit only after low-voltage
evidence is accepted:

| Command/API | Evidence required | Result |
|---|---|---|
| `--include-output-change --operator-prompts` | Safe-load confirmation, baseline Wiper/TCON, command log, readback, measurements, restore | Pending |
| `--include-wiper-ramp` | Bounded ramp values, measurements for each point, restore | Pending |
| Wiper write / percent / fraction | Baseline, command log, readback, analog measurement, restore | Pending |
| INC/DEC | Baseline, step count, readback, analog measurement, restore | Pending |
| `--include-tcon-toggle` | TCON terminal mode toggle, readback, terminal measurement, restore | Pending |
| TCON write / terminal mode | Baseline, command log, readback, terminal measurement, restore | Pending |
| TCON software shutdown | Distinguish TCON software shutdown from hardware SHDN pin | Pending |
| Raw Wiper/TCON write | Explicit operator gate, readback, analog measurement, restore | Pending |
| Optional startup writes | `begin()` evidence showing readback first, write only when enabled, recoverable failure path | Pending |
| Restore failure | Runner verdict is `FAIL_RESTORE_UNCERTAIN`; CLI/API uncertainty and original error recorded | Pending |

## Analog Measurement Checks

| Measurement | Required evidence | Result |
|---|---|---|
| P0A/P0W/P0B voltage at endpoints | DMM/scope capture under safe load | Pending |
| P0W movement across codes | Measured low-voltage transfer or resistance behavior | Pending |
| Terminal current | Measured within datasheet limits for selected RAB and rails | Pending |
| Final-circuit loading | Board-level load, leakage, and rail interaction documented | Pending |
| High-voltage operation | Only after low-voltage evidence; required for any high-voltage claim | Pending |

Helper resistance math is idealized. It does not prove tolerance, wiper
resistance, leakage, INL/DNL, temperature behavior, or board-level loading.

## SHDN And WLAT Checks

These checks require explicit runner flags and operator prompts. Register
readback must be recorded separately from physical output observations.

| Check | Expected evidence | Result |
|---|---|---|
| `--include-shdn --operator-prompts` | SHDN wiring, asserted/released observations, readback logs | Pending |
| `--include-wlat --operator-prompts` | WLAT wiring, hold/release observations, readback logs | Pending |
| SHDN asserted low | Registers read back but physical terminals remain hardware-shutdown overridden | Pending |
| SHDN released high | Physical output follows register/TCON state when external circuitry permits | Pending |
| WLAT asserted | Wiper register changes but physical wiper output is held | Pending |
| WLAT released | Physical output updates from volatile Wiper register | Pending |
| External override | Readback-vs-physical-output caveat documented for board design | Pending |

## General Call Gate

Output-changing General Call commands require an isolated MCP45HVX1 bus unless
a documented shared-bus risk acceptance is approved.

The HIL runner must be invoked with `--include-general-call`,
`--confirm-isolated-bus`, and `--operator-prompts` before any General Call
command is sent. The operator must confirm the isolated bus and safe-load setup.
The errata warning must appear in `raw_serial.txt`, `summary.json`, and
`report.md`.

| Command | ACK/NACK | Readback verified | Analog measured | Isolation/risk decision |
|---|---|---|---|---|
| `gc wiper <code>` | Pending | Pending | Pending | Pending |
| `gc tcon <value>` | Pending | Pending | Pending | Pending |
| `gc inc` | Pending | Pending | Pending | Pending |
| `gc dec` | Pending | Pending | Pending | Pending |

Shared-bus deployments must not claim General Call safety without logged
evidence and signoff.

## Errata Review Gate

- Record package marking and date code.
- Review `DS80000649B` and any newer Microchip errata before release approval.
- Treat Rev A1 through `1518NNN`, unreadable markings, or unknown silicon as
  affected until proven otherwise.
- For affected or unknown silicon, either isolate MCP45HVX1 on its own I2C bus
  or document explicit shared-bus risk acceptance.

## Evidence Template

Use one entry per check:

```text
Check:
Date/time:
Operator:
Commit:
Fixture and instruments:
Rails and load:
Command/API:
Raw serial/log output:
HIL output directory:
summary.json verdict:
report.md path:
commands.txt path:
operator_notes.md path:
Logic analyzer capture:
Measurements:
Restore action:
Final readback:
Uncertainty state:
Verdict:
Evidence links:
Reviewer/signoff:
Notes/risk acceptance:
```

Allowed HIL verdicts are `PASS_SAFE_ONLY`, `PASS_WITH_OUTPUT_CHANGE`, `FAIL`,
`FAIL_RESTORE_UNCERTAIN`, `OPERATOR_REVIEW_REQUIRED`, and `SKIPPED_UNSAFE`.
