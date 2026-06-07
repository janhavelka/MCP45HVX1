# MCP45HVX1 Begin Startup Recoverability Report

Prompt: 02 - Fix H2 begin/startup side effects and recoverability.

## Selected Contract

Selected contract: Option C, two-phase begin, with the existing read-only
default preserved.

`begin()` first validates configuration and performs safe baseline Wiper/TCON
reads. Only after those reads succeed does it attempt optional startup writes.
Both startup write flags remain opt-in and default to disabled.

## Public API Changes

No breaking public API changes were made. Existing fields remain:

- `Config::writeInitialTcon`
- `Config::initialTcon`
- `Config::writeInitialWiper`
- `Config::initialWiperCode`

Public Doxygen now describes these writes as output-changing. `begin()` Doxygen
now documents that failed optional startup writes preserve diagnostic readback
state and use the Prompt 01 uncertainty model.

## Startup Failure Behavior

Failures before baseline read success still reset the runtime snapshot and leave
the driver uninitialized. These failures have not performed optional
output-changing startup writes.

After baseline reads succeed, the driver marks itself initialized and READY
before attempting optional writes. If an optional write fails:

- `begin()` returns the original `Status`;
- config and transport callbacks remain available;
- `readWiper()`, `readTcon()`, `readSnapshot()`, and `recover()` remain usable;
- ambiguous attempted write failures mark affected cache state unknown;
- `hardwareStateUncertain` and `hardwareStateUncertainError` expose the risk.

Startup write order remains TCON first, then Wiper.

## Safety Implications

Default `begin()` remains read-only and sends no output-changing frames. Enabling
startup Wiper or TCON writes is an explicit high-voltage analog decision by the
application. If a startup write reports a late transport error, the physical
device may have changed even though `begin()` returned failure.

Applications should inspect `hardwareStateUncertain`, cache-known flags, and the
last uncertainty error before trusting analog output after failed startup.

## Tests Added

Native tests cover:

- read-only default `begin()` sends only Wiper/TCON reads;
- initial Wiper write is sent only when explicitly configured;
- TCON startup success followed by Wiper post-mutation failure is recoverable;
- TCON post-mutation startup failure is recoverable;
- address-NACK startup failure before mutation does not set false uncertainty;
- `recover()` after failed startup write re-reads volatile state and clears
  uncertainty.

## Remaining Limitations

The driver cannot prove analog rail safety, terminal current, SHDN/WLAT pin
state, or external circuit behavior. Hardware validation must use safe loads and
external measurement. Wiper-success-then-TCON-failure is not reachable with the
current TCON-before-Wiper startup order.
