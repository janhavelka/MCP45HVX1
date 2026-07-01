# MCP45HVX1 v1.0.0 Release Notes

Release date: 2026-06-30

This is the first pre-production software package for the MCP45HVX1
framework-neutral high-voltage I2C digital potentiometer driver.

Do not describe this release as production-ready, industry-ready,
high-voltage-validated, analog-accurate, or General Call safe. The bundled
evidence supports software behavior and safe/read-only HIL behavior only.

## Highlights

- Framework-neutral core driver with injected, non-owning I2C transport
  callbacks.
- MCP45HV31 and MCP45HV51 variant support, including distinct resolution and
  POR/BOR Wiper defaults.
- Wiper, TCON, terminal-mode, raw-register, General Call, health, recovery, and
  hardware-uncertainty APIs.
- Output-changing startup writes are opt-in and readback-first.
- Ambiguous state-changing write failures preserve the original status, mark
  affected cache entries unknown, and require readback/recovery evidence before
  the volatile hardware state is trusted again.
- Poll-chunked jobs expose bounded Wiper, terminal, snapshot, step, and recover
  work for application-owned I2C tasks.
- Arduino/PlatformIO and native ESP-IDF diagnostic CLI examples share a
  repository-checked command contract without sharing Arduino implementation
  code into the ESP-IDF example.
- HIL evidence tooling defaults to safe/read-only behavior and requires
  explicit operator-gated flags for output-changing, SHDN/WLAT, and General
  Call checks.

## Validation Evidence

- Local validation passed with `python tools/validate.py`.
- Package validation passed with `pio pkg pack`; generated archives are ignored
  and not committed.
- ESP32-S2 safe-only HIL passed:
  [`docs/reports/hil-validation-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-validation-COM8-20260629.md)
  recorded `PASS_SAFE_ONLY`, `183221 / 183221 / 0` soak commands, and worst
  latency `0.188 s`.
- ESP32-S2 1-hour panic-repro safe-only HIL passed:
  [`docs/reports/hil-panic-repro-COM8-20260629.md`](https://github.com/janhavelka/MCP45HVX1/blob/v1.0.0/docs/reports/hil-panic-repro-COM8-20260629.md)
  recorded `PASS_SAFE_ONLY` and `23056 / 23056 / 0` soak commands.
- Pure ESP-IDF build evidence should be taken from the GitHub Actions run
  triggered by tag `v1.0.0`, because local `idf.py` was not available on the
  release-prep machine.

## Known Limits

The following evidence gates are not complete in this release:

- output-changing HIL with safe-load confirmation and restore measurements
- analog P0A/P0W/P0B movement and terminal-current measurements
- high-voltage operation
- SHDN and WLAT physical override behavior
- address strap matrix
- POR/BOR rail cycling and reset-exit timing margin
- I2C fault-injection fixture results
- General Call isolated-bus evidence or shared-bus risk acceptance

See [`docs/MCP45HVX1_RELEASE_CHECKLIST.md`](MCP45HVX1_RELEASE_CHECKLIST.md) and
[`docs/MCP45HVX1_HARDWARE_VALIDATION.md`](MCP45HVX1_HARDWARE_VALIDATION.md)
before making any stronger hardware or deployment claims.
