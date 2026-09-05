# MCP45HVX1 API Contract

This document is the authoritative public API contract for the current
release. It describes software behavior only; it is not hardware
validation evidence.

## Scope

Software tests and static guards verify transport framing, status propagation,
cache behavior, and CLI contracts. They cannot prove output-changing analog
behavior, analog accuracy, high-voltage safety, WLAT/SHDN physical output
behavior, rail-cycle behavior, fault recovery on a physical fault fixture, or
General Call safety on a shared bus. Those gates live in
[`MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md).

## Status Codes

`Err` values returned by the core. The enum is append-only; existing values do
not change.

| Code | Meaning |
|---|---|
| `OK` | Operation succeeded |
| `NOT_INITIALIZED` | `begin()` has not been called, or `end()` has been |
| `INVALID_CONFIG` | A `Config` field is missing or invalid |
| `INVALID_PARAM` | An argument is out of range for the configured variant |
| `UNSUPPORTED` | The operation is disabled by configuration or has no callback |
| `BUSY` | A poll job is active |
| `OFFLINE` | The driver is latched offline; recovery is required |
| `IN_PROGRESS` | `pollJob()` has work remaining; call it again |
| `DEVICE_NOT_FOUND` | Presence check saw a definite address NACK |
| `REGISTER_MISMATCH` | Readback did not match the documented register format |
| `I2C_NACK_ADDR` | Transport reported an address NACK |
| `I2C_NACK_DATA` | Transport reported a data-byte NACK |
| `I2C_TIMEOUT` | Transport reported a transaction timeout |
| `I2C_BUS` | Transport reported a bus error (arbitration loss, stuck line) |
| `I2C_ERROR` | Transport reported an unclassified I2C failure |
| `TIMEOUT` | Generic timeout reported by a transport callback |

`Status::ok()` tests for `OK`; `Status::inProgress()` tests for `IN_PROGRESS`.
`Status::detail` carries a transport-specific value and `Status::msg` a static
string the driver never owns.

The transport-detail codes are produced by application callbacks. The core maps
only one of them: a definite address NACK during `begin()` or `probe()` becomes
`DEVICE_NOT_FOUND`. Everything else is returned as the callback reported it.

An adapter must preserve uncertainty when its framework cannot distinguish
address and data NACKs. ESP-IDF 6.0.1 combines these into
`ESP_ERR_INVALID_RESPONSE`; the example maps a transmit/combined-transfer NACK
to `I2C_ERROR`, retaining the native detail. The same native response can
also wrap an internal transaction timeout, including on receive-only calls,
so that response never proves an address NACK. ESP32 Wire combines write NACK causes and exposes
only a byte count for reads; its example reports ambiguous NACKs and short
reads (including zero bytes) as `I2C_ERROR`. Thus `DEVICE_NOT_FOUND` is not
guaranteed from either example's combined `begin()`/`probe()` read. No extra
probe or retry attempts to reconstruct a failed transaction's history.

## Transport Ownership

- Core code under `include/` and `src/` is framework-neutral.
- The driver never owns `Wire`, ESP-IDF I2C buses, GPIO pins, FreeRTOS tasks,
  interrupts, mutexes, rail sequencing, or bus-reset policy.
- Applications provide `Config::i2cWrite`, `Config::i2cWriteRead`, and optional
  `Config::busReset` callbacks.
- The write callback must support up to `cmd::MAX_COMMAND_CHUNK` (64) bytes
  per INC/DEC transaction. This is a driver work/buffer bound, not a silicon
  limit. Both supplied adapters support it; smaller transports must reject
  oversize requests before bus access with `INVALID_PARAM`.
- `i2cUser`, `controlUser`, and `timeUser` are non-owning context pointers and
  must outlive every driver call that may use them.

## Thread And ISR Model

- Public APIs are synchronous and may block inside application-supplied I2C
  callbacks.
- Public APIs are not ISR-safe.
- The driver does not provide internal locking. Use one task/thread per driver
  instance or serialize access externally.
- Callback code must implement its own timeout, bus arbitration, and recovery
  policy.

## Address, Variant, And RAB Policy

- The documented address range is `0x3C..0x3F` (DS20005304B §6.2.4,
  Table 6-2).
- All other addresses are rejected. `0x5C..0x5F` are 8-bit *control bytes* of
  the standard-voltage MCP45XX/46XX family, not 7-bit addresses of this part.
  The former alternate-address option and CLI command have been removed.
  See [`DEVICE_REFERENCE.md`](DEVICE_REFERENCE.md).
- MCP45HV31 is modeled as 7-bit / 128 taps with POR/BOR Wiper default `0x3F`.
- MCP45HV51 is modeled as 8-bit / 256 taps with POR/BOR Wiper default `0x7F`.
- RAB selection is used for nominal helper math and terminal-current limits. It
  does not measure the actual resistor value, tolerance, leakage, INL/DNL, or
  board loading.

## Volatile Wiper/TCON State

- The implemented volatile registers are Wiper0 `0x00` and TCON0 `0x04`.
- TCON reserved bits `[7:4]` are forced high on writes and expected high on
  readback.
- Register readback verifies volatile register contents only. It does not prove
  physical analog output movement when WLAT is high, SHDN is asserted, or
  external circuitry overrides the terminal output.
- `writeWiperFraction()` rejects NaN and values outside `0.0..1.0`; it does not
  clamp output-changing requests.

## Uncertainty Model

If a state-changing write reaches I2C and then fails ambiguously, the driver
preserves the original `Status`, marks affected cache entries unknown, and sets
`hardwareStateUncertain()`.

Uncertainty applies to Wiper/TCON/output-changing paths, including:

- Wiper writes, percent/fraction helpers, increments, and decrements
- TCON writes, terminal helpers, and TCON software-shutdown helpers
- raw writes to Wiper0/TCON0
- reset/default helpers that write volatile state
- General Call Wiper/TCON/write/step helpers

Validation failures before I2C access do not set uncertainty. Address NACK and
device-not-found results are treated as no device accepting the transaction and
do not set uncertainty. Timeouts, bus errors, data NACKs, and generic I2C errors
after an attempted write can leave physical state uncertain.

Uncertainty clears only by evidence:

- a successful readback of each affected volatile register, or
- a successful `recover()`/readback path that verifies all unknown affected
  volatile state.

`resetI2cState()` does not clear uncertainty.

## Begin And Startup Writes

`begin()` validates configuration and reads Wiper/TCON before any optional
startup write is attempted. By default, `begin()` is read-only and sends no
output-changing writes.

Startup Wiper and TCON writes are enabled only by explicit output-changing
config fields:

- `Config::writeInitialTcon`
- `Config::initialTcon`
- `Config::writeInitialWiper`
- `Config::initialWiperCode`

When both writes are requested, `begin()` uses the TCON readback to order
them. It writes the wiper before leaving software shutdown or enabling any
previously disconnected terminal. For shutdown, unchanged topology, and changes
that only disconnect terminals, it writes TCON first. Partial reconnections use the
same rule as a full reconnection. These two writes cannot make an arbitrary
live circuit transition atomic or override SHDN/WLAT; applications still own
external isolation and analog sequencing.

If an optional startup write fails after attempted I2C access, `begin()` returns
the original error and preserves enough runtime state for `readWiper()`,
`readTcon()`, `readSnapshot()`, or `recover()` to inspect possible side effects.

## Probe, Reset, And Recover

- `probe()` is diagnostic. It performs a raw Wiper read and does not update
  health counters, Wiper/TCON cache, or READY/DEGRADED/OFFLINE state.
- `resetI2cState()` runs the optional bus-reset callback. A successful callback
  is not device proof and does not mark a driver READY by itself.
- `recover()` does not call `busReset`. It performs tracked Wiper and TCON
  reads. Both must succeed before `recover()` returns OK and marks the driver
  READY.
- `restorePowerOnDefaults()` writes Wiper 0 first, then TCON0. The default TCON
  connects every terminal, so writing it first would connect the terminals
  while Wiper 0 still holds the previous code.
- Partial recovery keeps any unverified cache entry unknown and preserves
  uncertainty for that state.
- If recovery starts from OFFLINE and later fails, the OFFLINE latch is
  restored.

## State, Snapshots, And Helpers

- `readSnapshot(RegisterSnapshot&)` reads Wiper then TCON and updates the
  caller snapshot only after both reads succeed.
- `getSettings()` and `getSettings(SettingsSnapshot&)` expose the active
  copied config, lifecycle state, cache-known flags, address-pointer knowledge,
  health counters, and hardware-uncertainty status without issuing I2C traffic.
- `getConfig()` returns the active copied `Config`; callback pointers and user
  contexts remain non-owning.
- `state()` and `driverState()` return the same `DriverState`.
- `isInitialized()` reports whether runtime config/transport state is retained.
  A failed optional startup write may leave this true for diagnostics.
- `isOnline()` reports READY or DEGRADED. OFFLINE blocks normal bus-touching
  APIs until recovery.
- `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`,
  `totalFailures()`, and `totalSuccess()` expose tracked health information.
  Local `INVALID_CONFIG`, `INVALID_PARAM`, `NOT_INITIALIZED`, and
  `UNSUPPORTED` results do not count as device failures, including when
  returned by a callback.
- `getDeviceInfo()` reports active address, configured resolution and RAB,
  max/default Wiper code, nominal resistance, ideal step, terminal-current
  limit.
- `siliconErrataInfo()` returns static DS80000649B release-gate information for
  CLI diagnostics and documentation; it is not proof that any physical silicon
  lot is unaffected.
- `maxWiperCode()`, `defaultWiperCode()`, `nominalResistanceOhms()`,
  `stepResistanceOhms()`, `maxTerminalCurrentMilliAmps()`,
  `resistanceBToWOhms()`, and `resistanceAToWOhms()` are static helper math or
  datasheet-limit helpers. Resistance helpers are ideal and exclude tolerance,
  wiper resistance, leakage, INL/DNL, temperature, and board loading.
- `sanitizeTcon()` forces reserved TCON bits `[7:4]` high before writes.
- `decodeTcon()` decodes a TCON byte into terminal bits and a best-fit terminal
  mode.

## Poll-Chunked Jobs

The core exposes an explicit job API for applications that need bounded I2C
work per owner-task poll:

- `startSetWiperJob(code)`
- `startReadSnapshotJob()`
- `startSetTerminalJob(terminal, enabled)`
- `startIncrementWiperJob(steps)`
- `startDecrementWiperJob(steps)`
- `startRecoverJob()`
- `pollJob(nowMs, maxInstructions)`
- `getJobSnapshot()`
- `getJobSnapshot(JobSnapshot&)`
- `jobActive()`

The legacy `tick()` function is retained as a deprecated, source-compatible
no-op. It does not advance jobs; applications must call `pollJob()` explicitly.

One register read or command-write chunk is one instruction. Snapshot jobs read
Wiper and TCON as separate instructions. Terminal jobs expose the TCON
read-modify-write sequence as separate read and write instructions, or complete
after the read when the requested bit already matches. Wiper step jobs split
multi-step operations into bounded command chunks and run at most one chunk per
poll. A direct Wiper set job is a single output-changing instruction.

Polling before any job has been started, or after `begin()`/`end()` resets
the job state, returns `INVALID_PARAM`. Once a job completes, repeated polls
return its final status. A zero-step increment/decrement publishes its own
successful completed snapshot.

While a job is active, other bus-touching public APIs return `Err::BUSY` without
issuing I2C traffic. `JobSnapshot` reports job type, active state,
output-changing classification, planned and completed instructions, the last
poll's attempted instruction count, final status, and completed readback
registers for snapshot/recover jobs.

Ambiguous output-changing job failures preserve the original failing `Status`,
mark affected cache entries unknown, and set hardware uncertainty using the
same rules as synchronous writes. Read-side failures stop the job at the first
failing instruction and do not mark hardware uncertainty by themselves.

`startRecoverJob()` is the only job accepted while OFFLINE. When it starts from
OFFLINE, the OFFLINE latch remains asserted after the first successful Wiper
read and clears only after the second TCON read succeeds.

## Raw Register And General Call Warnings

Raw writes and General Call helpers are output-changing. General Call frames are
broadcast and not device-specific; success does not prove the local device was
the only responder. The core General Call helpers are disabled unless
`Config::allowGeneralCall` is explicitly true. When disabled, they return
`UNSUPPORTED` without issuing bus traffic or changing cache/uncertainty state.
The configuration gate is checked before the OFFLINE latch, so a disabled
helper reports `UNSUPPORTED` rather than `OFFLINE` even on an offline driver —
`recover()` cannot enable a helper that configuration disabled.
Production firmware must require isolated-bus evidence or a documented
shared-bus risk acceptance before enabling output-changing General Call use.

The interactive Arduino and ESP-IDF diagnostic examples opt into this core flag
because their CLI layer provides an additional one-shot `gc arm` gate and
operator warnings. Application bus managers should make their own explicit
policy decision rather than inheriting that example policy blindly.

## High-Voltage Safety

Software cannot enforce analog rail voltage, terminal current, load impedance,
thermal limits, or external circuit response. Use low-voltage safe-load testing
before high-voltage testing, measure P0A/P0W/P0B behavior externally, and keep
output-changing CLI/HIL commands opt-in and state-restoring.
