# MCP45HVX1 I2C Library Hardening Report

Prompt: MCP45HVX1 I2C Library Hardening And TunnelMonitor Fit

Branch: `hardening/mcp45hvx1-industry-readiness`

Status: software hardening chunk implemented; final check and commit details
are recorded in the commit history and final response.

This report covers core I2C API safety policy, General Call guarding,
dirty/cache uncertainty, and TunnelMonitor fit. It does not cover the companion
poll-chunking prompt's job API, `maxInstructions`, or transaction-budget tests.
It is not hardware validation evidence.

## General Call Policy

Core General Call helpers are disabled by default through
`Config::allowGeneralCall = false`.

When disabled, the four public helpers return `UNSUPPORTED` without issuing I2C
traffic, changing Wiper/TCON cache-known flags, or setting hardware
uncertainty:

- `generalCallWriteWiper()`
- `generalCallWriteTcon()`
- `generalCallIncrementWiper()`
- `generalCallDecrementWiper()`

The Arduino and ESP-IDF diagnostic CLIs explicitly set `allowGeneralCall = true`
because they already require a one-shot `gc arm` command and print bus-wide /
DS80000649B / isolated-bus warnings before issuing a broadcast command. That
example policy is not a production bus-manager policy.

Production firmware must enable General Call only behind an application-owned
isolated-bus evidence gate or documented shared-bus risk acceptance.

## Operation Classification

Steady-path raw operations:

- `readWiper()`
- `writeWiper(code)`
- `readTcon()`
- `writeTcon(raw)`
- `readRegister()` / `writeRegister()` for documented volatile registers

Setup, diagnostics, or recovery operations:

- `begin()`
- `probe()`
- `recover()`
- `resetI2cState()`
- `restorePowerOnDefaults()`
- `readSnapshot()`
- `readLastAddress()`

Convenience/output-changing helpers:

- `writeWiperFraction()`
- `setTerminalEnabled()`
- `setSoftwareShutdown()`
- `setTerminalMode()`
- `incrementWiper()`
- `decrementWiper()`

Unsafe broadcast helpers:

- all General Call helpers, even when explicitly enabled

`readSnapshot()` is diagnostic/readback evidence for volatile register contents.
It reads Wiper then TCON through the normal tracked read paths, but it updates
the caller's `RegisterSnapshot` only after both reads succeed. It does not prove
physical analog movement when WLAT, SHDN, or external circuitry overrides
output behavior.

## Dirty And Uncertain State

The driver already models dirty or uncertain hardware state through:

- `SettingsSnapshot::hardwareStateUncertain`
- `SettingsSnapshot::hardwareStateUncertainError`
- `cachedWiperKnown`
- `cachedTconKnown`
- public `hardwareStateUncertain()` and `hardwareStateUncertainError()`

Validation failures before I2C access do not set uncertainty. Ambiguous
state-changing transport failures after attempted I2C access mark the affected
cache entry unknown and preserve the original failing `Status`.

Successful readback of each affected register, or `recover()` successfully
reading all affected volatile state, is required before uncertainty clears.

## TunnelMonitor Fit

The core remains callback-only and does not own `Wire`, ESP-IDF I2C handles,
GPIO pins, locks, tasks, polling loops, or timing policy. TunnelMonitor can keep
I2C ownership behind its `I2cTask` and call the synchronous core operations from
that owner.

The remaining TunnelMonitor-specific bounded-poll work belongs to the companion
poll-chunking prompt.

## Tests

Native tests cover:

- default General Call disablement with no bus traffic;
- explicit General Call opt-in behavior;
- `readSnapshot()` all-or-nothing caller output assignment on second-read
  failure;
- ambiguous Wiper/TCON/raw/General Call write uncertainty;
- offline latch blocking normal operations without extra bus I/O;
- explicit `recover()` path out of OFFLINE.

## Package Notes

`library.json` exports top-level `docs/*.md` so README and docs-index links stay
valid in PlatformIO package consumers. PDFs, extracted markdown trees, tools,
tests, prompts, generated archives, local build output, and HIL logs remain
excluded.

No hardware/HIL transcript was produced by this chunk.
