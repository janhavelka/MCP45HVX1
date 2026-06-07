# MCP45HVX1 Status, Health, and Lifecycle Contract Report

Prompt: 03 - Status fidelity, health/recover semantics, copy/move, and public
API contracts.

## Status Mapping Changes

`begin()` and `probe()` now use one presence-read mapping rule:

- `Err::I2C_NACK_ADDR` maps to `Err::DEVICE_NOT_FOUND` with numeric `detail`
  preserved;
- `Err::I2C_TIMEOUT`, `Err::I2C_BUS`, `Err::I2C_NACK_DATA`, generic
  `Err::I2C_ERROR`, local validation failures, and `Err::REGISTER_MISMATCH` are
  returned with their original public status code.

The `Status` type has no nested cause field, so mapped `DEVICE_NOT_FOUND` can
preserve only the callback detail value, not the original enum as a separate
cause.

## Health And Recover Contract

`resetI2cState()` now treats successful bus-reset callbacks as interface-only
events. Success clears only the cached I2C address-pointer knowledge. It does
not perform a device transaction, does not update Wiper/TCON cache, does not
clear hardware uncertainty, and does not mark a DEGRADED or OFFLINE driver
READY.

Failed reset callbacks remain tracked health failures when they represent real
transport/reset errors. Unsupported or invalid reset callback configuration is
reported without mutating health.

`probe()` remains diagnostic and health-neutral. It performs a raw Wiper read
for presence/format checking and does not update health counters, Wiper/TCON
cache knowledge, uncertainty, or READY/DEGRADED/OFFLINE state. The raw read may
update address-pointer knowledge.

`recover()` is the proof path. It does not call the bus-reset callback. It reads
Wiper then TCON with tracked transactions, refreshes caches, clears uncertainty
only for verified readback, and returns READY only after both reads succeed. If
recovery starts from OFFLINE and only partially succeeds, the OFFLINE latch is
restored.

## Copy/Move Decision

`MCP45HVX1` is explicitly non-copyable and non-movable:

- copy construction deleted;
- copy assignment deleted;
- move construction deleted;
- move assignment deleted.

The driver stores non-owning callback pointers, user contexts, health counters,
address-pointer state, and volatile register-cache knowledge. Implicit copies or
moves could duplicate stale runtime state across driver objects bound to the
same physical device or expired callback contexts.

## API Docs Updated

Updated public Doxygen and README contract text for:

- callback and context lifetime requirements;
- external serialization and non-ISR-safe public calls;
- failed-write hardware uncertainty;
- `begin()` side effects and status mapping;
- `probe()` status mapping and health-neutral side effects;
- `recover()` readback and partial-failure semantics;
- `resetI2cState()` interface-only semantics;
- deleted copy/move policy.

`docs/MCP45HVX1_HARDWARE_VALIDATION.md` now includes bench checks for status
mapping, probe side effects, reset-without-proof behavior, and recover
readback.

## Tests Added

Native tests cover:

- `begin()` preserves timeout, bus-error, and data-NACK status while mapping
  address NACK to `DEVICE_NOT_FOUND`;
- `probe()` preserves the same transport statuses and remains health-neutral;
- successful `resetI2cState()` from DEGRADED does not mark READY or issue a
  device read;
- a subsequent successful tracked read after reset returns the driver to READY;
- static assertions that `MCP45HVX1` is not copy/move constructible or
  assignable.

## Validation

Prompt 03 validation results:

- `python tools/check_core_timing_guard.py`: PASSED.
- `python tools/check_cli_contract.py`: PASSED.
- `python tools/check_idf_example_contract.py`: PASSED.
- `python tools/check_generated_artifacts.py`: PASSED.
- `python scripts/generate_version.py check`: `Version.h` up to date.
- `python -m platformio test -e native`: PASSED, 54 test cases succeeded.
- `python -m platformio run -e esp32s3dev`: SUCCESS.
- `python -m platformio run -e esp32s2dev`: SUCCESS.
- `python -m platformio pkg pack`: SUCCESS, wrote
  `MCP45HVX1-1.0.0.tar.gz`; generated tarball removed before commit.

PlatformIO emitted its existing obsolete-core warning during PlatformIO
commands; the commands still completed successfully.
