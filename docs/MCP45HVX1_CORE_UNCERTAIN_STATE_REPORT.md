# MCP45HVX1 Core Uncertain State Report

Prompt: 01 - Fix H1 core uncertain hardware state after failed state-changing writes.

## Summary

The driver now tracks core hardware uncertainty separately from health state. An
ambiguous failed Wiper, TCON, step, raw register, or General Call state-changing
write invalidates the affected cache and records the original `Status` in
`hardwareStateUncertainError`.

No public clear API was added. Uncertainty clears only after readback verifies
all affected volatile registers.

## Public Model

`SettingsSnapshot` now includes:

- `hardwareStateUncertain`
- `hardwareStateUncertainError`
- existing `cachedWiperKnown`
- existing `cachedTconKnown`

The driver exposes matching const accessors:

- `hardwareStateUncertain()`
- `hardwareStateUncertainError()`

## What Sets Uncertainty

Validation and policy failures before I2C access do not set uncertainty:

- invalid config
- invalid argument
- not initialized
- unsupported command/register
- offline `BUSY`
- device not found or register mismatch
- address NACK

Ambiguous failures after an attempted state-changing write set uncertainty:

- generic I2C error
- data NACK
- timeout
- I2C timeout
- bus error

Affected paths:

- `writeWiper()`
- `writeWiperFraction()`
- `incrementWiper()`
- `decrementWiper()`
- `writeTcon()`
- terminal enable/disable helpers
- software shutdown helper
- terminal mode helper
- `restorePowerOnDefaults()` / `resetToDefaults()`
- optional output-changing startup writes in `begin()`
- `writeRegister()`
- General Call Wiper/TCON write and Wiper step helpers

## Clearing Rules

Readback is required. `readWiper()` verifies Wiper state. `readTcon()` verifies
TCON state. `readSnapshot()` and `recover()` clear uncertainty only after all
affected unknown volatile registers have been read successfully.

`resetI2cState()` does not clear uncertainty because it only resets the bus or
I2C interface state; it does not verify Wiper or TCON contents.

## CLI And HIL Visibility

Arduino and ESP-IDF CLIs now expose uncertainty consistently:

- `state` prints `uncertain=...`
- `drv` / `health` print aggregate uncertainty and last core uncertainty error
- `cfg` / `settings` print Wiper/TCON cache-known flags
- stress summaries print uncertainty so restore failures are not hidden

HIL and self-test procedures must treat `uncertain=yes` as requiring external
measurement or readback before trusting analog output state.

## Why It Matters

MCP45HVX1 is a high-voltage volatile digital potentiometer. Wiper and TCON
changes can affect connected analog circuits, terminal connectivity, and
software shutdown state. If the host observes a late I2C failure, the device may
have accepted enough bytes to change physical output even though the operation
returned an error.

The driver now avoids presenting stale cached Wiper/TCON values as known after
such failures.

## Tests Added

Native tests cover:

- pre-mutation Wiper write failure without false uncertainty
- post-mutation Wiper write failure
- post-mutation TCON write failure
- partial increment/decrement failure
- post-mutation General Call failure
- readback clearing only after all affected registers are verified
- snapshot/accessor exposure of uncertainty and last error

## Remaining Risks

Software cannot prove analog rail safety, terminal current, WLAT state, SHDN
override state, or external circuit behavior. Hardware validation must still use
external measurement and safe loads. Successful General Call ACK remains
broadcast and not device-specific; affected caches are marked unknown and should
be verified with readback.
