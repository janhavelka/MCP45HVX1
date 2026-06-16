# MCP45HVX1 API Contract

This document is the authoritative public API contract for the current
hardening branch. It describes software behavior only; it is not hardware
validation evidence.

## Release Readiness

The library is a pre-production candidate pending hardware validation. Software
tests and static guards can verify transport framing, status propagation, cache
behavior, and CLI contracts. They cannot prove analog accuracy, high-voltage
safety, WLAT/SHDN physical output behavior, or General Call safety on a shared
bus.

## Transport Ownership

- Core code under `include/` and `src/` is framework-neutral.
- The driver never owns `Wire`, ESP-IDF I2C buses, GPIO pins, FreeRTOS tasks,
  interrupts, mutexes, rail sequencing, or bus-reset policy.
- Applications provide `Config::i2cWrite`, `Config::i2cWriteRead`, and optional
  `Config::busReset` callbacks.
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

- The documented default address range is `0x3C..0x3F`.
- The alternate `0x5C..0x5F` range is accepted only when
  `Config::allowAlternateAddressRange` is explicitly enabled for
  hardware-verification builds.
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
- Partial recovery keeps any unverified cache entry unknown and preserves
  uncertainty for that state.
- If recovery starts from OFFLINE and later fails, the OFFLINE latch is
  restored.

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

One register read or command-write chunk is one instruction. Snapshot jobs read
Wiper and TCON as separate instructions. Terminal jobs expose the TCON
read-modify-write sequence as separate read and write instructions, or complete
after the read when the requested bit already matches. Wiper step jobs split
multi-step operations into bounded command chunks and run at most one chunk per
poll. A direct Wiper set job is a single output-changing instruction.

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
