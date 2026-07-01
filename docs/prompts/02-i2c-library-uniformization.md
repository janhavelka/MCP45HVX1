# MCP45HVX1 I2C Uniformization Prompt

Repository: `MCP45HVX1`

Absolute path: `C:\Users\Honza\Documents\Projects\MCP45HVX1`

## Execution Rules

You are working inside this single repository. Implement this prompt directly;
do not repeat the cross-repository audit.

You may spawn subagents for read-only inspection of APIs, tests, I2C
transactions, docs, and diagnostics. Keep final judgment, edits, and
verification in the main agent.

Prefer simple, robust, readable code. Before adding code, inspect whether
existing code can be simplified, reused, tightened, or deleted.

Preserve dirty user changes. Do not commit unless explicitly asked.

## Common Uniformization Target

Apply this shared I2C library contract: injected non-owning transport, `Status` returns, cache-only `getSettings(SettingsSnapshot&) const`, active `probe()`/diagnostics named explicitly, `DriverState` with `state()` and `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()`.

Keep the common `Err` vocabulary append-only where missing: `OK`, `NOT_INITIALIZED`, `INVALID_CONFIG`, `INVALID_PARAM`, `I2C_ERROR`, `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `DEVICE_NOT_FOUND`, `TIMEOUT`, `BUSY`, and `IN_PROGRESS`. Preserve MCP45HVX1-specific register mismatch, unsupported, volatile-cache, and `hardwareStateUncertain()` behavior.

Uniformization is not a new base class or framework. Make only local, source-compatible additions and tests.

## Current State

- Public lifecycle and diagnostics are in `include\MCP45HVX1\MCP45HVX1.h`: `SettingsSnapshot` includes `hardwareStateUncertain` at lines 118-126, `probe()`/`recover()` are around lines 236-244, and `getSettings()`/`getSettings(SettingsSnapshot&)` are around lines 284-287.
- Health counters are exposed at `include\MCP45HVX1\MCP45HVX1.h:303-324`.
- `hardwareStateUncertain()` and `hardwareStateUncertainError()` are at `include\MCP45HVX1\MCP45HVX1.h:312-316`.
- Implementation updates hardware uncertainty in `src\MCP45HVX1.cpp:1370-1392`.
- HIL runner exists as `tools\run_hil_mcp45hvx1.py`.
- A `driverState()` alias was not found in the main header.

## Best Sources To Adapt

- Add the simple cross-driver alias from SHT3x/BME280: `SHT3x-main\include\SHT3x\SHT3x.h:227-230`.
- Keep MCP45HVX1's `hardwareStateUncertain` naming; it is more accurate than `hardwareConfigDirty` for volatile wiper/TCON state.
- For HIL parser tests, adapt SSD1315 `tools\test_hil_runner_parser.py` or BME280 `tools\test_run_i2c_hil_parser.py`.

## Implementation Tasks

1. Add `DriverState driverState() const { return state(); }` beside the existing state accessor.
   Preserve existing compatibility aliases; do not remove or rename public APIs to achieve uniform naming.
2. Preserve `hardwareStateUncertain()` semantics. Do not rename it to `hardwareConfigDirty()`.
3. Ensure `SettingsSnapshot` exposes `hardwareStateUncertain` and `hardwareStateUncertainError` whenever raw/diagnostic writes or ambiguous state-changing failures can diverge from cache.
4. Audit every wait/poll path for finite timeout bounds and visible status returns. Normal wiper/TCON operations must not hide retries; recovery remains explicit and application-scheduled.
5. Add host-side HIL parser/classifier tests for `tools\run_hil_mcp45hvx1.py`, especially failure-token detection, operator confirmation for output-changing commands, common minimum `version`/`scan`/`probe`/`settings`/`health` coverage, and report generation without false PASS.
6. Review HIL runner docs so output-changing steps require a safe load and explicit confirmation. `tools\run_hil_mcp45hvx1.py:274` already prompts; add test coverage if feasible without hardware.
7. Confirm `getSettings()` remains cache-only and does not perform readback.

## API Changes Required

- Add `driverState()` alias only.

## Simplifications Before Adding Code

- Do not add both "dirty" and "uncertain" public names unless a compatibility alias is clearly needed by a caller.

## Tests To Add Or Update

- Native alias test for `driverState()`.
- Native test proving `getSettings()` is bus-silent.
- Host HIL parser tests for no false PASS and destructive-command confirmation.

## Commands To Run

- `pio test -e native`
- `pio run -e esp32s3dev`
- If parser tests are added: `python tools\<new_parser_test>.py`
- Live HIL only with safe load, explicit serial port, and operator confirmation.

## Constraints And Non-Goals

- Do not control analog rails or SHDN hardware pins from the core library.
- Do not convert device-specific uncertainty into a generic framework.
- Injected transport only: no global `Wire`, new bus manager, pin ownership, or shared bus reset from the device driver.
- Preserve distinct timeout, address NACK/device-not-found, data NACK, bus, register mismatch, unsupported, and hardware-uncertain statuses. Do not collapse them into generic `I2C_ERROR` or use `DEVICE_NOT_FOUND` for timeout/data/bus failures.

## Risks And Open Questions

- Open: whether HIL should be renamed to the common `tools\run_i2c_hil.py` shape or keep `run_hil_mcp45hvx1.py` to reflect output-changing risk.
