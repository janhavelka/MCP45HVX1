# MCP45HVX1 8-Hour HIL Evidence Summary - COM8

This compact record preserves the result and timing data from the original
generated report. On 2026-08-05, the 183,221 repetitive per-command Markdown
rows were removed from the repository. Machine-readable and raw serial files
belonged to the original local `hil_logs` bundle and were never tracked here;
this document must not be represented as a full transcript.

## Run Metadata

| Field | Value |
|---|---|
| End UTC | `2026-06-30T01:29:26+00:00` |
| End local | `2026-06-30T03:29:26+02:00` |
| Verdict | `PASS_SAFE_ONLY` |
| Port / baud | `COM8` / `115200` |
| Board / environment | `esp32s2` / `esp32s2dev` |
| Flashed commit (fixture note) | `4de67ab` |
| Runner repository state | `hardening/mcp45hvx1-industry-readiness` at `4de67ab`, `dirty=True` |
| Local evidence directory | `hil_logs\mcp45hvx1_20260629_192903_359179` |

The runner's dirty flag came from an uncommitted serial-drain evidence patch,
not from the flashed firmware. That patch was later committed in `b2ae78f`.

## Invocation

```text
tools\run_hil_mcp45hvx1.py --port COM8 --baud 115200 --serial-dtr on --serial-rts off --timeout-s 5 --idle-timeout-s 0.35 --boot-settle-s 2 --command-pacing-s 0.05 --stress-count 100 --stress-timeout-s 25 --benchmark-samples 20 --soak-duration-s 28800 --max-failure-burst 1 --board esp32s2 --environment esp32s2dev --fixture-note "Full 8-hour safe-only HIL on freshly flashed 4de67ab firmware; no external analog measurement or output safe-load confirmation provided" --safety-assumption "Safe/read-only CLI paths only; output-changing, General Call, SHDN, WLAT, rail-cycle, and fault-injection tests remain not run" --report-file docs\reports\hil-validation-COM8-20260629.md --no-color
```

## Results

| Group | Passed | Failed |
|---|---:|---:|
| Safe sequence | 23 | 0 |
| Invalid-input checks | 3 | 0 |
| Benchmarks | 100 | 0 |
| Soak commands | 183,221 | 0 |
| Final state/health | 4 | 0 |
| Total executed checks | 183,351 | 0 |

Eight hardware-dependent groups were not run and three device-inapplicable
checks were recorded as not applicable.

## Benchmark Summary

| Path | Attempts | Success | Failures | Min s | Mean s | Max s | Effective Hz |
|---|---:|---:|---:|---:|---:|---:|---:|
| `readwiper` | 20 | 20 | 0 | 0.156 | 0.157 | 0.172 | 6.367 |
| `readtcon` | 20 | 20 | 0 | 0.156 | 0.158 | 0.172 | 6.337 |
| `dump` | 20 | 20 | 0 | 0.141 | 0.157 | 0.172 | 6.367 |
| `read` | 20 | 20 | 0 | 0.156 | 0.157 | 0.172 | 6.369 |
| `probe` | 20 | 20 | 0 | 0.141 | 0.156 | 0.171 | 6.400 |

## Soak Summary

- Start UTC: `2026-06-29T17:29:26+00:00`
- End UTC: `2026-06-30T01:29:25+00:00`
- Requested / actual duration: `28800.0 s` / `28800.047 s`
- Attempts / success / failures: `183221` / `183221` / `0`
- Worst latency: `0.188 s`
- Effective rate: `6.362 Hz`
- Completed requested duration: `True`
- Commands: `readwiper`, `readtcon`, `dump`, `state`, `drv`, `probe`,
  `recover`, and `stress 5`

## Scope And Limits

The safe/read-only sequence, read benchmarks, soak command mix, recovery
readback, final state, and final health completed without a detected command
failure. This supports safe-only CLI behavior on the recorded fixture.

It does not validate output-changing behavior, analog movement or accuracy,
terminal current, high-voltage behavior, SHDN/WLAT physical overrides,
address-strap combinations, POR/BOR rail cycling, injected I2C faults, or
General Call safety.
