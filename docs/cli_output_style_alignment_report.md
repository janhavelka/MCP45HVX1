# MCP45HVX1 CLI Output Style Alignment Report

## Sibling Projects Inspected

- `BME280`: primary reference for compact health/detail views, stress progress, stress summary, first/last failure reporting, and grouped help.
- `MB85RC`: primary reference for write/read stress summaries, restore-aware diagnostics, raw/detail command separation, and PASS/FAIL self-test rows.
- `INA228`: reference for measurement-style status output, raw commands, self-test result rows, and `Status: NAME (code, detail)` formatting.
- `INA3221`: reference for multi-step stress progress, compact status rows, raw register commands, and grouped help.
- `PCA9555`: reference for progress cadence and final stress summaries with health deltas.
- `SSD1315`: reference for grouped help, explicit raw/buffer diagnostics, compact command output, and self-test report shape.
- `RV3032-C7`: reference for grouped help sections and register/status diagnostics.

## CLI Style Conventions Found

- Help uses `=== <Device> CLI Help ===`, green section headers, and cyan fixed-width command names.
- Normal command output is line-oriented with two-space indented key/value rows.
- Status lines use `Status: <ERR_NAME> (code=<n>, detail=<n>)`; failures add human-readable detail when useful.
- `PASS`, `FAIL`, `SKIP`, OK counters, failure counters, and health states are colorized, while surrounding prose remains plain.
- Default output is compact; raw/detail output is behind explicit commands.
- Stress tests print start intent, progress at fixed intervals, final totals, success rate, timing, health deltas, and last-error detail.
- Verbose mode may print per-attempt details; default stress output avoids low-level transaction spam.
- Repeated output uses fixed buffers, literals, and `printf`; no dynamic string growth is used inside stress loops.

## Files Changed

- `examples/01_basic_bringup_cli/main.cpp`
- `examples/common/HealthView.h`
- `examples/common/HealthDiag.h`
- `README.md`

## Commands Whose Output Changed

- `help`, `?`
- `begin`, `probe`, `recover`, `iface_reset`, `defaults`
- `read`, `rregs`, `dump`
- `raw` (new alias for `dump`)
- `last`, `rreg`, `wiper`, `frac`, `pos`, `term`, `shutdown`, `mode`, `rab`, `verbose`
- `cfg`, `settings`
- `detail` (new alias for `cfg`/`settings`)
- `drv`
- `info`, `errata`
- `selftest`
- `stress`, `stress_mix`
- General Call subcommands keep behavior but use the new shared status formatting.

## Before/After Examples

Before status:

```text
OK
ERR code=I2C_NACK_ADDR(11) detail=2 msg=Device not responding
```

After status:

```text
  Status: OK (code=0, detail=0)
  Status: I2C_NACK_ADDR (code=11, detail=2)
  Cause: bus/device not found or address was not acknowledged
  Message: Device not responding
```

Before read:

```text
wiper=0x7F pos=0.4980 r_bw=4980.4 ohm r_aw=5019.6 ohm
tcon=0xFF mode=pot shdn=no A=yes W=yes B=yes
```

After read:

```text
=== Register Snapshot ===
  Wiper: 0x7F (127)
  Position: 0.4980
  R_BW: 4980.4 ohm
  R_AW: 5019.6 ohm
  TCON: 0xFF
  Mode: pot
  Shutdown: no
  Terminals: A=yes W=yes B=yes
```

Before health:

```text
state=READY failures=0 totalFail=0 totalOk=3 lastErr=0
```

After health:

```text
Health: state=READY online=true consec=0 ok=3 fail=0 rate=100.0% lastErr=0
```

## Stress-Test Output Changes

- Added shared `StressStats` tracking for test name, mode, configured iterations, attempts, success/error counts, timing, first/last failure, abort state, restore status, and health deltas.
- `stress [n]` now reports `n` configured increment/decrement restore iterations and `2n` operations.
- `stress_mix [n]` now reports `n` configured mixed write/mode iterations and `2n` operations.
- Progress lines use the sibling style:

```text
  Progress: 8/16 (50%, ok=8, fail=0)
```

- Final summaries include target, mode, attempts, success, errors, success rate, duration, ops/s when measurable, min/avg/max attempt timing, health delta, restore result, PASS/FAIL result, first/last failure detail, restore failure detail, and final health.
- `verbose 1` enables per-attempt operation lines with operation name, OK/FAIL state, timing in microseconds, and error name on failure.

## Color And Formatting Behavior

- Reused existing `Log.h` ANSI color macros and `CliStyle.h` color helpers.
- Colored tokens are limited to severity/status/result/counter values.
- Section headers and help formatting stay aligned with sibling projects.
- Output remains plain-text readable if ANSI escape sequences are removed or color macros are compiled to empty strings.

## Raw And Detail Behavior

- Default `read`/`rregs` output is decoded and compact, not raw.
- `raw` is an explicit alias for `dump`, which includes the decoded register snapshot plus the current address pointer.
- `rreg` and `last` remain direct raw register/pointer inspection commands.
- `detail` is an explicit alias for `cfg`/`settings`, printing detailed health, I2C config, device, and cache sections.
- `drv` remains the compact one-line health view.

## Memory-Safety Notes

- Stress loops use stack/local scalars and static string literals only.
- No heap allocations or dynamic `String` growth were added inside repeated stress output paths.
- Output is produced with bounded `printf` calls and existing fixed token buffers.
- Core driver files under `include/` and `src/` were not changed; library transport behavior remains callback-driven and transport-agnostic.

## Validation Commands And Results

- `python tools/validate.py`: passed.
- `tools/check_cli_contract.py`: passed.
- `tools/check_core_timing_guard.py`: passed.
- `scripts/generate_version.py check`: passed.
- Native compile of `src/MCP45HVX1.cpp`: passed.
- Native compile of `examples/01_basic_bringup_cli/main.cpp`: passed.
- `pio test -e native`: passed, 39/39 tests.
- `pio run -e esp32s3dev`: passed.
- `pio run -e esp32s2dev`: passed.

PlatformIO printed an existing warning about multiple/obsolete PIO Core installs, but all validation commands completed successfully.

## Manual CLI Checks

Expected hardware-session checks:

- `help`: grouped sections, fixed-width commands, raw/detail aliases visible.
- `status` equivalent: `drv` one-line health and `detail` detailed health/config/cache sections.
- `scan`/`probe`: scan grid unchanged; probe uses `Status:` row and cause/message on error.
- `read`: decoded register snapshot with no raw pointer unless `raw`/`dump` is used.
- `stress 8`: progress lines, final summary, restore PASS/FAIL, final health line.
- `stress_mix 8`: same summary structure with mixed write/mode mode text.
- Error case, such as missing device: error name, cause, detail, and message are visible.
- `verbose 1` followed by `stress 3`: per-attempt operation/timing lines appear.

## Remaining Limitations Or TODOs

- Hardware output was not captured in this pass; validation covered compile/tests and expected manual CLI checks are documented above.
- The CLI does not implement retry logic, so summaries report attempts and failures but no retry count.
- ANSI color is controlled by the existing shared macros rather than a runtime color toggle.
