# MCP45HVX1 Digital Potentiometer Implementation Manual

This driver follows the workspace-standard I2C library pattern: injected
transport callbacks, explicit `Status` returns, managed lifecycle, health
counters, native fake-bus tests, and example-only Arduino helpers.

## Driver Model

The core library never calls `Wire` directly. Applications provide:

- `Config::i2cWrite`
- `Config::i2cWriteRead`
- `Config::i2cUser`
- `Config::busReset` and `Config::controlUser` for optional board-specific
  I2C interface reset

The example adapter in `examples/common/I2cTransport.h` maps `TwoWire` errors to
the driver `Status` model.

`Config` is copied into the driver, but callback pointers and
`i2cUser`/`controlUser`/`timeUser` contexts are non-owning. They must remain
valid for every driver call that can use them. Driver calls are synchronous,
not internally locked, and not ISR-safe; applications must serialize access to
shared drivers and buses externally.

`MCP45HVX1` instances are non-copyable and non-movable. One instance should
represent one physical device/bus binding because it owns health counters,
volatile register-cache knowledge, and non-owning callback contexts.

## Initialization

`begin()` validates configuration, reads Wiper 0 and TCON0 using the documented
random-read command format, and caches the values. It does not write the analog
state by default.

The datasheet timing table gives `TBORD`, the delay after the device exits reset
state with `VL > VBOR`, as 10 us typical and 20 us maximum. A separate `tPOR`
ready delay is not identified in the local datasheet extract. The core driver
does not own rail sequencing, reset supervision, SHDN/WLAT pins, or startup
delays. Applications must ensure digital and analog rails, reset, SHDN, and
WLAT are stable before `begin()`. `Config::nowMs` is used for health timestamps,
not for delay during `begin()` or `recover()`.

Optional startup writes are available:

- `Config::writeInitialWiper`
- `Config::writeInitialTcon`

Use these only when changing the wiper/terminal state during MCU startup is
intentional for the hardware design. Validate them on a safe load with external
measurement, record WLAT/SHDN state, and confirm that any restore path leaves
`hardwareStateUncertain` clear before production use.

`Config::requirePowerOnDefaults` compares immediate Wiper/TCON readback against
documented POR/BOR default values. It does not prove that a fresh POR/BOR event
occurred or that WLAT/SHDN/external circuitry allowed analog movement.

`restorePowerOnDefaults()` explicitly writes the documented volatile defaults:
TCON0 `0xFF` and Wiper 0 `0x7F` for 8-bit parts or `0x3F` for 7-bit parts.
`resetI2cState()` calls the optional board reset callback and does not alter
Wiper/TCON state.

For baseline presence reads in `begin()`, an address NACK is reported as
`DEVICE_NOT_FOUND` with numeric detail preserved. Timeout, bus-error,
data-NACK, generic I2C, and register-format failures keep their original public
status code.

## Register Access

Only two addresses are implemented:

- `0x00`: Volatile Wiper 0, R/W/INC/DEC
- `0x04`: Volatile TCON0, R/W

All other addresses are rejected in software before I2C access. This avoids
intentionally generating the device's documented reserved-address NACK state.

MCP45HVX1 reads return two bytes. The first byte is the compatibility MSB and
should be `0x00`; the second byte is the 8-bit register value.

## Wiper Handling

The configured resolution controls input validation:

- `Resolution::Bits7`: `0x00-0x7F`
- `Resolution::Bits8`: `0x00-0xFF`

`incrementWiper()` and `decrementWiper()` send the documented command-byte-only
INC/DEC frames. The driver cache clamps at full-scale and zero-scale after
successful commands.

`readWiperFraction()` and `writeWiperFraction()` provide normalized `0.0-1.0`
position helpers. `codeFromFraction()` clamps because it is a pure conversion
helper. `writeWiperFraction()` rejects out-of-range and NaN input with
`INVALID_PARAM` because it changes high-voltage analog output.
`stepResistanceOhms()`, `resistanceBToWOhms()`, and `resistanceAToWOhms()` are
ideal nominal helpers from the configured ordering option. They do not include
analog tolerance terms.

## TCON Handling

TCON reserved bits `[7:4]` are forced high on all driver writes. Public helpers
operate on the documented lower bits:

- `R0HW`: TCON software shutdown when cleared
- `R0A`: P0A terminal connection
- `R0W`: P0W terminal connection
- `R0B`: P0B terminal connection

`decodeTcon()`, `readTerminalStatus()`, and `getTerminalMode()` expose decoded
state for CLI and diagnostics. TCON combinations that are valid but do not match
a named preset decode as `TerminalMode::Custom`; `Custom` is not accepted by
`setTerminalMode()`.

The external SHDN pin is active-low hardware shutdown and is not controlled by
the core unless an application adapter owns that pin. APIs named shutdown in
this library control TCON software shutdown via R0HW. WLAT is also external to
the core; when WLAT is high, Wiper register writes can be accepted and read back
without moving the physical wiper until WLAT is released. Register readback
therefore proves volatile register contents only, not analog terminal movement
when WLAT, SHDN, or external circuitry overrides output.

## Health Tracking

Tracked public I2C operations update:

- last success/error timestamp
- last error `Status`
- consecutive failures
- total failures/successes
- READY/DEGRADED/OFFLINE state

`probe()` uses the raw path and does not update health counters, Wiper/TCON
cache, uncertainty, or READY/DEGRADED/OFFLINE state. Address NACK maps to
`DEVICE_NOT_FOUND`; timeout, bus-error, data-NACK, generic I2C, and
register-format failures keep their original public status code. The raw read
may update address-pointer knowledge.

`resetI2cState()` calls only the optional bus-reset callback. A successful
callback clears address-pointer knowledge but does not prove device presence,
clear hardware uncertainty, refresh Wiper/TCON cache, or mark a DEGRADED or
OFFLINE driver READY. Reset callback failures are tracked as health failures
when they are real transport/reset errors.

`recover()` does not call `busReset`. It performs tracked Wiper then TCON reads,
refreshes cache, clears uncertainty only for verified registers, and returns
READY only after both reads succeed. If recovery starts OFFLINE and only
partially succeeds, the OFFLINE latch is restored.

Semantic readback failures, such as a non-zero read MSB when
`requireReadMsbZero` is enabled, are also recorded as tracked health failures.

## Hardware Uncertainty

Failed state-changing Wiper, TCON, raw register, startup, or General Call
writes can leave volatile analog state uncertain if the bus transaction may have
reached the device before the host observed an error. In that case the driver
preserves the original `Status`, marks affected cache entries unknown, and sets
`hardwareStateUncertain` until readback verifies every affected volatile
register. Validation failures before I2C access and address NACKs do not set
uncertainty.

## General Call

The driver exposes helpers for the documented General Call frames. It does not
attempt to configure `GCEN` because the register location is not documented in
the available source notes. Successful General Call writes mark the affected
local cache entry unknown because ACKs are broadcast and do not prove the local
configured device executed the command. The CLI requires `gc arm` before each
broadcast command attempt.

DS80000649B is a production release gate. Affected silicon can accept traffic
intended for other I2C devices and has General Call decode anomalies. Production
firmware must either isolate the MCP45HVX1 on its own bus or record explicit
risk acceptance for shared-bus deployment. Output-changing General Call use
requires isolated-bus evidence.

## Poll-Chunked Jobs

The synchronous public APIs remain the simplest path for most applications.
For applications with a single owner task that must bound bus work per loop,
the driver also exposes explicit poll jobs:

- `startSetWiperJob(code)` for a one-instruction output-changing Wiper write
- `startReadSnapshotJob()` for separate Wiper and TCON readback instructions
- `startSetTerminalJob(terminal, enabled)` for visible TCON
  read-modify-write sequencing
- `startIncrementWiperJob(steps)` and `startDecrementWiperJob(steps)` for
  bounded Wiper step chunks
- `startRecoverJob()` for Wiper/TCON recovery readback, including from OFFLINE
- `pollJob(nowMs, maxInstructions)` to execute bounded instructions
- `getJobSnapshot()` / `getJobSnapshot(out)` and `jobActive()` for status

While a job is active, other bus-touching public APIs return `BUSY` without
issuing I2C traffic. Ambiguous output-changing job failures use the same
hardware-uncertainty and cache-invalidation semantics as the synchronous APIs.
Read-side failures stop at the first failed instruction and preserve the last
job status in `JobSnapshot`.

## CLI Coverage

The Arduino `01_basic_bringup_cli` and native `espidf_basic` examples expose
the same command contract while keeping their framework code separate:

- common controls: `help`, `?`, `version`, `ver`, `color`, `verbose`
- device setup: `scan`, `begin`, `addr`, `addr_alt`, `variant`, `res`, `rab`,
  `probe`, `recover`, `iface_reset`
- read-only diagnostics: `cfg`, `settings`, `detail`, `drv`, `health`, `state`,
  `info`, `errata`
- register reads: `read`, `rregs`, `readwiper`, `readtcon`, `last`, `reg`,
  `rreg`, `dump`, `raw`
- output-changing Wiper operations: `wiper`, `wiper percent`,
  `wiper fraction`, `frac`, `pos`, `zero`, `mid`, `max`, `inc`, `dec`
- output-changing TCON operations: `tcon`, `defaults`, `term`, `terminal`,
  `shutdown`, `software-shutdown`, `mode`
- safe tests: `selftest`, `selftest safe`, `stress`
- operator-gated or dangerous tests: `selftest output`, `stress_mix`,
  `raw write`, `wreg`, `wregs`
- General Call frames: `gc arm`, `gc disarm`, `gc wiper`, `gc tcon`, `gc inc`,
  `gc dec`

The ESP-IDF example uses `app_main`, `driver/i2c_master.h`, `esp_timer`,
`vTaskDelay`, and fixed C buffers. It does not include Arduino CLI sources or
compatibility facades. `tools/check_cli_contract.py` and
`tools/check_idf_example_contract.py` enforce command parity, native-IDF
boundaries, bounded parsing, functional color support, non-placeholder
`selftest output`, and General Call warning behavior.
