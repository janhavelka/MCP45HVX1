# Changelog

## Unreleased

No changes yet.

## 1.1.0 - 2026-08-05

### Added

- Added table-driven `help <command>` / `? <command>` lookup with canonical
  aliases, colored safety classifications, syntax, examples, Arduino/native
  CLI parity guards, and safe HIL coverage.
- Added a targeted ESP32-S2 COM9 `PASS_SAFE_ONLY` report with 31 executed
  checks, zero failures, and final READY MCP45HV51 state at `0x3C`.
- Added Doxygen validation with warnings treated as errors to GitHub Actions.

### Changed

- Upgraded Arduino example builds from pioarduino `platform-espressif32`
  `54.03.20` (Arduino-ESP32 3.2.0 / ESP-IDF 5.4.1) to `55.03.311`
  (Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5).
- Pinned PlatformIO Core 6.1.19 in CI and centralized its cache key version.
- Updated official checkout, cache, and Python setup actions to their Node 24
  majors so GitHub-hosted CI does not depend on the deprecated Node 20 runtime.
- Removed the forced PSRAM macro and original-ESP32 PSRAM cache workaround from
  the declared no-PSRAM ESP32-S3 target.
- Excluded repository-local `.pio` content explicitly from all source filters.
- Added Arduino-ESP32 and ESP-IDF versions to the Arduino CLI `version` output
  for build and HIL provenance.
- Consolidated the documentation around the maintained README, device
  reference, API contract, hardware-validation procedure, release checklist,
  ESP-IDF notes, datasheets, and compact HIL evidence summaries.
- Made repository HIL report copies explicit through `--report-file`, omitted
  repetitive benchmark/soak rows from human reports, and renamed the hard-coded
  `8-hour soak` feature label to the duration-neutral `Safe-only soak`.
- Made the README quick start read-only after initialization and documented the
  legacy `tick()` hook as unnecessary for new code.
- Reworked example logging macros to remain standard C++17 when a log message
  has no format arguments.
- Added a portable Windows PlatformIO wrapper and aligned the aggregate
  validator and maintained contributor/release instructions with it.

### Removed

- Removed completed prompt files, one-off hardening/release summaries, and the
  duplicate implementation manual after merging their durable information into
  the maintained documentation and changelog.
- Removed unused `CommandHandler` and `HealthDiag` helpers, the include-only
  `TransportAdapter` and `BusDiag` wrappers, duplicate status/rate formatters,
  and an unused second I2C recovery implementation from the examples.
- Removed two private transport/register helpers that had no callers.
- Cleared generated build/cache output, duplicate local HIL staging bundles,
  and roughly 21 MB of repetitive command-by-command Markdown rows while
  preserving compact HIL provenance and result summaries.

### Validation

- Passed all 74 native tests, the aggregate repository validator, package
  creation, and clean ESP32-S2/ESP32-S3 Arduino builds on pioarduino
  `55.03.311`.
- Passed warning-free Doxygen generation and the HIL parser/self-tests.
- The targeted COM9 run completed `PASS_SAFE_ONLY`; output-changing and
  physical hardware gates remained disabled.

### Known Limits

- This remains a pre-production software package. It does not claim analog
  accuracy, high-voltage safety, production readiness, or General Call safety.
- Output-changing, SHDN/WLAT, address-matrix, rail-cycle, physical fault,
  analog, and high-voltage evidence gates remain open.

## 1.0.0 - 2026-06-30

### Added

- Hardware-uncertainty tracking for ambiguous failed Wiper/TCON/raw/General
  Call state-changing writes, including cache invalidation, public snapshot
  fields, and readback-only clearing.
- Poll-chunked job APIs for one-instruction Wiper writes, bounded snapshot
  readback, visible terminal read-modify-write sequencing, chunked Wiper step
  commands, and recovery readback.
- HIL evidence tooling in `tools/run_hil_mcp45hvx1.py` with safe read-only
  defaults, explicit output-changing opt-ins, General Call isolated-bus gating,
  SHDN/WLAT operator prompts, restore checks, and attachable evidence bundles.
- GitHub Actions coverage for ESP32-S3/ESP32-S2 PlatformIO builds, native
  tests, guard scripts, package validation, version checks, and pure ESP-IDF
  `esp32s2`/`esp32s3` matrix builds.
- Maintained documentation set: docs index, API contract, hardware validation
  template, release checklist, ESP-IDF port notes, hardening summary, and
  v1.0.0 release notes.

### Changed

- Made optional `begin()` startup Wiper/TCON writes explicitly opt-in,
  readback-first, visible after failure, and recoverable through normal
  readback/recovery paths.
- Gated core General Call helpers behind explicit `Config::allowGeneralCall`
  opt-in while keeping diagnostic CLI General Call commands behind one-shot
  operator arming and warnings.
- Aligned the device model and documentation with datasheet-driven
  MCP45HV31/MCP45HV51 resolution/POR defaults, Wiper/TCON registers, TCON
  reserved bits, SHDN/WLAT physical semantics, POR/BOR timing, and DS80000649B
  errata gates.
- Updated package/export rules so normal packages include headers, source,
  examples, metadata, and selected current docs while excluding generated
  archives, local build output, tests, tools, prompts, CI metadata, large PDFs,
  HIL reports, and extracted reference markdown.
- Simplified `docs/` around maintained API, device reference, ESP-IDF,
  hardware validation, release checklist, hardening summary, and release notes.

### Fixed

- Clarified status, health, lifecycle, reset/probe/recover, callback lifetime,
  non-ISR, threading, and deleted copy/move contracts.
- Completed native ESP-IDF CLI behavioral parity: bounded command parsing,
  functional `color on|off`, real `selftest output`, read-only `stress`,
  state-restoring `stress_mix`, state/health/cfg parity, null-handle guards,
  raw-write warnings, and General Call warning/argument parity.
- Strengthened native tests, fault-injection coverage, generated-artifact
  checks, Arduino/ESP-IDF CLI contract guards, stale-placeholder guards, and
  ESP-IDF native-boundary checks.

### Validation

- Added bundled safe-only ESP32-S2 HIL evidence: one 1-hour panic-repro run and
  one 8-hour safe-only soak.
- The 8-hour run finished `PASS_SAFE_ONLY` with `183221 / 183221 / 0` soak
  commands, worst latency `0.188 s`, and final driver state READY with zero
  failures.
- Release tag CI for `v1.0.0` passed after the tag was pushed, including
  PlatformIO ESP32-S2/ESP32-S3 builds, native tests, package validation, and
  pure ESP-IDF `esp32s2`/`esp32s3` builds.

### Known Limits

- This is a pre-production software package. It does not claim production
  readiness, hardware validation, high-voltage safety, analog accuracy, General
  Call safety, or industry readiness.
- Output-changing, General Call, SHDN, WLAT, rail-cycle, fault-injection,
  analog, and high-voltage checks were not run and remain separate evidence
  gates.
- Production-readiness claims remain blocked until the hardware validation
  checklist evidence is complete.
