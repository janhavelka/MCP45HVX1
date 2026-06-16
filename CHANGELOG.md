# Changelog

## Unreleased

- Documented the current status as a pre-production candidate pending hardware
  validation; no production, hardware-validation, high-voltage, analog
  accuracy, General Call safety, or release-readiness claim is made without
  logged evidence.
- Added hardware-uncertainty tracking for ambiguous failed Wiper/TCON/raw/
  General Call state-changing writes, including cache invalidation, public
  snapshot fields, and readback-only clearing.
- Made optional `begin()` startup Wiper/TCON writes explicitly opt-in,
  readback-first, visible after failure, and recoverable through normal
  readback/recovery paths.
- Clarified status, health, lifecycle, reset/probe/recover, callback lifetime,
  non-ISR, threading, and deleted copy/move contracts.
- Aligned device model and docs with datasheet-driven MCP45HV31/MCP45HV51
  resolution/POR defaults, Wiper/TCON registers, TCON reserved bits, SHDN/WLAT
  physical semantics, POR/BOR timing, and DS80000649B errata gates.
- Completed native ESP-IDF CLI behavioral parity work: bounded command parsing,
  functional `color on|off`, real `selftest output`, read-only `stress`,
  state-restoring `stress_mix`, state/health/cfg parity, null-handle guards,
  raw-write warnings, and General Call warning/argument parity.
- Gated core General Call helpers behind explicit `Config::allowGeneralCall`
  opt-in while keeping diagnostic CLI General Call commands behind their
  existing operator arming and warnings.
- Added poll-chunked job APIs for one-instruction Wiper writes, bounded
  snapshot readback, visible terminal read-modify-write sequencing, chunked
  Wiper step commands, and recovery readback with instruction-budget tests.
- Strengthened native tests, fault-injection coverage, generated-artifact
  checks, Arduino/ESP-IDF CLI contract guards, stale-placeholder guards, and
  ESP-IDF native-boundary checks.
- Added HIL evidence tooling in `tools/run_hil_mcp45hvx1.py` with safe
  read-only defaults, explicit output-changing opt-ins, General Call isolated
  bus gating, SHDN/WLAT operator prompts, restore checks, and attachable
  evidence bundles. No hardware HIL transcript is bundled.
- Added GitHub Actions coverage for ESP32-S3/ESP32-S2 PlatformIO builds, native
  tests, guard scripts, package validation, version checks, and pure ESP-IDF
  `esp32s2`/`esp32s3` matrix builds. Remote CI status must still be checked
  from an actual run before it is cited.
- Added authoritative docs index, API contract, hardware validation template,
  release checklist, ESP-IDF parity report, final hardening report, and final
  docs/release cleanup report.
- Updated package/export rules so normal packages include headers, source,
  examples, metadata, and selected current docs while excluding generated
  archives, local build output, tests, tools, prompts, CI metadata, large PDFs,
  and extracted reference markdown.
- Kept release tag creation pending explicit approval and release-checklist
  evidence.

## 1.0.0

- Initial framework-neutral MCP45HVX1 driver package.
- Added injected I2C transport, status/error model, health tracking, register
  helpers, wiper and TCON APIs, General Call helpers, examples, tests, and docs.
- Release tag and production-readiness claims remain pending until the release
  checklist evidence is complete.
