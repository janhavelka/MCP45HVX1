# MCP45HVX1 HIL Tooling Report

Branch: `hardening/mcp45hvx1-industry-readiness`

Prompt: `08_hil_tooling_and_hardware_evidence`

## Tool Features

- Added `tools/run_hil_mcp45hvx1.py`.
- Uses the existing Arduino or ESP-IDF serial CLI; no firmware behavior changes
  are required.
- Opens serial with DTR/RTS inactive to match repository monitor defaults.
- Sends LF-terminated commands and reads until the CLI prompt or timeout.
- Disables CLI color with `color off` and can strip ANSI from captured output
  with `--no-color`.
- Writes attachable evidence under `hil_logs/mcp45hvx1_<timestamp>/`.
- Does not claim hardware validation by itself; the generated evidence bundle
  must be attached and reviewed.

## Safe Default Group

The default sequence is safe/read-only:

```text
version
color off
help
scan
addr
addr <address> or addr_alt <address>, if --address is provided
probe
cfg
state
drv
readwiper
readtcon
dump
selftest safe
stress 100
state
drv
```

The runner uses `dump`, not `raw write`, for the read-only raw/register view.

## Opt-In Groups

- `--include-output-change`
  - Captures Wiper/TCON baseline.
  - Writes bounded Wiper values.
  - Reads back each step.
  - Restores baseline Wiper/TCON.
  - Reports `FAIL_RESTORE_UNCERTAIN` if restore cannot be verified.
- `--include-wiper-ramp`
  - Expands output-changing Wiper coverage to a multi-point bounded ramp.
- `--include-tcon-toggle`
  - Adds TCON terminal-mode toggle coverage to the output-changing group.
- `--include-shdn` / `--include-wlat`
  - Require `--operator-prompts`.
  - Record pin wiring and physical observations separately from register
    readback.
- `--include-general-call`
  - Requires `--confirm-isolated-bus`.
  - Prints the errata and isolated-bus warning into the evidence.
  - Captures baseline and restores after the General Call step.

## Evidence Artifacts

Each HIL run creates:

- `raw_serial.txt` - timestamped TX/RX transcript and runner events.
- `commands.txt` - parseable command ledger.
- `summary.json` - machine-readable summary with verdict, repo state, runner
  options, command results, baseline/final state, restore status, skipped
  unsafe groups, and artifact paths.
- `report.md` - human-readable report.
- `operator_notes.md` - operator measurements, pin observations, risk
  acceptances, skipped groups, and signoff notes.

Verdicts are `PASS_SAFE_ONLY`, `PASS_WITH_OUTPUT_CHANGE`, `FAIL`,
`FAIL_RESTORE_UNCERTAIN`, `OPERATOR_REVIEW_REQUIRED`, and `SKIPPED_UNSAFE`.

## Documentation Updated

- README now documents HIL usage, safe/default behavior, opt-in examples, and
  evidence artifacts.
- `docs/MCP45HVX1_HARDWARE_VALIDATION.md` now includes HIL runner fields,
  runner command groups, General Call gates, SHDN/WLAT prompt requirements, and
  HIL verdicts.
- `docs/MCP45HVX1_RELEASE_CHECKLIST.md` now requires runner syntax validation
  and HIL artifact bundle review before release claims.

## Hardware HIL Run Status

Hardware HIL was not run in this prompt. No serial port, hardware fixture, safe
load, operator confirmation, or attached HIL evidence bundle was provided in
the local environment.

## Software Checks

Validation results are recorded after the final Prompt 08 validation run.
