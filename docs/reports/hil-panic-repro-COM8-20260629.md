# MCP45HVX1 1-Hour Panic-Repro HIL Evidence Summary - COM8

This compact record preserves the result and timing data from the original
generated report. On 2026-08-05, the 23,056 repetitive per-command Markdown
rows were removed from the repository. Machine-readable and raw serial files
belonged to the original local `hil_logs` bundle and were never tracked here;
this document must not be represented as a full transcript.

## Run Metadata

| Field | Value |
|---|---|
| End UTC | `2026-06-29T17:28:31+00:00` |
| End local | `2026-06-29T19:28:31+02:00` |
| Verdict | `PASS_SAFE_ONLY` |
| Port / baud | `COM8` / `115200` |
| Board / environment | `esp32s2` / `esp32s2dev` |
| Flashed commit (fixture note) | `4de67ab` |
| Runner repository state | `hardening/mcp45hvx1-industry-readiness` at `4de67ab`, `dirty=True` |
| Local evidence directory | `hil_logs\mcp45hvx1_20260629_182824_222589` |

The run exercised a safe/read-only command mix intended to reproduce a prior
panic. No panic or detected command failure occurred during the requested hour.

## Invocation

```text
tools\run_hil_mcp45hvx1.py --port COM8 --baud 115200 --serial-dtr on --serial-rts off --timeout-s 5 --idle-timeout-s 0.35 --boot-settle-s 2 --command-pacing-s 0.05 --stress-count 100 --stress-timeout-s 25 --benchmark-samples 0 --soak-duration-s 3600 --max-failure-burst 1 --board esp32s2 --environment esp32s2dev --fixture-note "Panic repro on freshly flashed 4de67ab firmware; safe/read-only command mix only" --safety-assumption "Safe/read-only CLI paths only; output-changing, General Call, SHDN, WLAT, rail-cycle, and fault-injection tests remain not run" --report-file docs\reports\hil-panic-repro-COM8-20260629.md --no-color
```

## Results

| Group | Passed | Failed |
|---|---:|---:|
| Safe sequence | 23 | 0 |
| Invalid-input checks | 3 | 0 |
| Soak commands | 23,056 | 0 |
| Final state/health | 4 | 0 |
| Total executed checks | 23,086 | 0 |

Eight hardware-dependent groups were not run and three device-inapplicable
checks were recorded as not applicable. Benchmark mode was not requested.

## Soak Summary

- Start UTC: `2026-06-29T16:28:31+00:00`
- End UTC: `2026-06-29T17:28:31+00:00`
- Requested / actual duration: `3600.0 s` / `3600.032 s`
- Attempts / success / failures: `23056` / `23056` / `0`
- Worst latency: `0.187 s`
- Effective rate: `6.404 Hz`
- Completed requested duration: `True`
- Command distribution: `2,882` attempts each for `readwiper`, `readtcon`,
  `dump`, `state`, `drv`, `probe`, `recover`, and `stress 5`

## Scope And Limits

This supports safe-only CLI stability for the recorded ESP32-S2 fixture and
run duration. It does not prove the absence of every panic or fault outside the
tested command mix and fixture.

It does not validate output-changing behavior, analog movement or accuracy,
terminal current, high-voltage behavior, SHDN/WLAT physical overrides,
address-strap combinations, POR/BOR rail cycling, injected I2C faults, or
General Call safety.
