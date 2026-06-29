# MCP45HVX1 HIL Validation Report - COM8

Date/time UTC: `2026-06-29T14:36:52+00:00`
Date/time local: `2026-06-29T16:36:52+02:00`
Verdict: `UNKNOWN`
Repo: `hardening/mcp45hvx1-industry-readiness` at `49b18be` dirty=True
Runner: `tools\run_hil_mcp45hvx1.py --dry-run --port COM8 --baud 115200 --board esp32s2 --environment esp32s2dev --fixture-note COM8 remained in ESP32-S2 bootloader mode (VID:PID=303A:0002) after upload; app CLI not reachable without physical reset --safety-assumption No output-changing HIL run; hardware HIL not claimed --no-color`
Port: `COM8`
Baud: `115200`
Address: `None`
Board/environment: `esp32s2` / `esp32s2dev`
Output directory: `hil_logs\mcp45hvx1_20260629_163652_376328`

## Hardware Setup

- Fixture notes: COM8 remained in ESP32-S2 bootloader mode (VID:PID=303A:0002) after upload; app CLI not reachable without physical reset
- Safety assumptions: No output-changing HIL run; hardware HIL not claimed
- Detected identity/address comes from CLI scan/probe/config output; MCP45HVX1 has no unique ID register exposed by this driver.
- Analog, high-voltage, SHDN, WLAT, and General Call safety require external evidence and are not inferred from register readback.

## Scope

- Safe/read-only sequence always runs first.
- Output-changing group requested: `False`.
- General Call requested: `False`.
- SHDN check requested: `False`.
- WLAT check requested: `False`.
- Benchmark samples per path: `0`.
- Soak duration requested: `0.0` seconds.

## Evidence Files

- `raw_serial.txt` - raw command transcript after optional ANSI stripping.
- `commands.txt` - command list with group, timing, and failure flags.
- `summary.json` - machine-readable summary and verdict.
- `operator_notes.md` - measurements, observations, skipped unsafe checks.
- `report.md` - this report.

## Commands Run

- HIL command: `tools\run_hil_mcp45hvx1.py --dry-run --port COM8 --baud 115200 --board esp32s2 --environment esp32s2dev --fixture-note COM8 remained in ESP32-S2 bootloader mode (VID:PID=303A:0002) after upload; app CLI not reachable without physical reset --safety-assumption No output-changing HIL run; hardware HIL not claimed --no-color`
- Build, upload, and repository validation commands are recorded in the parent validation notes/final response.

## Summary

| Result | Count |
|---|---:|
| PASS | 0 |
| FAIL | 0 |
| UNKNOWN | 25 |
| NOT RUN | 8 |
| NOT APPLICABLE | 3 |

## Group Summary

| Group | Sent | Failed | Output-changing |
|---|---:|---:|---:|
| dry-run-plan | 25 | 0 | 0 |
| not-applicable | 3 | 0 | 0 |
| not-run | 8 | 0 | 0 |

## Baseline

- Wiper: `unknown`
- TCON: `unknown`

## Detailed Steps

| Test ID | Area | Command/Step | Expected | Observed | Elapsed s | Result | Notes |
|---|---|---|---|---|---:|---|---|
| DRY-RUN-PLAN-001 | Dry run | `version` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-002 | Dry run | `color off` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-003 | Dry run | `help` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-004 | Dry run | `scan` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-005 | Dry run | `addr` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-006 | Dry run | `probe` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-007 | Dry run | `cfg` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-008 | Dry run | `settings` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-009 | Dry run | `state` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-010 | Dry run | `drv` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-011 | Dry run | `health` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-012 | Dry run | `info` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-013 | Dry run | `errata` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-014 | Dry run | `readwiper` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-015 | Dry run | `readtcon` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-016 | Dry run | `read` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-017 | Dry run | `reg 0x00` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-018 | Dry run | `reg 0x04` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-019 | Dry run | `last` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-020 | Dry run | `dump` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-021 | Dry run | `selftest safe` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-022 | Dry run | `stress 100` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-023 | Dry run | `recover` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-024 | Dry run | `state` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| DRY-RUN-PLAN-025 | Dry run | `drv` | Fixture and operator preconditions available | UNKNOWN | 0.000 | UNKNOWN | dry-run plan only |
| NOT-RUN-026 | Output-changing | `output-changing matrix` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires explicit safe load and --include-output-change |
| NOT-RUN-027 | General Call | `General Call matrix` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires isolated bus confirmation and operator prompts |
| NOT-RUN-028 | SHDN | `SHDN physical behavior` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires controlled SHDN wiring and external physical observation |
| NOT-RUN-029 | WLAT | `WLAT physical behavior` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires controlled WLAT wiring and external physical observation |
| NOT-RUN-030 | Fault injection | `I2C fault injection` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | no safe fault-injection fixture was declared |
| NOT-RUN-031 | Address straps | `address strap matrix` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires rewiring A1/A0 strap combinations |
| NOT-RUN-032 | Reset/persistence | `POR/BOR rail cycling` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires controlled rail cycling fixture |
| NOT-RUN-033 | Staged jobs | `poll-job HIL` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | current CLI does not expose start*/pollJob job API |
| NOT-APPLICABLE-034 | Identity | `unique silicon identity register` | Fixture and operator preconditions available | NOT_APPLICABLE | 0.000 | NOT_APPLICABLE | MCP45HVX1 exposes no unique chip ID in this driver |
| NOT-APPLICABLE-035 | Modes/timing | `ADC conversion modes` | Fixture and operator preconditions available | NOT_APPLICABLE | 0.000 | NOT_APPLICABLE | digital potentiometer has no ADC conversion modes |
| NOT-APPLICABLE-036 | Diagnostics | `thresholds/alerts/FIFO/accumulators` | Fixture and operator preconditions available | NOT_APPLICABLE | 0.000 | NOT_APPLICABLE | not exposed by MCP45HVX1 volatile register model |

## Sampling And Timing

- No benchmark mode was requested.

## Soak Summary

- Soak mode was not requested in this run.

## Failures And Anomalies

- safe sequence completed without detected command failures

## Limitations And Not Run

- version: dry-run plan only
- color off: dry-run plan only
- help: dry-run plan only
- scan: dry-run plan only
- addr: dry-run plan only
- probe: dry-run plan only
- cfg: dry-run plan only
- settings: dry-run plan only
- state: dry-run plan only
- drv: dry-run plan only
- health: dry-run plan only
- info: dry-run plan only
- errata: dry-run plan only
- readwiper: dry-run plan only
- readtcon: dry-run plan only
- read: dry-run plan only
- reg 0x00: dry-run plan only
- reg 0x04: dry-run plan only
- last: dry-run plan only
- dump: dry-run plan only
- selftest safe: dry-run plan only
- stress 100: dry-run plan only
- recover: dry-run plan only
- state: dry-run plan only
- drv: dry-run plan only
- output-changing matrix: requires explicit safe load and --include-output-change
- General Call matrix: requires isolated bus confirmation and operator prompts
- SHDN physical behavior: requires controlled SHDN wiring and external physical observation
- WLAT physical behavior: requires controlled WLAT wiring and external physical observation
- I2C fault injection: no safe fault-injection fixture was declared
- address strap matrix: requires rewiring A1/A0 strap combinations
- POR/BOR rail cycling: requires controlled rail cycling fixture
- poll-job HIL: current CLI does not expose start*/pollJob job API
- unique silicon identity register: MCP45HVX1 exposes no unique chip ID in this driver
- ADC conversion modes: digital potentiometer has no ADC conversion modes
- thresholds/alerts/FIFO/accumulators: not exposed by MCP45HVX1 volatile register model

## Proposed Fixes And Audit Notes

- Review any FAIL rows against `raw_serial.txt` before making hardware claims.
- Staged poll-job HIL remains a CLI coverage gap unless a read-only job command is added.
- Physical analog movement, terminal current, high-voltage behavior, SHDN/WLAT overrides, and General Call safety require external measurements.

## Fixes Implemented During Run

- Recorded separately in git diff and final response; this runner report captures HIL evidence only.

## Final Verification

- Final state and health commands are included in the detailed steps when the serial session reached them.
- Repository build/test commands and results should be recorded in the parent validation notes or final response.

## Claims Supported

- Safe-only HIL evidence: `False`.
- Output-changing HIL evidence: `False`.
- General Call isolated-bus command evidence: `False`.
- General Call analog/safety evidence: `false` unless operator notes attach external measurements and release-checklist signoff.
- High-voltage evidence: `false` unless operator notes attach external measurements.
