# MCP45HVX1 Poll-Chunked I2C Execution Report

Date: 2026-06-16

Branch: `hardening/mcp45hvx1-industry-readiness`

## Scope

This report covers the poll-chunking prompt for the MCP45HVX1 library. It
implements explicit jobs for bounded I2C execution while preserving the
framework-neutral, callback-only core transport.

This is software behavior only. No hardware or HIL validation was run or
claimed by this chunk.

## Public API Added

- `startSetWiperJob(uint8_t code)`
- `startReadSnapshotJob()`
- `startSetTerminalJob(Terminal terminal, bool enabled)`
- `startIncrementWiperJob(uint8_t steps = 1)`
- `startDecrementWiperJob(uint8_t steps = 1)`
- `startRecoverJob()`
- `pollJob(uint32_t nowMs, uint8_t maxInstructions)`
- `getJobSnapshot()`
- `jobActive()`

`JobSnapshot` reports job type, active state, output-changing classification,
planned and completed instruction counts, last-poll attempted instruction
count, final status, and completed Wiper/TCON readback for snapshot and recover
jobs.

## Instruction Semantics

One register read or command-write chunk counts as one instruction.

- `startSetWiperJob()` is a one-instruction output-changing Wiper write.
- `startReadSnapshotJob()` reads Wiper and TCON as separate instructions.
- `startSetTerminalJob()` exposes the TCON read-modify-write sequence as a
  read instruction followed by a write instruction. If the requested terminal
  bit already matches, the job completes after the read and sends no write.
- Wiper increment/decrement jobs split multi-step requests into bounded command
  chunks and execute at most one chunk per `pollJob()` call.
- `startRecoverJob()` reads Wiper and TCON as separate recovery instructions.

While a job is active, other bus-touching public APIs return `Err::BUSY`
without issuing I2C traffic. This keeps ordering explicit for applications that
own all I2C work behind one task.

## Failure And Uncertainty Semantics

Jobs stop on the first failing instruction and preserve the original `Status`.

Ambiguous output-changing failures mark the affected volatile cache unknown and
set `hardwareStateUncertain()` using the same rules as synchronous writes. Read
failures stop the job but do not mark hardware uncertainty by themselves.

`startRecoverJob()` is accepted while OFFLINE. If recovery starts from OFFLINE,
the OFFLINE latch remains asserted after the first successful Wiper read and is
cleared only after the TCON read also succeeds.

General Call helpers remain disabled by default through
`Config::allowGeneralCall`; this chunk did not add General Call poll jobs.

## Tests Added

Native tests cover:

- one-instruction Wiper set jobs and interleaving `BUSY` behavior;
- snapshot job `maxInstructions` budgeting, including zero-budget polls;
- terminal setter read-modify-write visibility;
- terminal setter no-op read-only completion;
- Wiper step jobs executing one bounded chunk per poll;
- failed step jobs stopping before later chunks and marking Wiper uncertainty;
- recovery jobs keeping the OFFLINE latch until both readbacks succeed;
- pre-begin rejection for job start APIs.

## Checks

`python -m platformio test -e native` passed locally with 69/69 native tests.

Further release checks, Arduino builds, package pack, pure ESP-IDF evidence,
and HIL evidence remain governed by `MCP45HVX1_RELEASE_CHECKLIST.md`.
