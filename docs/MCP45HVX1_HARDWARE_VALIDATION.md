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

Safe-only HIL runs recorded against the v1.0.0 and v1.1.0 software packages, all
on an ESP32-S2 fixture driving an MCP45HV51 at `0x3C`:

| Date | Run | Result |
|---|---|---|
| 2026-06-30 | 8-hour soak | `PASS_SAFE_ONLY`; 183,221 soak commands, 0 failures; worst latency `0.188 s` |
| 2026-06-29 | 1-hour soak | `PASS_SAFE_ONLY`; 23,056 soak commands, 0 failures |
| 2026-08-05 | targeted run | `PASS_SAFE_ONLY`; 31 executed checks, 0 failures, final state READY |

These runs exercised safe/read-only CLI paths only. They do not complete the
output-changing, analog measurement, high-voltage, SHDN/WLAT, address strap,
rail-cycle, fault-injection, or General Call gates below.

Generated `hil_logs/` bundles are local, ignored artifacts. Attach the bundle
for a run to the release record rather than committing it; a per-session
transcript is provenance for one fixture, not library documentation.

## Running The HIL Runner

`tools/run_hil_mcp45hvx1.py` drives the Arduino or ESP-IDF CLI over serial and
writes an attachable evidence bundle. It is repository tooling and is excluded
from package exports; use a full checkout to capture evidence.

```bash
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C     --include-output-change --operator-prompts
python tools/run_hil_mcp45hvx1.py --port COM15 --baud 115200 --address 0x3C     --include-general-call --confirm-isolated-bus --operator-prompts
```

The default sequence is safe/read-only and matches the checklist below. Every
output-changing group requires an explicit flag plus `--operator-prompts`, so
the operator confirms a safe load and measurement setup first:

- `--include-output-change` captures the Wiper/TCON baseline, writes bounded
  values, reads back, restores the baseline, and reports
  `FAIL_RESTORE_UNCERTAIN` if the restore cannot be verified.
- `--include-shdn` and `--include-wlat` require operator prompts, because
  physical pin behavior must be observed independently of register readback.
- `--include-general-call` additionally requires `--confirm-isolated-bus`. The
  runner records the DS80000649B isolation warning in the evidence.

Each run writes `hil_logs/mcp45hvx1_<timestamp>/` containing `raw_serial.txt`,
`commands.txt`, `summary.json`, `report.md`, and `operator_notes.md`. That
directory is git-ignored staging: archive a required bundle with the release
record before clearing it. `--report-file <path>` additionally writes a report
copy to a chosen location.

Hardware validation is not claimed unless the runner was actually executed and
the resulting bundle is attached to a release or validation record.

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
reusable per-fixture checklist; completed run summaries are indexed above.

| Check | Expected evidence | Result |
|---|---|---|
| HIL safe runner | `python tools/run_hil_mcp45hvx1.py --port <port> --baud 115200 --address <addr>` | Pending |
| `version`, `color off`, `help` | Firmware/library version and color-disabled transcript | Pending |
| `scan`, `addr`, optional address selection | Bus scan and active address recorded | Pending |
| `probe` | Raw Wiper read result and status; no health/cache mutation claim | Pending |
| `help wiper`, `? health`, `help gc` | Detailed help shows aliases, safety class, syntax, examples | Pending |
| `cfg`, `settings`, `state`, `drv`, `health` | Address, variant, cache-known flags, uncertainty, and last status visible | Pending |
| `info`, `errata` | Device info and DS80000649B errata summary recorded | Pending |
| `readwiper`, `readtcon`, `read`, `dump` | Volatile Wiper/TCON and aggregate readback captured | Pending |
| `reg 0x00`, `reg 0x04`, `last` | Raw register and last-address reads captured | Pending |
| `selftest safe` | Read-only or state-restoring behavior documented by log | Pending |
| `stress 100` | Read-only stress summary with no output-changing frames | Pending |
| `recover` | Tracked Wiper/TCON re-read returns the driver to READY | Pending |
| `invalid_command`, `reg 0x01`, `addr 0x60` | Invalid input visibly rejected | Pending |
| Final `state`, final `drv` | Final online/uncertainty/dirty state recorded | Pending |

## Address Strap Matrix

Record each populated A1:A0 combination:

| A1 | A0 | Expected 7-bit address | Observed 7-bit address | Pass/fail |
|---:|---:|---:|---|---|
| 0 | 0 | `0x3C` | Pending | Pending |
| 0 | 1 | `0x3D` | Pending | Pending |
| 1 | 0 | `0x3E` | Pending | Pending |
| 1 | 1 | `0x3F` | Pending | Pending |

`0x3C..0x3F` is the only documented range for this part (DS20005304B §6.2.4,
Table 6-2). A device answering at `0x5C..0x5F` is not an MCP45HVX1 — see the
addressing section of [`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md). The
`Config::allowAlternateAddressRange` opt-in is retained for compatibility only
and is scheduled for removal; do not record evidence against it.

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
