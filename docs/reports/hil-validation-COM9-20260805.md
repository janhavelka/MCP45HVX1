# MCP45HVX1 HIL Validation Report - COM9

This curated report preserves the generated run metadata, command results,
timing, and scope limits. The original ignored `hil_logs` staging bundle was
removed during release cleanup on 2026-08-05; references to its raw files below
are provenance notes, not repository file links.

Date/time UTC: `2026-08-05T13:29:20+00:00`
Date/time local: `2026-08-05T15:29:20+02:00`
Verdict: `PASS_SAFE_ONLY`
Repo: `main` at `90f4b5d` dirty=False
Runner: `tools\run_hil_mcp45hvx1.py --port COM9 --baud 115200 --serial-dtr on --serial-rts off --timeout-s 5 --idle-timeout-s 0.35 --boot-settle-s 1 --command-pacing-s 0.05 --stress-count 10 --stress-timeout-s 10 --benchmark-samples 0 --soak-duration-s 0 --max-failure-burst 1 --board esp32s2 --environment esp32s2dev --fixture-note Targeted safe-only COM9 HIL after freshly flashed main@90f4b5d; I2C defaults GPIO8 SDA/GPIO9 SCL, 400 kHz --safety-assumption Read-only CLI paths only; no output-changing, General Call, SHDN, WLAT, rail-cycle, or fault-injection checks --no-color`
Port: `COM9`
Baud: `115200`
Address: `None`
Board/environment: `esp32s2` / `esp32s2dev`
Output directory: `hil_logs\mcp45hvx1_20260805_152913_824232`

## Hardware Setup

- Fixture notes: Targeted safe-only COM9 HIL after freshly flashed main@90f4b5d; I2C defaults GPIO8 SDA/GPIO9 SCL, 400 kHz
- Safety assumptions: Read-only CLI paths only; no output-changing, General Call, SHDN, WLAT, rail-cycle, or fault-injection checks
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

## Original Local Bundle Files

- `raw_serial.txt` - raw command transcript after optional ANSI stripping.
- `commands.txt` - command list with group, timing, and failure flags.
- `summary.json` - machine-readable summary and verdict.
- `operator_notes.md` - measurements, observations, skipped unsafe checks.
- `report.md` - this report.

These files were generated in the local directory recorded above. They were
not committed; this curated report is the retained repository evidence.

## Commands Run

- HIL command: `tools\run_hil_mcp45hvx1.py --port COM9 --baud 115200 --serial-dtr on --serial-rts off --timeout-s 5 --idle-timeout-s 0.35 --boot-settle-s 1 --command-pacing-s 0.05 --stress-count 10 --stress-timeout-s 10 --benchmark-samples 0 --soak-duration-s 0 --max-failure-burst 1 --board esp32s2 --environment esp32s2dev --fixture-note Targeted safe-only COM9 HIL after freshly flashed main@90f4b5d; I2C defaults GPIO8 SDA/GPIO9 SCL, 400 kHz --safety-assumption Read-only CLI paths only; no output-changing, General Call, SHDN, WLAT, rail-cycle, or fault-injection checks --no-color`
- Build, upload, and repository validation commands are recorded in the parent validation notes/final response.

## Summary

| Result | Count |
|---|---:|
| PASS | 31 |
| FAIL | 0 |
| UNKNOWN | 0 |
| NOT RUN | 8 |
| NOT APPLICABLE | 3 |

## Group Summary

| Group | Sent | Failed | Output-changing |
|---|---:|---:|---:|
| invalid-input | 3 | 0 | 0 |
| not-applicable | 3 | 0 | 0 |
| not-run | 8 | 0 | 0 |
| safe | 26 | 0 | 0 |
| safe-final | 2 | 0 | 0 |

## Baseline

- Wiper: `unknown`
- TCON: `unknown`

## Detailed Steps

| Test ID | Area | Command/Step | Expected | Observed | Elapsed s | Result | Notes |
|---|---|---|---|---|---:|---|---|
| SAFE-001 | Connectivity | `version` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-002 | CLI | `color off` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-003 | CLI | `help` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-004 | CLI detailed help | `help wiper` | No CLI error tokens | see raw_serial.txt | 0.157 | PASS | - |
| SAFE-005 | CLI detailed-help alias | `? health` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-006 | CLI detailed-help safety | `help gc` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-007 | I2C discovery | `scan` | No CLI error tokens | see raw_serial.txt | 0.266 | PASS | - |
| SAFE-008 | Configuration | `addr` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-009 | Connectivity | `probe` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-010 | Configuration | `cfg` | No CLI error tokens | see raw_serial.txt | 0.157 | PASS | - |
| SAFE-011 | Configuration | `settings` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-012 | Lifecycle/health | `state` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-013 | Lifecycle/health | `drv` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-014 | Lifecycle/health | `health` | No CLI error tokens | see raw_serial.txt | 0.157 | PASS | - |
| SAFE-015 | Identity/configuration | `info` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-016 | Diagnostics | `errata` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-017 | Register read | `readwiper` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-018 | Register read | `readtcon` | No CLI error tokens | see raw_serial.txt | 0.157 | PASS | - |
| SAFE-019 | Aggregate read | `read` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-020 | Raw register read | `reg 0x00` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-021 | Raw register read | `reg 0x04` | No CLI error tokens | see raw_serial.txt | 0.172 | PASS | - |
| SAFE-022 | Raw register read | `last` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-023 | Aggregate/raw read | `dump` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-024 | Self-test | `selftest safe` | No CLI error tokens | see raw_serial.txt | 0.157 | PASS | - |
| SAFE-025 | Read-only stress | `stress 10` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| SAFE-026 | Recovery | `recover` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| INVALID-INPUT-027 | Invalid input | `invalid_command` | Unknown command must be visible | see raw_serial.txt | 0.156 | PASS | Expected CLI-visible invalid-command rejection; expected visible CLI rejection |
| INVALID-INPUT-028 | Invalid input | `reg 0x01` | Reserved register rejected | see raw_serial.txt | 0.157 | PASS | expected visible CLI rejection |
| INVALID-INPUT-029 | Invalid input | `addr 0x60` | Invalid address rejected | see raw_serial.txt | 0.156 | PASS | expected visible CLI rejection |
| SAFE-FINAL-030 | Lifecycle/health | `state` | No CLI error tokens | see raw_serial.txt | 0.172 | PASS | - |
| SAFE-FINAL-031 | Lifecycle/health | `drv` | No CLI error tokens | see raw_serial.txt | 0.156 | PASS | - |
| NOT-RUN-032 | Output-changing | `output-changing matrix` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires explicit safe load and --include-output-change |
| NOT-RUN-033 | General Call | `General Call matrix` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires isolated bus confirmation and operator prompts |
| NOT-RUN-034 | SHDN | `SHDN physical behavior` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires controlled SHDN wiring and external physical observation |
| NOT-RUN-035 | WLAT | `WLAT physical behavior` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires controlled WLAT wiring and external physical observation |
| NOT-RUN-036 | Fault injection | `I2C fault injection` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | no safe fault-injection fixture was declared |
| NOT-RUN-037 | Address straps | `address strap matrix` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires rewiring A1/A0 strap combinations |
| NOT-RUN-038 | Reset/persistence | `POR/BOR rail cycling` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | requires controlled rail cycling fixture |
| NOT-RUN-039 | Staged jobs | `poll-job HIL` | Fixture and operator preconditions available | NOT_RUN | 0.000 | NOT_RUN | current CLI does not expose start*/pollJob job API |
| NOT-APPLICABLE-040 | Identity | `unique silicon identity register` | Fixture and operator preconditions available | NOT_APPLICABLE | 0.000 | NOT_APPLICABLE | MCP45HVX1 exposes no unique chip ID in this driver |
| NOT-APPLICABLE-041 | Modes/timing | `ADC conversion modes` | Fixture and operator preconditions available | NOT_APPLICABLE | 0.000 | NOT_APPLICABLE | digital potentiometer has no ADC conversion modes |
| NOT-APPLICABLE-042 | Diagnostics | `thresholds/alerts/FIFO/accumulators` | Fixture and operator preconditions available | NOT_APPLICABLE | 0.000 | NOT_APPLICABLE | not exposed by MCP45HVX1 volatile register model |

## Sampling And Timing

- No benchmark mode was requested.

## Soak Summary

- Soak mode was not requested in this run.

## Failures And Anomalies

- safe sequence completed without detected command failures

## Limitations And Not Run

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

- Safe-only HIL evidence: `True`.
- Output-changing HIL evidence: `False`.
- General Call isolated-bus command evidence: `False`.
- General Call analog/safety evidence: `false` unless operator notes attach external measurements and release-checklist signoff.
- High-voltage evidence: `false` unless operator notes attach external measurements.
